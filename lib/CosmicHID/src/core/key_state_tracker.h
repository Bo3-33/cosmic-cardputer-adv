#pragma once
#include <cstdint>

// Frame-differencing of the 56-bit pressed-key mask into press/release events.
// Debouncing is done in hardware by the TCA8418; no software debounce here.
class KeyStateTracker {
public:
    struct Event {
        uint8_t keyId;
        bool    down;
    };

    // Compare with the previous frame and emit up to 8 events per call.
    // If more than 8 bits changed, the surplus stays pending and is emitted on
    // the next call (prev_ is only advanced for bits actually reported).
    template <typename F>
    void update(uint64_t mask, F&& emit) {
        mask &= kValidMask;
        uint64_t diff = mask ^ prev_;
        int emitted = 0;
        while (diff != 0 && emitted < kMaxEventsPerCall) {
            const uint64_t bit = diff & (~diff + 1);  // lowest set bit
            const uint8_t  id  = bitIndex(bit);
            prev_ ^= bit;
            emit(Event{id, (mask & bit) != 0});
            diff &= diff - 1;
            ++emitted;
        }
    }

private:
    static constexpr int      kMaxEventsPerCall = 8;
    static constexpr uint64_t kValidMask = (1ULL << 56) - 1;

    static uint8_t bitIndex(uint64_t bit) {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<uint8_t>(__builtin_ctzll(bit));
#else
        uint8_t n = 0;
        while (!(bit & 1)) { bit >>= 1; ++n; }
        return n;
#endif
    }

    uint64_t prev_ = 0;
};
