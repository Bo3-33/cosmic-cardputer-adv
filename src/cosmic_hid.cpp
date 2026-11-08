#ifdef COSMIC_CARDPUTER
#include "cosmic_hid.h"
#include "controller_bridge.h"
#include "cosmic.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "powerSave.h"
#include <Preferences.h>
#include <Wire.h>
#include <bmi270_config.h>
#include <cmath>
#include <core/input_router.h>
#include <core/key_state_tracker.h>
#include <drivers/hid_ble.h>
#include <drivers/hid_usb.h>

namespace {
// Construct USB descriptors before the first USB.begin(); reports are sent only inside this app.
UsbHidTransport usb;
BleHidTransport ble;
bool usbStarted = false;
uint8_t motionSource = 0; // 0 real gyro, 1 accelerometer tilt, 2 arrows
bool inverseX = false, inverseY = false;
struct Imu {
    uint8_t address = 0;
    bool write(uint8_t reg, const uint8_t *p, size_t n) {
        Wire.beginTransmission(address);
        Wire.write(reg);
        if (Wire.write(p, n) != n) {
            Wire.endTransmission();
            return false;
        }
        return Wire.endTransmission() == 0;
    }
    bool put(uint8_t reg, uint8_t value) { return write(reg, &value, 1); }
    bool read(uint8_t reg, uint8_t *p, size_t n) {
        Wire.beginTransmission(address);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom(address, n) != n) return false;
        for (size_t i = 0; i < n; ++i) p[i] = Wire.read();
        return true;
    }
    bool begin() {
        launcherInputLock();
        bool found = false;
        uint8_t id = 0;
        for (uint8_t a : {uint8_t(0x69), uint8_t(0x68)}) {
            address = a;
            if (read(0, &id, 1) && id == 0x24) {
                found = true;
                break;
            }
        }
        if (!found) {
            address = 0;
            launcherInputUnlock();
            return false;
        }
        bool ok = found && put(0x7e, 0xb6);
        launcherDelayMs(5);
        ok = ok && put(0x7c, 0) && put(0x59, 0);
        launcherDelayMs(2);
        for (size_t offset = 0; ok && offset < sizeof(bmi270_config_file); offset += 32) {
            uint8_t addr[2] = {uint8_t((offset >> 1) & 15), uint8_t(offset >> 5)};
            ok = write(0x5b, addr, 2) && write(
                                             0x5e,
                                             bmi270_config_file + offset,
                                             std::min(size_t(32), sizeof(bmi270_config_file) - offset)
                                         );
        }
        ok = ok && put(0x59, 1);
        launcherDelayMs(200);
        uint8_t status = 0;
        ok = ok && read(0x21, &status, 1) && (status & 15) == 1;
        ok = ok && put(0x40, 0xa9) && put(0x41, 2) && put(0x42, 0xe9) && put(0x43, 0) && put(0x7d, 0x0e);
        launcherInputUnlock();
        launcherDelayMs(80);
        return ok;
    }
    bool sample(AccelSample &acc, float gyro[3]) {
        launcherInputLock();
        uint8_t ready = 0, raw[12];
        bool ok = read(3, &ready, 1) && (ready & 0x40) && read(0x0c, raw, 12);
        launcherInputUnlock();
        if (!ok) return false;
        auto value = [&](int i) { return int16_t(uint16_t(raw[i]) | (uint16_t(raw[i + 1]) << 8)); };
        acc = {value(0) / 4096.0f, value(2) / 4096.0f, value(4) / 4096.0f};
        for (int i = 0; i < 3; ++i) gyro[i] = value(6 + i * 2) * (2000.0f / 32768.0f);
        return true;
    }
    void end() {
        launcherInputLock();
        if (address) put(0x7d, 0);
        launcherInputUnlock();
    }
};
void load(Settings &s) {
    s.clutchMode = 3;
    snprintf(s.bleName, sizeof(s.bleName), "COSMIC Cardputer");
    Preferences p;
    if (p.begin("cosmic_hid", true)) {
        s.inputMode = std::min(unsigned(p.getUChar("mode", 2)), 2u);
        s.sensLevel = std::max(1u, std::min(unsigned(p.getUChar("sens", 3)), 5u));
        motionSource = std::min(unsigned(p.getUChar("source", 0)), 2u);
        s.clutchMode = p.getBool("pause", false) ? 0 : 3;
        inverseX = p.getBool("invx", false);
        inverseY = p.getBool("invy", false);
        p.end();
    }
    s.cursorSource = motionSource == 2 ? 1 : 0;
}
void save(const Settings &s) {
    Preferences p;
    if (!p.begin("cosmic_hid", false)) return;
    p.putUChar("mode", s.inputMode);
    p.putUChar("sens", s.sensLevel);
    p.putUChar("source", motionSource);
    p.putBool("pause", s.clutchMode == 0);
    p.putBool("invx", inverseX);
    p.putBool("invy", inverseY);
    p.end();
}
void release(IHidTransport &transport) {
    // Retry neutral reports briefly; a host can be busy at an interval boundary.
    for (int i = 0; i < 4; ++i) {
        bool k = transport.sendKeyboard(KeyboardReport{}), m = transport.sendMouse(MouseReport{});
        if (k && m) return;
        launcherDelayMs(15);
    }
}
const char *modeName(unsigned mode) {
    return mode == 0 ? "Keyboard only" : mode == 1 ? "Mouse only" : "Keyboard + Mouse";
}
const char *sourceName(bool available) {
    return !available          ? "Arrow fallback"
           : motionSource == 0 ? "Gyroscope"
           : motionSource == 1 ? "Tilt"
                               : "Arrow keys";
}
void run(bool wired) {
    if (wired && controllerUsbHostActive()) {
        displayError("DS4 owns USB. Reboot before USB keyboard mode.", true);
        return;
    }
    if (!wired && ESP.getFreeHeap() < 115000) {
        displayError("Close other apps before Bluetooth HID", true);
        return;
    }
    Settings settings;
    load(settings);
    settings.transport = wired ? 1 : 0;
    tft->fillScreen(cosmic::background);
    cosmic::header("Keyboard & Mouse");
    cosmic::label("Starting... Hold still to calibrate", 8, 50);
    tft->display(false);
    Imu imu;
    bool imuOk = imu.begin();
    MouseEngine engine;
    MouseTuning tune;
    tune.sensLevel = settings.sensLevel;
    engine.configure(tune);
    InputRouter router;
    router.attach(&engine, &settings);
    router.forceArrowSource(!imuOk);
    KeyStateTracker tracker;
    IHidTransport &transport = wired ? static_cast<IHidTransport &>(usb) : static_cast<IHidTransport &>(ble);
    if (wired) {
        if (!usbStarted) {
            usb.begin(settings);
            usbStarted = true;
        }
    } else ble.begin(settings);
    if ((wired && !usb.started()) || (!wired && !ble.started())) {
        imu.end();
        if (!wired) ble.end();
        displayError("HID could not start. Close apps or reboot.", true);
        return;
    }
    cardputerHidCapture(true);
    uint64_t blocked = 0;
    bool menu = false, done = false, buttonPrev = false, longFired = false, wasConnected = false;
    unsigned selection = 0;
    unsigned long buttonAt = 0, lastUi = 0, lastSample = 0, lastHid = 0;
    unsigned long previousMicros = micros();
    bool dirty = true, previousMenu = false, previousCalibration = false, mouseRetry = false;
    MouseReport pendingMouse;
    constexpr unsigned menuCount = 10;
    while (!done) {
        unsigned long now = millis();
        uint64_t mask = cardputerHidKeys();
        blocked &= mask;
        mask &= ~blocked;
        bool button = launcherGpioRead(0) == LOW;
        if (mask || button) wakeUpScreen();
        if (button && !buttonPrev) {
            buttonAt = now;
            longFired = false;
        }
        if (button && !longFired && now - buttonAt >= 700) {
            longFired = true;
            menu = !menu;
            release(transport);
            router.setSuspended(menu);
            blocked |= mask;
            mask = 0;
            if (!menu) {
                router = InputRouter();
                router.attach(&engine, &settings);
                router.forceArrowSource(!imuOk);
                tracker = KeyStateTracker();
            }
            dirty = true;
        }
        buttonPrev = button;
        router.onButtonA(button);
        tracker.update(mask, [&](KeyStateTracker::Event event) {
            dirty = true;
            if (!menu) {
                router.onKeyEvent(event.keyId, event.down);
                return;
            }
            if (!event.down) return;
            if (event.keyId == keyid::Semicolon) selection = (selection + menuCount - 1) % menuCount;
            if (event.keyId == keyid::Dot) selection = (selection + 1) % menuCount;
            if (event.keyId == keyid::Backtick || event.keyId == keyid::Backspace) {
                menu = false;
                blocked |= mask;
            }
            if (event.keyId == keyid::Enter) {
                if (selection == 0) { settings.inputMode = (settings.inputMode + 1) % 3; }
                if (selection == 1) {
                    motionSource = (motionSource + 1) % 3;
                    settings.cursorSource = motionSource == 2 ? 1 : 0;
                    engine.requestRecalibration();
                }
                if (selection == 2) {
                    settings.sensLevel = settings.sensLevel % 5 + 1;
                    tune.sensLevel = settings.sensLevel;
                    engine.configure(tune);
                }
                if (selection == 3) settings.clutchMode = settings.clutchMode == 0 ? 3 : 0;
                if (selection == 4) inverseX = !inverseX;
                if (selection == 5) inverseY = !inverseY;
                if (selection == 6) {
                    engine.requestRecalibration();
                    menu = false;
                    blocked |= mask;
                }
                if (selection == 7 && !wired) {
                    release(transport);
                    ble.clearBonding();
                }
                if (selection == 8) {
                    menu = false;
                    blocked |= mask;
                }
                if (selection == 9) done = true;
                save(settings);
            }
        });
        // When resuming, discard all menu-held keys so they cannot type into the host.
        if (previousMenu && !menu) {
            release(transport);
            router = InputRouter();
            router.attach(&engine, &settings);
            router.forceArrowSource(!imuOk);
            tracker = KeyStateTracker();
            blocked |= mask;
        }
        previousMenu = menu;
        router.setSuspended(menu);
        if (imuOk && now - lastSample >= 5) {
            lastSample = now;
            AccelSample acc;
            float gyro[3];
            if (imu.sample(acc, gyro)) {
                unsigned long current = micros(), dt = current - previousMicros;
                previousMicros = current;
                if (dt > 50000) dt = 5000;
                if (motionSource == 0) engine.feedGyro(gyro[0], gyro[1], gyro[2], dt);
                else if (motionSource == 1) engine.feedAccel(acc, dt);
            }
        }
        if (now - lastHid >= 8) {
            lastHid = now;
            auto output = router.collect(now);
            if (output.kbDirty && !transport.sendKeyboard(output.kb)) router.keepKbDirty();
            if (output.msDirty) {
                if (output.ms.dx || output.ms.dy || output.ms.buttons || output.ms.wheel) wakeUpScreen();
                if (inverseX) output.ms.dx = -output.ms.dx;
                if (inverseY) output.ms.dy = -output.ms.dy;
                mouseRetry = !transport.sendMouse(output.ms);
                pendingMouse = output.ms;
                pendingMouse.dx = pendingMouse.dy = pendingMouse.wheel = 0;
            } else if (mouseRetry) mouseRetry = !transport.sendMouse(pendingMouse);
        }
        if (!menu && settings.inputMode != unsigned(router.mode())) {
            settings.inputMode = unsigned(router.mode());
            save(settings);
            dirty = true;
        }
        bool connected = transport.connected();
        if (connected != wasConnected) {
            release(transport);
            mouseRetry = false;
            router = InputRouter();
            router.attach(&engine, &settings);
            router.forceArrowSource(!imuOk);
            tracker = KeyStateTracker();
            blocked |= mask;
            wasConnected = connected;
            dirty = true;
        }
        if (previousCalibration != engine.calibrated()) {
            previousCalibration = engine.calibrated();
            dirty = true;
        }
        if (now - lastUi >= 200) {
            lastUi = now;
            if (dirty) {
                tft->fillScreen(cosmic::background);
                cosmic::header("Keyboard & Mouse");
                if (menu) {
                    String labels[] = {
                        String("Mode: ") + modeName(settings.inputMode),
                        String("Pointer: ") + sourceName(imuOk),
                        String("Sensitivity: ") + settings.sensLevel,
                        String("Typing pause: ") + (settings.clutchMode == 0 ? "On" : "Off"),
                        String("Invert horizontal: ") + (inverseX ? "On" : "Off"),
                        String("Invert vertical: ") + (inverseY ? "On" : "Off"),
                        "Recalibrate (hold still)",
                        wired ? "Pairing: USB needs none" : "Clear Bluetooth pairings",
                        "Resume",
                        "Exit to Apps"
                    };
                    unsigned first = (selection / 4) * 4;
                    for (unsigned i = first; i < std::min(first + 4, menuCount); ++i) {
                        int y = 23 + (i - first) * 22;
                        uint16_t bg = i == selection ? cosmic::panel : cosmic::surface;
                        tft->fillRoundRect(6, y, 228, 20, 5, bg);
                        cosmic::label(labels[i].substring(0, 36), 12, y + 6, bg);
                    }
                    cosmic::footer("; Up  . Down  ENTER Choose  ` Resume");
                } else {
                    cosmic::label(
                        String(wired ? "USB-C" : "Bluetooth LE") + " / " +
                            (connected ? "Connected" : "Waiting for host"),
                        8,
                        27
                    );
                    tft->fillRoundRect(7, 43, 226, 29, 7, cosmic::panel);
                    cosmic::label(modeName(unsigned(router.mode())), 15, 54, cosmic::panel);
                    cosmic::label(String("Pointer: ") + sourceName(imuOk), 8, 82);
                    cosmic::label(
                        !imuOk ? "No IMU: mouse mode uses arrows"
                               : (motionSource == 2 || engine.calibrated()
                                      ? "Fn+Space changes input mode"
                                      : "Hold still: calibrating motion..."),
                        8,
                        99
                    );
                    cosmic::footer("G0 Click / Hold for menu and exit");
                }
                tft->display(false);
                dirty = false;
            }
        }
        launcherDelayMs(1);
    }
    release(transport);
    cardputerHidCapture(false);
    imu.end();
    if (!wired) ble.end();
    tft->fillScreen(cosmic::background);
    tft->display(false);
    resetGlobals();
}
} // namespace
bool cosmicHidUsbActive() { return usbStarted; }
void cosmicHidMenu() {
    bool done = false;
    while (!done) {
        std::vector<Option> items = {
            {"USB keyboard & mouse",
             [&]() {
                 run(true);
                 done = true;
             }                                                   },
            {"Bluetooth keyboard & mouse",
             [&]() {
                 run(false);
                 done = true;
             }                                                   },
            {"Back",                       [&]() { done = true; }}
        };
        loopOptions(items);
        if (check(EscPress)) break;
    }
}
#endif
