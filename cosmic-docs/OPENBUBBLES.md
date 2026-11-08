# OpenBubbles feasibility on Cardputer ADV

**The full OpenBubbles application cannot run directly on this hardware.**
Its app is built with Flutter/Dart and a Rust backend using rustpush, Tokio,
OpenSSL, persistent databases, and platform integration. The ADV is an ESP32-S3
microcontroller with no external PSRAM in this build. It cannot host the existing
desktop/mobile runtime or the full client within the launcher's memory budget.
This is a software/runtime and memory constraint, not a missing keyboard or radio.

A **small Wi-Fi text messaging client** is physically plausible: the Cardputer
could show a conversation list, read short messages and compose text, while a
phone/computer runs the full messaging client and a custom adapter. Attachments
would need filtering or downscaling; FaceTime/video is outside this design.

The inspected OpenBubbles repository does not expose a documented ready-made
remote HTTP server API for this purpose. Its HTTP service is a *client* backend
for the separate BlueBubbles server; its Rust interface is an in-process Flutter
bridge. A Cardputer cannot simply point at a running OpenBubbles app and use that
bridge. An adapter/fork or a supported server backend would still be required.

OpenBubbles' README requires access to a **Mac and an Apple ID**. This does not
mean every messaging session necessarily needs a permanently running Mac server;
setup/validation and the selected backend determine that. A companion design
still relies on the phone/computer that handles Apple's protocols.

No OpenBubbles app or speculative placeholder is included in the launcher, and
no Apple credentials, activation or messages were accessed during this review.

Reviewed [OpenBubbles/openbubbles-app](https://github.com/OpenBubbles/openbubbles-app)
commit eed1b6332efbb17adbf5ebfa2263ad770169f75e: README, pubspec.yaml,
rust/Cargo.toml, rust/src/api/api.rs and lib/services/network/http_service.dart.
Hardware reference: [M5Stack Cardputer-Adv](https://docs.m5stack.com/en/core/Cardputer-Adv).
