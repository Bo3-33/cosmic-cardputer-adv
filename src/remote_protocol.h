#pragma once
#include <string>
namespace remoteProtocol {
inline bool validIp(const char *text) {
    if (!text) return false;
    unsigned octets[4] = {};
    const char *p = text;
    for (int i = 0; i < 4; ++i) {
        const char *start = p;
        while (*p >= '0' && *p <= '9') {
            if (p - start >= 3) return false;
            octets[i] = octets[i] * 10 + unsigned(*p++ - '0');
        }
        if (p == start || octets[i] > 255 || (p - start > 1 && *start == '0')) return false;
        if (i < 3) {
            if (*p++ != '.') return false;
        } else if (*p) return false;
    }
    return octets[0] > 0 && octets[0] < 224 && octets[0] != 127 &&
           !(octets[0] == 255 && octets[1] == 255 && octets[2] == 255 && octets[3] == 255);
}
inline std::string encodeText(const char *text) {
    const char *hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
        unsigned char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out += char(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
} // namespace remoteProtocol
