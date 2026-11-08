# USB and Bluetooth keyboard/mouse — v0.7

Open **Apps > Keyboard & Mouse**, then choose **USB** or **Bluetooth**. This is a
native app inside the launcher, using the existing COSMIC theme.

Choose one of three input modes: **Keyboard only**, **Mouse only**, or
**Keyboard + Mouse**. The combined mode permits typing and real gyro aiming at
the same time. It starts with typing pause **off**; turn it on in the app menu if
you prefer the cursor to freeze while typing.

## Install

Use the v0.7 **full image** and its matching flashing script. Bluetooth support
needs Espressif's full SDK and the launcher now occupies a **2.25 MB** partition,
instead of v0.6's 2 MB. Do not use an app-only update against an older partition
table. The script makes an 8 MB backup before writing. Existing separately
installed firmware may overlap the expanded region and need reinstalling; the
SD card and authenticator vault do not need to be reformatted.

If download mode is needed: unplug USB, put the side switch OFF, hold G0 while
connecting a USB-C data cable to the computer, then release G0 after power-up.

## Connect and control

- USB: connect a USB-C **data** cable to the target computer/tablet/phone.
- Bluetooth: pair **COSMIC Cardputer** in the target's Bluetooth settings.
  Pairing uses Bluetooth LE HID with bonding. One host connection is active.
- **Fn+Space**: cycle Keyboard → Mouse → Keyboard + Mouse.
- **G0 short press**: left click in mouse/combined mode.
- **Hold G0 for 0.7 seconds**: open the app menu and pause host input.
- In the menu: `;` up, `.` down, ENTER choose, backtick/backspace resume.
- **Exit to Apps** in the menu releases held keys/buttons and returns to the launcher.

Keyboard mode supports the 56 physical keys, Ctrl/Shift/Alt/GUI modifiers, a
six-key HID report and the reference project's Fn layer. US physical key mapping
is used; the target computer's keyboard layout determines the resulting symbols.
No host input is sent while navigating the launcher or local app menu.

Mouse mode with gyro/tilt: comma or ENTER left button, slash right button,
`m` middle button, semicolon/dot wheel up/down. In combined mode use
**Opt+comma/slash/m** for those buttons and **Opt+semicolon/dot** for the wheel.
Opt alone is the GUI/Win/Cmd modifier in Keyboard mode; combined mode reserves it
for mouse combinations (a quick solo tap sends one GUI tap).

Fn layer: numbers 1–0 produce F1–F10, minus/equal F11/F12, Fn+backspace Delete,
Fn+backtick Escape, Fn+semicolon/comma/dot/slash arrow keys. See the credited
reference repository for the complete keymap.

## Pointer and settings

The ADV's onboard BMI270 supports three pointer sources:

- **Gyroscope** (default): angular movement steers the cursor like an air mouse.
- **Tilt**: tilt away from a calibrated pose to move continuously, like a joystick.
- **Arrow keys**: in Mouse mode, semicolon/comma/dot/slash move up/left/down/right;
  `j`/`k` left/right button, `u`/`n` wheel, ENTER/G0 left click.

Keep the device still for about one second after launch or **Recalibrate**.
Gyro calibration estimates bias; a deadzone rejects small sensor noise. Tilt
automatically captures a neutral pose when movement engages. If the IMU cannot
start, the app falls back to arrow controls in Mouse mode. Sensitivity (1–5), axis
inversion, source, input mode and typing pause are saved in NVS. An older Cardputer
without the BMI270 gets arrow fallback. Physical gyro axis orientation still
needs verification on a real ADV; horizontal/vertical inversion is available.

## USB and memory limits

USB **device** HID and USB **host** DualShock use the same controller/PHY. Once
either role starts, reboot before choosing the other. Bluetooth HID does not
need the USB port. The USB device stack stays registered after leaving this app,
but receives only neutral reports; it does not forward launcher keystrokes.
Starting USB HID disconnects the hardware USB serial/JTAG connection for that
boot. Use G0 download mode for the next flash if automatic connection fails.

BLE is stopped and its dynamic resources are released on exit; saved pairings
remain. Closing other apps or disconnecting Wi-Fi may be necessary if RAM is low.
Menu entry, exit and reconnection clear held host input to avoid stuck keys.

## Validation and credits

Firmware compilation and portable tests pass. Tests exercise three input modes,
simultaneous typing/gyro, modifier and Fn mapping, local shortcut suppression,
neutral reports on suspension, gyro bias/deadzone/calibration, invalid samples,
motion bounds and arrow fallback. **USB enumeration, Bluetooth pairing/bond
reconnection, actual sensor operation/axes, host layouts and free memory have not
been tested on a physical device.**

Adapted from [Cardputer-Keyboard-Mouse](https://github.com/jayden-sudo/Cardputer-Keyboard-Mouse)
(MIT), with a native launcher adapter and true gyro integration. The original
uses accelerometer tilt for motion. Driver image/register references come from
[M5Unified](https://github.com/m5stack/M5Unified) (MIT); BLE uses
[NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) 2.5.0 (Apache/BSD).
