#pragma once
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include <vector>

namespace esphome {
namespace solivia {

// SOLIVIA is the Delta's SECOND protocol on the '485' port, alongside SunSpec/Modbus.
// It carries values Modbus does not expose - notably the per-string PV data.
//
// Frame:  02 | 05 | addr | 02 | cmd | sub | crc_lo | crc_hi | 03
// Reply:  02 | 06 | ...  | <payload>      | crc    | 03      (payload = bytes 6 .. len-4)
// CRC is CRC-16/ARC: poly 0xA001, init 0x0000  -- NOT Modbus's 0xFFFF init.
//
// 🔴 READ ONLY BY CONSTRUCTION:
//   * sub >= 0x80 sets the high bit and is a WRITE -> rejected at COMPILE time in sensor.py,
//     and re-checked here at runtime as a second gate.
//   * address 255 is broadcast -> rejected by the config schema (max 254) and re-checked here.
class Solivia : public PollingComponent, public uart::UARTDevice {
 public:
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_address(uint8_t a) { this->address_ = a; }
  void add_sensor(uint8_t cmd, uint8_t sub, float divisor, sensor::Sensor *s) {
    this->items_.push_back(Item{cmd, sub, divisor, s});
  }
  /// Transactions issued back-to-back per update(). Pair a V with its A (declare them
  /// adjacently) and double update_interval: same bus load, but the pair is ~100 ms apart
  /// instead of a whole tick, which is what a V*A product needs.
  void set_burst(uint8_t n) { this->burst_ = n; }

 protected:
  struct Item {
    uint8_t cmd, sub;
    float divisor;
    sensor::Sensor *sensor;
  };
  bool transact_(uint8_t cmd, uint8_t sub, uint16_t &value_out);

  uint8_t address_{1};
  std::vector<Item> items_;
  size_t next_{0};                 // index of the next item in the round-robin
  uint8_t burst_{1};               // transactions issued back-to-back per update()
  uint32_t ok_{0}, fail_{0}, refused_{0}, mismatch_{0};
};

}  // namespace solivia
}  // namespace esphome
