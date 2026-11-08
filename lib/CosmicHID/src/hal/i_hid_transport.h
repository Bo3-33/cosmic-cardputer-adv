#pragma once
#include <cstdint>

#include "../core/hid_report.h"
#include "../core/settings.h"

struct IHidTransport {
    virtual ~IHidTransport() = default;
    virtual void begin(const Settings& s) = 0;
    virtual bool connected() const = 0;
    virtual bool sendKeyboard(const KeyboardReport& r) = 0;  // false = not sent (disconnected/busy)
    virtual bool sendMouse(const MouseReport& r) = 0;
    virtual void onLed(void (*cb)(uint8_t leds, void* ctx), void* ctx) = 0;  // CapsLock etc.
    // BLE-only concepts; USB implementation returns defaults / no-ops.
    virtual uint8_t hostSlot() const { return 0; }
    virtual void    switchHostSlot(uint8_t) {}
    virtual void    clearBonding() {}
};
