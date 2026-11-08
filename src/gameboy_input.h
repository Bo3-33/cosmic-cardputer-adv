#pragma once
#include "ds4_protocol.h"
namespace gameboy {
// Active-low GB pad: right,left,up,down,A,B,Select,Start.
inline uint8_t pad(const ds4::State &s) {
    uint8_t value = 255;
    auto press = [&](bool held, uint8_t bit) {
        if (held) value &= ~(1u << bit);
    };
    unsigned hat = s.face & 15;
    press(hat == 1 || hat == 2 || hat == 3 || s.lx > 180, 0);
    press(hat == 5 || hat == 6 || hat == 7 || s.lx < 76, 1);
    press(hat == 0 || hat == 1 || hat == 7 || s.ly < 76, 2);
    press(hat == 3 || hat == 4 || hat == 5 || s.ly > 180, 3);
    press(s.face & 0x20, 4);    // Cross -> A
    press(s.face & 0x40, 5);    // Circle -> B
    press(s.buttons & 0x10, 6); // SHARE -> Select
    press(s.buttons & 0x20, 7); // OPTIONS -> Start
    return value;
}
} // namespace gameboy
