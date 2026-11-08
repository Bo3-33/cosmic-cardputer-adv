// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(COSMIC_CARDPUTER) || defined(AUTH_HOST)
#include "authenticator_core.h"
#include <cstdio>
#include <cstring>
#ifdef AUTH_HOST
#include <openssl/evp.h>
#include <openssl/hmac.h>
#else
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#endif
namespace authcore {
void wipe(void *p, size_t n) {
    volatile uint8_t *b = static_cast<volatile uint8_t *>(p);
    while (n--) *b++ = 0;
}
Entry::~Entry() { wipe(secret, sizeof(secret)); }
bool base32(const std::string &s, uint8_t *out, size_t capacity, size_t &n) {
    n = 0;
    uint32_t bits = 0;
    unsigned count = 0;
    bool padding = false;
    for (char c : s) {
        if (c == '=') {
            padding = true;
            continue;
        }
        if (padding) return false;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        unsigned v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= '2' && c <= '7' ? c - '2' + 26 : 32;
        if (v == 32) return false;
        bits = (bits << 5) | v;
        count += 5;
        if (count >= 8) {
            count -= 8;
            if (n >= capacity) return false;
            out[n++] = uint8_t(bits >> count);
        }
    }
    return n > 0 && count < 5 && (bits & ((1u << count) - 1)) == 0;
}
static int hex(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
static bool decode(const std::string &s, std::string &out) {
    out.clear();
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '%') {
            if (i + 2 >= s.size() || hex(s[i + 1]) < 0 || hex(s[i + 2]) < 0) return false;
            c = char(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        }
        if (!c) return false;
        out += c;
    }
    return true;
}
static bool number(const std::string &s, unsigned &n, unsigned lo, unsigned hi) {
    if (s.empty() || s.size() > 5) return false;
    n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + c - '0';
    }
    return n >= lo && n <= hi;
}
bool uri(const std::string &s, Entry &out) {
    if (s.size() > 2048 || s.compare(0, 15, "otpauth://totp/") != 0) return false;
    Entry e;
    size_t q = s.find('?');
    if (q == std::string::npos) return false;
    if (!decode(s.substr(15, q - 15), e.name)) return false;
    bool secret = false, alg = false, period = false, digits = false;
    for (size_t pos = q + 1; pos < s.size();) {
        size_t end = s.find('&', pos);
        if (end == std::string::npos) end = s.size();
        size_t eq = s.find('=', pos);
        if (eq == std::string::npos || eq >= end) return false;
        std::string k, v;
        if (!decode(s.substr(pos, eq - pos), k) || !decode(s.substr(eq + 1, end - eq - 1), v)) return false;
        if (k == "secret") {
            if (secret || !base32(v, e.secret, sizeof(e.secret), e.secretSize)) return false;
            secret = true;
            wipe(&v[0], v.size());
        } else if (k == "issuer") e.issuer = v;
        else if (k == "algorithm") {
            if (alg) return false;
            alg = true;
            if (v == "SHA1") e.hash = Hash::Sha1;
            else if (v == "SHA256") e.hash = Hash::Sha256;
            else if (v == "SHA512") e.hash = Hash::Sha512;
            else return false;
        } else if (k == "digits") {
            if (digits || !number(v, e.digits, 6, 8)) return false;
            digits = true;
        } else if (k == "period") {
            if (period || !number(v, e.period, 1, 3600)) return false;
            period = true;
        }
        pos = end + 1;
    }
    if (!secret) return false;
    if (e.issuer.empty()) {
        size_t colon = e.name.find(':');
        if (colon != std::string::npos) e.issuer = e.name.substr(0, colon);
    }
    if (e.name.size() > 96) e.name.resize(96);
    if (e.issuer.size() > 64) e.issuer.resize(64);
    out = e;
    return true;
}
// Read only the small protobuf envelope used by Proton; enforce every boundary.
struct Reader {
    const uint8_t *p;
    size_t n;
    bool var(uint64_t &v) {
        v = 0;
        for (unsigned i = 0; i < 10; ++i) {
            if (!n) return false;
            uint8_t b = *p++;
            --n;
            if (i == 9 && b > 1) return false;
            v |= uint64_t(b & 127) << (i * 7);
            if (!(b & 128)) return true;
        }
        return false;
    }
    bool next(unsigned &field, unsigned &wire, Reader &value) {
        uint64_t k, len;
        if (!var(k) || !k || k > UINT32_MAX) return false;
        field = unsigned(k >> 3);
        wire = k & 7;
        if (!field) return false;
        if (wire == 2) {
            if (!var(len) || len > n) return false;
            value = {p, size_t(len)};
            p += len;
            n -= len;
            return true;
        }
        if (wire == 0) return var(len);
        size_t skip = wire == 1 ? 8 : wire == 5 ? 4 : SIZE_MAX;
        if (skip > n) return false;
        p += skip;
        n -= skip;
        return true;
    }
};
static bool stringField(Reader r, unsigned wanted, std::string &s) {
    bool found = false;
    while (r.n) {
        unsigned f, w;
        Reader v = {};
        if (!r.next(f, w, v)) return false;
        if (f == wanted) {
            if (found || w != 2 || v.n > 2048) return false;
            s.assign(reinterpret_cast<const char *>(v.p), v.n);
            found = true;
        }
    }
    return found;
}
bool protobuf(const uint8_t *data, size_t size, Entry &out) {
    if (size > 4096) return false;
    Reader root = {data, size}, content = {};
    bool found = false;
    std::string name;
    while (root.n) {
        unsigned f, w;
        Reader v = {};
        if (!root.next(f, w, v)) return false;
        if (f == 1 && w == 2) stringField(v, 1, name);
        if (f == 2) {
            if (w != 2 || found) return false;
            content = v;
            found = true;
        }
    }
    if (!found) return false;
    Entry result;
    found = false;
    while (content.n) {
        unsigned f, w;
        Reader v = {};
        if (!content.next(f, w, v)) return false;
        if (f == 1 || f == 2) {
            if (found || w != 2) return false;
            found = true;
            std::string s;
            if (!stringField(v, 1, s)) return false;
            bool ok =
                f == 1 ? uri(s, result) : base32(s, result.secret, sizeof(result.secret), result.secretSize);
            wipe(&s[0], s.size());
            if (!ok) return false;
            if (f == 2) {
                result.steam = true;
                result.name = name.substr(0, 96);
                result.issuer = "Steam";
            }
        }
    }
    if (found) out = result;
    return found;
}
bool code(const Entry &e, uint64_t seconds, std::string &out) {
    if (!e.secretSize || e.secretSize > 128 || e.period < 1 || e.period > 3600 || e.digits < 6 ||
        e.digits > 8)
        return false;
    uint64_t counter = seconds / e.period;
    uint8_t msg[8], digest[64] = {};
    for (int i = 7; i >= 0; --i) {
        msg[i] = counter & 255;
        counter >>= 8;
    }
    size_t len;
#ifdef AUTH_HOST
    const EVP_MD *md = e.hash == Hash::Sha1     ? EVP_sha1()
                       : e.hash == Hash::Sha256 ? EVP_sha256()
                                                : EVP_sha512();
    unsigned l = 0;
    if (!HMAC(md, e.secret, int(e.secretSize), msg, 8, digest, &l)) return false;
    len = l;
#else
    auto *md = mbedtls_md_info_from_type(
        e.hash == Hash::Sha1     ? MBEDTLS_MD_SHA1
        : e.hash == Hash::Sha256 ? MBEDTLS_MD_SHA256
                                 : MBEDTLS_MD_SHA512
    );
    if (!md || mbedtls_md_hmac(md, e.secret, e.secretSize, msg, 8, digest)) return false;
    len = mbedtls_md_get_size(md);
#endif
    unsigned offset = digest[len - 1] & 15;
    uint32_t binary = (uint32_t(digest[offset] & 127) << 24) | (uint32_t(digest[offset + 1]) << 16) |
                      (uint32_t(digest[offset + 2]) << 8) | digest[offset + 3];
    if (e.steam) {
        static const char chars[] = "23456789BCDFGHJKMNPQRTVWXY";
        out.clear();
        for (int i = 0; i < 5; ++i) {
            out += chars[binary % 26];
            binary /= 26;
        }
    } else {
        unsigned mod = 1;
        for (unsigned i = 0; i < e.digits; ++i) mod *= 10;
        char buf[10];
        snprintf(buf, sizeof(buf), "%0*u", int(e.digits), binary % mod);
        out = buf;
        wipe(buf, sizeof(buf));
    }
    wipe(digest, sizeof(digest));
    return true;
}
bool derive(const char *password, size_t size, const uint8_t salt[16], uint8_t key[32]) {
#ifdef AUTH_HOST
    return PKCS5_PBKDF2_HMAC(password, int(size), salt, 16, iterations, EVP_sha256(), 32, key) == 1;
#else
    return mbedtls_pkcs5_pbkdf2_hmac_ext(
               MBEDTLS_MD_SHA256,
               reinterpret_cast<const uint8_t *>(password),
               size,
               salt,
               16,
               iterations,
               32,
               key
           ) == 0;
#endif
}
static bool crypt(
    bool encrypt, const uint8_t key[32], const uint8_t nonce[12], const char *aad, const uint8_t *in,
    size_t n, uint8_t *out, uint8_t tag[16]
) {
    bool ok = false;
#ifdef AUTH_HOST
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int l = 0, total = 0;
    if (ctx) {
        ok = EVP_CipherInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, nonce, encrypt) == 1 &&
             EVP_CipherUpdate(ctx, nullptr, &l, reinterpret_cast<const uint8_t *>(aad), int(strlen(aad))) ==
                 1 &&
             EVP_CipherUpdate(ctx, out, &l, in, int(n)) == 1;
        total = l;
        if (ok && !encrypt) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag) == 1;
        if (ok) ok = EVP_CipherFinal_ex(ctx, out + total, &l) == 1;
        if (ok && encrypt) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;
        EVP_CIPHER_CTX_free(ctx);
    }
#else
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int r = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (!r)
        r = encrypt ? mbedtls_gcm_crypt_and_tag(
                          &ctx,
                          MBEDTLS_GCM_ENCRYPT,
                          n,
                          nonce,
                          12,
                          reinterpret_cast<const uint8_t *>(aad),
                          strlen(aad),
                          in,
                          out,
                          16,
                          tag
                      )
                    : mbedtls_gcm_auth_decrypt(
                          &ctx,
                          n,
                          nonce,
                          12,
                          reinterpret_cast<const uint8_t *>(aad),
                          strlen(aad),
                          tag,
                          16,
                          in,
                          out
                      );
    mbedtls_gcm_free(&ctx);
    ok = r == 0;
#endif
    if (!ok) wipe(out, n);
    return ok;
}
bool seal(
    const uint8_t key[32], const uint8_t nonce[12], const char *aad, const uint8_t *plain, size_t n,
    uint8_t *cipher, uint8_t tag[16]
) {
    return crypt(true, key, nonce, aad, plain, n, cipher, tag);
}
bool open(
    const uint8_t key[32], const uint8_t nonce[12], const char *aad, const uint8_t *cipher, size_t n,
    const uint8_t tag[16], uint8_t *plain
) {
    uint8_t t[16];
    memcpy(t, tag, 16);
    return crypt(false, key, nonce, aad, cipher, n, plain, t);
}
} // namespace authcore
#endif
