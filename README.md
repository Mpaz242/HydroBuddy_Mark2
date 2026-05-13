# HydroBuddy Mark 2

Automatic plant watering controller built on the **XIAO ESP32S3** with WiFi, NTP time sync, a 128×64 SSD1306 OLED, and reservoir-level protection via a reed switch.

---

## Hardware

| Component | Part |
|---|---|
| Microcontroller | Seeed XIAO ESP32S3 |
| Display | SSD1306 128×64 OLED (I2C) |
| Reservoir sensor | Reed switch (magnetic float) |
| Button | PBS-33B-BK momentary pushbutton |
| Pump driver | IRLZ44N N-channel MOSFET |
| Pump | 3V–5V submersible or peristaltic |

---

## Wiring

| Signal | XIAO Pin | Notes |
|---|---|---|
| Reed switch | D0 | One leg to D0, other to GND. LOW = full, HIGH = empty. |
| Button | D1 | One leg to D1, other to GND. Active LOW. |
| MOSFET gate | D2 | IRLZ44N gate via 100Ω resistor. Add 10kΩ gate-to-GND pull-down. |
| OLED SDA | D4 | 4.7kΩ pull-up to 3.3V |
| OLED SCL | D5 | 4.7kΩ pull-up to 3.3V |
| OLED VCC | 3V3 | |
| OLED GND | GND | |

> **MOSFET note:** The IRLZ44N is logic-level but its drain/source handles pump power. Connect pump positive to your supply rail, pump negative to MOSFET drain, MOSFET source to GND. Add a flyback diode across the pump terminals (cathode toward supply).

---

## Required Libraries

Install via **Arduino IDE → Tools → Manage Libraries**:

- `Adafruit SSD1306`
- `Adafruit GFX Library`

The ESP32 Arduino core provides `WiFi.h` and `time.h`.

---

## Board Setup

1. In Arduino IDE, add the Seeed XIAO ESP32S3 board:
   - **File → Preferences → Additional board manager URLs:**
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. **Tools → Board → ESP32 Arduino → XIAO_ESP32S3**
3. **Tools → Upload Speed → 115200**

---

## First-time Setup

1. Copy `secrets_template.h` to `secrets.h`
2. Fill in your WiFi credentials in `secrets.h`
3. Upload the sketch

`secrets.h` is in `.gitignore` and will never be committed.

---

## Configuration Variables

At the top of `HydroBuddy_Mark2.ino`:

| Variable | Default | Description |
|---|---|---|
| `PUMP_DURATION` | `60000` ms | How long the pump runs per cycle |
| `DOUBLE_PRESS_WIN` | `750` ms | Time window to detect a double press |
| `NTP_SERVER` | `pool.ntp.org` | NTP time server |
| `TZ_STRING` | `PST8PDT,M3.2.0,M11.1.0` | POSIX timezone (US/Pacific) |

---

## Behavior

### Auto-watering
- The 24-hour cycle starts at boot.
- When the timer elapses the pump runs for `PUMP_DURATION` ms.
- The timer resets automatically after each auto-water cycle.

### Button
| Action | Result |
|---|---|
| Single press | Run pump now for `PUMP_DURATION` ms |
| Double press (within `DOUBLE_PRESS_WIN`) | Reset 24-hr schedule from this moment; OLED blinks confirmation showing the current time |

### Reservoir protection
- Reed switch LOW = reservoir OK, pump allowed.
- Reed switch HIGH = reservoir empty — pump is immediately stopped and blocked.
- This check runs every loop iteration and cannot be overridden.

---

## OLED Screens

| State | Display |
|---|---|
| Boot | "Connecting to WiFi / Please wait..." |
| Standby | Current time, next water countdown, tank status, WiFi IP |
| Pump running | "PUMP RUNNING" + large countdown in seconds |
| Reservoir empty | "!! EMPTY !!" warning with instructions |
| Schedule reset | Blinks current time 3× as confirmation |

---

## Timezone Reference

Change `TZ_STRING` in the sketch to any POSIX timezone string:

| Zone | String |
|---|---|
| US/Pacific | `PST8PDT,M3.2.0,M11.1.0` |
| US/Mountain | `MST7MDT,M3.2.0,M11.1.0` |
| US/Central | `CST6CDT,M3.2.0,M11.1.0` |
| US/Eastern | `EST5EDT,M3.2.0,M11.1.0` |
