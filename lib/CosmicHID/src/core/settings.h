#pragma once
#include <cstddef>
#include <cstdint>

// Persisted user settings (guide §5.6). Plain "key=value\n" text so users can
// hand-edit the file on the TF card; no JSON library in core.
struct Settings {
    uint8_t version      = 1;
    uint8_t transport    = 0;   // 0=BLE, 1=USB (D1: manual switch, reboot applies)
    uint8_t inputMode    = 2;   // last InputMode, default Combo (D2)
    uint8_t sensLevel    = 3;   // 1..5
    uint8_t clutchMode   = 0;   // 0=mode-default, 1=hold-to-move, 2=hold-to-freeze, 3=always-on
    uint8_t cursorSource = 0;   // 0=IMU air-mouse, 1=arrow keys (FR-13 / D14)
    uint8_t bleSlot      = 0;   // 0..2
    uint8_t brightness   = 80;  // screen backlight percent, 20..100 (menu steps by 20)
    char    bleName[32]  = "Cardputer-Keyboard-Mouse";

    // Serialize as key=value lines. Returns bytes written (excluding the NUL
    // terminator), or 0 if cap is too small.
    size_t toText(char* buf, size_t cap) const;

    // Tolerant parse: resets to defaults first, unknown keys are ignored,
    // out-of-range values keep the default. Returns true if at least one
    // recognized key was applied.
    bool fromText(const char* buf);
};
