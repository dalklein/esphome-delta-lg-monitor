# Getting started

A step-by-step build for someone who has not used ESPHome before. No Linux, C or YAML
experience assumed. If you already run ESPHome, the short version is in the README's
[Use](../README.md#use) section instead.

---

## 0. Before you start — will this work for you?

**Known to work:** Delta **E6-TL-US** inverter with an **LG RESU10H-Prime** battery.
The other E-series units (E4/E8/E10-TL-US) use the same interfaces and should work.

🔑 **Which config file?** Battery + inverter → `delta-monitor.yaml`. **No battery (PV-only) →
`delta-pv-only.yaml`**, one bus and one RS485 module. Substitute it wherever this guide says
`delta-monitor.yaml`.

⚠️ **Delta M-series: the `'485'` side works, but there is no RGM bus.** The green RGM terminal
block is **not populated** on an M(4/6/8/10)-TL-US, so there is no battery and no revenue meter to
read. Use `delta-pv-only.yaml`, `delta-pv1-only.yaml` or `delta-pv-only-sunspec.yaml` — **not
`delta-monitor.yaml`**, which polls a bus that hardware does not have. The register map itself was
derived on an E-series, so treat individual values as unconfirmed until you have checked them.

**You will also need:**

| | |
|---|---|
| ESP32 board | ESP32-WROOM-32 devkit (the config targets `esp32dev`) |
| 2 × RS485-to-TTL modules | **auto-direction** type, no DE/RE pin — **one** is enough for `delta-pv-only.yaml` |
| An MQTT broker | **Required.** Mosquitto is the usual choice |
| ESPHome **2026.3.0 or newer** | older versions poll the '485' bus with no spacing and drop replies — the config refuses to build on them |
| A computer | Linux, macOS or Windows, with Python 3 |
| A USB cable | For the first flash only; later updates go over WiFi |

> **Why a broker is not optional:** this config has no `api:` and no `web_server:` block.
> MQTT is the only way data leaves the device. Without a broker it will run and show you
> nothing.

---

## 1. Install ESPHome

ESPHome is a Python program. Install it into a *virtual environment* — a private folder for
this project's Python packages, so it cannot disturb anything else on your system.

```bash
python3 -m venv ~/venvs/esphome
source ~/venvs/esphome/bin/activate
pip install esphome
```

Check it worked:

```bash
esphome version
```

You should see a version number. If instead you get `command not found`, the `activate`
line did not run — you need it once per terminal window, and your prompt will usually show
`(esphome)` when it is active.

---

## 2. Get the project

Using the CLI from step 1, clone it:

```bash
git clone https://github.com/dalklein/esphome-delta-lg-monitor.git
cd esphome-delta-lg-monitor
```

On the Home Assistant add-on you do not clone at all — copy the one YAML you want into
`/config/esphome/` and read the next part, which is the only thing that differs.

### Which ESPHome are you running? This decides one line of the config

**CLI (the venv above), with the repo cloned** — the config works as shipped. `solivia` loads by
*local path*, which resolves because the files sit next to the YAML:

```yaml
  - source:
      type: local
      path: components
    components: [solivia]
```

**Home Assistant ESPHome add-on** — you have no clone, and your YAML lives in `/config/esphome/`.
That local path then points at `/config/esphome/components`, which does not exist, and the build
stops with:

```
Could not find directory '/config/esphome/components'. Please make sure it exists
(full path: /config/esphome/components)
```

**Fix: replace those four lines with one.** ESPHome then fetches the component from GitHub, exactly
as it already does for the sniffer:

```yaml
  - source: github://dalklein/esphome-delta-lg-monitor@v1.0.0
    components: [solivia]
```

Nothing else changes, and you never need to clone. (Copying the repo's `components/` folder into
`/config/esphome/components/` also works, but it is a copy you then have to keep up to date.)

### The other component, and what `@v1.1.0` means

The second component, `modbus_rtu_sniffer`, you do **not** download — ESPHome fetches it for
you at build time:

```yaml
  - source: github://dalklein/esphome-modbus-rtu-sniffer@v1.1.0
    components: [modbus_rtu_sniffer]
```

The `@v1.1.0` on the end is a **tag** — a permanent label on one specific snapshot of that
repo, frozen at a moment in time. It says "use exactly this version", not "use whatever is
newest".

**Leave it as it is.** `v1.1.0` is the version the author runs against a real inverter and
battery, and it is known to work. You do not need to change it to get started.

It is worth understanding *why* it is there, though, because it is the difference between a
build you control and one you don't. Without the `@v1.1.0` part, ESPHome would follow the
newest code on that repo's main branch. Someone could push a change tomorrow, and your very
next build would quietly pick it up — new code arriving in firmware you are about to flash
onto hardware wired into a live solar and battery system, with nobody having decided that
should happen. Naming a version means the code only changes when *you* change that line.

This is also why the component is versioned at all: it is still being developed, so it
genuinely does change. `v1.0.0` and `v1.1.0` are two such snapshots. Pinning lets the author
keep improving it without that work landing unannounced in your build.

If you ever do move to a different version, delete the cached copy afterwards so the new one
is actually fetched:

```bash
rm -rf .esphome/external_components
```

---

## 3. Fill in your settings

The config keeps passwords out of the YAML in a separate file. Copy the example and edit it:

```bash
cp secrets.yaml.example secrets.yaml
nano secrets.yaml          # or any text editor
```

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

Notes:

- **The ESP32 only joins 2.4 GHz networks.** It cannot see a 5 GHz-only SSID. If your
  router publishes both under one name this is usually fine; if not, use the 2.4 GHz one.
- Both SSIDs share `wifi_password`. If your two networks have different passwords, edit
  the `wifi:` block in `delta-monitor.yaml` rather than `secrets.yaml`.
- `ap_password` and `ota_password` are ones **you invent now**. The first is for the
  fallback hotspot the device raises if WiFi fails; the second protects wireless updates.
- `secrets.yaml` is listed in `.gitignore`, so it will not be committed if you push this
  repo somewhere. Keep it that way.

---

## 4. Build and flash, over USB

Plug the ESP32 into your computer, then:

```bash
esphome run delta-monitor.yaml
```

The first build downloads a toolchain and compiles ESP-IDF. **Expect 5–15 minutes**, and a
large amount of scrolling output — this is normal and happens only once. Later builds take
well under a minute.

When it finishes, ESPHome asks how to upload. Choose the **USB / serial port** option
(something like `/dev/ttyUSB0`, or a `COM` port on Windows).

If no port is offered:

- On Linux, your user may not be allowed to use serial ports. Add yourself to the `dialout`
  group, then **log out and back in** for it to take effect:
  ```bash
  sudo usermod -aG dialout $USER
  ```
- Some ESP32 boards need a USB-serial driver (CP2102 or CH340) on macOS or Windows.
- A charge-only USB cable will power the board but carry no data. Try another cable.

---

## 5. Wire it up

Power off the inverter at its disconnect before opening anything.

The photo in the README's [Hardware](../README.md#hardware) section shows the two places
you connect on an E6-TL-US: the green **RGM** terminal block, and the **'485'** port on
either RJ45 (pin 7 = A+, pin 8 = B−).

| | RGM bus | the Delta's '485' port |
|---|---|---|
| ESP32 pin | GPIO17, **receive only** | GPIO18 TX, GPIO19 RX |
| Speed | 9600 8N1 | 38400 8N1 |
| Wiring | A+/B− from the RS485 module | RJ45 pin 7 = A+, pin 8 = B− |

Both RS485 modules take 3.3 V and GND from the ESP32.

> 🛑 **Never transmit on the RGM bus.** The inverter is the client there, and a second
> transmitter will corrupt its control loop. This firmware is built so it *cannot* — the
> RGM UART declares no `tx_pin` and no transmit buffer. Do not add one.

---

## 6. See whether it is working

🔑 **Do not expect logs over USB.** The config sets `logger: baud_rate: 0`, which frees the
serial port for other uses. Watching the USB port will show you nothing at all, even on a
perfectly working device. This is the single most common "it's broken" that isn't.

Logs come over the network instead:

```bash
esphome logs delta-monitor.yaml
```

To confirm real data, subscribe to the MQTT topics — from any machine with
`mosquitto_clients` installed:

```bash
mosquitto_sub -h <broker-ip> -u <user> -P <password> -t 'delta/#' -v
```

You should see `delta/rgm/...` values from the battery and, if you have wired the '485'
port, `delta/485/...` from the inverter.

Nothing arriving? Work down in this order:

1. `delta/rgm/status` should read `online`. If it does not, the device is not reaching your
   broker — check the IP, username and password in `secrets.yaml`.
2. If status is `online` but no readings follow, the ESP32 is on the network but not seeing
   bus traffic. Re-check the A+/B− wiring; swapping the two is the usual cause and is
   harmless to try.
3. The RGM bus is only busy when the inverter is polling. At night, with the system idle,
   there may genuinely be little to see.

---

## 7. Later updates, without the cable

Once it is on WiFi, the same command updates it wirelessly — pick the OTA option rather
than the serial port when asked:

```bash
esphome run delta-monitor.yaml
```

---

## Adding this to an ESP32 that already does something else

Start from **`delta-pv-only.yaml`** if you have no battery — it is one bus and far less to merge.

YAML has no merge: **a top-level key can appear only once.** Paste a second `uart:` or
`external_components:` and the later one silently replaces the first, which shows up as a component
that "isn't configured" rather than as an error. Combine the *entries* under one key instead:

```yaml
external_components:
  - source: github://dalklein/esphome-modbus-rtu-sniffer@v1.1.0   # only if you sniff the RGM bus
    components: [modbus_rtu_sniffer]
  - source: github://dalklein/esphome-delta-lg-monitor@v1.0.0
    components: [solivia]
  - source: ...your existing one...

uart:
  - id: uart_485          # this project
    tx_pin: GPIO18
    rx_pin: GPIO19
    baud_rate: 38400
  - id: your_existing_uart
    ...
```

Check these before building:

- **GPIO18 and GPIO19 must be free.** The ESP32 has several UARTs, so pick different pins if they
  clash — change them in the `uart:` block and nowhere else.
- **Ids must be unique** across the merged file: `uart_485`, `mb485`, `delta485`, `sol485`.
- **Do not copy `logger: baud_rate: 0`** if your existing device logs over USB. That line frees UART0
  and is why this project sees no serial output.
- **Topics are hardcoded per entity** as `delta/485/...`, not derived from `topic_prefix`. They will
  not follow your device's prefix — edit them if you want them somewhere else.
- **Keep one `mqtt:`, one `wifi:`, one `esphome:`.** Take your existing ones; this project needs
  nothing special from them beyond MQTT being present.

## If you get stuck

- **`esphome: command not found`** — the venv is not active. Run
  `source ~/venvs/esphome/bin/activate` again.
- **`Could not find directory '/config/esphome/components'`** — you are on the Home Assistant
  ESPHome add-on, where the `type: local` source cannot resolve. Swap it for the `github://` source
  shown in step 2. Nothing needs cloning.
- **The build fails on a missing component** — you are probably running from a downloaded
  ZIP rather than a `git clone`, or from the wrong folder. `components/` must sit beside
  `delta-monitor.yaml`.
- **It connects, then drops repeatedly** — weak 2.4 GHz signal at the inverter. The
  fallback hotspot (`Delta RGM Sniff Fallback`) appearing is the symptom.
- **Values look wrong rather than absent** — that is a decoding question, not a setup one.
  Open an issue with a sample of the MQTT output.

The README covers what each reading means, the register map, and the reasoning behind the
bus settings.
