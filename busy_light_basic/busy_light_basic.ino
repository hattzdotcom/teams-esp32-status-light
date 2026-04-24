/*
 * Teams Busy Light — Budget Build (ESP-WROOM-32)
 * 
 * LED-strip-only variant for generic/Elegoo ESP32 boards.
 * No display, no PSRAM, no special libraries — just FastLED + Serial.
 * Same serial protocol as the T-Display S3 build.
 *
 * Wiring:
 *   GPIO 13  → WS2812B data in
 *   GND      → WS2812B GND
 *   5V (VIN) → WS2812B +5V  (use external PSU for long strips)
 *
 * Serial: 115200 baud, single-byte commands
 *   0x00=Offline  0x01=Available  0x02=Busy  0x03=InMeeting
 *   0x04=BRB      0x05=Away       0x06=DND   0x07=Unknown
 */

#include <FastLED.h>
#include <WiFi.h>
#include <esp_bt.h>

// ── Pin & LED config ────────────────────────────────────────────────
#define DATA_PIN    13
#define NUM_LEDS    150
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS  180

CRGB leds[NUM_LEDS];

// ── Status tracking ─────────────────────────────────────────────────
enum Status : uint8_t {
  STATUS_OFFLINE   = 0x00,
  STATUS_AVAILABLE = 0x01,
  STATUS_BUSY      = 0x02,
  STATUS_INMEETING = 0x03,
  STATUS_BRB       = 0x04,
  STATUS_AWAY      = 0x05,
  STATUS_DND       = 0x06,
  STATUS_UNKNOWN   = 0x07
};

Status currentStatus = STATUS_OFFLINE;

const char* statusLabel(Status s) {
  switch (s) {
    case STATUS_AVAILABLE: return "Available";
    case STATUS_BUSY:      return "Busy";
    case STATUS_INMEETING: return "InMeeting";
    case STATUS_BRB:       return "BeRightBack";
    case STATUS_AWAY:      return "Away";
    case STATUS_DND:       return "DoNotDisturb";
    case STATUS_UNKNOWN:   return "Unknown";
    default:               return "Offline";
  }
}

// ── LED patterns ────────────────────────────────────────────────────
void updateLEDs() {
  switch (currentStatus) {

    case STATUS_AVAILABLE:                       // solid green
      fill_solid(leds, NUM_LEDS, CRGB(0, 255, 0));
      break;

    case STATUS_BUSY:                            // solid red
      fill_solid(leds, NUM_LEDS, CRGB(220, 20, 10));
      break;

    case STATUS_INMEETING: {                     // pulsing red
      uint8_t pulse = beatsin8(30, 60, 255);
      fill_solid(leds, NUM_LEDS, CRGB(pulse, pulse / 12, 0));
      break;
    }

    case STATUS_BRB:                             // solid yellow
      fill_solid(leds, NUM_LEDS, CRGB(220, 180, 0));
      break;

    case STATUS_AWAY: {                          // breathing yellow
      uint8_t breath = beatsin8(15, 40, 220);
      fill_solid(leds, NUM_LEDS, CRGB(breath, breath * 3 / 4, 0));
      break;
    }

    case STATUS_DND:                             // solid magenta
      fill_solid(leds, NUM_LEDS, CRGB(200, 0, 120));
      break;

    case STATUS_UNKNOWN:                         // dim white
      fill_solid(leds, NUM_LEDS, CRGB(40, 40, 40));
      break;

    case STATUS_OFFLINE:                         // off
    default:
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      break;
  }

  FastLED.show();
}

// ── Startup self-test ───────────────────────────────────────────────
void startupFlash() {
  CRGB colors[] = { CRGB::Red, CRGB::Green, CRGB::Blue };
  for (int i = 0; i < 3; i++) {
    fill_solid(leds, NUM_LEDS, colors[i]);
    FastLED.show();
    delay(300);
  }
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// ── Setup ───────────────────────────────────────────────────────────
void setup() {
  // Disable radios — this device is serial-only, no attack surface
  WiFi.mode(WIFI_OFF);
  esp_bt_controller_disable();
  
  Serial.begin(115200);
  delay(500);

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  startupFlash();

  Serial.println("Teams Busy Light (Budget) ready");
  Serial.println("Send single byte: 0x00-0x07");
}

// ── Main loop ───────────────────────────────────────────────────────
void loop() {
  if (Serial.available()) {
    uint8_t cmd = Serial.read();
    if (cmd <= 0x07) {
      currentStatus = (Status)cmd;
      char buf[40];
      snprintf(buf, sizeof(buf), "ACK:0x%02X %s", cmd, statusLabel(currentStatus));
      Serial.println(buf);
    }
  }

  updateLEDs();
  FastLED.delay(20);   // ~50 fps, also calls FastLED.show()
}
