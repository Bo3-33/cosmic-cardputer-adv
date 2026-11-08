#pragma once
#include <cstdint>

#include "hid_report.h"
#include "keymap.h"
#include "mouse_engine.h"
#include "settings.h"

enum class InputMode : uint8_t { Keyboard = 0, Mouse = 1, Combo = 2 };  // Combo = default

// Mode state machine: turns key/button events plus IMU deltas into HID reports.
// Hides the Fn layer, 6KRO assembly, mouse key bindings, clutch/freeze logic,
// Opt interception, arrow-key cursor source and menu suspension (guide §5.5).
class InputRouter {
public:
    struct Output {              // pulled by the main loop each tick
        bool kbDirty = false;
        KeyboardReport kb;
        bool msDirty = false;
        MouseReport ms;
    };

    void attach(MouseEngine* me, const Settings* s);
    void onKeyEvent(uint8_t keyId, bool down);   // from KeyStateTracker
    void onButtonA(bool down);                   // G0, level-sampled every loop
    void setSuspended(bool s);                   // menu open -> true (D11)
    Output collect(uint32_t nowMs);              // builds reports, applies clutch to MouseEngine

    // UI queries
    InputMode mode() const { return mode_; }
    bool fnHeld() const { return fn_; }
    bool cursorMoveEnabled() const { return lastMoveEnabled_; }
    bool arrowSourceActive() const { return effectiveArrowSource(); }

    // Main-loop support
    void keepKbDirty() { stickyKb_ = true; }     // failed send -> retry next tick (guide §9)
    void forceArrowSource(bool f) { forcedArrow_ = f; }  // IMU init failure (D14)

private:
    enum LatchKind : uint8_t {
        Idle = 0,
        Swallowed,   // physical key held, no ongoing effect
        HidKeyLatch, // data = usage ID
        ModLatch,    // data = modifier mask
        FnLatch,
        OptLatch,    // Combo-mode Opt (suppressed LGui, D4 simplified rule)
        MouseBtn,    // data = button index 0..2
        WheelKey,    // data = 0 down / 1 up
        ArrowMove,   // data = direction 0..3 (up/left/down/right)
    };
    struct KeyLatch {
        uint8_t kind = Idle;
        uint8_t data = 0;
        bool clutch = false;   // counted in clutchCount_
        bool typing = false;   // counted in typingCount_ (Combo freeze)
    };

    void press(uint8_t keyId);
    void release(uint8_t keyId);
    void pressKeyboard(uint8_t keyId, const KeyAction& a);
    void pressMouseMode(uint8_t keyId);
    void pressCombo(uint8_t keyId, const KeyAction& a);

    void latchSimple(uint8_t keyId, uint8_t kind, uint8_t data = 0);
    void latchHidKey(uint8_t keyId, uint8_t usage);
    void latchClutch(uint8_t keyId);
    void latchTyping(uint8_t keyId);
    void mouseButtonPress(uint8_t keyId, uint8_t btn);
    void wheelPress(uint8_t keyId, int8_t dir);
    void arrowPress(uint8_t keyId, uint8_t dir);

    void cycleMode();
    void resetTransientState();

    bool addKey(uint8_t usage);
    void removeKey(uint8_t usage);
    KeyboardReport buildKb() const;
    uint8_t heldButtons() const;

    bool effectiveArrowSource() const;
    bool moveAllowed(uint32_t nowMs) const;
    void collectArrowMotion(uint32_t nowMs, int& dx, int& dy);
    void serviceWheelRepeat(uint32_t nowMs);

    // --- wiring ---
    MouseEngine*    engine_   = nullptr;
    const Settings* settings_ = nullptr;

    // --- mode / global state ---
    InputMode mode_ = InputMode::Combo;
    bool suspended_ = false;
    bool flushPending_ = false;
    bool fn_ = false;
    bool forcedArrow_ = false;
    uint32_t lastNow_ = 0;

    // --- keyboard state ---
    KeyLatch latch_[keyid::Count];
    uint8_t modifiers_ = 0;
    uint8_t keys6_[6] = {0};
    KeyboardReport lastSentKb_;
    bool stickyKb_ = false;
    bool forceKb_ = false;

    // --- Combo-mode Opt handling (simplified per review: never reported while
    // held; solo tap < 250ms replays one GUI press+release; fully swallowed
    // once it triggered a mouse combo or another key was pressed) ---
    bool optDown_ = false;
    uint32_t optDownMs_ = 0;
    bool optCombo_ = false;
    bool optOther_ = false;
    uint8_t guiTapPhase_ = 0;   // 2 = press report pending, 1 = release report pending

    // --- mouse state ---
    uint8_t btnCount_[3] = {0, 0, 0};  // several keys may hold the same button
    uint8_t lastButtons_ = 0;
    bool forceMs_ = false;
    bool lastMoveEnabled_ = false;

    uint16_t clutchCount_ = 0;   // Mouse-mode clutch keys currently held
    uint16_t typingCount_ = 0;   // Combo-mode keyboard keys currently held
    uint32_t lastTypingReleaseMs_ = 0;

    // BtnA short press -> one left click (press report, then release report)
    bool btnADown_ = false;
    uint32_t btnADownMs_ = 0;
    uint8_t btnAClickPhase_ = 0;

    // Wheel key: immediate tick on press, then auto-repeat while held.
    bool wheelActive_ = false;
    uint8_t wheelKeyId_ = 0;
    int8_t wheelDir_ = 0;
    uint32_t wheelPressMs_ = 0;
    uint32_t wheelLastMs_ = 0;
    int16_t wheelAccum_ = 0;

    // Arrow-key cursor source (Mouse mode only, D14).
    bool arrowHeld_[4] = {false, false, false, false};
    uint32_t arrowPressMs_[4] = {0, 0, 0, 0};
};
