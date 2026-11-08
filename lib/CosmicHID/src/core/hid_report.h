#pragma once
#include <cstdint>

// Boot-compatible keyboard report (8 bytes on the wire: the transport adds the
// reserved byte / report ID as its protocol requires). D10.
struct KeyboardReport {
    uint8_t modifiers = 0;    // bit0 LCtrl, bit1 LShift, bit2 LAlt, bit3 LGui, bit4..7 right side
    uint8_t keys[6]   = {0};  // HID usage IDs, 0 = empty slot

    bool operator==(const KeyboardReport& o) const {
        if (modifiers != o.modifiers) return false;
        for (int i = 0; i < 6; ++i) {
            if (keys[i] != o.keys[i]) return false;
        }
        return true;
    }
    bool operator!=(const KeyboardReport& o) const { return !(*this == o); }
};

// Relative mouse report.
struct MouseReport {
    uint8_t buttons = 0;      // bit0 L, bit1 R, bit2 M
    int8_t  dx = 0, dy = 0, wheel = 0;
};
