// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Patryk Strzelecki <patryk@strzelecki.info>
#pragma once

#include "RemoteKey.h"

namespace lgremote {

enum class LinkState : uint8_t {
    Off,         // not configured or disabled
    Connecting,  // working on it (Wi-Fi join, TLS handshake, waiting for pairing prompt)
    Ready,       // commands will be delivered
    Error        // configured but failing
};

// A way of delivering remote commands to the TV (infrared, network, Bluetooth).
class Transport {
public:
    virtual ~Transport() = default;

    virtual const char* name() const = 0;
    virtual LinkState state() const = 0;
    virtual bool supports(RemoteKey key) const = 0;
    virtual bool sendKey(RemoteKey key) = 0;

    // Called on every main loop iteration.
    virtual void loop() {}

    virtual bool supportsPointer() const { return false; }
    virtual void movePointer(int /*dx*/, int /*dy*/) {}
    virtual void click() {}
    virtual void scroll(int /*dy*/) {}

    virtual bool supportsText() const { return false; }
    virtual void typeText(const char* /*text*/) {}
    virtual void deleteText(int /*count*/) {}
    virtual void sendEnter() {}

    bool isReady() const { return state() == LinkState::Ready; }
};

}  // namespace lgremote
