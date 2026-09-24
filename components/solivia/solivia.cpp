#include "solivia.h"
#include <algorithm>
#include "esphome/core/log.h"

namespace esphome {
namespace solivia {

static const char *const TAG = "solivia";

// CRC-16/ARC: poly 0xA001, init 0x0000.
// NOTE the init differs from Modbus RTU (0xFFFF). Using the Modbus variant here yields
// frames the inverter silently ignores, which looks exactly like a dead bus.
static uint16_t crc_arc(const uint8_t *d, size_t len) {
  uint16_t c = 0;
  for (size_t i = 0; i < len; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++)
      c = (c & 1) ? (uint16_t) ((c >> 1) ^ 0xA001) : (uint16_t) (c >> 1);
  }
  return c;
}

bool Solivia::transact_(uint8_t cmd, uint8_t sub, uint16_t &value_out) {
  // Runtime gate #2. sensor.py already refuses sub >= 0x80 at compile time; this is the
  // belt-and-braces check so no future code path can emit a write.
  if (sub >= 0x80) {
    this->refused_++;
    ESP_LOGE(TAG, "REFUSED sub 0x%02x: high bit set = WRITE", sub);
    return false;
  }
  if (this->address_ == 255) {
    this->refused_++;
    ESP_LOGE(TAG, "REFUSED address 255: broadcast");
    return false;
  }

  uint8_t body[5] = {0x05, this->address_, 0x02, cmd, sub};
  uint16_t crc = crc_arc(body, sizeof(body));
  uint8_t frame[9] = {0x02, body[0], body[1], body[2], body[3], body[4],
                      (uint8_t) (crc & 0xFF), (uint8_t) (crc >> 8), 0x03};

  while (this->available()) {           // discard anything stale before asking
    uint8_t junk;
    this->read_byte(&junk);
  }
  this->write_array(frame, sizeof(frame));
  this->flush();

  // 🛑 DO NOT MAKE THIS NON-BLOCKING. Tried and reverted 2026-09-19.
  //
  // The busy-wait below looks like a lazy end-of-frame detector that could be replaced
  // with a loop()-driven state machine -- the reply IS self-delimiting (ETX + valid CRC),
  // so on paper nothing needs waiting out. That refactor compiles, drops the loop stall
  // from 154 ms to 23 ms p99, and BREAKS THE COMPONENT COMPLETELY: ok=0 fail=50.
  //
  // The reason is that `modbus` (id mb485, the SunSpec client) is a UARTDevice on this
  // SAME uart. ESPHome has no arbitration between two UARTDevices sharing one port --
  // whichever reads first gets the bytes. Blocking here is what keeps modbus's loop()
  // from running between our request and our reply and swallowing it. The busy-wait is
  // an accidental mutex, and it is load-bearing.
  //
  // Evidence: with the state machine in place, modbus logged 97 buffer-clear warnings in
  // 150 s (against ~35 normally) while solivia recorded zero successful transactions.
  //
  // The stall it costs is ~150 ms per burst, ~25% of seconds. That is tolerable: the RGM
  // sniffer is immune to it (rx_full_threshold 120 means the hardware delivers whole
  // frames straight through a blocked loop -- measured pollcarry=0 across ~14,000 frames),
  // and the modbus client on this port re-polls on its own schedule.
  //
  // A real fix needs arbitration -- either the two protocols on separate UARTs, or a
  // shared bus-owner that both components ask before reading.

  // Collect until 50 ms of silence, capped at 300 ms overall.
  uint8_t buf[128];
  size_t n = 0;
  uint32_t last = millis();
  const uint32_t deadline = last + 300;
  while (millis() < deadline && n < sizeof(buf)) {
    if (this->available()) {
      uint8_t b;
      if (this->read_byte(&b)) {
        buf[n++] = b;
        last = millis();
        // A complete reply is recognisable the instant it lands -- ETX last, and the CRC
        // over bytes 1..n-4 matching the pair before it -- so there is nothing to wait
        // out. This skips the 50 ms silence on the SUCCESS path only; the silence rule
        // below still catches a truncated reply. CRC is what makes ETX usable as a
        // boundary at all, since 0x03 also occurs inside payload data.
        // ⚠️ Note this BREAKS THE while, it does not return: see the DO NOT MAKE THIS
        // NON-BLOCKING note above. Holding the loop is what keeps the co-resident modbus
        // client from eating this reply.
        if (n >= 10 && buf[n - 1] == 0x03 &&
            crc_arc(&buf[1], n - 4) == (uint16_t) (buf[n - 3] | (buf[n - 2] << 8)))
          break;
      }
    } else if (millis() - last > 50) {
      break;
    }
  }

  // Reply must be an ACK (0x06) and long enough to hold a payload.
  if (n < 10 || buf[1] != 0x06) {
    this->fail_++;
    ESP_LOGV(TAG, "cmd=%u sub=%u: no valid reply (%u bytes)", cmd, sub, (unsigned) n);
    return false;
  }
  // 🔑 The reply must answer the question we asked. Delta's own spec (Public Solar Inverter
  // Communication Protocol v1.2, packet 2) puts "repeat command being responded to" at byte 5
  // and "repeat sub command" at byte 6 -- buf[4] and buf[5] here -- which is why the payload
  // starts at buf[6].
  //
  // Without this, a LATE reply to the PREVIOUS request is accepted as the answer to the current
  // one: it is a well-formed frame with a good CRC, so length/ETX/CRC/ACK all pass. The '485'
  // UART is shared with the modbus client and loses 7-9% of SOLIVIA polls when the inverter is
  // working hard, which is exactly the condition that desynchronises request and reply. The
  // result is one sensor publishing another's value -- plausible-looking, and wrong.
  if (buf[4] != cmd || buf[5] != sub) {
    this->mismatch_++;
    ESP_LOGW(TAG, "cmd=%u sub=%u: reply was for cmd=%u sub=%u - discarded",
             cmd, sub, buf[4], buf[5]);
    return false;
  }

  // Payload is bytes 6 .. n-4 inclusive; take the first 16-bit big-endian word.
  const size_t plen = (n >= 9) ? (n - 3 - 6) : 0;
  if (plen < 2) {
    this->fail_++;
    return false;
  }
  value_out = ((uint16_t) buf[6] << 8) | buf[7];
  this->ok_++;
  return true;
}

void Solivia::update() {
  if (this->items_.empty())
    return;
  // `burst_` transactions per update, back to back.
  //
  // WHY: a derived quantity like PV power needs its V and its A sampled CLOSE TOGETHER. With one
  // transaction per tick the pair was a whole tick apart (2 s), and measured on this array the
  // string voltage moves a mean of 7.8 V per 13 s and up to 97 V, so a stale voltage cost ~6 W
  // typically and ~75 W on a moving string. Polling the pair inside one tick puts them ~100 ms
  // apart instead.
  //
  // Bus load is UNCHANGED: pair the sensors and double update_interval, and it is still the same
  // number of requests per second and the same refresh period per value. That matters because the
  // '485' UART is shared with the Modbus client and already loses 7-9% of SOLIVIA polls when the
  // inverter is working hard. Declare V immediately before its A so they land in the same burst.
  // NEVER WRAP MID-BURST. If the burst would run off the end of the list, poll only to the
  // end and restart at 0 next tick. Without this, an ODD item count makes the grouping DRIFT:
  // with 5 items and burst 2 you get (0,1) (2,3) (4,0) (1,2) ... and a V/A pair silently ends
  // up split across two ticks - undoing the entire reason the burst exists.
  size_t remaining = this->items_.size() - this->next_;
  uint8_t n = (uint8_t) std::min((size_t) this->burst_, remaining);
  for (uint8_t k = 0; k < n; k++) {
    Item &it = this->items_[this->next_++];

    uint16_t raw = 0;
    if (this->transact_(it.cmd, it.sub, raw)) {
      const float v = it.divisor != 0.0f ? raw / it.divisor : (float) raw;
      if (it.sensor != nullptr)
        it.sensor->publish_state(v);
    }
  }
  if (this->next_ >= this->items_.size())
    this->next_ = 0;

  static uint32_t last_report = 0;
  if (millis() - last_report > 60000) {
    last_report = millis();
    ESP_LOGI(TAG, "stats: ok=%" PRIu32 " fail=%" PRIu32 " refused=%" PRIu32
             " mismatch=%" PRIu32,
             this->ok_, this->fail_, this->refused_, this->mismatch_);
  }
}

void Solivia::dump_config() {
  ESP_LOGCONFIG(TAG, "SOLIVIA (read-only) on address %u, %u values",
                this->address_, (unsigned) this->items_.size());
}

}  // namespace solivia
}  // namespace esphome
