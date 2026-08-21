#pragma once
// Simplified two-channel indication (owner decision 2026-08-21, allowed by
// SPEC 208/209): onboard LED = system health, one external output = alert.
// The alert output may drive a bare LED or a relay module with a lamp -
// `steady` suppresses blink patterns so an indication relay never chatters.
// The semantic inputs are backend-agnostic: a future RGB backend (WS2812 or
// MCP23017, SPEC 56-60) implements the same setters.
#include <Arduino.h>

class Indication {
public:
    enum class SysState : uint8_t {
        Ok,       // solid: Wi-Fi + time + token + API fresh
        Degraded, // slow blink: something is down, see Web UI / log
        Setup     // fast blink: provisioning / no token / OTA
    };
    enum class AlertView : uint8_t { None, Partial, Full };

    void begin(uint8_t systemPin, bool systemInverted,
               uint8_t alertPin, bool alertActiveHigh, bool steady) {
        sysPin_ = systemPin;
        sysInv_ = systemInverted;
        alertPin_ = alertPin;
        alertAh_ = alertActiveHigh;
        steady_ = steady;
        // alert output may drive a relay: force OFF before pinMode, like the
        // siren relay (Invariant 3 spirit)
        digitalWrite(alertPin_, alertAh_ ? LOW : HIGH);
        pinMode(alertPin_, OUTPUT);
        digitalWrite(alertPin_, alertAh_ ? LOW : HIGH);
        pinMode(sysPin_, OUTPUT);
        writeSys(false);
    }

    void setSystem(SysState s) { sys_ = s; }
    void setAlert(AlertView v, bool muted, bool sirenOn) {
        alert_ = v;
        muted_ = muted;
        sirenOn_ = sirenOn;
    }

    void tick(uint32_t now) {
        bool sysOn = false;
        switch (sys_) {
            case SysState::Ok: sysOn = true; break;
            case SysState::Degraded: sysOn = (now / 1000) % 2 == 0; break;
            case SysState::Setup: sysOn = (now / 200) % 2 == 0; break;
        }
        writeSys(sysOn);

        bool aOn = false;
        if (alert_ != AlertView::None) {
            if (steady_) {
                aOn = true; // relay/lamp mode: any alert = solid ON
            } else if (sirenOn_) {
                aOn = (now / 125) % 2 == 0; // mirrors the siren pattern
            } else if (muted_) {
                aOn = (now % 3000) < 150; // short flash, long pause (SPEC 51)
            } else if (alert_ == AlertView::Partial) {
                aOn = (now / 1000) % 2 == 0;
            } else {
                aOn = true;
            }
        }
        writeAlert(aOn);
    }

private:
    void writeSys(bool on) {
        if (on == sysCur_) return;
        sysCur_ = on;
        digitalWrite(sysPin_, on != sysInv_ ? HIGH : LOW);
    }
    void writeAlert(bool on) {
        if (on == alertCur_) return;
        alertCur_ = on;
        digitalWrite(alertPin_, on == alertAh_ ? HIGH : LOW);
    }

    uint8_t sysPin_ = 2, alertPin_ = 16;
    bool sysInv_ = true, alertAh_ = true, steady_ = false;
    bool sysCur_ = false, alertCur_ = false;
    SysState sys_ = SysState::Setup;
    AlertView alert_ = AlertView::None;
    bool muted_ = false, sirenOn_ = false;
};
