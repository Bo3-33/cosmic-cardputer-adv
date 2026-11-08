#include "authenticator_core.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <openssl/evp.h>
#include <vector>
using namespace authcore;
static std::vector<uint8_t> field(unsigned n, const std::vector<uint8_t> &s) {
    std::vector<uint8_t> r = {uint8_t(n * 8 + 2)};
    size_t len = s.size();
    do {
        uint8_t v = len & 127;
        len >>= 7;
        r.push_back(v | (len ? 128 : 0));
    } while (len);
    r.insert(r.end(), s.begin(), s.end());
    return r;
}
static std::vector<uint8_t> bytes(const std::string &s) { return {s.begin(), s.end()}; }
static std::vector<uint8_t> read(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    assert(f);
    return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char **argv) {
    // All published RFC 6238 vectors, including 64-bit timestamps.
    const uint64_t times[] = {59, 1111111109, 1111111111, 1234567890, 2000000000, 20000000000ULL};
    const char *expected[][6] = {
        {"94287082", "07081804", "14050471", "89005924", "69279037", "65353130"},
        {"46119246", "68084774", "67062674", "91819424", "90698825", "77737706"},
        {"90693936", "25091201", "99943326", "93441116", "38618901", "47863826"}
    };
    for (unsigned alg = 0; alg < 3; ++alg) {
        Entry e;
        e.hash = Hash(alg);
        e.digits = 8;
        e.secretSize = alg == 0 ? 20 : alg == 1 ? 32 : 64;
        for (size_t i = 0; i < e.secretSize; ++i) e.secret[i] = "1234567890"[i % 10];
        for (int i = 0; i < 6; ++i) {
            std::string s;
            assert(code(e, times[i], s));
            assert(s == expected[alg][i]);
        }
    }
    Entry e;
    assert(uri(
        "otpauth://totp/Test%3Aalice?secret=JBSWY3DPEHPK3PXP&issuer=Test&algorithm=SHA256&digits=8&period=60",
        e
    ));
    assert(e.issuer == "Test" && e.name == "Test:alice" && e.period == 60 && e.hash == Hash::Sha256);
    const char *bad[] = {
        "otpauth://hotp/x?secret=MY======",
        "otpauth://totp/x?secret=0bad",
        "otpauth://totp/x?secret=MY&secret=MY",
        "otpauth://totp/x?secret=MY&algorithm=MD5",
        "otpauth://totp/x?secret=MY&period=0",
        "otpauth://totp/%00?secret=MY",
        "otpauth://totp/x?secret=MZ",
        "otpauth://totp/x?secret=MY&digits=99"
    };
    for (auto s : bad) assert(!uri(s, e));
    std::string source = "otpauth://totp/Test?secret=JBSWY3DPEHPK3PXP";
    auto proto = field(2, field(1, field(1, bytes(source))));
    assert(protobuf(proto.data(), proto.size(), e));
    for (size_t n = 0; n < proto.size(); ++n) assert(!protobuf(proto.data(), n, e));
    auto steam = field(2, field(2, field(1, bytes("JBSWY3DPEHPK3PXP"))));
    assert(protobuf(steam.data(), steam.size(), e) && e.steam);
    std::string sc;
    assert(code(e, 59, sc) && sc.size() == 5);
    // Independent PBKDF2-HMAC-SHA256 known answer (Python hashlib).
    uint8_t salt[16] = {}, key[32];
    assert(derive("test passphrase", 15, salt, key));
    const uint8_t expectedKey[32] = {0x52, 0x82, 0x62, 0x2a, 0xd0, 0x4e, 0xea, 0xee, 0x96, 0xd8, 0x7e,
                                     0x67, 0xc1, 0x45, 0xa2, 0x54, 0x05, 0xa7, 0xfd, 0xe8, 0x1c, 0xae,
                                     0x82, 0x21, 0x3c, 0x86, 0x2f, 0xf0, 0x72, 0x71, 0xf4, 0x34};
    assert(memcmp(key, expectedKey, 32) == 0);
    uint8_t nonce[12] = {}, tag[16];
    std::vector<uint8_t> cipher(proto.size()), plain(proto.size());
    assert(seal(key, nonce, "entrycontent", proto.data(), proto.size(), cipher.data(), tag));
    assert(open(key, nonce, "entrycontent", cipher.data(), cipher.size(), tag, plain.data()));
    assert(plain == proto);
    assert(!open(key, nonce, "wrong aad", cipher.data(), cipher.size(), tag, plain.data()));
    for (auto c : plain) assert(c == 0);
    tag[0] ^= 1;
    assert(!open(key, nonce, "entrycontent", cipher.data(), cipher.size(), tag, plain.data()));
    tag[0] ^= 1;
    key[0] ^= 1;
    assert(!open(key, nonce, "entrycontent", cipher.data(), cipher.size(), tag, plain.data()));
    if (argc > 1) {
        std::string dir = argv[1];
        uint8_t fixtureKey[32];
        memset(fixtureKey, 42, 32);
        for (int i = 0; i < 2; ++i) {
            auto remote = read(dir + "/cosmic-auth-entry-" + std::to_string(i) + ".bin");
            auto expected = read(dir + "/cosmic-auth-code-" + std::to_string(i) + ".txt");
            std::vector<uint8_t> decoded(remote.size() - 28);
            assert(open(
                fixtureKey,
                remote.data(),
                "entrycontent",
                remote.data() + 12,
                decoded.size(),
                remote.data() + remote.size() - 16,
                decoded.data()
            ));
            Entry entry;
            assert(protobuf(decoded.data(), decoded.size(), entry));
            std::string actual;
            assert(code(entry, 1234567890, actual));
            assert(actual == std::string(expected.begin(), expected.end()));
        }
        auto vault = read(dir + "/cosmic-auth-fixture.cav");
        assert(vault.size() > 56 && memcmp(vault.data(), vaultMagic, 8) == 0);
        assert(derive("test passphrase", 15, vault.data() + 12, key));
        std::vector<uint8_t> decoded(vault.size() - 56);
        assert(open(
            key,
            vault.data() + 28,
            vaultAad,
            vault.data() + 40,
            decoded.size(),
            vault.data() + vault.size() - 16,
            decoded.data()
        ));
        JsonDocument doc;
        assert(!deserializeJson(doc, decoded));
        assert(doc["format"] == "cosmic.authenticator" && doc["entries"].size() == 1);
        assert(derive("wrong passphrase", 16, vault.data() + 12, key));
        assert(!open(
            key,
            vault.data() + 28,
            vaultAad,
            vault.data() + 40,
            decoded.size(),
            vault.data() + vault.size() - 16,
            decoded.data()
        ));
    }
    std::cout << "Authenticator RFC6238, protobuf bounds, URI validation, Proton WASM interop and encrypted "
                 "vault passed\n";
}
