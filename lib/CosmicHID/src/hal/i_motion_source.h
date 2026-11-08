#pragma once
#include "../core/mouse_engine.h"  // AccelSample

struct IMotionSource {
    virtual ~IMotionSource() = default;
    virtual bool readAccel(AccelSample& out) = 0;  // true if a fresh sample was read
};
