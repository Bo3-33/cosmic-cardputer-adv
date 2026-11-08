#if defined(COSMIC_CARDPUTER) || defined(COSMIC_HID_HOST)
#include "keymap.h"

namespace {

// Sentinel encoding for the normal-layer table:
//   0x00        -> no action
//   0xE0 | mask -> modifier key with the given bitmask (0xE1 LCtrl, 0xE2 LShift, 0xE4 LAlt, 0xE8 LGui)
//   0xF1        -> Fn layer key (local, never reported)
//   otherwise   -> plain HID usage ID
constexpr uint8_t kModSentinel = 0xE0;
constexpr uint8_t kFnSentinel  = 0xF1;

constexpr uint8_t MOD(uint8_t mask) { return static_cast<uint8_t>(kModSentinel | mask); }

// Normal layer (Appendix A, rows top to bottom).
constexpr uint8_t kNormal[56] = {
    // Row 0: ` 1 2 3 4 5 6 7 8 9 0 - = BS
    0x35, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A,
    // Row 1: Tab q w e r t y u i o p [ ] backslash
    0x2B, 0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30, 0x31,
    // Row 2: Fn Shift a s d f g h j k l ; ' Enter
    kFnSentinel, MOD(0x02), 0x04, 0x16, 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x28,
    // Row 3: Ctrl Opt Alt z x c v b n m , . / Space
    MOD(0x01), MOD(0x08), MOD(0x04), 0x1D, 0x1B, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0x2C,
};

// Fn layer (0 = no Fn mapping). Fn+Space is a local mode switch handled by the
// router, so its table entry stays 0 here.
constexpr uint8_t kFnLayer[56] = {
    // Row 0: Esc F1..F12 Del
    0x29, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x4C,
    // Row 1: e=End u=PgUp i=Ins p=PrtSc
    0, 0, 0, 0x4D, 0, 0, 0, 0x4B, 0x49, 0, 0x46, 0, 0, 0,
    // Row 2: a=Caps d=PgDn h=Home ;=Up
    0, 0, 0x39, 0, 0x4E, 0, 0, 0x4A, 0, 0, 0, 0x52, 0, 0,
    // Row 3: ,=Left .=Down /=Right
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x50, 0x51, 0x4F, 0,
};

} // namespace

KeyAction lookup(uint8_t keyId, bool fnHeld) {
    if (keyId >= keyid::Count) return {KeyClass::None, 0};

    const uint8_t raw = kNormal[keyId];
    if (raw == kFnSentinel) return {KeyClass::FnLayer, 0};
    if ((raw & 0xF0) == kModSentinel && raw != 0xE0) {
        // Modifiers keep their role on the Fn layer too (Fn shows no remap for them).
        return {KeyClass::Modifier, static_cast<uint8_t>(raw & 0x0F)};
    }

    if (fnHeld) {
        const uint8_t fnCode = kFnLayer[keyId];
        if (fnCode == 0) return {KeyClass::None, 0};
        return {KeyClass::HidKey, fnCode};
    }
    if (raw == 0) return {KeyClass::None, 0};
    return {KeyClass::HidKey, raw};
}

#endif
