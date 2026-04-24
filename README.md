# Teams Busy Light

A physical presence indicator that shows your Microsoft Teams status on a
WS2812B LED strip — no Azure app registration, no Graph API, no cloud calls.
Everything runs locally.

Two hardware options:

| | T-Display S3 AMOLED (Full) | Generic ESP32 (Budget) |
|---|---|---|
| **Board** | LilyGO T-Display S3 AMOLED 1.91" | Any ESP-WROOM-32 (Elegoo, NodeMCU, etc.) |
| **Price** | ~$25 | ~$8 |
| **Display** | 1.91" AMOLED status bar | None (LED strip only) |
| **LED strip** | ✅ GPIO 46 | ✅ GPIO 13 |
| **Firmware** | `busy_light_s3/` | `busy_light_basic/` |

```
  Microsoft Teams (New)
       │
       │  writes presence to local log file
       ▼
  ┌─────────────────────────┐
  │  Send-TeamsPresence.ps1  │     USB Serial (115200 baud)
  │  (PowerShell log tailer) │─────────────────────┐
  └─────────────────────────┘                      │
                                                   ▼
                          ┌──────────────────────────────────────┐
                          │  ESP32 (either variant)               │
                          │                                       │
                          │  Full build: + AMOLED status display  │
                          │  Budget:     LED strip only           │
                          │                                       │
                          │  GPIO 46 (S3) ──► WS2812B LED Strip  │
                          │  GPIO 13 (basic)─► WS2812B LED Strip │
                          └──────────────────────────────────────┘
```

## How It Works

1. **New Teams** writes presence changes to a local log file on every status transition.
2. **`Send-TeamsPresence.ps1`** tails that log, parses the status, and sends a
   single-byte command over USB serial to the ESP32.
3. **The ESP32** drives the onboard 1.91" AMOLED display (colored status bar)
   and an external WS2812B LED strip (solid/pulsing/breathing patterns).

No network calls, no tokens, no permissions — just a log file and a serial cable.

---

## Hardware

### Option A — Full Build (T-Display S3 AMOLED)

| Part | Notes |
|------|-------|
| [LilyGO T-Display S3 AMOLED](https://www.lilygo.cc/products/t-display-s3-amoled) | **1.91" Touch version** (ESP32-S3 + RM67162 AMOLED) |
| WS2812B LED strip | Any length — firmware default is 150 LEDs (16.4 ft) |
| 5V power supply | For the LED strip — **do NOT power long strips from USB** |
| USB-C cable | Powers the ESP32 and carries serial data to PC |
| Breadboard + jumper wires | For prototyping (or solder directly) |

**Wiring:**

```
                         ┌─────────────────────┐
                         │  T-Display S3 AMOLED │
                         │     (Touch 1.91")    │
                         │                      │
              USB-C ◄────┤ USB                   │
              to PC      │                      │
                         │ GPIO 46 (pin header) ├──────► WS2812B Data In (DIN)
                         │                      │
                         │ GND    (pin header)  ├──┬──► WS2812B GND
                         │                      │  │
                         │ VBUS   (pin header)  │  │    (optional: can power
                         │                      │  │     short strips < 30 LEDs)
                         └─────────────────────┘  │
                                                   │
                                              ┌────┴────┐
                                              │  5V PSU  │
                                              │  GND ────┼──► WS2812B GND
                                              │  +5V ────┼──► WS2812B VCC
                                              └─────────┘
```

> ⚡ **Critical: share ground** between the ESP32 and the external 5V supply.
> Without a common ground, the data signal won't work.

> ⚠️ **GPIO pin choice matters!** On the T-Display S3 AMOLED Touch:
> - **GPIO 8** — used by the AMOLED display. Do NOT use.
> - **GPIO 1** — ESP32-S3 strapping pin. Does NOT work with FastLED after boot.
> - **GPIO 46** — clean, no conflicts. **This is the one to use.**

> 💡 Check the arrows printed on the LED strip — data flows in one direction.
> Connect GPIO 46 to the **DIN** (input) end.

### Option B — Budget Build (Generic ESP32)

Any ESP-WROOM-32 dev board works: [Elegoo ESP32](https://www.amazon.com/ELEGOO-ESP-WROOM-32-Development-Bluetooth-Microcontroller/dp/B0D8T53CQ5), NodeMCU-32S, DOIT DevKit V1, etc. (~$8).

| Part | Notes |
|------|-------|
| ESP-WROOM-32 dev board | Any board with a USB-UART chip (CP2102, CH340, etc.) |
| WS2812B LED strip | Any length — firmware default is 150 LEDs |
| 5V power supply | For the LED strip |
| Micro-USB cable | Powers the ESP32 and carries serial data to PC |

**Driver note:** If your board uses a CP2102 chip and doesn't appear as a COM port,
install the [Silicon Labs CP210x driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers).
CH340 boards may need the [CH340 driver](http://www.wch-ic.com/downloads/CH341SER_EXE.html).

**Wiring:**

```
                         ┌─────────────────────┐
                         │  ESP-WROOM-32 Board  │
                         │  (Elegoo / generic)  │
                         │                      │
            Micro-USB ◄──┤ USB                   │
              to PC      │                      │
                         │ GPIO 13 (D13)        ├──────► WS2812B Data In (DIN)
                         │                      │
                         │ GND                  ├──┬──► WS2812B GND
                         │                      │  │
                         │ VIN (5V)             │  │    (can power short strips
                         │                      │  │     < 30 LEDs directly)
                         └─────────────────────┘  │
                                                   │
                                              ┌────┴────┐
                                              │  5V PSU  │
                                              │  GND ────┼──► WS2812B GND
                                              │  +5V ────┼──► WS2812B VCC
                                              └─────────┘
```

> ⚡ **Share ground** between the ESP32 and the external 5V supply.

> 💡 GPIO 13 is a safe, general-purpose output with no boot conflicts on the ESP-WROOM-32.

---

## Quick Start

### 1. Flash the ESP32

#### Full Build (T-Display S3 AMOLED)

**Option A — Pre-compiled binary (no build tools needed):**

1. Install [esptool](https://github.com/espressif/esptool): `pip install esptool`
2. Put the board in bootloader mode: hold **BOOT** → tap **RST** → release **BOOT**
3. Flash:
   ```powershell
   esptool --chip esp32s3 --port COM4 write_flash 0x0 firmware/busy_light_s3.ino.merged.bin
   ```

**Option B — Build from source (Arduino CLI):**

1. Install [arduino-cli](https://arduino.github.io/arduino-cli/)
2. Install ESP32 core and libraries:
   ```powershell
   arduino-cli core install esp32:esp32@3.3.8
   arduino-cli lib install "FastLED@3.10.3"
   # Install from ZIP: LilyGo-AMOLED-Series 1.2.4, lvgl 8.4.0, SensorLib 0.3.4
   ```
3. Edit `busy_light_s3/busy_light_s3.ino` — adjust `NUM_LEDS` for your strip length.
4. Compile and upload:
   ```powershell
   arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=opi" busy_light_s3
   arduino-cli upload -p COM4 --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=opi" busy_light_s3
   ```

| Arduino IDE Setting | Value |
|---------------------|-------|
| Board | `ESP32S3 Dev Module` |
| USB Mode | `Hardware CDC and JTAG` |
| USB CDC On Boot | `Enabled` |
| PSRAM | `OPI PSRAM` |

#### Budget Build (Generic ESP32)

**Option A — Pre-compiled binary:**

1. Install [esptool](https://github.com/espressif/esptool): `pip install esptool`
2. Flash (no bootloader button needed — CP2102 boards auto-reset):
   ```powershell
   esptool --chip esp32 --port COM7 write_flash 0x0 firmware/busy_light_basic.ino.merged.bin
   ```

**Option B — Build from source:**

1. Install [arduino-cli](https://arduino.github.io/arduino-cli/)
2. Install ESP32 core and FastLED:
   ```powershell
   arduino-cli core install esp32:esp32@3.3.8
   arduino-cli lib install "FastLED@3.10.3"
   ```
   That's it — the budget build has **no other library dependencies**.
3. Edit `busy_light_basic/busy_light_basic.ino` — adjust `NUM_LEDS` for your strip length.
4. Compile and upload:
   ```powershell
   arduino-cli compile --fqbn "esp32:esp32:esp32" busy_light_basic
   arduino-cli upload -p COM7 --fqbn "esp32:esp32:esp32" busy_light_basic
   ```

| Arduino IDE Setting | Value |
|---------------------|-------|
| Board | `ESP32 Dev Module` |
| Upload Speed | `921600` |

### 2. Run the PowerShell Script

```powershell
.\desktop\Send-TeamsPresence.ps1
```

The script will:
- Auto-detect the ESP32's COM port
- Scan the Teams log for the last known presence (instant light-up on start)
- Tail the log for real-time changes
- Reconnect automatically if the serial connection drops

**Optional parameters:**
```powershell
.\desktop\Send-TeamsPresence.ps1 -ComPort COM4 -PollIntervalMs 1000
```

### 3. Startup Automation (Optional)

Run once to register the script to launch automatically at login (hidden):

```powershell
.\desktop\Install-Startup.ps1
```

To remove:
```powershell
.\desktop\Install-Startup.ps1 -Remove
```

---

## Presence → Color Map

| Teams Status | AMOLED (full build) | LED Strip (both builds) | Behavior |
|---|---|---|---|
| Available | Green bar | Green | Solid |
| Busy | Red bar | Red | Solid |
| In a Meeting / Call / Presenting | Red bar | Red | Pulsing (30 BPM) |
| Be Right Back | Yellow bar | Yellow | Solid |
| Away | Yellow bar | Yellow | Breathing (15 BPM) |
| Do Not Disturb | Magenta bar | Magenta | Solid |
| Unknown | Dim white bar | Dim white | Solid |
| Offline | Dim blue bar | Off | — |

## Serial Protocol

Single-byte commands over USB serial at **115200 baud**:

| Byte | Status | LED Color |
|------|--------|-----------|
| `0x00` | Offline | Off |
| `0x01` | Available | Green |
| `0x02` | Busy | Red |
| `0x03` | In a Meeting | Red (pulsing) |
| `0x04` | Be Right Back | Yellow |
| `0x05` | Away | Yellow (breathing) |
| `0x06` | Do Not Disturb | Magenta |
| `0x07` | Unknown | Dim white |

ESP32 responds: `ACK:0x0N [StatusLabel]\n`

---

## Project Structure

```
teams-busy-light/
├── busy_light_s3/             # Full build firmware (T-Display S3 AMOLED)
│   └── busy_light_s3.ino     #   AMOLED display + WS2812B LEDs + serial
├── busy_light_basic/          # Budget build firmware (generic ESP32)
│   └── busy_light_basic.ino   #   WS2812B LEDs + serial (no display)
├── desktop/
│   └── Send-TeamsPresence.ps1 # PowerShell log tailer (works with either build)
├── firmware/
│   ├── busy_light_s3.ino.merged.bin     # Pre-compiled: T-Display S3 AMOLED
│   └── busy_light_basic.ino.merged.bin  # Pre-compiled: generic ESP32
└── README.md
```

## How Each Piece Works

### `busy_light_s3.ino` — Full Build Firmware (T-Display S3 AMOLED)

- Initializes the RM67162 AMOLED display using the LilyGo-AMOLED-Series library
- Allocates a raw RGB565 framebuffer in PSRAM and pushes pixels directly
- RGB565 values are **byte-swapped** for the RM67162's big-endian expectation
- Listens on USB serial (Hardware CDC) for single-byte presence commands
- Drives the AMOLED with a colored status bar (top 80px) on a black background
- Drives the WS2812B strip via FastLED on GPIO 46 with solid, pulsing, or breathing patterns
- Sends heartbeat messages every 10 seconds and ACKs every command

### `busy_light_basic.ino` — Budget Build Firmware (Generic ESP32)

- **No display, no PSRAM, no LilyGo library** — just FastLED + Serial
- Only dependency: FastLED 3.10.3
- Listens on hardware UART serial for single-byte presence commands
- Drives WS2812B strip via FastLED on GPIO 13 with the same color patterns as the full build
- R→G→B startup flash confirms wiring is correct
- Same serial protocol — `Send-TeamsPresence.ps1` works unchanged

### `Send-TeamsPresence.ps1` — Desktop Script

- Finds the latest `MSTeams_*.log` in the New Teams log directory
- On startup, scans the entire log for the most recent presence → sends immediately
- Tails the log for two patterns:
  - `CloudStateChanged` — real-time presence transitions
  - `BroadcastGlobalState` — periodic (~5 min) presence broadcasts
- Extracts `availability: <Status>` from matched lines
- Maps Teams statuses to single-byte commands and writes them to USB serial
- Auto-detects the ESP32 COM port on startup (searches for USB/CH340/CP210x/ESP devices)
- Handles log rotation (Teams creates new log files periodically)
- Reconnects on serial errors

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| **AMOLED stays blank** | Ensure you selected `ESP32S3 Dev Module` with `PSRAM: OPI PSRAM`. The plain T-Display S3 (non-AMOLED) uses a different display driver. |
| **LEDs don't light up** | Check wiring: GPIO 46 → DIN, shared GND. Verify data flows with the arrows on the strip. If using a different GPIO, beware of strapping pins (GPIO 0, 1, 3, 45, 46 are safe; GPIO 46 is tested). |
| **LEDs flicker or wrong colors** | Ensure common GND between ESP32 and external PSU. A 330Ω resistor on the data line can help with long wires. |
| **Script says "Teams log not found"** | You need **New Teams** (MSIX package). Classic Teams uses a different log path. |
| **Script doesn't detect COM port** | Specify manually: `-ComPort COM4`. Check Device Manager for the port number. |
| **Colors lag behind status changes** | Default poll interval is 2 seconds. Presence appears in logs within ~5–30 seconds of the actual change. |
| **esptool blocked by App Control** | On managed Windows, PyInstaller `.exe` files may be blocked. Use `python -m esptool` instead, or build from source with arduino-cli. |
| **Bootloader mode** | Hold **BOOT** → tap **RST** → release **BOOT**. The board will appear as a new COM port. |
| **Budget board not detected** | Install the [CP2102 driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) (Silicon Labs) or [CH340 driver](http://www.wch-ic.com/downloads/CH341SER_EXE.html). Unplug and replug after installing. |

## Credits

Built with:
- [LilyGo-AMOLED-Series](https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series) — display driver
- [FastLED](https://github.com/FastLED/FastLED) — LED strip control
- [arduino-cli](https://arduino.github.io/arduino-cli/) — build toolchain

## License

MIT
