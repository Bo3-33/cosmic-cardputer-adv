// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Patryk Strzelecki <patryk@strzelecki.info>
#pragma once

#include <Arduino.h>

namespace lgremote {

// Parses "AA:BB:CC:DD:EE:FF" (or with '-') into 6 bytes.
bool parseMacAddress(const String& text, uint8_t out[6]);

// Broadcasts a Wake-on-LAN magic packet on UDP ports 9 and 7.
// The TV must have "Turn on via Wi-Fi" / "Mobile TV On" enabled.
bool sendWakeOnLan(const String& mac);

}  // namespace lgremote
