# COSMIC Cardputer ADV launcher — development build

Revision v0.2.1 fixes saved colors being reloaded during Wi-Fi connection and replaces the header letter with connected/disconnected Wi-Fi icons.

Revision v0.2: buffered LCD frames, stable static screens, scrollable icon lists, Controller Bridge and Music inside Apps, themed physical-keyboard entry, and a redesigned WebUI including settings dialogs. See MUSIC.md for MP3/WAV setup.

Revision v0.3 adds LG webOS and Roku Wi-Fi remotes under Apps, with saved addresses, discovery and LG pairing. See REMOTES.md for setup and controls. The 2 MB partition layout is unchanged.

A custom build of bmorcelli/Launcher with your COSMIC theme, a desktop dock, SD-card Lua apps, MP3/WAV Cover Flow music, a state-preserving app switcher, and a wired DualShock 4 → Wi-Fi → Luma3DS controller bridge.

This build has compile and portable automated-test validation. It has not been flashed or tested on a physical Cardputer ADV or 2DS XL in this session. USB power, enumeration, keyboard behavior, real free memory, and gameplay latency need device testing.

## Files

- `cosmic-cardputer-adv-full.bin`: merged bootloader, matching partition table and application; flash at address **0x0** on ESP32-S3.
- `sdcard.zip`: extract to the SD-card root, producing `/apps/stopwatch`, `/apps/notes` and `/apps/orbit`.
- `APP-FORMAT.md`: app package and API specification.
- `cosmic-source.zip`: buildable source including vendored dependencies, SDK template and portable tests; upstream vendor datasheets, reference photos and examples are omitted. The build downloads the pinned ESP32 toolchain/framework as needed.

The custom launcher uses a 2 MB test-app partition in the ADV's 8 MB flash. It does not add PSRAM. Remaining flash is managed by the existing Launcher partition manager. This layout differs from upstream: back up existing firmware, partitions and settings before the first full flash; existing firmware partitions can overlap the expanded launcher region. Do not install this first build as an app-only OTA update against the old partition table. No device has been erased or flashed here.

## First install

1. Back up the ADV's current flash and any SD-card data you need.
2. Put the ADV into download mode if required: power off, hold G0, power on.
3. Use an ESP32 flasher with **ESP32-S3**, flash size **8 MB**, the full image and address **0x0**. With esptool installed, the equivalent command is:

   ```sh
   python -m esptool --chip esp32s3 --port YOUR_PORT write-flash 0x0 cosmic-cardputer-adv-full.bin
   ```

4. Extract `sdcard.zip` to the SD-card root and insert it.
5. Press ENTER at the COSMIC boot screen to enter the launcher. Existing installed-firmware auto-boot behavior is retained.

The full image's SHA-256 is listed in `SHA256SUMS`. A full flash changes the partition table and launcher region; it is not a backup of your existing device.

## Desktop and apps

Use the existing arrow/navigation keys to select dock icons; ENTER opens them. The dock contains Apps, Firmware, SD Files, Wi-Fi and Settings. TAB opens the switcher on the desktop and inside Lua apps. Backtick/ESC returns a Lua app to the desktop without closing it. Backspace edits inside Lua apps. Menus use backspace or backtick as Back.

Apps run inside the launcher. Up to three Lua states are retained if memory permits; only the foreground Lua app receives updates. Closing frees its state. The bridge keeps forwarding in the background. Native system tools are modal; standalone ESP32 firmware `.bin` files still boot separately. See `APP-FORMAT.md` to write or package apps.

Your theme is applied to the shell and list menus: background `#060606`, panel `#690C0C`, accent `#FF0404`, text `#FFDFDA`, surface `#262626`. The layout is fixed to landscape 240 × 135. System lists and physical-keyboard entry use the shared theme. Advanced diagnostic content keeps its existing controls and data.

## DualShock 4 USB bridge

1. On the 2DS XL, enable Luma3DS Input Redirection through Rosalina (normally L + Down + Select → Miscellaneous options → Start InputRedirection). Note its local IPv4 address.
2. Connect the ADV and console to the same local Wi-Fi network; client isolation must not block UDP between them.
3. Open Controller Bridge, enter the console's IPv4 address, and select Start USB Bridge.
4. Connect the DualShock 4 to the ADV USB-C port using a **data-capable** micro-USB cable and suitable adapter/power arrangement. The port becomes a USB host; it cannot simultaneously be the flashing/serial device port. A charged controller is advisable; reliable USB VBUS power is hardware-dependent and may require a powered USB setup. This build does not claim the ADV can supply controller charging current from its battery.
5. Look for increasing report and sent-packet counts. `Sent` means UDP was sent locally, not that the 2DS acknowledged it; InputRedirection has no handshake here. Exit the monitor to use the desktop or Lua apps while forwarding continues.
6. Select Stop Bridge to send three neutral packets and stop forwarding. Input older than 250 ms, a detached controller, or a failed read produces neutral input while the network remains reachable. If the network disappears, a remote release cannot be guaranteed; stop InputRedirection on the console to recover stuck input.

Supported USB identities are Sony VID `054c`, original DS4 PID `05c4`, and DS4 v2 PID `09cc`, report ID `01`. Clones and the Sony wireless USB adapter are not supported in this build. Rumble, gyro, touchpad-to-touchscreen mapping, and console video streaming are not implemented.

| DualShock 4 | 2DS XL |
|---|---|
| Circle / Cross | A / B by default |
| Triangle / Square | X / Y by default |
| D-pad | D-pad |
| Left stick | Circle Pad |
| Right stick | C-stick |
| L1 / R1 | L / R |
| L2 / R2 | ZL / ZR |
| SHARE / OPTIONS | SELECT / START |
| PS | HOME |

The alternate mapping makes Cross=A, Circle=B, Square=X, Triangle=Y. Stick dead zone is adjustable between 8, 16 and 24 raw counts. Bridge IP, mapping and dead zone are stored in a separate NVS namespace. Only destination IP is stored; there is no automatic console discovery.

After first starting the bridge, USB host resources remain initialized until reboot, even after forwarding stops. Reboot and use download mode to regain USB flashing/serial access if needed. USB SD-card mass-storage mode is excluded from this build to avoid competing with the USB host; use the SD card or upstream WebUI file management instead.

## Build and checks

```sh
pio run -e m5stack-cardputer
python3 test/cosmic/run.py
```

The PlatformIO environment uses the upstream Cardputer/ADV keyboard detection and selected ESP32 Arduino framework. Portable tests compile the real Lua runtime and protocol code with host GCC/G++, checking controller mapping and packet encoding, independent app states, pause/resume state, callback failures, instruction limits, memory limits, and sample app lifecycles. They do not simulate USB electrical behavior or hardware gameplay.

Upstream base: `bmorcelli/Launcher` commit `2f43aa7bd3ef03e770c0a97135cbcfcc79c15aba`. Lua: official 5.4.8 source with one `luaconf.h` change allowing `LUA_32BITS` to be set at build time. The package retains upstream licenses, submodule licenses and Lua's copyright notice.

References: [Launcher](https://github.com/bmorcelli/Launcher), [M5Stack ADV](https://docs.m5stack.com/en/core/Cardputer-Adv), [ESP-IDF USB Host API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html), [Luma InputRedirection receiver](https://github.com/LumaTeam/Luma3DS/blob/master/sysmodules/rosalina/source/input_redirection.c), [reference client packet format](https://github.com/mastermune/InputRedirectionClient-Qt), [Lua](https://www.lua.org/).
