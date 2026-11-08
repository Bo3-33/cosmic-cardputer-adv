#if defined(COSMIC_CARDPUTER) || defined(COSMIC_HID_HOST)
#include "input_router.h"

namespace {
constexpr uint32_t kOptTapMs        = 250;  // Opt solo tap window (D4 simplified rule)
constexpr uint32_t kBtnALongMs      = 700;  // >= this is the menu gesture, not a click
constexpr uint32_t kTypingFreezeMs  = 300;  // cursor stays frozen after the last key release
constexpr uint32_t kWheelDelayMs    = 300;  // wheel auto-repeat initial delay
constexpr uint32_t kWheelRepeatMs   = 100;  // wheel auto-repeat period
constexpr uint32_t kArrowRampStart  = 600;  // hold time before arrow-cursor acceleration
constexpr uint32_t kArrowRampLenMs  = 1000; // linear ramp duration 3 -> 12 px/tick
constexpr int      kArrowBaseSpeed  = 3;
constexpr int      kArrowMaxSpeed   = 12;

// Clutch modes (Settings::clutchMode)
constexpr uint8_t kClutchDefault    = 0;
constexpr uint8_t kClutchHoldMove   = 1;
constexpr uint8_t kClutchHoldFreeze = 2;
constexpr uint8_t kClutchAlwaysOn   = 3;

int8_t clampDelta(int v) {
    if (v > 127) return 127;
    if (v < -127) return -127;
    return static_cast<int8_t>(v);
}
} // namespace

void InputRouter::attach(MouseEngine* me, const Settings* s) {
    engine_ = me;
    settings_ = s;
    mode_ = (s != nullptr && s->inputMode <= 2) ? static_cast<InputMode>(s->inputMode)
                                                : InputMode::Combo;
}

bool InputRouter::effectiveArrowSource() const {
    if (forcedArrow_) return true;
    return settings_ != nullptr && settings_->cursorSource == 1;
}

// ---------------------------------------------------------------- key events

void InputRouter::onKeyEvent(uint8_t keyId, bool down) {
    if (keyId >= keyid::Count) return;
    if (down) press(keyId); else release(keyId);
}

void InputRouter::press(uint8_t keyId) {
    if (latch_[keyId].kind != Idle) return;  // duplicate press: ignore

    const KeyAction a = lookup(keyId, fn_);

    // Fn key and the Fn+Space mode switch work in every mode.
    if (a.cls == KeyClass::FnLayer) {
        latchSimple(keyId, FnLatch);
        fn_ = true;
        // Held Fn acts as a clutch key in IMU mouse mode like any other key.
        if (mode_ == InputMode::Mouse && !effectiveArrowSource()) latchClutch(keyId);
        return;
    }
    if (keyId == keyid::Space && fn_) {
        latchSimple(keyId, Swallowed);
        if (!suspended_) cycleMode();
        return;
    }

    switch (mode_) {
        case InputMode::Keyboard: pressKeyboard(keyId, a); break;
        case InputMode::Mouse:    pressMouseMode(keyId);   break;
        case InputMode::Combo:    pressCombo(keyId, a);    break;
    }
}

void InputRouter::pressKeyboard(uint8_t keyId, const KeyAction& a) {
    switch (a.cls) {
        case KeyClass::Modifier:
            latchSimple(keyId, ModLatch, a.code);
            modifiers_ |= a.code;
            break;
        case KeyClass::HidKey:
            latchHidKey(keyId, a.code);
            break;
        default:
            latchSimple(keyId, Swallowed);
            break;
    }
}

void InputRouter::pressMouseMode(uint8_t keyId) {
    // Mouse mode sends no keyboard reports at all (D3).
    if (fn_) {  // "Fn+other = ignored" (Fn+Space already handled)
        latchSimple(keyId, Swallowed);
        return;
    }

    if (!effectiveArrowSource()) {
        // IMU source: inverted-T mouse keys, everything else is a clutch key.
        switch (keyId) {
            case keyid::Comma:
            case keyid::Enter:     mouseButtonPress(keyId, 0); break;
            case keyid::Slash:     mouseButtonPress(keyId, 1); break;
            case keyid::KeyM:      mouseButtonPress(keyId, 2); break;
            case keyid::Semicolon: wheelPress(keyId, 1);  break;
            case keyid::Dot:       wheelPress(keyId, -1); break;
            default:
                latchSimple(keyId, Swallowed);
                latchClutch(keyId);
                break;
        }
    } else {
        // Arrow-key source (D14): ;=up ,=left .=down /=right, j/k buttons,
        // u/n wheel, Enter left. No clutch keys in this source.
        switch (keyId) {
            case keyid::Semicolon: arrowPress(keyId, 0); break;
            case keyid::Comma:     arrowPress(keyId, 1); break;
            case keyid::Dot:       arrowPress(keyId, 2); break;
            case keyid::Slash:     arrowPress(keyId, 3); break;
            case keyid::KeyJ:      mouseButtonPress(keyId, 0); break;
            case keyid::KeyK:      mouseButtonPress(keyId, 1); break;
            case keyid::Enter:     mouseButtonPress(keyId, 0); break;
            case keyid::KeyU:      wheelPress(keyId, 1);  break;
            case keyid::KeyN:      wheelPress(keyId, -1); break;
            default:
                latchSimple(keyId, Swallowed);
                break;
        }
    }
}

void InputRouter::pressCombo(uint8_t keyId, const KeyAction& a) {
    // Opt is the local mouse prefix: never reported while held (simplified rule).
    if (a.cls == KeyClass::Modifier && a.code == mod::LGui) {
        latchSimple(keyId, OptLatch);
        optDown_ = true;
        optDownMs_ = lastNow_;
        optCombo_ = false;
        optOther_ = false;
        return;
    }

    if (optDown_) {
        switch (keyId) {
            case keyid::Comma:     mouseButtonPress(keyId, 0); optCombo_ = true; return;
            case keyid::Slash:     mouseButtonPress(keyId, 1); optCombo_ = true; return;
            case keyid::KeyM:      mouseButtonPress(keyId, 2); optCombo_ = true; return;
            case keyid::Semicolon: wheelPress(keyId, 1);  optCombo_ = true; return;
            case keyid::Dot:       wheelPress(keyId, -1); optCombo_ = true; return;
            default:
                optOther_ = true;  // no solo tap on release
                break;
        }
    }

    switch (a.cls) {
        case KeyClass::Modifier:
            latchSimple(keyId, ModLatch, a.code);
            modifiers_ |= a.code;
            latchTyping(keyId);
            break;
        case KeyClass::HidKey:
            latchHidKey(keyId, a.code);
            latchTyping(keyId);   // freeze even if 6KRO-dropped: the key is held
            break;
        default:
            latchSimple(keyId, Swallowed);
            break;
    }
}

void InputRouter::release(uint8_t keyId) {
    KeyLatch l = latch_[keyId];
    latch_[keyId] = KeyLatch{};

    if (l.typing && typingCount_ > 0) {
        --typingCount_;
        lastTypingReleaseMs_ = lastNow_;
    }
    if (l.clutch && clutchCount_ > 0) --clutchCount_;

    switch (l.kind) {
        case HidKeyLatch: removeKey(l.data); break;
        case ModLatch:    modifiers_ &= static_cast<uint8_t>(~l.data); break;
        case FnLatch:     fn_ = false; break;
        case OptLatch: {
            optDown_ = false;
            const bool tap = !optCombo_ && !optOther_ &&
                             (lastNow_ - optDownMs_) < kOptTapMs;
            if (tap && !suspended_) guiTapPhase_ = 2;
            break;
        }
        case MouseBtn:
            if (btnCount_[l.data] > 0) --btnCount_[l.data];
            break;
        case WheelKey:
            if (wheelActive_ && wheelKeyId_ == keyId) wheelActive_ = false;
            break;
        case ArrowMove:
            arrowHeld_[l.data] = false;
            break;
        default:
            break;
    }
}

// ------------------------------------------------------------- latch helpers

void InputRouter::latchSimple(uint8_t keyId, uint8_t kind, uint8_t data) {
    latch_[keyId] = KeyLatch{kind, data, false, false};
}

void InputRouter::latchHidKey(uint8_t keyId, uint8_t usage) {
    if (addKey(usage)) {
        latchSimple(keyId, HidKeyLatch, usage);
    } else {
        latchSimple(keyId, Swallowed);  // >6 keys: drop the newest (D10)
    }
}

void InputRouter::latchClutch(uint8_t keyId) {
    latch_[keyId].clutch = true;
    ++clutchCount_;
}

void InputRouter::latchTyping(uint8_t keyId) {
    latch_[keyId].typing = true;
    ++typingCount_;
}

void InputRouter::mouseButtonPress(uint8_t keyId, uint8_t btn) {
    latchSimple(keyId, MouseBtn, btn);
    ++btnCount_[btn];
}

void InputRouter::wheelPress(uint8_t keyId, int8_t dir) {
    latchSimple(keyId, WheelKey, dir > 0 ? 1 : 0);
    wheelActive_ = true;
    wheelKeyId_ = keyId;
    wheelDir_ = dir;
    wheelPressMs_ = lastNow_;
    wheelLastMs_ = lastNow_;
    if (!suspended_) wheelAccum_ += dir;  // immediate tick
}

void InputRouter::arrowPress(uint8_t keyId, uint8_t dir) {
    latchSimple(keyId, ArrowMove, dir);
    arrowHeld_[dir] = true;
    arrowPressMs_[dir] = lastNow_;
}

// -------------------------------------------------------------- mode / BtnA

void InputRouter::cycleMode() {
    mode_ = static_cast<InputMode>((static_cast<uint8_t>(mode_) + 1) % 3);
    resetTransientState();
}

// Clear every reportable effect but keep tracking held keys (their releases
// must be consumed silently -> converted to Swallowed). Fn keeps its role.
void InputRouter::resetTransientState() {
    for (uint8_t i = 0; i < keyid::Count; ++i) {
        if (latch_[i].kind == Idle) continue;
        if (latch_[i].kind == FnLatch) {
            latch_[i].clutch = false;
            latch_[i].typing = false;
            continue;
        }
        latch_[i] = KeyLatch{Swallowed, 0, false, false};
    }
    modifiers_ = 0;
    for (auto& k : keys6_) k = 0;
    for (auto& c : btnCount_) c = 0;
    typingCount_ = 0;
    clutchCount_ = 0;
    optDown_ = false;
    guiTapPhase_ = 0;
    btnAClickPhase_ = 0;
    wheelActive_ = false;
    wheelAccum_ = 0;
    for (auto& h : arrowHeld_) h = false;
    forceKb_ = true;   // push the empty report (no stuck keys)
    forceMs_ = true;   // push released buttons
}

void InputRouter::onButtonA(bool down) {
    if (down && !btnADown_) {
        btnADown_ = true;
        btnADownMs_ = lastNow_;
    } else if (!down && btnADown_) {
        btnADown_ = false;
        const bool shortPress = (lastNow_ - btnADownMs_) < kBtnALongMs;
        // BtnA short press = left click, except in pure keyboard mode (D3/D4).
        if (shortPress && !suspended_ && mode_ != InputMode::Keyboard) {
            btnAClickPhase_ = 2;
        }
    }
}

void InputRouter::setSuspended(bool s) {
    if (s == suspended_) return;
    suspended_ = s;
    if (s) {
        flushPending_ = true;   // one all-released report pair (D11)
        guiTapPhase_ = 0;
        btnAClickPhase_ = 0;
        wheelAccum_ = 0;
    } else {
        // Keys held at resume were menu input (the close key itself, e.g. `):
        // swallow them so they never reach the host; releases stay silent.
        resetTransientState();
    }
}

// ------------------------------------------------------------------ reports

bool InputRouter::addKey(uint8_t usage) {
    for (const auto k : keys6_) {
        if (k == usage) return true;  // already reported
    }
    for (auto& k : keys6_) {
        if (k == 0) { k = usage; return true; }
    }
    return false;
}

void InputRouter::removeKey(uint8_t usage) {
    for (auto& k : keys6_) {
        if (k == usage) { k = 0; return; }
    }
}

KeyboardReport InputRouter::buildKb() const {
    KeyboardReport r;
    r.modifiers = modifiers_;
    int n = 0;
    for (const auto k : keys6_) {
        if (k != 0) r.keys[n++] = k;  // compact: hosts cope either way, tidy anyway
    }
    return r;
}

uint8_t InputRouter::heldButtons() const {
    uint8_t b = 0;
    if (btnCount_[0] > 0) b |= 0x01;
    if (btnCount_[1] > 0) b |= 0x02;
    if (btnCount_[2] > 0) b |= 0x04;
    return b;
}

bool InputRouter::moveAllowed(uint32_t nowMs) const {
    uint8_t cm = settings_ != nullptr ? settings_->clutchMode : kClutchDefault;
    if (mode_ == InputMode::Mouse) {
        if (cm == kClutchDefault) cm = kClutchHoldMove;  // mouse-mode default (Q2)
        switch (cm) {
            case kClutchHoldMove:   return clutchCount_ > 0;
            case kClutchHoldFreeze: return clutchCount_ == 0;
            default:                return true;
        }
    }
    // Combo mode: default = always-on with typing freeze (D4).
    switch (cm) {
        case kClutchHoldMove:   return typingCount_ > 0;
        case kClutchHoldFreeze: return typingCount_ == 0;
        case kClutchAlwaysOn:   return true;
        default:
            return typingCount_ == 0 &&
                   static_cast<int32_t>(nowMs - lastTypingReleaseMs_) >=
                       static_cast<int32_t>(kTypingFreezeMs);
    }
}

void InputRouter::collectArrowMotion(uint32_t nowMs, int& dx, int& dy) {
    static constexpr int kDirX[4] = {0, -1, 0, 1};
    static constexpr int kDirY[4] = {-1, 0, 1, 0};
    for (int d = 0; d < 4; ++d) {
        if (!arrowHeld_[d]) continue;
        const uint32_t held = nowMs - arrowPressMs_[d];
        int speed = kArrowBaseSpeed;
        if (held > kArrowRampStart) {
            uint32_t ramp = held - kArrowRampStart;
            if (ramp > kArrowRampLenMs) ramp = kArrowRampLenMs;
            speed += static_cast<int>(ramp * (kArrowMaxSpeed - kArrowBaseSpeed) / kArrowRampLenMs);
        }
        dx += kDirX[d] * speed;
        dy += kDirY[d] * speed;
    }
}

void InputRouter::serviceWheelRepeat(uint32_t nowMs) {
    if (!wheelActive_ || suspended_) return;
    if (nowMs - wheelPressMs_ < kWheelDelayMs) return;
    if (nowMs - wheelLastMs_ < kWheelRepeatMs) return;
    wheelLastMs_ = nowMs;
    wheelAccum_ += wheelDir_;
}

InputRouter::Output InputRouter::collect(uint32_t nowMs) {
    lastNow_ = nowMs;
    Output out;

    if (suspended_) {
        if (engine_ != nullptr) engine_->setMoveEnabled(false);
        lastMoveEnabled_ = false;
        if (flushPending_) {
            flushPending_ = false;
            out.kbDirty = true;      // all keys up
            out.msDirty = true;      // all buttons released
            lastSentKb_ = out.kb;
            lastButtons_ = 0;
        }
        return out;
    }

    // ---- mouse motion ----
    int dx = 0, dy = 0;
    const bool arrowSrc = effectiveArrowSource();
    bool moveEn = false;
    if (mode_ != InputMode::Keyboard) {
        if (!arrowSrc) {
            moveEn = moveAllowed(nowMs);
            if (engine_ != nullptr) {
                engine_->setMoveEnabled(moveEn);
                int8_t gx = 0, gy = 0;
                engine_->takeDelta(gx, gy);
                dx = gx;
                dy = gy;
            }
        } else {
            if (engine_ != nullptr) engine_->setMoveEnabled(false);
            if (mode_ == InputMode::Mouse) {   // arrow source only acts in Mouse mode (D14)
                moveEn = true;
                collectArrowMotion(nowMs, dx, dy);
            }
        }
    } else if (engine_ != nullptr) {
        engine_->setMoveEnabled(false);
    }
    lastMoveEnabled_ = moveEn;

    // ---- wheel ----
    serviceWheelRepeat(nowMs);
    int wheel = wheelAccum_;
    wheelAccum_ = 0;

    // ---- buttons (held keys + BtnA click pulse) ----
    uint8_t buttons = heldButtons();
    if (btnAClickPhase_ == 2) {
        buttons |= 0x01;
        btnAClickPhase_ = 1;
    } else if (btnAClickPhase_ == 1) {
        btnAClickPhase_ = 0;
    }

    out.ms.dx = clampDelta(dx);
    out.ms.dy = clampDelta(dy);
    out.ms.wheel = clampDelta(wheel);
    out.ms.buttons = buttons;
    out.msDirty = forceMs_ || out.ms.dx != 0 || out.ms.dy != 0 ||
                  out.ms.wheel != 0 || buttons != lastButtons_;
    if (out.msDirty) {
        lastButtons_ = buttons;
        forceMs_ = false;
    }

    // ---- keyboard ----
    KeyboardReport kb = buildKb();
    if (guiTapPhase_ == 2) {          // Opt solo tap: one GUI press...
        kb.modifiers |= mod::LGui;
        guiTapPhase_ = 1;
        forceKb_ = true;
    } else if (guiTapPhase_ == 1) {   // ...then the matching release
        guiTapPhase_ = 0;
        forceKb_ = true;
    }

    out.kbDirty = forceKb_ || stickyKb_ || kb != lastSentKb_;
    if (out.kbDirty) {
        out.kb = kb;
        lastSentKb_ = kb;
        forceKb_ = false;
        stickyKb_ = false;
    }
    return out;
}

#endif
