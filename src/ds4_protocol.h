#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Portable USB-report decoder and Luma InputRedirection encoder.
// Wire format documented by Luma3DS and InputRedirectionClient-Qt.
namespace ds4 {
struct State {
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
    uint8_t face = 8, buttons = 0, special = 0;
};
inline bool decode(const uint8_t *p, size_t n, State &out) {
    if (!p || n < 10 || p[0] != 1 || (p[5] & 15) > 8) return false;
    out = {p[1], p[2], p[3], p[4], p[5], p[6], p[7]};
    return true;
}
inline float axis(uint8_t value, unsigned deadzone) {
    int v = int(value) - 128;
    int magnitude = v < 0 ? -v : v;
    if (magnitude <= int(deadzone)) return 0;
    int range = v < 0 ? 128 : 127;
    return (v < 0 ? -1.0f : 1.0f) * (magnitude - deadzone) / (range - deadzone);
}
inline void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (i * 8));
}
inline uint32_t get32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint8_t clampByte(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
inline void encode(const State &s, uint8_t packet[20], unsigned deadzone = 16, bool crossIsA = false) {
    if (deadzone > 64) deadzone = 64;
    uint32_t buttons = 0xfff;
    auto press = [&](bool held, unsigned bit) {
        if (held) buttons &= ~(1u << bit);
    };
    // Default follows Nintendo's physical layout: Circle=A, Cross=B, Triangle=X, Square=Y.
    press(s.face & (crossIsA ? 0x20 : 0x40), 0);
    press(s.face & (crossIsA ? 0x40 : 0x20), 1);
    press(s.buttons & 0x10, 2); // SHARE -> SELECT
    press(s.buttons & 0x20, 3); // OPTIONS -> START
    uint8_t hat = s.face & 15;
    press(hat == 1 || hat == 2 || hat == 3, 4);
    press(hat == 5 || hat == 6 || hat == 7, 5);
    press(hat == 0 || hat == 1 || hat == 7, 6);
    press(hat == 3 || hat == 4 || hat == 5, 7);
    press(s.buttons & 2, 8); // R1
    press(s.buttons & 1, 9); // L1
    press(s.face & (crossIsA ? 0x10 : 0x80), 10);
    press(s.face & (crossIsA ? 0x80 : 0x10), 11);
    float x = axis(s.lx, deadzone), y = -axis(s.ly, deadzone);
    uint32_t circle = 0x007ff7ff;
    if (x != 0 || y != 0) circle = uint32_t(2048 + int(x * 1488)) | uint32_t(2048 + int(y * 1488)) << 12;
    // C-stick's hardware coordinates are rotated 45 degrees.
    float rx = axis(s.rx, deadzone), ry = -axis(s.ry, deadzone);
    uint32_t cx = clampByte(int(128 + (rx + ry) * 89.80256f));
    uint32_t cy = clampByte(int(128 + (ry - rx) * 89.80256f));
    uint32_t ir = 0x81 | (cx << 16) | (cy << 24);
    if (s.buttons & 4) ir |= 1u << 9;  // L2 -> ZL
    if (s.buttons & 8) ir |= 1u << 10; // R2 -> ZR
    put32(packet, buttons);
    put32(packet + 4, 0x02000000); // Touch released
    put32(packet + 8, circle);
    put32(packet + 12, ir);
    put32(packet + 16, s.special & 1); // PS -> HOME; never send POWER
}
} // namespace ds4
