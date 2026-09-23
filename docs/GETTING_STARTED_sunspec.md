# Quick start — PV monitoring, SunSpec only

The short path. One config file, one RS485 module, **no components to fetch or clone** — so it
works unchanged on the Home Assistant ESPHome add-on as well as the command line.

Use this if you want PV and inverter data and nothing else. For a battery and a revenue meter you
need the two-bus setup instead: [GETTING_STARTED.md](GETTING_STARTED.md).

> **Delta M-series owners: this is the one to use.** The M-series has no RGM bus — the green
> terminal block is not populated — so the `'485'` port is all there is to read anyway.

---

## 1. What you need

| | |
|---|---|
| ESP32 board | ESP32-WROOM-32 devkit (the config targets `esp32dev`) |
| **One** RS485-to-TTL module | **auto-direction** type, with no DE/RE pin |
| An MQTT broker | **Required** — see the note below |
| ESPHome **2026.3.0 or newer** | older versions poll the bus with no spacing; the config refuses to build on them |
| A USB cable | first flash only; later updates go over WiFi |

> **Why a broker is not optional:** this config has no `api:` and no `web_server:` block. MQTT is
> the only way data leaves the device. Without a broker it runs perfectly and shows you nothing.

---

## 2. Get the file

Just the one file — there is nothing else to download.

```bash
wget https://raw.githubusercontent.com/dalklein/esphome-delta-lg-monitor/master/delta-pv-only-sunspec.yaml
```

**On the Home Assistant add-on**, create a new device in the ESPHome dashboard, then edit it and
paste the file's contents over what it generated. Nothing else is needed — that is the whole point
of this config.

---

## 3. Fill in your settings

Passwords live in a separate file. Create `secrets.yaml` next to the config:

```yaml
wifi_ssid: "your-ssid"
wifi_ssid_g: "your-ssid-2.4G"       # a second network; both are tried
wifi_password: "your-wifi-password"
ap_password: "fallback-ap-password"
ota_password: "ota-password"
mqtt_broker_ip: "192.168.1.10"
mqtt_username: "mqtt-user"
mqtt_password: "mqtt-password"
```

- **The ESP32 only joins 2.4 GHz networks.** It cannot see a 5 GHz-only SSID.
- Both SSIDs share `wifi_password`. Different passwords? Edit the `wifi:` block in the config.
- `ap_password` and `ota_password` are ones **you invent now** — a fallback hotspot and wireless
  updates respectively.

---

## 4. Wire it

Power off the inverter at its disconnect first.

| | |
|---|---|
| ESP32 → RS485 module | GPIO18 = TX, GPIO19 = RX, plus 3.3 V and GND |
| RS485 module → inverter | **either RJ45** on the inverter — **pin 7 = A+, pin 8 = B−** |
| Speed | 38400 8N1, the inverter answers as Modbus address 1 |

GPIO17 is unused here. If A+/B− are swapped you get silence, not damage — swapping them back is
the usual fix.

---

## 5. Build and flash

```bash
esphome run delta-pv-only-sunspec.yaml
```

The first build downloads a toolchain and compiles ESP-IDF: **expect 5–15 minutes** and a lot of
scrolling. It happens once. Choose the USB serial port when asked; later updates can go over WiFi.

If no port is offered: on Linux add yourself to `dialout` and **log out and back in**
(`sudo usermod -aG dialout $USER`); on macOS/Windows you may need a CP2102 or CH340 driver; and a
charge-only USB cable will power the board but carry no data.

---

## 6. Check it works

🔑 **Do not expect logs over USB.** The config sets `logger: baud_rate: 0`, which frees the serial
port. Watching it shows nothing even on a perfectly working device — this is the most common
"it's broken" that isn't. Logs come over the network:

```bash
esphome logs delta-pv-only-sunspec.yaml
```

For real data, subscribe to MQTT from any machine with `mosquitto_clients`:

```bash
mosquitto_sub -h <broker-ip> -u <user> -P <password> -t 'delta/485/#' -v
```

You should see ~39 values under `delta/485/...`, including `pv_total_w`, `ac_w_raw`, `hz` and
`lifetime_kwh`.

Many signals appear as a `_raw` / `_sf` pair: SunSpec publishes a value and a separate scale factor,
and the config applies it for you where it matters — `hz` is the scaled version of `hz_raw`. Where a
scale factor is zero there is nothing to apply, so `ac_w_raw` is already watts and has no scaled
twin.

Nothing arriving? In order:

1. `delta/485/status` should read `online` — if not, the ESP32 is not reaching your broker; check
   the IP, username and password in `secrets.yaml`.
2. Online but no readings: swap A+/B− at the RJ45. Harmless, and the usual cause.
3. Still nothing: confirm the inverter is Modbus **address 1** on its own display or manual.
4. Only then suspect that your model speaks something different.

The `'485'` port has no master until this board becomes one, so **silence is the symptom of every
mistake** — it looks the same whether the wiring is backwards or the protocol is wrong. Work down
the list rather than guessing.

---

## What you do not get

No battery, no revenue meter — those live on the RGM bus, which this config does not touch and the
M-series does not have. SunSpec also exposes **one MPPT**, so a second PV string is invisible here,
and there is no DC bus voltage. If you have two strings and want both, use
[`delta-pv-only.yaml`](../delta-pv-only.yaml) and read [GETTING_STARTED.md](GETTING_STARTED.md).
