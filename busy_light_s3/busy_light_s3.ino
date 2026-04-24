/*
 * Teams Busy Light — LilyGO T-Display S3 AMOLED Edition
 *
 * Receives Teams presence status over USB serial from a
 * PowerShell script that monitors the local Teams log file.
 * No Azure App Registration, no Graph API, no HID needed.
 *
 * The onboard 1.91" AMOLED shows the current presence status.
 * An optional WS2812B LED strip mirrors the color.
 *
 * Hardware:
 *   - LilyGO T-Display S3 AMOLED (ESP32-S3 + 1.91" RM67162)
 *   - Optional: externally powered 5V WS2812B strip on GPIO 46
 *
 *   WiFi and Bluetooth are explicitly disabled at boot.
 *
 * Serial Protocol (115200 baud, single byte commands):
 *   0x00 = Offline/Off       (dim blue on AMOLED, LEDs off)
 *   0x01 = Available          (green)
 *   0x02 = Busy               (red)
 *   0x03 = In a Meeting       (pulsing red)
 *   0x04 = Be Right Back      (yellow)
 *   0x05 = Away               (breathing yellow)
 *   0x06 = Do Not Disturb     (magenta)
 *   0x07 = Unknown            (dim white)
 *
 * Arduino IDE / CLI Setup:
 *   Board:   ESP32S3 Dev Module
 *   USB Mode: Hardware CDC and JTAG
 *   PSRAM:   OPI PSRAM
 */

#include <FastLED.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <LilyGo_AMOLED.h>

// ─── Hardware Config ──────────────────────────────────────────
#define NUM_LEDS      150      // WS2812B LED count (16.4ft strip)
#define DATA_PIN      46       // GPIO 46 (header pin, GPIO 8 used by AMOLED)
#define BRIGHTNESS    120
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB

// ─── Timing ───────────────────────────────────────────────────
#define DISPLAY_UPDATE_MS 500
#define LOOP_DELAY_MS     20

// ─── Display dimensions (set from library at runtime) ─────────
static int dispW = 536;
static int dispH = 240;

// ─── Presence Status Enum ─────────────────────────────────────
enum PresenceStatus {
  STATUS_OFFLINE,
  STATUS_AVAILABLE,
  STATUS_BUSY,
  STATUS_IN_MEETING,
  STATUS_BRB,
  STATUS_AWAY,
  STATUS_DND,
  STATUS_UNKNOWN
};

// ─── Global State ─────────────────────────────────────────────
CRGB leds[NUM_LEDS];
LilyGo_Class amoled;
uint16_t *fb = nullptr;

uint8_t serialMode = 0x00;
unsigned long lastDisplayUpdate = 0;
PresenceStatus currentStatus = STATUS_OFFLINE;
PresenceStatus displayedStatus = (PresenceStatus)-1;

// ─── Helpers ──────────────────────────────────────────────────
const char* statusLabel(PresenceStatus s) {
  switch (s) {
    case STATUS_AVAILABLE:  return "Available";
    case STATUS_BUSY:       return "Busy";
    case STATUS_IN_MEETING: return "In a Meeting";
    case STATUS_AWAY:       return "Away";
    case STATUS_BRB:        return "Be Right Back";
    case STATUS_DND:        return "Do Not Disturb";
    case STATUS_OFFLINE:    return "Offline";
    default:                return "Unknown";
  }
}

// 16-bit 565 color (byte-swapped for RM67162 big-endian)
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  return (c >> 8) | (c << 8);
}

void fbFill(uint16_t color) {
  if (!fb) return;
  for (uint32_t i = 0; i < (uint32_t)dispW * dispH; i++) fb[i] = color;
}

void fbRect(int x, int y, int w, int h, uint16_t color) {
  if (!fb) return;
  for (int row = y; row < y + h && row < dispH; row++)
    for (int col = x; col < x + w && col < dispW; col++)
      fb[row * dispW + col] = color;
}

// ─── Status → RGB mapping ─────────────────────────────────────
void statusToRGB(PresenceStatus s, uint8_t &r, uint8_t &g, uint8_t &b) {
  switch (s) {
    case STATUS_AVAILABLE:  r=0;   g=255; b=0;   break;
    case STATUS_BUSY:       r=220; g=20;  b=10;  break;
    case STATUS_IN_MEETING: r=220; g=20;  b=10;  break;
    case STATUS_BRB:        r=220; g=180; b=0;   break;
    case STATUS_AWAY:       r=220; g=180; b=0;   break;
    case STATUS_DND:        r=200; g=0;   b=120; break;
    case STATUS_UNKNOWN:    r=40;  g=40;  b=40;  break;
    default:                r=0;   g=0;   b=60;  break; // dim blue idle
  }
}

// ─── AMOLED Display Rendering ─────────────────────────────────
void updateDisplay(PresenceStatus status) {
  if (!fb) return;
  if (status == displayedStatus) return;
  displayedStatus = status;

  uint8_t r, g, b;
  statusToRGB(status, r, g, b);

  uint16_t statusColor = rgb565(r, g, b);
  uint16_t bgColor     = rgb565(0, 0, 0);

  fbFill(bgColor);

  // Full-screen colored status bar (top 80px)
  fbRect(0, 0, dispW, 80, statusColor);
  // Thin accent line
  fbRect(0, 82, dispW, 2, statusColor);

  amoled.pushColors(0, 0, dispW, dispH, fb);
  Serial.printf("[DISP] %s (#%02X%02X%02X)\n", statusLabel(status), r, g, b);
}

// ─── LED Patterns ─────────────────────────────────────────────
void setAll(CRGB color) {
  fill_solid(leds, NUM_LEDS, color);
  FastLED.show();
}

void pulseLeds(CRGB color) {
  uint8_t val = beatsin8(30, 40, 255);
  CRGB dimmed = color;
  dimmed.nscale8(val);
  fill_solid(leds, NUM_LEDS, dimmed);
  FastLED.show();
}

void breatheLeds(CRGB color) {
  uint8_t val = beatsin8(15, 20, 200);
  CRGB dimmed = color;
  dimmed.nscale8(val);
  fill_solid(leds, NUM_LEDS, dimmed);
  FastLED.show();
}

// ─── Serial → Status ──────────────────────────────────────────
PresenceStatus serialToStatus(uint8_t mode) {
  switch (mode) {
    case 0x00: return STATUS_OFFLINE;
    case 0x01: return STATUS_AVAILABLE;
    case 0x02: return STATUS_BUSY;
    case 0x03: return STATUS_IN_MEETING;
    case 0x04: return STATUS_BRB;
    case 0x05: return STATUS_AWAY;
    case 0x06: return STATUS_DND;
    default:   return STATUS_UNKNOWN;
  }
}

// ─── Setup ────────────────────────────────────────────────────
void setup() {
  // Disable radios — this device is serial-only, no attack surface
  WiFi.mode(WIFI_OFF);
  esp_bt_controller_disable();

  Serial.begin(115200);

  // ── AMOLED Display ──────────────────────────────────────────
  bool dispOk = amoled.begin();
  if (!dispOk) dispOk = amoled.beginAMOLED_191();

  if (dispOk) {
    amoled.setRotation(1);
    amoled.setBrightness(200);
    dispW = amoled.width();
    dispH = amoled.height();
    fb = (uint16_t *)ps_malloc(dispW * dispH * sizeof(uint16_t));
    if (!fb) fb = (uint16_t *)malloc(dispW * dispH * sizeof(uint16_t));
  }

  // Boot splash: blue → cyan
  if (fb && dispOk) {
    for (int i = 0; i < dispW * dispH; i++) fb[i] = rgb565(0, 0, 255);
    amoled.pushColors(0, 0, dispW, dispH, fb);
    delay(500);
  }

  // ── LED Strip ───────────────────────────────────────────────
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  setAll(CRGB::Black);

  // Cyan = ready
  if (fb && dispOk) {
    for (int i = 0; i < dispW * dispH; i++) fb[i] = rgb565(0, 200, 200);
    amoled.pushColors(0, 0, dispW, dispH, fb);
  }

  delay(500);

  Serial.println(F("────────────────────────────────────────"));
  Serial.println(F("Teams Busy Light — Serial Mode"));
  Serial.printf ("Display: %s (%dx%d)\n", dispOk ? "OK" : "FAIL", dispW, dispH);
  Serial.println(F("READY"));
  Serial.println(F("────────────────────────────────────────"));

  // Show idle state
  updateDisplay(STATUS_OFFLINE);
}

// ─── Main Loop ────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── Read serial commands ────────────────────────────────────
  if (Serial.available() > 0) {
    uint8_t cmd = Serial.read();
    if (cmd <= 0x07) {
      serialMode = cmd;
      currentStatus = serialToStatus(cmd);
      Serial.printf("ACK:0x%02X [%s]\n", cmd, statusLabel(currentStatus));
    }
  }

  // ── Drive LEDs ──────────────────────────────────────────────
  switch (serialMode) {
    case 0x00: setAll(CRGB::Black);            break;
    case 0x01: setAll(CRGB(0, 255, 0));       break;
    case 0x02: setAll(CRGB(220, 20, 10));      break;
    case 0x03: pulseLeds(CRGB(220, 20, 10));   break;
    case 0x04: setAll(CRGB(220, 180, 0));      break;
    case 0x05: breatheLeds(CRGB(220, 180, 0)); break;
    case 0x06: setAll(CRGB(200, 0, 120));      break;
    case 0x07: setAll(CRGB(40, 40, 40));       break;
    default:   setAll(CRGB::Black);            break;
  }

  // ── Update AMOLED ───────────────────────────────────────────
  if (now - lastDisplayUpdate > DISPLAY_UPDATE_MS) {
    lastDisplayUpdate = now;
    updateDisplay(currentStatus);
  }

  delay((serialMode == 0x03 || serialMode == 0x05) ? LOOP_DELAY_MS : 100);

  // Heartbeat
  static unsigned long lastHB = 0;
  if (now - lastHB > 10000) {
    lastHB = now;
    Serial.printf("[HB] %lus %s fb=%s\n",
      now/1000, statusLabel(currentStatus), fb ? "ok" : "null");
  }
}
