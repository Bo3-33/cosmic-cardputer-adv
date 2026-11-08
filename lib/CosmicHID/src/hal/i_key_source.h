#pragma once
#include <cstdint>

struct IKeySource {
    virtual ~IKeySource() = default;
    virtual void     poll() = 0;         // read hardware (I2C) — main task only
    virtual uint64_t pressedMask() = 0;  // bit keyId set = currently pressed
};
