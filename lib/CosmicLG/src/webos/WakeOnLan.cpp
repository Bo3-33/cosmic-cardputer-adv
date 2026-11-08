// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Patryk Strzelecki <patryk@strzelecki.info>
#include "WakeOnLan.h"

#include <WiFi.h>
#include <WiFiUdp.h>

namespace lgremote {

namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

bool parseMacAddress(const String& text, uint8_t out[6]) {
    int byteIndex = 0;
    int nibbles = 0;
    uint8_t current = 0;
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c == ':' || c == '-' || c == ' ') {
            continue;
        }
        int value = hexValue(c);
        if (value < 0 || byteIndex >= 6) {
            return false;
        }
        current = static_cast<uint8_t>((current << 4) | value);
        if (++nibbles == 2) {
            out[byteIndex++] = current;
            current = 0;
            nibbles = 0;
        }
    }
    return byteIndex == 6 && nibbles == 0;
}

bool sendWakeOnLan(const String& mac) {
    uint8_t macBytes[6];
    if (!parseMacAddress(mac, macBytes) || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    uint8_t packet[102];
    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; ++i) {
        memcpy(packet + 6 + i * 6, macBytes, 6);
    }

    IPAddress broadcast = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();
    for (int i = 0; i < 4; ++i) {
        broadcast[i] = broadcast[i] | static_cast<uint8_t>(~mask[i]);
    }

    WiFiUDP udp;
    bool sent = false;
    for (uint16_t port : {static_cast<uint16_t>(9), static_cast<uint16_t>(7)}) {
        if (udp.beginPacket(broadcast, port)) {
            udp.write(packet, sizeof(packet));
            sent = udp.endPacket() || sent;
        }
    }
    return sent;
}

}  // namespace lgremote
