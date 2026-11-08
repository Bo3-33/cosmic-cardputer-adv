#pragma once
#include <cstdint>

// Minimal drawing primitives so the UI stays portable (ARC-2).
struct IDisplay {
    virtual ~IDisplay() = default;
    virtual int  width() const = 0;
    virtual int  height() const = 0;
    virtual void clear(uint16_t rgb565) = 0;
    virtual void fillRect(int x, int y, int w, int h, uint16_t c) = 0;
    virtual void drawText(int x, int y, const char* s, uint16_t c, uint8_t size) = 0;
    virtual void flush() = 0;  // push frame (sprite) to panel
};
