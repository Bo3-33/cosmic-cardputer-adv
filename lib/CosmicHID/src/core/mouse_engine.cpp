#if defined(COSMIC_CARDPUTER) || defined(COSMIC_HID_HOST)
#include "mouse_engine.h"

#include <cmath>

namespace {
// 5 sensitivity levels, level 3 = 1.0 (guide §5.4).
constexpr float kSensScale[5] = {0.5f, 0.75f, 1.0f, 1.5f, 2.2f};
// Neutral capture accepts the window only if per-axis variance is below this
// (g^2): rejects a device being handled, accepts sensor noise.
constexpr float kCalibVarLimit = 0.005f;
// Samples whose magnitude is far from 1 g are shaking / free-fall, not a
// usable gravity direction: skip them.
constexpr float kMinNormG = 0.5f;
constexpr float kMaxNormG = 1.5f;
// Tangent-basis anchor: e_right comes from device X unless X is within ~11
// deg of the neutral gravity axis (device resting on its side) — anchor on
// device Y then. X and Y are orthogonal, so both can never degenerate at once.
constexpr float kAnchorMin2 = 0.04f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

void MouseEngine::configure(const MouseTuning &t) {
    tune_ = t;
    uint8_t lvl = t.sensLevel;
    if (lvl < 1) lvl = 1;
    if (lvl > 5) lvl = 5;
    sensScale_ = kSensScale[lvl - 1];
}

void MouseEngine::requestRecalibration() {
    calibrated_ = false;
    calibCount_ = 0;
    for (int i = 0; i < 3; ++i) calibSum_[i] = calibSumSq_[i] = 0;
    rezeroCount_ = 0;
    accX_ = accY_ = 0;
    gyroCount_ = 0;
    for (int i = 0; i < 3; ++i) gyroSum_[i] = gyroSq_[i] = gyroBias_[i] = 0;
}

void MouseEngine::feedGyro(float gx, float gy, float gz, uint32_t dtUs) {
    const float g[3] = {gx, gy, gz};
    for (float v : g)
        if (!std::isfinite(v)) return;
    if (!calibrated_) {
        for (float v : g)
            if (std::fabs(v) > 5) {
                gyroCount_ = 0;
                for (int i = 0; i < 3; ++i) gyroSum_[i] = gyroSq_[i] = 0;
                return;
            }
        for (int i = 0; i < 3; ++i) {
            gyroSum_[i] += g[i];
            gyroSq_[i] += g[i] * g[i];
        }
        if (++gyroCount_ < 200) return;
        bool stable = true;
        for (int i = 0; i < 3; ++i) {
            float mean = gyroSum_[i] / 200;
            gyroBias_[i] = mean;
            if (gyroSq_[i] / 200 - mean * mean > 0.5f) stable = false;
        }
        if (stable) calibrated_ = true;
        else {
            gyroCount_ = 0;
            for (int i = 0; i < 3; ++i) gyroSum_[i] = gyroSq_[i] = 0;
        }
        return;
    }
    if (!moveEnabled_ || !dtUs || dtUs > 50000) return;
    float x = -(gz - gyroBias_[2]), y = -(gx - gyroBias_[0]);
    if (std::fabs(x) < 0.8f) x = 0;
    if (std::fabs(y) < 0.8f) y = 0;
    float gain = 12.0f * sensScale_ * float(dtUs) * 1e-6f;
    accX_ += clampf(x, -1000, 1000) * gain;
    accY_ += clampf(y, -1000, 1000) * gain;
}

void MouseEngine::setMoveEnabled(bool en) {
    if (moveEnabled_ && !en) {
        // Drop whatever was accumulated so re-enabling does not jump.
        accX_ = accY_ = 0;
    }
    if (!moveEnabled_ && en) {
        // Engage: re-center on the pose held right now so the cursor starts
        // stationary regardless of how the device is being held.
        rezeroCount_ = kRezeroSamples;
        for (int i = 0; i < 3; ++i) rezeroSum_[i] = 0;
        accX_ = accY_ = 0;
    }
    moveEnabled_ = en;
}

// Store the neutral gravity direction (unit vector) and rebuild the tangent
// basis: eRight_ = device X minus its gravity component (Gram-Schmidt),
// eFwd_ = neutral x eRight_. The cross-product order makes eFwd_ equal +Y in
// the flat pose, and keeps the same handedness in every grip, so a given hand
// motion (e.g. "dip the top edge away from you") steers the same cursor
// direction whether the device is flat or upright.
void MouseEngine::setNeutral(float x, float y, float z) {
    neutral_[0] = x;
    neutral_[1] = y;
    neutral_[2] = z;

    float rx = 1.0f - x * x, ry = -x * y, rz = -x * z; // X - (X.g0)g0
    float n2 = rx * rx + ry * ry + rz * rz;
    if (n2 >= kAnchorMin2) {
        const float inv = 1.0f / std::sqrt(n2);
        eRight_[0] = rx * inv;
        eRight_[1] = ry * inv;
        eRight_[2] = rz * inv;
        eFwd_[0] = y * eRight_[2] - z * eRight_[1];
        eFwd_[1] = z * eRight_[0] - x * eRight_[2];
        eFwd_[2] = x * eRight_[1] - y * eRight_[0];
    } else {
        float fx = -y * x, fy = 1.0f - y * y, fz = -y * z; // Y - (Y.g0)g0
        n2 = fx * fx + fy * fy + fz * fz;
        const float inv = 1.0f / std::sqrt(n2);
        eFwd_[0] = fx * inv;
        eFwd_[1] = fy * inv;
        eFwd_[2] = fz * inv;
        eRight_[0] = eFwd_[1] * z - eFwd_[2] * y;
        eRight_[1] = eFwd_[2] * x - eFwd_[0] * z;
        eRight_[2] = eFwd_[0] * y - eFwd_[1] * x;
    }
}

void MouseEngine::feedCalibration(const AccelSample &a) {
    const float v[3] = {a.x, a.y, a.z};
    for (int i = 0; i < 3; ++i) {
        calibSum_[i] += v[i];
        calibSumSq_[i] += v[i] * v[i];
    }
    if (++calibCount_ < kCalibSamples) return;

    bool still = true;
    float mean[3];
    for (int i = 0; i < 3; ++i) {
        mean[i] = calibSum_[i] / kCalibSamples;
        const float var = calibSumSq_[i] / kCalibSamples - mean[i] * mean[i];
        if (var > kCalibVarLimit) still = false;
    }
    const float norm = std::sqrt(mean[0] * mean[0] + mean[1] * mean[1] + mean[2] * mean[2]);
    if (still && norm >= kMinNormG && norm <= kMaxNormG) {
        setNeutral(mean[0] / norm, mean[1] / norm, mean[2] / norm);
        calibrated_ = true;
    }
    // Movement detected: throw the window away and collect again.
    calibCount_ = 0;
    for (int i = 0; i < 3; ++i) calibSum_[i] = calibSumSq_[i] = 0;
}

// Tilt (unitless, ~sin of the angle) -> px/s. Quadratic response: fine control
// near the center, full speed at maxTilt.
float MouseEngine::shape(float tilt) const {
    const float mag = tilt < 0 ? -tilt : tilt;
    if (mag <= tune_.deadzoneTilt) return 0;
    float u = (mag - tune_.deadzoneTilt) / (tune_.maxTilt - tune_.deadzoneTilt);
    if (u > 1.0f) u = 1.0f;
    const float v = tune_.maxSpeedPxS * u * u * sensScale_;
    return tilt < 0 ? -v : v;
}

void MouseEngine::feedAccel(const AccelSample &a, uint32_t dtUs) {
    if (!calibrated_) {
        feedCalibration(a);
        return;
    }
    if (!moveEnabled_) return;

    const float norm = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    if (norm < kMinNormG || norm > kMaxNormG) return; // shaking / free-fall

    if (rezeroCount_ > 0) {
        // Engage re-center: average a short window, no motion until done.
        rezeroSum_[0] += a.x / norm;
        rezeroSum_[1] += a.y / norm;
        rezeroSum_[2] += a.z / norm;
        if (--rezeroCount_ == 0) {
            const float mx = rezeroSum_[0] / kRezeroSamples;
            const float my = rezeroSum_[1] / kRezeroSamples;
            const float mz = rezeroSum_[2] / kRezeroSamples;
            const float mn = std::sqrt(mx * mx + my * my + mz * mz);
            if (mn > 0) setNeutral(mx / mn, my / mn, mz / mn);
        }
        return;
    }

    // Tilt = current gravity direction projected onto the neutral tangent
    // basis (zero at neutral, ~sin of the tilt angle in every direction).
    const float gx = a.x / norm, gy = a.y / norm, gz = a.z / norm;
    const float tx = gx * eRight_[0] + gy * eRight_[1] + gz * eRight_[2];
    const float ty = gx * eFwd_[0] + gy * eFwd_[1] + gz * eFwd_[2];
    const float dt = static_cast<float>(dtUs) * 1e-6f;
    accX_ += shape(tx) * tune_.rollSign * dt;
    accY_ += shape(ty) * tune_.pitchSign * dt;
}

void MouseEngine::takeDelta(int8_t &dx, int8_t &dy) {
    const float cx = clampf(accX_, -127.0f, 127.0f);
    const float cy = clampf(accY_, -127.0f, 127.0f);
    dx = static_cast<int8_t>(cx);
    dy = static_cast<int8_t>(cy);
    accX_ -= static_cast<float>(dx);
    accY_ -= static_cast<float>(dy);
    // Keep saturation from stockpiling into a long tail after a strong tilt.
    accX_ = clampf(accX_, -127.0f, 127.0f);
    accY_ = clampf(accY_, -127.0f, 127.0f);
}

#endif
