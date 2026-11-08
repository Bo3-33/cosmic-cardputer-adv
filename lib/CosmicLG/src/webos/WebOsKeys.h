// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Patryk Strzelecki <patryk@strzelecki.info>
#pragma once

#include "core/RemoteKey.h"

namespace lgremote::webos {

// How a remote key is delivered over SSAP: either as a named button on the
// pointer input socket ("type:button\nname:<NAME>\n\n") or as an ssap:// request.
struct KeyMapping {
    RemoteKey key;
    const char* button;   // pointer socket button name, or nullptr
    const char* uri;      // ssap:// request, or nullptr
    const char* payload;  // JSON payload for the request, or nullptr
};

constexpr KeyMapping kKeyMappings[] = {
    {RemoteKey::Up, "UP", nullptr, nullptr},
    {RemoteKey::Down, "DOWN", nullptr, nullptr},
    {RemoteKey::Left, "LEFT", nullptr, nullptr},
    {RemoteKey::Right, "RIGHT", nullptr, nullptr},
    {RemoteKey::Ok, "ENTER", nullptr, nullptr},
    {RemoteKey::Back, "BACK", nullptr, nullptr},
    {RemoteKey::Exit, "EXIT", nullptr, nullptr},
    {RemoteKey::Home, "HOME", nullptr, nullptr},
    {RemoteKey::Settings, "MENU", nullptr, nullptr},
    {RemoteKey::QuickMenu, "QMENU", nullptr, nullptr},
    {RemoteKey::Info, "INFO", nullptr, nullptr},
    {RemoteKey::Guide, "GUIDE", nullptr, nullptr},
    {RemoteKey::Mute, "MUTE", nullptr, nullptr},
    {RemoteKey::ChannelUp, "CHANNELUP", nullptr, nullptr},
    {RemoteKey::ChannelDown, "CHANNELDOWN", nullptr, nullptr},
    {RemoteKey::Digit0, "0", nullptr, nullptr},
    {RemoteKey::Digit1, "1", nullptr, nullptr},
    {RemoteKey::Digit2, "2", nullptr, nullptr},
    {RemoteKey::Digit3, "3", nullptr, nullptr},
    {RemoteKey::Digit4, "4", nullptr, nullptr},
    {RemoteKey::Digit5, "5", nullptr, nullptr},
    {RemoteKey::Digit6, "6", nullptr, nullptr},
    {RemoteKey::Digit7, "7", nullptr, nullptr},
    {RemoteKey::Digit8, "8", nullptr, nullptr},
    {RemoteKey::Digit9, "9", nullptr, nullptr},
    {RemoteKey::Red, "RED", nullptr, nullptr},
    {RemoteKey::Green, "GREEN", nullptr, nullptr},
    {RemoteKey::Yellow, "YELLOW", nullptr, nullptr},
    {RemoteKey::Blue, "BLUE", nullptr, nullptr},
    {RemoteKey::VolumeUp, nullptr, "ssap://audio/volumeUp", nullptr},
    {RemoteKey::VolumeDown, nullptr, "ssap://audio/volumeDown", nullptr},
    {RemoteKey::Play, nullptr, "ssap://media.controls/play", nullptr},
    {RemoteKey::Pause, nullptr, "ssap://media.controls/pause", nullptr},
    {RemoteKey::Stop, nullptr, "ssap://media.controls/stop", nullptr},
    {RemoteKey::Rewind, nullptr, "ssap://media.controls/rewind", nullptr},
    {RemoteKey::FastForward, nullptr, "ssap://media.controls/fastForward", nullptr},
    {RemoteKey::Power, nullptr, "ssap://system/turnOff", nullptr},
    {RemoteKey::PowerOff, nullptr, "ssap://system/turnOff", nullptr},
    {RemoteKey::Hdmi1, nullptr, "ssap://tv/switchInput", "{\"inputId\":\"HDMI_1\"}"},
    {RemoteKey::Hdmi2, nullptr, "ssap://tv/switchInput", "{\"inputId\":\"HDMI_2\"}"},
    {RemoteKey::Hdmi3, nullptr, "ssap://tv/switchInput", "{\"inputId\":\"HDMI_3\"}"},
    {RemoteKey::Hdmi4, nullptr, "ssap://tv/switchInput", "{\"inputId\":\"HDMI_4\"}"},
};

inline const KeyMapping* findKeyMapping(RemoteKey key) {
    for (const KeyMapping& mapping : kKeyMappings) {
        if (mapping.key == key) {
            return &mapping;
        }
    }
    return nullptr;
}

}  // namespace lgremote::webos
