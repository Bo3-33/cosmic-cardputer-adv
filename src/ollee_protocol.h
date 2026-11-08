#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ollee {
constexpr const char *service = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char *writeCharacteristic = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char *notifyCharacteristic = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
inline uint16_t crc(const uint8_t *p, size_t n) {
    uint16_t value = 0xffff;
    while (n--) {
        value ^= uint16_t(*p++) << 8;
        for (int i = 0; i < 8; ++i)
            value = value & 0x8000 ? uint16_t((value << 1) ^ 0x1021) : uint16_t(value << 1);
    }
    return value;
}
inline size_t frame(uint8_t cmd, const uint8_t *payload, size_t n, uint8_t *out, size_t capacity) {
    if (n > 249 || capacity < n + 8) return 0;
    out[0] = 0;
    out[1] = uint8_t(n + 6);
    out[2] = 0xaa;
    out[3] = 0x55;
    out[6] = 2;
    out[7] = cmd;
    if (n) memcpy(out + 8, payload, n);
    uint16_t sum = crc(out + 6, n + 2);
    out[4] = sum >> 8;
    out[5] = sum;
    return n + 8;
}
inline void little(uint8_t *p, uint32_t n, unsigned count) {
    for (unsigned i = 0; i < count; ++i) p[i] = uint8_t(n >> (8 * i));
}
inline bool timePayload(
    uint32_t utc, int32_t offsetSeconds, int32_t latitudeMilli, int32_t longitudeMilli, uint16_t milliseconds,
    uint16_t tideMinutes, uint8_t out[20]
) {
    if (utc < 1704067200u || utc > 4102444799u || offsetSeconds < -43200 || offsetSeconds > 50400 ||
        latitudeMilli < -90000 || latitudeMilli > 90000 || longitudeMilli < -180000 ||
        longitudeMilli > 180000 || milliseconds > 999 || tideMinutes > 1440)
        return false;
    little(out, utc, 4);
    little(out + 4, uint32_t(offsetSeconds), 4);
    little(out + 8, uint32_t(latitudeMilli), 4);
    little(out + 12, uint32_t(longitudeMilli), 4);
    little(out + 16, milliseconds, 2);
    little(out + 18, tideMinutes, 2);
    return true;
}
struct Receiver {
    uint8_t buffer[257] = {};
    size_t used = 0;
    // True only on a complete valid frame; the caller consumes buffer[7] and buffer+8 immediately.
    bool byte(uint8_t b) {
        if (used >= sizeof(buffer)) used = 0;
        if (!used && b != 0) return false;
        buffer[used++] = b;
        if ((used == 2 && buffer[1] < 6) || (used == 3 && buffer[2] != 0xaa) ||
            (used == 4 && buffer[3] != 0x55)) {
            used = 0;
            return false;
        }
        if (used < 2 || used != size_t(buffer[1]) + 2) return false;
        size_t n = used;
        used = 0;
        return buffer[6] == 2 && crc(buffer + 6, n - 6) == uint16_t((uint16_t(buffer[4]) << 8) | buffer[5]);
    }
};
inline bool supportedVersion(const uint8_t *p, size_t n) {
    return n == 36 && memcmp(p + 8, "01.05.00", 8) == 0 && memcmp(p + 16, "00.01.11", 8) == 0;
}
} // namespace ollee
