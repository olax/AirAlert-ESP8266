// AirAlert-ESP8266 — Phase 1 skeleton.
// Boot order per SPEC 25: relay SAFE/OFF before anything else (Invariant 3).
#include <Arduino.h>

namespace pins {
constexpr uint8_t RELAY = 14; // D5 (SPEC 61, 64)
constexpr uint8_t MUTE = 12;  // D6
constexpr uint8_t TEST = 13;  // D7
} // namespace pins

// Polarity is unknown until hardware commissioning (SPEC 39).
// ACTIVE_HIGH assumed: LOW = relay off. Revisit in Phase 10.
static void forceRelayOff() {
    digitalWrite(pins::RELAY, LOW);
    pinMode(pins::RELAY, OUTPUT);
    digitalWrite(pins::RELAY, LOW);
}

void setup() {
    forceRelayOff(); // step 1, before any other init

    Serial.begin(115200);
    Serial.println();
    Serial.printf("[BOOT] AirAlert-ESP8266 %s (%s)\n", AIRALERT_VERSION, __DATE__);
    Serial.printf("[BOOT] reset reason: %s\n", ESP.getResetReason().c_str());
    Serial.printf("[BOOT] free heap: %u\n", ESP.getFreeHeap());

    pinMode(pins::MUTE, INPUT_PULLUP);
    pinMode(pins::TEST, INPUT_PULLUP);
}

void loop() {
    static uint32_t lastBeat = 0;
    if (millis() - lastBeat >= 5000) {
        lastBeat = millis();
        Serial.printf("[SYS] uptime=%lus heap=%u relay=OFF\n",
                      (unsigned long)(millis() / 1000), ESP.getFreeHeap());
    }
}
