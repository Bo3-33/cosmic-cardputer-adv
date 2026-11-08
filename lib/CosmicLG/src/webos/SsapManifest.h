// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Patryk Strzelecki <patryk@strzelecki.info>
#pragma once

namespace lgremote::webos {

// Registration manifest sent in the SSAP "register" message.
//
// Unsigned on purpose: since 2026 webOS firmware rejects the well-known signed
// "com.lge.test / LG Remote App" manifest with "403 blacklisted certificate"
// (see LGTVCompanion#351, aiowebostv#719). With an unsigned manifest every permission
// is listed in the outer array and the user grants them by accepting the TV prompt.
// Permissions can only be extended by pairing again, the COSMIC integration requests only its remote-control permission subset.
// CONTROL_MOUSE_AND_KEYBOARD is required by the pointer socket, CONTROL_INPUT_TEXT by the IME.
constexpr const char* kManifestJson = R"json({
  "manifestVersion": 1,
  "appVersion": "1.1",
  "permissions": [
    "CONTROL_AUDIO", "CONTROL_INPUT_MEDIA_PLAYBACK", "CONTROL_INPUT_TEXT",
    "CONTROL_INPUT_TV", "CONTROL_MOUSE_AND_KEYBOARD", "CONTROL_POWER",
    "READ_POWER_STATE"
  ]
})json";

}  // namespace lgremote::webos
