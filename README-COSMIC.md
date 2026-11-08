# COSMIC Cardputer ADV

A COSMIC-inspired desktop and app launcher for the **M5Stack Cardputer ADV**,
based on [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher).

Deep black, dark red panels, bright red accents and warm text, with a buffered
240 × 135 interface, SD-card apps and a matching configuration WebUI.

**Development firmware.** Build and portable tests pass; the newest hardware
features still need real-device validation. Keep a working backup before updating.

[Download firmware](https://github.com/Bo3-33/cosmic-cardputer-adv/releases) ·
[Firmware history](cosmic-docs/CHANGELOG.md) ·
[App package format](cosmic-docs/APP-FORMAT.md)

## Included apps

| App | What it does |
| --- | --- |
| Ollee Watch Clock | Wi-Fi time → Bluetooth clock sync for Ollee hardware 01.05.00 / firmware 00.01.11; experimental, with reviewed home-location fields |
| Keyboard & Mouse | USB or Bluetooth LE HID; keyboard only, mouse only, or simultaneous keyboard + gyro mouse; tilt and arrow fallback |
| Authenticator | Encrypted local vault, offline TOTP/Steam codes, direct read-only Proton Wi-Fi sync after one-time PC pairing |
| Game Boy / Color | SD ROM browser, GB/GBC emulation and battery saves; physical keyboard or USB DualShock 4 |
| Music | MP3/WAV playback with a Cover Flow-style cover browser |
| Controller Bridge | USB DualShock 4 → Wi-Fi → Luma3DS/2DS XL controls |
| LG TV Remote | LG webOS Wi-Fi pairing and remote controls |
| Roku Remote | Roku Wi-Fi remote controls and text entry |
| Honeywell Fan | HYF290B Power, Oscillate and Fan Speed raw IR commands |
| Lua apps | SD-card app packages, icons and a state-preserving switcher; sample Notes, Stopwatch and Orbit apps |

The launcher hosts native tools and Lua apps. Up to three Lua states are retained
when RAM permits. Background execution is limited; Authenticator locks when you
leave, and HID forwarding stops when you exit its app. Standalone firmware `.bin`
files boot separately through the original Launcher firmware manager.

## Install

1. Download the latest **kit** from [Releases](https://github.com/Bo3-33/cosmic-cardputer-adv/releases).
2. Extract it, then run its matching `flash-cardputer.py`. It checks the image
   checksum and backs up all 8 MB of flash before writing the merged image at `0x0`.
3. If no device appears: unplug USB, switch the ADV OFF, hold **G0** while
   connecting a USB-C data cable, then release G0 after power-up.
4. Extract the kit's `sdcard.zip` to your existing SD card. No reformat is needed.
5. Press ENTER at boot to open the launcher, then choose **Apps**.

The v0.7 and v0.7.1 builds use a **2.25 MB** partition in the ADV's 8 MB flash. Versions
v0.2–v0.6 used 2 MB. Use the full image and matching partition table when upgrading
to v0.7; an app-only write is insufficient. Previously installed standalone
firmware can overlap the expanded region and may need reinstalling. Keep the
flasher's full backup and your SD backup somewhere safe.

The script requires Python, esptool and pyserial. For a fresh machine:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install esptool pyserial
python flash-cardputer.py
```

Firmware releases contain clean build images, checksums, source archives and
setup guides. Personal flash backups, account vaults, tokens, Wi-Fi settings,
logs, commercial games and music are not distributed.

## Guides

- [Ollee watch clock setup and compatibility](cosmic-docs/OLLEE.md)
- [USB/Bluetooth keyboard and gyro mouse](cosmic-docs/KEYBOARD-MOUSE.md)
- [Authenticator pairing, encryption and sync](cosmic-docs/AUTHENTICATOR.md)
- [GB/GBC ROMs, saves and DualShock controls](cosmic-docs/GAMEBOY.md)
- [Music and cover artwork](cosmic-docs/MUSIC.md)
- [LG and Roku remotes](cosmic-docs/REMOTES.md)
- [Honeywell fan remote](cosmic-docs/FAN.md)
- [Lua app standard](cosmic-docs/APP-FORMAT.md) and [SDK](app-sdk/)
- [OpenBubbles feasibility](cosmic-docs/OPENBUBBLES.md)

## Important limits

The Honeywell capture has not operated the user's fan. The newly linked file has
the same signal data; v0.7.1 adds DMA transmission and an emitter diagnostic,
with physical compatibility still unconfirmed.

The ADV has no PSRAM in this build. Close background apps when memory is low.
USB keyboard/device mode and USB DualShock/host mode share one controller; reboot
before switching roles. Original DualShock 4 Bluetooth Classic is not supported
by the ESP32-S3; use a USB data cable for the controller.

Proton login/key unlocking currently happens on a computer. Once paired, the
Cardputer generates codes offline and syncs directly; it refreshes its saved
session. Revoked sessions or new account encryption keys require pairing again.
Sync is read-only. Set the clock after a cold boot. This custom authenticator is
not security-audited; retain your existing authenticator while validating it.

OpenBubbles itself cannot run on this device. A lightweight Wi-Fi text client
with a phone/computer companion is plausible, but needs an adapter; no
OpenBubbles integration is included. Its official setup requires access to a
Mac and an Apple ID. See the feasibility guide for source references.

## Build and test

The source is laid out like the upstream Launcher. Dependencies under `lib/` are
vendored so a regular clone includes them; no recursive submodule checkout is
needed. The toolchain and SDK download during the first build.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install platformio
pio run -e m5stack-cardputer
```

The merged output is `Launcher-m5stack-cardputer.bin` (flash at `0x0`). The
app-only image is `.pio/build/m5stack-cardputer/firmware.bin`; it requires a
compatible partition table. Do not use another board's image on the ADV.

Portable tests on Linux require a C/C++ compiler and OpenSSL development headers
(`build-essential` and `libssl-dev` on Debian/Ubuntu):

```sh
python3 test/cosmic/run.py
```

Tests cover Lua limits/state isolation, DS4 packets, TV remote helpers, exact fan
IR data, GB/GBC diagnostics, HID mode routing/calibration, RFC 6238 vectors and
encrypted vault compatibility against synthetic Proton WASM fixtures. They do
not establish physical USB/Bluetooth/IR/TV compatibility or real-account sync.
Each release kit records its actual build and test results in `BUILD-VALIDATION.json`.

## Versions and source history

All preserved firmware versions have separate releases: **v0.1, v0.2, v0.2.1,
v0.3, v0.5, v0.6, v0.7, v0.7.1, v0.7.2 and v0.8**. v0.4's fan feature was included in v0.5; a separate
v0.4 binary was not archived. The v0.1 kit originally had an unversioned filename.

Tags import the matching source saved inside each original release kit. This is
a reconstructed archive of the releases, not the original development commit
history. Historical kits remain unchanged, including their older limitations.

## Credits and licenses

This project extends [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher)
from commit `2f43aa7bd3ef03e770c0a97135cbcfcc79c15aba`. The original upstream
README is retained as [README-UPSTREAM.md](README-UPSTREAM.md), and its MIT
[LICENSE](LICENSE) is retained. Components carry their own licenses:

- Authenticator integration/pairing: GPL-3.0-or-later; [AUTHENTICATOR-LICENSE](AUTHENTICATOR-LICENSE).
  References [proton-authenticator-cli](https://github.com/brandonkramer/proton-authenticator-cli)
  and [Proton's common crypto](https://github.com/protonpass/proton-pass-common).
- HID core/transports: [Cardputer-Keyboard-Mouse](https://github.com/jayden-sudo/Cardputer-Keyboard-Mouse), MIT;
  [M5Unified](https://github.com/m5stack/M5Unified), MIT;
  [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino), Apache/BSD notices in its folder.
- Emulator: Walnut-CGB/Peanut-GB and MiniGB APU via
  [CYDboy](https://github.com/Rocka84/CYDboy), with licenses in `lib/CosmicGB`.
- LG remote: [IP9LGRemote](https://github.com/raptyk/IP9LGRemote), MIT;
  Roku reference: [Roku-ESP32-Remote](https://github.com/mgierhart/Roku-ESP32-Remote).
- Lua, audio, display, WebSockets and other bundled dependencies retain the
  licenses and attribution supplied in their respective directories.

This is an unofficial project, unaffiliated with M5Stack, System76/COSMIC,
Proton, Apple, Sony, Nintendo, LG, Roku or Honeywell.
