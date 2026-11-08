#include <cassert>
#include <cmath>
#include <core/input_router.h>
#include <core/key_state_tracker.h>
#include <iostream>
static bool has(const KeyboardReport &r, uint8_t usage) {
    for (auto k : r.keys)
        if (k == usage) return true;
    return false;
}
struct Fixture {
    Settings settings;
    MouseEngine engine;
    InputRouter router;
    Fixture(uint8_t mode) {
        settings.inputMode = mode;
        settings.clutchMode = 3;
        engine.configure(MouseTuning{});
        router.attach(&engine, &settings);
    }
    void calibrate() {
        for (int i = 0; i < 200; ++i) engine.feedGyro(0.2f, -0.1f, 0.3f, 5000);
        assert(engine.calibrated());
        router.collect(1000);
    }
    void aim() {
        for (int i = 0; i < 20; ++i) engine.feedGyro(12, 0, 20, 5000);
    }
};
int main() {
    // Keyboard mode sends genuine held-key reports and modifiers, never pointer motion.
    Fixture keyboard(0);
    keyboard.calibrate();
    keyboard.router.onKeyEvent(42, true);
    keyboard.router.onKeyEvent(30, true);
    keyboard.aim();
    auto k = keyboard.router.collect(1100);
    assert(k.kbDirty && k.kb.modifiers == 1 && has(k.kb, 4));
    assert(!k.ms.dx && !k.ms.dy);
    keyboard.router.onKeyEvent(30, false);
    keyboard.router.onKeyEvent(42, false);
    k = keyboard.router.collect(1110);
    assert(k.kbDirty && !has(k.kb, 4) && !k.kb.modifiers);
    // Fn+Space cycles all three modes locally without injecting a space.
    keyboard.router.onKeyEvent(keyid::Fn, true);
    keyboard.router.onKeyEvent(keyid::Space, true);
    k = keyboard.router.collect(1120);
    assert(keyboard.router.mode() == InputMode::Mouse && !has(k.kb, 0x2c));
    // Mouse-only mode swallows typing, but moves and reports button edges.
    Fixture mouse(1);
    mouse.calibrate();
    mouse.router.onKeyEvent(30, true);
    mouse.aim();
    auto m = mouse.router.collect(1100);
    assert(!has(m.kb, 4) && m.msDirty && m.ms.dx && m.ms.dy);
    mouse.router.onKeyEvent(keyid::Enter, true);
    m = mouse.router.collect(1110);
    assert(m.ms.buttons & 1);
    mouse.router.onKeyEvent(keyid::Enter, false);
    m = mouse.router.collect(1120);
    assert(!(m.ms.buttons & 1));
    // Combo truly allows typing and gyro aiming in the same output frame.
    Fixture combo(2);
    combo.calibrate();
    combo.router.onKeyEvent(30, true);
    combo.aim();
    auto both = combo.router.collect(1100);
    assert(both.kbDirty && has(both.kb, 4) && both.msDirty && both.ms.dx && both.ms.dy);
    combo.router.onKeyEvent(keyid::Opt, true);
    combo.router.onKeyEvent(keyid::Comma, true);
    both = combo.router.collect(1110);
    assert((both.ms.buttons & 1) && !(both.kb.modifiers & mod::LGui) && !has(both.kb, 0x36));
    // Opening a local menu must flush held keys/buttons to neutral.
    combo.router.setSuspended(true);
    both = combo.router.collect(1120);
    assert(both.kbDirty && both.msDirty && !both.kb.modifiers && !has(both.kb, 4) && both.ms.buttons == 0);
    combo.aim();
    both = combo.router.collect(1130);
    assert(!both.ms.dx && !both.ms.dy);
    // Optional typing pause and keyboard-only mode keep pointer motion off.
    Fixture pause(2);
    pause.settings.clutchMode = 0;
    pause.calibrate();
    pause.router.onKeyEvent(30, true);
    pause.router.collect(1100);
    pause.aim();
    auto p = pause.router.collect(1200);
    assert(!p.ms.dx && !p.ms.dy);
    // Calibration removes constant gyro bias; deadzone rejects noise; invalid dt cannot jump.
    MouseEngine e;
    e.configure(MouseTuning{});
    for (int i = 0; i < 200; ++i) e.feedGyro(0.2f, 0, 0.3f, 5000);
    e.setMoveEnabled(true);
    for (int i = 0; i < 100; ++i) e.feedGyro(0.3f, 0, 0.4f, 5000);
    int8_t x, y;
    e.takeDelta(x, y);
    assert(x == 0 && y == 0);
    e.feedGyro(100, 0, 100, 10000000);
    e.takeDelta(x, y);
    assert(x == 0 && y == 0);
    e.feedGyro(NAN, 0, 0, 5000);
    e.takeDelta(x, y);
    assert(x == 0 && y == 0);
    for (int i = 0; i < 100; ++i) e.feedGyro(1000, 0, 1000, 5000);
    e.takeDelta(x, y);
    assert(x >= -127 && y >= -127);
    e.setMoveEnabled(false);
    e.takeDelta(x, y);
    assert(!x && !y);
    e.requestRecalibration();
    for (int i = 0; i < 200; ++i) e.feedGyro(20, 0, 0, 5000);
    assert(!e.calibrated());
    // Arrow fallback works without a motion sensor.
    Fixture fallback(1);
    fallback.router.forceArrowSource(true);
    fallback.router.onKeyEvent(keyid::Slash, true);
    auto a = fallback.router.collect(1000);
    assert(a.ms.dx > 0);
    fallback.router.onKeyEvent(keyid::KeyJ, true);
    a = fallback.router.collect(1010);
    assert(a.ms.buttons & 1);
    KeyStateTracker tracker;
    unsigned presses = 0, releases = 0;
    auto event = [&](KeyStateTracker::Event ev) {
        if (ev.down) ++presses;
        else ++releases;
    };
    tracker.update((1ULL << 30) | (1ULL << 42), event);
    tracker.update(0, event);
    assert(presses == 2 && releases == 2);
    std::cout << "HID modes, simultaneous keyboard/gyro, modifiers, local shortcuts, neutral release, "
                 "calibration and fallback passed\n";
}
