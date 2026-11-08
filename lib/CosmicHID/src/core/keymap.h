#pragma once
#include <cstdint>

// Physical key ID = row*14 + col (row 0 = top .. 3 = bottom, col 0 = left .. 13 = right).
// The two-layer table is the single source of truth from the development guide, Appendix A.

enum class KeyClass : uint8_t { None, HidKey, Modifier, FnLayer };

struct KeyAction {
    KeyClass cls;
    uint8_t  code;   // HidKey: usage ID; Modifier: modifier bitmask (bit0 LCtrl..bit3 LGui)
};

// Layer resolution rules:
//  - modifiers and the Fn key keep their role on both layers,
//  - fnHeld selects the Fn-layer usage for plain keys; unmapped Fn positions -> None.
KeyAction lookup(uint8_t keyId, bool fnHeld);

// Modifier bitmask values (left-side HID modifier bits).
namespace mod {
constexpr uint8_t LCtrl  = 0x01;
constexpr uint8_t LShift = 0x02;
constexpr uint8_t LAlt   = 0x04;
constexpr uint8_t LGui   = 0x08;
}

// Well-known physical key IDs used by the router / UI.
namespace keyid {
constexpr uint8_t Backtick  = 0;    // row0 col0 (menu "back")
constexpr uint8_t Backspace = 13;   // row0 col13 (menu "back")
constexpr uint8_t KeyU      = 21;   // row1 col7 (arrow-source wheel up)
constexpr uint8_t Fn        = 28;   // row2 col0
constexpr uint8_t KeyJ      = 36;   // row2 col8 (arrow-source left button)
constexpr uint8_t KeyK      = 37;   // row2 col9 (arrow-source right button)
constexpr uint8_t Semicolon = 39;   // row2 col11
constexpr uint8_t Enter     = 41;   // row2 col13
constexpr uint8_t Opt       = 43;   // row3 col1 (LGui / mouse-combo prefix)
constexpr uint8_t KeyN      = 50;   // row3 col8 (arrow-source wheel down)
constexpr uint8_t KeyM      = 51;   // row3 col9 (middle button)
constexpr uint8_t Comma     = 52;   // row3 col10
constexpr uint8_t Dot       = 53;   // row3 col11
constexpr uint8_t Slash     = 54;   // row3 col12
constexpr uint8_t Space     = 55;   // row3 col13 (Fn+Space = mode switch)
constexpr uint8_t Count     = 56;
}
