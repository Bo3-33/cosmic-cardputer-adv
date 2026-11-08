// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace authcore {
enum class Hash { Sha1, Sha256, Sha512 };
struct Entry {
    std::string name, issuer;
    uint8_t secret[128] = {};
    size_t secretSize = 0;
    unsigned period = 30, digits = 6;
    Hash hash = Hash::Sha1;
    bool steam = false;
    ~Entry();
};
void wipe(void *p, size_t n);
bool base32(const std::string &s, uint8_t *out, size_t capacity, size_t &n);
bool uri(const std::string &s, Entry &out);
bool protobuf(const uint8_t *data, size_t size, Entry &out);
bool code(const Entry &entry, uint64_t seconds, std::string &out);
bool derive(const char *password, size_t size, const uint8_t salt[16], uint8_t key[32]);
bool seal(
    const uint8_t key[32], const uint8_t nonce[12], const char *aad, const uint8_t *plain, size_t n,
    uint8_t *cipher, uint8_t tag[16]
);
bool open(
    const uint8_t key[32], const uint8_t nonce[12], const char *aad, const uint8_t *cipher, size_t n,
    const uint8_t tag[16], uint8_t *plain
);
constexpr unsigned iterations = 120000;
constexpr size_t maxVault = 49152;
constexpr char vaultMagic[] = "CSAUTH01";
constexpr char vaultAad[] = "cosmic.authenticator.v1";
} // namespace authcore
