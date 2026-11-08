# Game Boy / Game Boy Color — v0.5 development build

Open Apps → Game Boy / Color. Put `.gb` files in `/roms/gb` and `.gbc` files in `/roms/gbc` on the SD card. This is a native app inside the launcher, not a separately booted firmware. No games or boot ROMs are included. Use uncompressed ROM files; ZIP archives are not read.

Connect a wired DualShock 4 through the ADV's USB-C host port using a data-capable cable and suitable adapter/power setup. USB power and controller enumeration remain hardware-dependent. The ADV cannot use that port for a PC serial/flashing connection while acting as the controller's USB host. Hold G0 while connecting to your PC to recover download mode when needed.

Entering the emulator starts the existing USB host and stops 2DS XL input forwarding, sending neutral inputs first. Controller input is then local to the game. The ROM browser and pause menu support DualShock D-pad/left stick navigation, Cross to select, Circle to go back and OPTIONS to select. The Cardputer keyboard can navigate these menus too.

| DualShock 4 | Game Boy |
|---|---|
| D-pad or left stick | D-pad |
| Cross | A |
| Circle | B |
| SHARE | SELECT |
| OPTIONS | START |
| PS button | Open pause menu |
| Cardputer backtick/ESC | Open pause menu |

The pause menu provides Resume, Save battery RAM, Enable/Mute audio, Save and return to launcher, and a separately confirmed Quit without saving option. The controller's PS button opens the menu on its press edge. Use Cross/Circle in the menu. Audio starts muted; enable it in the pause menu if desired. It uses the ADV ES8311 output, downmixed to mono at a low initial gain. If memory or I2S allocation is unavailable, keep it muted.

The game pauses when controller reports become stale/disconnected; all buttons release and audio stops. Reconnecting resumes the game. Both raw controller input and menus use the same wired DS4 reader as the 2DS bridge. No Bluetooth controller connection is implemented on the ESP32-S3.

## Rendering and memory

The 160×144 Game Boy picture is scaled to 150×135, centered with side borders, preserving its aspect ratio and showing every part of the original frame. GB games use a four-shade palette; GBC games use the core's color palettes. The existing buffered LCD output is retained. The emulator draws alternate emulated frames to reduce LCD traffic. Actual speed depends on the game, mapper, SD-card/cache behavior, controller and memory pressure; 60 FPS on hardware is not claimed.

ROMs stream through a 24 KB page cache rather than loading the entire ROM into RAM. The core context uses approximately 50 KB plus cartridge RAM and optional audio buffers. Large cartridge RAM allocations may not fit alongside other apps. Close Music/Lua apps and retry if a memory error appears. The loader validates header size codes and refuses allocations it cannot satisfy instead of silently truncating save RAM. Unsupported mappers, peripherals and ROM sizes are reported as errors. No link-cable emulation or full CPU save-state snapshots are implemented.

The game context is released when returning to the launcher. Reopening starts the game normally and loads its battery RAM, rather than resuming a frozen CPU snapshot. Other hosted Lua app states remain paused if memory allows; this first emulator version does not keep a game resident in the app switcher.

## Saves

Battery RAM is stored beside the ROM as `<filename>.sav` (for example `game.gbc.sav`), with a previous `.sav.bak` copy. Modified RAM is saved approximately every 15 seconds while playing and on Save/normal quit. Saving first writes a temporary file and then rotates the prior file to a backup. Save failures keep the game open when you choose Save and return, so you can retry or explicitly discard progress. The game must support battery saves and you must use the game's own save action when applicable; this is not a full emulator save-state feature.

If the main save is absent but its backup exists, the backup is loaded. A save with a mismatched size or a read error is not overwritten automatically; fix or move it using your computer first. The core's RTC ticks while running; offline real-time-clock persistence is not implemented in this version. Back up ROMs and saves independently of firmware flash backups, since they live on the SD card.

## Core and validation

Core: Walnut-CGB with full color support, derived from Peanut-GB and including the authors' SameBoy-derived sections. Vendored from [Rocka84/CYDboy](https://github.com/Rocka84/CYDboy), commit `358a1b75b1d09b999c44bde17fdb92e9405ec560`. Copyright and MIT notices for Mr. Paul, Mahyar Koshkouei, Lior Halphon and the CYDboy contributors are retained in `lib/CosmicGB`. Audio uses the MIT-licensed MiniGB APU from the same source. The launcher implements its own SD cache, UI, saves, controller integration and rendering; CYD-specific display/touch/Bluetooth drivers are not used.

Host tests execute an original synthetic diagnostic program through the actual GB and GBC CPU/LCD core, validate DS4 mapping and large cartridge RAM sizes, and check APU output-buffer bounds. Build and portable tests pass; no commercial game compatibility, SD streaming performance, physical USB gameplay or ADV audio tests were available in this session.

This release also includes the Honeywell HYF290B IR app; see FAN.md. The launcher partition remains 2 MB, so updating from prior COSMIC builds does not require a new partition layout.
