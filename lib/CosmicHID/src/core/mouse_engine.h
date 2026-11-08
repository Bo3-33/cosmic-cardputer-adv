#pragma once
#include <cstdint>

// One accelerometer sample in g, device frame.
struct AccelSample {
    float x = 0, y = 0, z = 0;
};

// Tilt-joystick tuning: the device acts like a joystick. Tilting it away from
// the calibrated neutral pose moves the cursor toward the lower edge, with
// speed proportional to the tilt (quadratic response for fine control near
// the center). Axis signs to verify on hardware: flip rollSign / pitchSign
// here if a direction is inverted.
struct MouseTuning {
    uint8_t sensLevel    = 3;      // 1..5 (FR-12)
    float   deadzoneTilt = 0.06f;  // |tilt| below this -> no motion (~3.5 deg)
    float   maxTilt      = 0.45f;  // tilt at which speed saturates (~27 deg)
    float   maxSpeedPxS  = 900.0f; // px/s at full tilt, sens level 3
    float   rollSign     = -1.0f;  // left side lower -> cursor left
    float   pitchSign    = 1.0f;   // top side lower  -> cursor up
};

// Accel tilt -> relative cursor displacement (joystick model). Hides neutral
// pose capture, deadzone, response curve and fractional accumulation.
// Every time movement engages (setMoveEnabled false -> true: clutch press,
// typing-freeze release, mode entry) the pose held at that moment is captured
// as the new neutral, so the cursor never moves by itself on engage no matter
// how the user holds the device.
//
// Tilt is measured as the projection of the current gravity direction onto an
// orthonormal basis of the plane perpendicular to the neutral gravity vector
// (built at every neutral capture). This keeps the joystick rule — "the edge
// you dip below the engage pose steers the cursor, deeper is faster" — first
// order in every direction for ANY grip: flat on a desk, upright facing the
// user, or anything between. Reading raw x/y components instead (the naive
// flat-pose formula) collapses to a second-order, sign-ambiguous signal for
// pitch as the grip approaches vertical.
class MouseEngine {
public:
    void configure(const MouseTuning& t);
    void feedAccel(const AccelSample& a, uint32_t dtUs);
    void feedGyro(float gx, float gy, float gz, uint32_t dtUs);
    void setMoveEnabled(bool en);            // clutch/freeze decision comes from the router
    void takeDelta(int8_t& dx, int8_t& dy);  // accumulated integer delta, clamped to +/-127
    bool calibrated() const { return calibrated_; }
    void requestRecalibration();

    // Test/diagnostic access to the captured neutral pose (unit vector).
    AccelSample neutral() const { return {neutral_[0], neutral_[1], neutral_[2]}; }

private:
    void feedCalibration(const AccelSample& a);
    void setNeutral(float x, float y, float z);  // stores g0 + rebuilds the tangent basis
    float shape(float tilt) const;

    MouseTuning tune_{};
    float sensScale_ = 1.0f;

    bool  calibrated_ = false;
    bool  moveEnabled_ = false;  // starts disabled: the first enable re-centers

    // Boot/menu neutral capture (N samples, variance + 1g-magnitude gate).
    static constexpr int kCalibSamples = 200;
    int   calibCount_ = 0;
    float calibSum_[3]   = {0, 0, 0};
    float calibSumSq_[3] = {0, 0, 0};

    // Engage re-center: short averaging window started on every enable edge.
    static constexpr int kRezeroSamples = 8;  // 40 ms @ 200 Hz
    int   rezeroCount_ = 0;
    float rezeroSum_[3] = {0, 0, 0};

    // Neutral gravity direction + tangent-plane basis (flat-pose defaults).
    // eRight_/eFwd_ span the plane perpendicular to neutral_; when flat they
    // are exactly the device X/Y axes, so flat behavior matches the naive
    // formula bit for bit.
    float neutral_[3] = {0, 0, 1};
    float eRight_[3]  = {1, 0, 0};
    float eFwd_[3]    = {0, 1, 0};

    // Fractional accumulation (px).
    float accX_ = 0, accY_ = 0;
    int gyroCount_ = 0;
    float gyroSum_[3] = {}, gyroSq_[3] = {}, gyroBias_[3] = {};
};
