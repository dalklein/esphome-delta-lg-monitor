# Quick start — PV monitoring, SunSpec only

The short path. One config file, one RS485 module, **nothing to fetch or clone** — which is what
makes this the easy one under Home Assistant.

Use this if you want PV and inverter data and nothing else. For a battery and a revenue meter you
need the two-bus setup: [GETTING_STARTED.md](GETTING_STARTED.md).

> **Delta M-series owners: this is the one to use.** The M-series has no RGM bus — the green
> terminal block is not populated — so the `'485'` port is all there is to read anyway.

---

## 1. What you need

| | |
|---|---|
| ESP32 board | ESP32-WROOM-32 devkit (the config targets `esp32dev`) |
| **One** RS485-to-TTL module | **auto-direction** type, with no DE/RE pin |
| An MQTT broker | **Required.** On Home Assistant that is the Mosquitto add-on |
| ESPHome **2026.3.0 or newer** | older versions poll the bus with no spacing; the config refuses to build on them |
| A USB cable | first flash only; later updates go over WiFi |

> **Why a broker is not optional:** this config has no `api:` block. MQTT is the only way data
> leaves the device. Without a broker it runs perfectly and shows you nothing.

---

# Path A — you already run Home Assistant

Most people get ESPHome this way, and this config is built to need no special handling here.

### A1. Add-ons

In **Settings → Add-ons**, you want two:

- **ESPHome Device Builder** (older installs just call it *ESPHome*)
- **Mosquitto broker** — unless you already run a broker elsewhere

Then **Settings → Devices & Services → Add Integration → MQTT**, pointed at that broker. Skip it if
MQTT is already set up.

### A2. Create the device and paste the config

ESPHome Device Builder → **+ New Device** → name it → pick **ESP32**. Let it generate the skeleton,
then **Edit** and replace the whole thing with
[`delta-pv-only-sunspec.yaml`](../delta-pv-only-sunspec.yaml).

That is the entire installation. No component to download, no path to fix, nothing to clone — this
is exactly the step where the other configs in this repo need extra work and this one does not.

### A3. Secrets

In the ESPHome dashboard, top-right **⋮ → Secrets**, and add:

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

🪤 **`mqtt_broker_ip` must be your Home Assistant machine's LAN IP**, something like
`192.168.1.10`. Not `localhost`, and not `core-mosquitto` — that name only resolves *inside* HA's
own container network, and the ESP32 is outside it.

The MQTT username and password are a Home Assistant user's. Many people create a dedicated one
under **Settings → People** for this.

### A4. First flash

Click **Install**. The ESP32 has to be plugged into whichever machine you pick:

- **"Plug into this computer"** — the usual choice, flashing over USB from the browser you are
  sitting at. 🪤 Needs **Chrome or Edge**. Firefox and Safari do not support Web Serial, and the
  option will not work in them.
- **"Plug into the server"** — only if the ESP32 is physically attached to the HA machine.

After this first flash, updates go over WiFi and none of that applies.

### A5. The entities appear by themselves

This config has MQTT discovery enabled, so once it is running and talking to the broker **all ~39
sensors show up automatically** under **Settings → Devices & Services → MQTT**. Nothing goes into
`configuration.yaml`.

`delta lifetime energy kWh` is already marked as an energy total, so it can be used directly in the
**Energy dashboard** as a solar production source.

---

# Path B — ESPHome on its own

No Home Assistant needed; you just need a broker somewhere.

```bash
python3 -m venv ~/venvs/esphome && source ~/venvs/esphome/bin/activate
pip install esphome
wget https://raw.githubusercontent.com/dalklein/esphome-delta-lg-monitor/master/delta-pv-only-sunspec.yaml
```

Create `secrets.yaml` next to it with the same keys as A3, then:

```bash
esphome run delta-pv-only-sunspec.yaml
```

Choose the USB serial port when asked. If none is offered: on Linux add yourself to `dialout` and
**log out and back in** (`sudo usermod -aG dialout $USER`); on macOS or Windows you may need a
CP2102 or CH340 driver; and a charge-only USB cable powers the board but carries no data.

---

## 2. Wire it — both paths

Power off the inverter at its disconnect first.

| | |
|---|---|
| ESP32 → RS485 module | GPIO18 = TX, GPIO19 = RX, plus 3.3 V and GND |
| RS485 module → inverter | **either RJ45** on the inverter — **pin 7 = A+, pin 8 = B−** |
| Speed | 38400 8N1, the inverter answers as Modbus address 1 |

GPIO17 is unused here. Swapped A+/B− gives silence, not damage — swapping back is the usual fix.

Applies whichever path you took:

- **The ESP32 only joins 2.4 GHz networks.** It cannot see a 5 GHz-only SSID.
- **The first build takes 5–15 minutes** and scrolls a lot. It happens once.
- **There are no USB logs.** The config sets `logger: baud_rate: 0`, freeing the serial port, so
  watching it shows nothing even on a perfectly working device. This is the most common "it's
  broken" that isn't — read the logs in the ESPHome dashboard, or with `esphome logs …`.

---

## 3. Check it works

On Home Assistant, look under **Settings → Devices & Services → MQTT** for the device. Live values
there means you are done.

Otherwise, or on Path B, subscribe directly:

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

1. `delta/485/status` should read `online`. If not, the ESP32 is not reaching your broker — check
   `mqtt_broker_ip` is the LAN IP, and the username and password.
2. Online but no readings: swap A+/B− at the RJ45. Harmless, and the usual cause.
3. Still nothing: confirm the inverter is Modbus **address 1**, on its own display or in its manual.
4. Only then suspect that your model speaks something different.

The `'485'` port has no master until this board becomes one, so **silence is the symptom of every
mistake** — backwards wiring and an unsupported protocol look identical. Work down the list rather
than guessing.

---

## What you do not get

No battery, no revenue meter — those live on the RGM bus, which this config does not touch and the
M-series does not have. SunSpec also exposes **one MPPT**, so a second PV string is invisible here,
and there is no DC bus voltage. For two strings use [`delta-pv-only.yaml`](../delta-pv-only.yaml)
and read [GETTING_STARTED.md](GETTING_STARTED.md).
