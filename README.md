# esphome-delta-lg-monitor

ESPHome config for monitoring a **Delta E-series hybrid inverter** E(4/6/8/10)-TL-US
and an **LG RESU10H-Prime** battery over RS485, on a single ESP32. Publishes ~70 battery registers and the inverter telemetry to MQTT / Home Assistant.  The '485' PV side also works on a similar **Delta M-series PV inverter** M(4/6/8/10)-TL-US.

## Which config?

| file | what it reads | external components |
|---|---|---|
| **[`delta-monitor.yaml`](delta-monitor.yaml)** | the lot — LG RESU battery, revenue meter, inverter, both PV strings. Two buses. **E-series only** — the M-series has no RGM bus. | sniffer + `solivia` |
| **[`delta-pv-only.yaml`](delta-pv-only.yaml)** | inverter + **both** PV strings. No battery, one bus. | `solivia` |
| **[`delta-pv1-only.yaml`](delta-pv1-only.yaml)** | inverter + **PV1 only**. An unconnected input does not reliably read zero, and `pv_total_w` sums both strings. | `solivia` |
| **[`delta-pv-only-sunspec.yaml`](delta-pv-only-sunspec.yaml)** | inverter + **MPPT1**, stock Modbus only. No PV2, no DC bus voltage. | **none** |

🏠 **On the Home Assistant ESPHome add-on, use `delta-pv-only-sunspec.yaml`.** The other three
load the `solivia` component by *local path*, which on the add-on resolves to
`/config/esphome/components` — a directory that does not exist there, because you never cloned
anything. They work if you change that source to
`github://dalklein/esphome-delta-lg-monitor@v1.0.0` (see
[docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)). **The SunSpec one has no external components
at all**, so it needs no change.

It **never transmits on the battery & meter bus** — that side is receive-only, by wiring and by config.

**New to ESPHome?** Start with **[docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)** — a
step-by-step build with nothing assumed.

## The buses

A Delta E-series normally has **two** RS485 buses. This project uses both.
RGM bus = revenue grade meter & battery connect here. The meter is for modes to control inflow/outflow.
'485' bus = monitoring bus

🔴 **The M-series has no RGM bus.** The green RGM terminal block is **not populated** on an
M(4/6/8/10)-TL-US, so there is no battery and no revenue meter to read, and the `'485'` port really
is all you get. Everything below about the RGM bus applies to the E-series only.
➡️ On an M-series use one of the `'485'`-only configs — `delta-pv-only.yaml`, `delta-pv1-only.yaml`
or `delta-pv-only-sunspec.yaml`. **`delta-monitor.yaml` cannot work there**: it polls a bus the
hardware does not have.

| | **RGM bus** | **the Delta's '485' port** |
|---|---|---|
| where | green RGM terminal block | either RJ45 — pin 7 = A+, pin 8 = B− |
| speed | 9600 8N1 | **38400 8N1**, address 1 |
| who is client | **the Delta** | **nobody** — the Delta is a *server* here |
| who else is on it | LG RESU at `0x0F`, revenue meter at `0x02` | just the Delta |
| protocol | LG's Modbus register map | **SunSpec** (Modbus RTU, base 40000) **+ Delta's own SOLIVIA**, on the same wire |
| this project | **listens only** — no `tx_pin`, no TX buffer | **polls it** as client |

On the RGM bus, the inverter is the client: it polls the battery and the meter, and
they answer. `0x0E` is a second battery if fitted; `0x03`, `0x1E` and `0xC9` are also addressed.
Adding a silent listener disturbs nothing.

The '485' port is the opposite situation — nobody drives it until you do. That is why a passive
listen there is correctly silent and proves nothing: you have to poll it to get anything.

> 🛑 **Never transmit on the RGM bus.** The Delta drives it, and a second transmitter will corrupt
> its control loop.

### A third bus, if a meter MITM is used

Putting a man-in-the-middle in front of the grid meter splits the meter onto a bus of its
own. The MITM answers the Delta at `0x02` as if it were the meter, and separately polls the real
meter on the new segment:

```
  RGM bus          Delta (client) ─── LG RESU 0x0F (server)
                                  └── MITM answering as the meter 0x02 (server)
  meter segment    MITM (client) ──── real revenue meter 0x02 (server)
  '485' port       Delta (server) ←── this project polls it (client)
```

That is a different project and is **not** part of this repo — see "Related" below.
If you have no MITM, there are two buses and the meter simply sits on the RGM bus.

Two facts that invert how you read negatives on the '485' port:
the device **always NAKs** an unsupported command — so **silence means a comms fault, not
"unsupported"** — and the SOLIVIA log indices are **fixed slot IDs, not dates**.

## Hardware

* **Delta E6-TL-US** inverter — E4/E8/E10-TL-US should work
* **LG RESU Prime 10H** battery — 16H should work

![Delta E6-TL-US communication ports: two RJ45 jacks labelled Ethernet/485 and CAN/485, and the green RGM terminal block below them](docs/E6-TL-US_comm_ports.jpg)

Where the two buses land on an E6-TL-US: the green **RGM** terminal block, and the **'485'** port —
either RJ45 works, pin 7 = A+, pin 8 = B−. On an M-series that green block is **not populated**, so
only the RJ45 side applies.

```
ESP32-WROOM-32 (esp32dev, esp-idf)
  UART2  GPIO17 RX only  ->  RGM bus, 9600 8N1      (no tx_pin, no TX buffer)
  UART1  GPIO18 TX / GPIO19 RX -> Delta '485', 38400 8N1
```

Two RS485-TTL modules, auto-direction (no DE/RE pin), powered from the ESP32's 3.3 V rail.

🪤 **GPIO16/17 are used opposite to the devkit silkscreen** (`RX2`/`TX2`). Follow the table, not
the board.

⚠️ The '485' module's driver input is on GPIO18. If no `uart:` declares that pin it floats, which
can spuriously enable the transmitter onto a live bus. The config declares it for that reason even
when not polling.

## Use

```yaml
external_components:
  - source: github://dalklein/esphome-modbus-rtu-sniffer@v1.1.0
    components: [modbus_rtu_sniffer]
  - source: github://dalklein/esphome-delta-lg-monitor@v1.0.0
    components: [solivia]
```

🔑 **Note the `@v1.1.0`.** Without a ref, `external_components` tracks the component repo's
default branch, so any push there arrives on your next build once the ~1 day cache expires —
an upstream change landing in firmware you flash to a live device, with nobody choosing it.
Pin it and move the pin deliberately. (ESPHome keys its cache by URL *and* ref, so changing
the ref fetches fresh rather than reusing the old checkout. Clearing `.esphome/` forces a
refetch if you ever need one.)

The same applies to `@v1.0.0` for `solivia`. If you cloned this repo you can instead point at
your own copy with `source: {type: local, path: components}`, which is what the shipped YAML does —
it then moves only when you pull.

Copy `secrets.yaml.example` to `secrets.yaml`, fill it in, then `esphome run delta-monitor.yaml`.

The passive sniffing is done by
[**esphome-modbus-rtu-sniffer**](https://github.com/dalklein/esphome-modbus-rtu-sniffer), a separate
component — see its README for the one setting that matters (`rx_full_threshold` must exceed the
longest burst on your bus).

`components/solivia/` ships here: it speaks Delta's own SOLIVIA protocol, which shares the '485'
wire with SunSpec.

🔑 Pace '485' requests at **≥95–100 ms** per device. That is a property of the hardware, not the
topology.

## What it reads

**LG RESU — 83 registers**, sniffed passively from the Delta's own polling:

| | |
|---|---|
| state of charge | `206`, `221` |
| battery power, signed | `203` |
| pack voltage / current | `215`, `216` |
| DC-bus voltage / current | `202`, `220` |
| **battery temperature, max and min** | `217`, `219` |
| charge / discharge current limits | `226`, `227` (pack side) |
| charge / discharge power limits | `211`-`214`, `229` |
| charge-complete flag | `222` |
| identity and firmware strings | `102`-`103`, `110`-`127`, `141`-`142` |
| config block the inverter writes at boot | `1100`-`1112` |
| alarm block | `2001`-`2034` |

⚠️ **There are no per-cell voltages.** The pack reports aggregate values only — nothing in the map
exposes individual cell voltages, and an exhaustive sweep did not find them. If you need cell-level
data from a RESU Prime, this will not give it to you.

Two register meanings were retracted during this work and are worth knowing, because both looked
solid for days: `226` and `227` are **current limits**, not temperatures. `227` in particular passed
as "warmest cell" for a whole project. The temperature set is exactly two registers, `217` and `219`.

**Delta inverter** — AC current, voltage, power and frequency, apparent and reactive power, and PV
string voltage and current per MPPT, via SunSpec and SOLIVIA.

**The revenue meter at `0x02`**, sniffed from the same RGM bus — `W`, `WphA`, `WphB`, `AphA`,
`AphB` and the variable `W_SF`. The Delta polls the meter on this wire anyway, so these frames
already arrive and cost nothing extra to decode. They answer "what is the inverter being told
about the grid?", which is not the same question as "what is the grid doing" if anything sits
in between.

⚠️ `W_SF` (40022) is **not constant** — 0 means coefficient 1, 1 means coefficient 10, switching
by magnitude. Apply the factor sampled alongside the value, never a remembered one. It arrives
in the same response frame, since the Delta reads `40018` ×5. `AphA`/`AphB` use `A_SF`, a fixed
−1 (0.1 A per count) taken from the meter's register map — the Delta never reads that register,
so it cannot be sniffed.

## Register map

`docs/LG_RESU_Prime_register_map.ods` — the LG read/write registers, the Delta '485' register list,
the SOLIVIA command map, and the learnings behind them.  The last sheet delta_pv_data is a subset from Solivia & Sunspec lists, relevant for logging, included in the .yaml files.

## Related

A companion project puts an ESP32 **in series with the grid connection meter** — creating the third bus described above — and steers charge and discharge by offsetting what the inverter sees as grid power flow.

That is a different category of thing from this repo: it **transmits**, it **changes inverter
behaviour**. https://github.com/dalklein/esphome-delta-acrel-mitm

**[robertklep/esphome-delta-solivia](https://github.com/robertklep/esphome-delta-solivia)** — an
ESPHome component for the older Delta Solivia inverters, and where to find Delta's
["Public Solar Inverter Communication Protocol v1.2"](https://github.com/robertklep/esphome-delta-solivia/blob/main/assets/Public%20RS485%20Protocol%201V2.pdf).

**If you have a documented Solivia model, use that project, not this one.** It implements the
protocol as published: one command, `CMD[96] + SUB-CMD[1]`, returning a struct, with a frame parser
per variant (4, 15, 27, 53, 212).

This repo's inverter is not in that document at all — it never mentions TL-US, E-series, M-series or
hybrid. The **frame layer is identical** (STX/ENQ/ACK/NAK/ETX, CRC16 `X16+X15+X2+1`), but above it
everything differs: `CMD 96` does not respond here, measurements come from `CMD 111` with a sub per
value, and the scalings carry one more decimal place. The full comparison is in the
`485 SOLIVIA MAP` sheet of the register map.  There may be more, but this is what we found, with trial & error searching the 485 port, starting from the older Solivia protocol.

## Credits

Built by [@dalklein](https://github.com/dalklein) with [Claude Code](https://claude.com/claude-code).
Protocol findings were cross-checked against an independent Raspberry Pi with an FTDI adapter on the
same bus, and against the vendor apps.

## Licence

[MIT](LICENSE).
