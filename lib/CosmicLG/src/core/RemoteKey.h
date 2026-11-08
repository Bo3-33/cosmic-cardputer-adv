// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Patryk Strzelecki <patryk@strzelecki.info>
#pragma once

#include <stdint.h>

namespace lgremote {

// Logical remote-control buttons, independent of how they are delivered to the TV.
enum class RemoteKey : uint8_t {
    None,
    Power,
    PowerOn,
    PowerOff,
    Up,
    Down,
    Left,
    Right,
    Ok,
    Back,
    Exit,
    Home,
    Settings,
    QuickMenu,
    Input,
    Info,
    Guide,
    VolumeUp,
    VolumeDown,
    Mute,
    ChannelUp,
    ChannelDown,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    PlayPause,
    Play,
    Pause,
    Stop,
    Rewind,
    FastForward,
    Red,
    Green,
    Yellow,
    Blue,
    Hdmi1,
    Hdmi2,
    Hdmi3,
    Hdmi4,
    EzAdjust,
    InStart,
    Count
};

inline RemoteKey digitKey(uint8_t digit) {
    return static_cast<RemoteKey>(static_cast<uint8_t>(RemoteKey::Digit0) + (digit % 10));
}

inline bool isDigitKey(RemoteKey key) {
    return key >= RemoteKey::Digit0 && key <= RemoteKey::Digit9;
}

// True for keys that should auto-repeat while held (volume, channel, arrows).
inline bool isRepeatableKey(RemoteKey key) {
    switch (key) {
        case RemoteKey::Up:
        case RemoteKey::Down:
        case RemoteKey::Left:
        case RemoteKey::Right:
        case RemoteKey::VolumeUp:
        case RemoteKey::VolumeDown:
        case RemoteKey::ChannelUp:
        case RemoteKey::ChannelDown:
            return true;
        default:
            return false;
    }
}

// Short label shown on screen after a key is sent.
const char* remoteKeyLabel(RemoteKey key);

}  // namespace lgremote
