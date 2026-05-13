/*
 * HydroBuddy_Mark2
 * Automatic plant watering controller
 *
 * Hardware: XIAO ESP32S3, SSD1306 128x64 OLED (I2C),
 *           reed switch reservoir sensor, PBS-33B-BK button,
 *           IRLZ44N MOSFET pump driver
 *
 * Libraries required:
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 */

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include "secrets.h"

// ── Easy-adjust configuration ─────────────────────────────────────────────────
const unsigned long PUMP_DURATION    = 60000;                    // ms pump runs per cycle
const unsigned long DOUBLE_PRESS_WIN = 750;                      // ms window for double-press
const char*         NTP_SERVER       = "pool.ntp.org";
const char*         TZ_STRING        = "PST8PDT,M3.2.0,M11.1.0"; // US/Pacific

// ── Pin definitions (XIAO ESP32S3) ───────────────────────────────────────────
#define PIN_REED    D0   // Reed switch: LOW = reservoir OK, HIGH = empty
#define PIN_BUTTON  D1   // Momentary button (active LOW, internal pull-up)
#define PIN_PUMP    D2   // IRLZ44N MOSFET gate

#define OLED_SDA    D4
#define OLED_SCL    D5
#define OLED_ADDR   0x3C
#define SCREEN_W    128
#define SCREEN_H    64

// ── Internal constants ────────────────────────────────────────────────────────
const unsigned long WATER_INTERVAL = 24UL * 3600UL * 1000UL; // 24 hours in ms

// ── Globals ───────────────────────────────────────────────────────────────────
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

unsigned long lastWaterMs = 0;
bool          pumpRunning = false;
unsigned long pumpStartMs = 0;

// Button state
unsigned long btnPressMs  = 0;
bool          awaitDouble = false;
bool          btnLastLow  = false;

// ── Prototypes ────────────────────────────────────────────────────────────────
void   handleButton(unsigned long now);
void   onSinglePress();
void   onDoublePress();
void   startPump();
void   stopPump();
bool   reservoirOK();
void   updateDisplay();
void   drawStandby();
void   drawPumpRunning();
void   drawReservoirEmpty();
void   drawWiFiConnecting();
void   drawOTAProgress(unsigned int progress, unsigned int total);
void   showResetConfirm();
String formatNextWater();

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(PIN_REED,   INPUT_PULLUP);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_PUMP,   OUTPUT);
    digitalWrite(PIN_PUMP, LOW);

    Wire.begin(OLED_SDA, OLED_SCL);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println(F("SSD1306 init failed – check wiring"));
        while (true) delay(100);
    }

    display.setTextColor(SSD1306_WHITE);
    drawWiFiConnecting();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        configTzTime(TZ_STRING, NTP_SERVER);
        struct tm ti;
        t0 = millis();
        while (!getLocalTime(&ti) && millis() - t0 < 10000) {
            delay(200);
        }

        ArduinoOTA.setHostname("hydrobuddy-mark2");
        ArduinoOTA.onStart([]() {
            stopPump(); // safe state before flashing
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            drawOTAProgress(progress, total);
        });
        ArduinoOTA.onError([](ota_error_t error) {
            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(16, 24);
            display.print("OTA Error: ");
            display.print(error);
            display.display();
            delay(2000);
        });
        ArduinoOTA.begin();
    }

    lastWaterMs = millis(); // 24-hr clock starts at boot
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    ArduinoOTA.handle();

    unsigned long now = millis();

    // Reservoir protection – highest priority, overrides everything
    if (!reservoirOK() && pumpRunning) {
        stopPump();
    }

    handleButton(now);

    // Auto-water trigger
    if (!pumpRunning && now - lastWaterMs >= WATER_INTERVAL) {
        lastWaterMs = now; // reset regardless to prevent back-to-back retries
        if (reservoirOK()) startPump();
    }

    // Pump auto-stop
    if (pumpRunning && now - pumpStartMs >= PUMP_DURATION) {
        stopPump();
    }

    updateDisplay();
    delay(50);
}

// ── Button handling ───────────────────────────────────────────────────────────
void handleButton(unsigned long now) {
    bool isLow = (digitalRead(PIN_BUTTON) == LOW);

    if (isLow && !btnLastLow) {          // falling edge = press
        btnLastLow = true;
        if (awaitDouble && now - btnPressMs <= DOUBLE_PRESS_WIN) {
            awaitDouble = false;
            onDoublePress();
        } else {
            awaitDouble = true;
            btnPressMs  = now;
        }
    } else if (!isLow && btnLastLow) {   // rising edge = release
        btnLastLow = false;
    }

    // Single-press fires after the double-press window expires with no second press
    if (awaitDouble && now - btnPressMs > DOUBLE_PRESS_WIN) {
        awaitDouble = false;
        onSinglePress();
    }
}

void onSinglePress() {
    if (!pumpRunning && reservoirOK()) startPump();
}

void onDoublePress() {
    lastWaterMs = millis(); // reset 24-hr schedule from this moment
    showResetConfirm();
}

// ── Pump control ──────────────────────────────────────────────────────────────
void startPump() {
    if (!reservoirOK()) return;
    pumpStartMs = millis();
    pumpRunning = true;
    digitalWrite(PIN_PUMP, HIGH);
}

void stopPump() {
    digitalWrite(PIN_PUMP, LOW);
    pumpRunning = false;
}

bool reservoirOK() {
    return digitalRead(PIN_REED) == LOW;
}

// ── Display routing ───────────────────────────────────────────────────────────
void updateDisplay() {
    if (!reservoirOK()) {
        drawReservoirEmpty();
    } else if (pumpRunning) {
        drawPumpRunning();
    } else {
        drawStandby();
    }
}

// ── Screen: WiFi connecting (shown on boot) ───────────────────────────────────
void drawWiFiConnecting() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(8, 18);
    display.print("Connecting to WiFi");
    display.setCursor(34, 34);
    display.print("Please wait...");
    display.display();
}

// ── Screen: Standby ───────────────────────────────────────────────────────────
void drawStandby() {
    display.clearDisplay();

    // Current time – large
    struct tm ti;
    display.setTextSize(2);
    display.setCursor(0, 0);
    if (getLocalTime(&ti)) {
        char buf[9];
        strftime(buf, sizeof(buf), "%I:%M %p", &ti);
        display.print(buf);
    } else {
        display.print("--:-- --");
    }

    display.setTextSize(1);

    display.setCursor(0, 19);
    display.print("Next: ");
    display.print(formatNextWater());

    display.setCursor(0, 31);
    display.print("Tank:  OK");

    display.setCursor(0, 43);
    if (WiFi.status() == WL_CONNECTED) {
        display.print("WiFi: ");
        display.print(WiFi.localIP().toString());
    } else {
        display.print("WiFi: offline");
    }

    display.display();
}

// ── Screen: Pump running countdown ───────────────────────────────────────────
void drawPumpRunning() {
    unsigned long elapsed   = millis() - pumpStartMs;
    unsigned long remaining = (elapsed < PUMP_DURATION) ? PUMP_DURATION - elapsed : 0;
    int secs = (int)(remaining / 1000);

    display.clearDisplay();

    display.setTextSize(1);
    display.setCursor(22, 4);
    display.print("PUMP RUNNING");

    display.setTextSize(4);
    char buf[3];
    snprintf(buf, sizeof(buf), "%02d", secs);
    display.setCursor(40, 18);
    display.print(buf);

    display.setTextSize(1);
    display.setCursor(46, 55);
    display.print("sec");

    display.display();
}

// ── Screen: Reservoir empty warning ──────────────────────────────────────────
void drawReservoirEmpty() {
    display.clearDisplay();

    display.setTextSize(2);
    display.setCursor(8, 3);
    display.print("!! EMPTY !!");

    display.setTextSize(1);
    display.setCursor(4, 28);
    display.print("Reservoir is empty.");
    display.setCursor(16, 40);
    display.print("Pump blocked.");
    display.setCursor(10, 52);
    display.print("Refill to resume.");

    display.display();
}

// ── Confirmation: schedule reset blink ───────────────────────────────────────
void showResetConfirm() {
    struct tm ti;
    char hourBuf[9] = "--:-- --";
    if (getLocalTime(&ti)) {
        strftime(hourBuf, sizeof(hourBuf), "%I:%M %p", &ti);
    }

    for (int i = 0; i < 3; i++) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(10, 4);
        display.print("Schedule Reset!");
        display.setTextSize(2);
        display.setCursor(4, 20);
        display.print("From:");
        display.setTextSize(2);
        display.setCursor(4, 42);
        display.print(hourBuf);
        display.display();
        delay(300);

        display.clearDisplay();
        display.display();
        delay(150);
    }

    updateDisplay(); // restore normal display after blink
}

// ── Screen: OTA progress ─────────────────────────────────────────────────────
void drawOTAProgress(unsigned int progress, unsigned int total) {
    int pct = (total > 0) ? (int)((progress * 100UL) / total) : 0;
    int barW = (SCREEN_W - 4) * pct / 100;

    display.clearDisplay();

    display.setTextSize(1);
    display.setCursor(28, 6);
    display.print("OTA Update...");

    // Progress bar outline
    display.drawRect(2, 24, SCREEN_W - 4, 12, SSD1306_WHITE);
    // Progress bar fill
    if (barW > 0) display.fillRect(2, 24, barW, 12, SSD1306_WHITE);

    display.setTextSize(2);
    char buf[5];
    snprintf(buf, sizeof(buf), "%3d%%", pct);
    display.setCursor(28, 42);
    display.print(buf);

    display.display();
}

// ── Utility: next water countdown string ─────────────────────────────────────
String formatNextWater() {
    unsigned long elapsed = millis() - lastWaterMs;
    if (elapsed >= WATER_INTERVAL) return "now";
    unsigned long rem  = (WATER_INTERVAL - elapsed) / 1000;
    int h = (int)(rem / 3600);
    int m = (int)((rem % 3600) / 60);
    char buf[10];
    snprintf(buf, sizeof(buf), "%dh %02dm", h, m);
    return String(buf);
}
