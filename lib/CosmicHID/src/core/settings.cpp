#if defined(COSMIC_CARDPUTER) || defined(COSMIC_HID_HOST)
#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool parseUint(const char* s, size_t len, long& out) {
    if (len == 0 || len > 10) return false;
    char tmp[12];
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    char* end = nullptr;
    const long v = strtol(tmp, &end, 10);
    if (end == tmp || *end != '\0') return false;
    out = v;
    return true;
}

// Apply an integer field only when the value is inside its valid range.
bool applyRanged(uint8_t& field, const char* v, size_t len, long lo, long hi) {
    long n;
    if (!parseUint(v, len, n) || n < lo || n > hi) return false;
    field = static_cast<uint8_t>(n);
    return true;
}

bool keyIs(const char* k, size_t len, const char* name) {
    return strlen(name) == len && memcmp(k, name, len) == 0;
}

} // namespace

size_t Settings::toText(char* buf, size_t cap) const {
    const int n = snprintf(buf, cap,
                           "version=%u\n"
                           "transport=%u\n"
                           "input_mode=%u\n"
                           "sens_level=%u\n"
                           "clutch_mode=%u\n"
                           "cursor_source=%u\n"
                           "ble_slot=%u\n"
                           "brightness=%u\n"
                           "ble_name=%s\n",
                           version, transport, inputMode, sensLevel,
                           clutchMode, cursorSource, bleSlot, brightness, bleName);
    if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
    return static_cast<size_t>(n);
}

bool Settings::fromText(const char* buf) {
    *this = Settings{};  // bad/missing values fall back to defaults
    if (buf == nullptr) return false;

    bool any = false;
    const char* p = buf;
    while (*p != '\0') {
        const char* lineEnd = strchr(p, '\n');
        const size_t lineLen = lineEnd ? static_cast<size_t>(lineEnd - p) : strlen(p);
        size_t len = lineLen;
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ')) --len;  // trim CR/trailing spaces

        const char* eq = static_cast<const char*>(memchr(p, '=', len));
        if (eq != nullptr && eq != p) {
            const char*  k    = p;
            const size_t klen = static_cast<size_t>(eq - p);
            const char*  v    = eq + 1;
            const size_t vlen = len - klen - 1;

            if (keyIs(k, klen, "version")) {
                any |= applyRanged(version, v, vlen, 1, 255);
            } else if (keyIs(k, klen, "transport")) {
                any |= applyRanged(transport, v, vlen, 0, 1);
            } else if (keyIs(k, klen, "input_mode")) {
                any |= applyRanged(inputMode, v, vlen, 0, 2);
            } else if (keyIs(k, klen, "sens_level")) {
                any |= applyRanged(sensLevel, v, vlen, 1, 5);
            } else if (keyIs(k, klen, "clutch_mode")) {
                any |= applyRanged(clutchMode, v, vlen, 0, 3);
            } else if (keyIs(k, klen, "cursor_source")) {
                any |= applyRanged(cursorSource, v, vlen, 0, 1);
            } else if (keyIs(k, klen, "ble_slot")) {
                any |= applyRanged(bleSlot, v, vlen, 0, 2);
            } else if (keyIs(k, klen, "brightness")) {
                any |= applyRanged(brightness, v, vlen, 20, 100);
            } else if (keyIs(k, klen, "ble_name")) {
                if (vlen > 0 && vlen < sizeof(bleName)) {
                    bool printable = true;
                    for (size_t i = 0; i < vlen; ++i) {
                        if (v[i] < 0x20 || v[i] > 0x7E) { printable = false; break; }
                    }
                    if (printable) {
                        memcpy(bleName, v, vlen);
                        bleName[vlen] = '\0';
                        any = true;
                    }
                }
            }
            // Unknown keys: ignored by design.
        }

        if (lineEnd == nullptr) break;
        p = lineEnd + 1;
    }
    return any;
}

#endif
