// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef COSMIC_CARDPUTER
#include "cosmic_authenticator.h"
#include "authenticator_ca.h"
#include "authenticator_core.h"
#include "cosmic.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "sd_functions.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <esp_random.h>
#include <esp_sntp.h>
#include <mbedtls/base64.h>
#include <sys/time.h>

namespace {
// Wipe document arenas and temporary ciphertext/plaintext buffers on release.
class SecretAllocator : public ArduinoJson::Allocator {
public:
    void *allocate(size_t n) override {
        auto *p = static_cast<size_t *>(malloc(n + sizeof(size_t)));
        if (!p) return nullptr;
        *p = n;
        return p + 1;
    }
    void deallocate(void *p) override {
        if (!p) return;
        auto *b = static_cast<size_t *>(p) - 1;
        authcore::wipe(p, *b);
        free(b);
    }
    void *reallocate(void *p, size_t n) override {
        if (!p) return allocate(n);
        void *q = allocate(n);
        if (!q) return nullptr;
        memcpy(q, p, std::min(n, *(static_cast<size_t *>(p) - 1)));
        deallocate(p);
        return q;
    }
} secrets;
class Bytes {
    uint8_t *p = nullptr;
    size_t length = 0, capacity = 0;

public:
    explicit Bytes(size_t n = 0) { resize(n); }
    Bytes(const Bytes &) = delete;
    Bytes &operator=(const Bytes &) = delete;
    ~Bytes() {
        if (p) {
            authcore::wipe(p, capacity);
            free(p);
        }
    }
    uint8_t *data() { return p; }
    size_t size() const { return length; }
    bool reserve(size_t n) {
        if (n <= capacity) return true;
        if (n > authcore::maxVault + 1) return false;
        auto *q = static_cast<uint8_t *>(malloc(n));
        if (!q) return false;
        if (p) {
            memcpy(q, p, length);
            authcore::wipe(p, capacity);
            free(p);
        }
        p = q;
        capacity = n;
        return true;
    }
    bool resize(size_t n) {
        if (!reserve(n)) return false;
        length = n;
        return true;
    }
};
struct Vault {
    JsonDocument data{&secrets};
    uint8_t key[32] = {}, salt[16] = {};
    ~Vault() { authcore::wipe(key, sizeof(key)); }
};
constexpr const char *path = "/authenticator/vault.cav";
constexpr const char *backup = "/authenticator/vault.bak";
constexpr const char *temporary = "/authenticator/vault.tmp";
void eraseString(String &s) {
    if (s.length()) authcore::wipe(const_cast<char *>(s.c_str()), s.length());
    s = "";
}
void busy(const String &message) {
    tft->fillScreen(cosmic::background);
    cosmic::header("Authenticator");
    cosmic::label(message.substring(0, 37), 8, 49);
    cosmic::footer("Your vault stays on this device");
    tft->display(false);
}
String passwordInput() {
    String s;
    s.reserve(65);
    resetGlobals();
    bool dirty = true;
    while (true) {
        if (dirty) {
            tft->fillScreen(cosmic::background);
            cosmic::header("Unlock vault");
            cosmic::label("Vault passphrase", 9, 30);
            tft->fillRoundRect(7, 48, 226, 42, 7, cosmic::surface);
            String stars;
            for (unsigned i = 0; i < s.length(); ++i) stars += '*';
            cosmic::label(
                stars.substring(stars.length() > 32 ? stars.length() - 32 : 0), 13, 65, cosmic::surface
            );
            cosmic::footer("ENTER Unlock   ESC Cancel");
            tft->display(false);
            dirty = false;
        }
        auto k = _getKeyPress();
        if ((check(EscPress) || k.exit_key) && !k.del) {
            eraseString(s);
            return "";
        }
        if (k.enter || check(SelPress)) {
            resetGlobals();
            return s;
        }
        if (k.pressed) {
            if (k.del) {
                if (s.length()) {
                    const_cast<char *>(s.c_str())[s.length() - 1] = 0;
                    s.remove(s.length() - 1);
                }
            } else
                for (char c : k.word)
                    if (c >= 32 && c <= 126 && s.length() < 64) s += c;
            dirty = true;
        }
        launcherDelayMs(10);
    }
}
bool decryptVault(Vault &v, const String &password) {
    File f = SDM.open(path, FILE_READ);
    if (!f) return false;
    size_t total = f.size();
    if (total < 56 || total > authcore::maxVault + 56) {
        f.close();
        return false;
    }
    uint8_t header[40];
    if (f.read(header, 40) != 40 || memcmp(header, authcore::vaultMagic, 8)) {
        f.close();
        return false;
    }
    uint32_t iterations = uint32_t(header[8]) | (uint32_t(header[9]) << 8) | (uint32_t(header[10]) << 16) |
                          (uint32_t(header[11]) << 24);
    if (iterations != authcore::iterations) {
        f.close();
        return false;
    }
    memcpy(v.salt, header + 12, 16);
    size_t n = total - 56;
    Bytes cipher(n), plain(n);
    if (!cipher.data() || !plain.data()) {
        f.close();
        return false;
    }
    uint8_t tag[16];
    bool ok = f.read(cipher.data(), n) == n && f.read(tag, 16) == 16;
    f.close();
    if (!ok || !authcore::derive(password.c_str(), password.length(), v.salt, v.key) ||
        !authcore::open(v.key, header + 28, authcore::vaultAad, cipher.data(), n, tag, plain.data()))
        return false;
    auto err = deserializeJson(v.data, static_cast<const uint8_t *>(plain.data()), n);
    return !err && !v.data.overflowed() && v.data["format"] == "cosmic.authenticator" &&
           v.data["version"] == 1 && v.data["entries"].is<JsonArray>() && v.data["entries"].size() <= 64 &&
           v.data["keys"].is<JsonArray>() && v.data["keys"].size() <= 8;
}
bool saveVault(Vault &v) {
    size_t n = measureJson(v.data);
    if (n > authcore::maxVault || v.data.overflowed()) return false;
    Bytes plain(n + 1), cipher(n);
    if (!plain.data() || !cipher.data()) return false;
    serializeJson(v.data, reinterpret_cast<char *>(plain.data()), plain.size());
    uint8_t header[40] = {}, tag[16];
    memcpy(header, authcore::vaultMagic, 8);
    uint32_t iter = authcore::iterations;
    for (int i = 0; i < 4; ++i) header[8 + i] = iter >> (8 * i);
    memcpy(header + 12, v.salt, 16);
    esp_fill_random(header + 28, 12);
    if (!authcore::seal(v.key, header + 28, authcore::vaultAad, plain.data(), n, cipher.data(), tag))
        return false;
    SDM.remove(temporary);
    File f = SDM.open(temporary, FILE_WRITE);
    if (!f) return false;
    bool ok = f.write(header, 40) == 40 && f.write(cipher.data(), n) == n && f.write(tag, 16) == 16;
    f.flush();
    f.close();
    if (!ok) {
        SDM.remove(temporary);
        return false;
    }
    // A failed replacement leaves the previous encrypted file available for recovery.
    SDM.remove(backup);
    if (!SDM.rename(path, backup)) {
        SDM.remove(temporary);
        return false;
    }
    if (!SDM.rename(temporary, path)) {
        SDM.rename(backup, path);
        return false;
    }
    return true;
}
bool b64(const char *s, Bytes &out, size_t limit) {
    if (!s || strlen(s) > limit * 2) return false;
    size_t n = 0;
    int r = mbedtls_base64_decode(nullptr, 0, &n, reinterpret_cast<const uint8_t *>(s), strlen(s));
    if ((r != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && r != 0) || n > limit) return false;
    if (!out.resize(n)) return false;
    if (mbedtls_base64_decode(out.data(), n, &n, reinterpret_cast<const uint8_t *>(s), strlen(s)))
        return false;
    return out.resize(n);
}
bool parseEntry(Vault &v, JsonVariantConst remote, authcore::Entry &out) {
    if ((remote["ContentFormatVersion"] | 0) != 1) return false;
    const char *id = remote["AuthenticatorKeyID"] | "";
    Bytes key, cipher;
    for (JsonVariantConst k : v.data["keys"].as<JsonArrayConst>())
        if (k["id"] == id) {
            if (!b64(k["key"] | "", key, 32) || key.size() != 32) return false;
            break;
        }
    if (key.size() != 32 || !b64(remote["Content"] | "", cipher, 4124) || cipher.size() < 28) return false;
    size_t n = cipher.size() - 28;
    Bytes plain(n);
    if (!plain.data()) return false;
    return authcore::open(
               key.data(),
               cipher.data(),
               "entrycontent",
               cipher.data() + 12,
               n,
               cipher.data() + 12 + n,
               plain.data()
           ) &&
           authcore::protobuf(plain.data(), n, out);
}
String encoded(const String &s) {
    String out;
    const char *hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
int request(Vault &v, const String &endpoint, JsonDocument &reply, const String &body = "") {
    WiFiClientSecure tls;
    tls.setCACert(authenticatorCA);
    tls.setTimeout(12000);
    HTTPClient http;
    http.useHTTP10(true);
    http.setConnectTimeout(10000);
    http.setTimeout(12000);
    if (!http.begin(tls, String("https://authenticator-api.proton.me/api") + endpoint)) return -1;
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");
    http.addHeader("Content-Type", "application/json");
    // Retain the paired client's compatibility header; identify the device honestly in User-Agent.
    http.addHeader("x-pm-appversion", v.data["appVersion"] | "linux-authenticator@0.1.0");
    http.addHeader("User-Agent", "cosmic-cardputer-authenticator/0.6.0 (paired unofficial client)");
    http.addHeader("x-pm-uid", v.data["session"]["UID"] | "");
    String bearer = String("Bearer ") + v.data["session"]["AccessToken"].as<String>();
    http.addHeader("Authorization", bearer);
    eraseString(bearer);
    int status = body.length() ? http.POST(body) : http.GET();
    if (status != 200) {
        http.end();
        return status;
    }
    int expected = http.getSize();
    if (expected > int(authcore::maxVault)) {
        http.end();
        return -2;
    }
    Bytes raw;
    if (!raw.reserve(expected > 0 ? expected : 4096)) {
        http.end();
        return -2;
    }
    auto *stream = http.getStreamPtr();
    unsigned long last = millis();
    while ((http.connected() || stream->available()) && (expected < 0 || raw.size() < size_t(expected))) {
        int available = stream->available();
        if (available) {
            size_t old = raw.size(), take = std::min(size_t(available), size_t(1024));
            if (old + take > authcore::maxVault) {
                http.end();
                return -2;
            }
            if (!raw.resize(old + take)) {
                http.end();
                return -2;
            }
            size_t got = stream->readBytes(raw.data() + old, take);
            raw.resize(old + got);
            last = millis();
        } else {
            if (millis() - last > 12000) {
                http.end();
                return -3;
            }
            launcherDelayMs(5);
        }
    }
    http.end();
    if (expected >= 0 && raw.size() != size_t(expected)) return -3;
    auto err = deserializeJson(reply, static_cast<const uint8_t *>(raw.data()), raw.size());
    if (err || reply.overflowed()) return -2;
    int code = reply["Code"] | 0;
    return code == 1000 || code == 1001 ? 200 : -4;
}
bool clockSync() {
    if (!launcherWifiIsConnected() && !connectWifi()) return false;
    busy("Setting clock over Wi-Fi...");
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
    for (int i = 0; i < 100; ++i) {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && time(nullptr) > 1704067200) return true;
        launcherDelayMs(100);
    }
    return false;
}
bool sync(Vault &v, String &status) {
    if (!v.data["session"]["RefreshToken"].is<const char *>()) {
        status = "No Proton pairing in this vault";
        return false;
    }
    if (!launcherWifiIsConnected() && !connectWifi()) {
        status = "Wi-Fi is not connected";
        return false;
    }
    if (time(nullptr) < 1704067200 && !clockSync()) {
        status = "Set the clock before syncing";
        return false;
    }
    if (ESP.getFreeHeap() < 130000) {
        status = "Close other apps, then retry sync";
        return false;
    }
    busy("Connecting directly to Proton...");
    JsonDocument reply(&secrets);
    int r = request(v, "/core/v4/users", reply);
    if (r == 401 || r == 403) {
        reply.clear();
        JsonDocument payload(&secrets);
        payload["ResponseType"] = "token";
        payload["GrantType"] = "refresh_token";
        payload["RefreshToken"] = v.data["session"]["RefreshToken"];
        payload["RedirectURI"] = "https://proton.me";
        String body;
        serializeJson(payload, body);
        r = request(v, "/auth/v4/refresh", reply, body);
        eraseString(body);
        if (r != 200 || !reply["AccessToken"].is<const char *>() ||
            !reply["RefreshToken"].is<const char *>()) {
            status = "Session expired: pair again on PC";
            return false;
        }
        v.data["session"]["AccessToken"] = reply["AccessToken"];
        v.data["session"]["RefreshToken"] = reply["RefreshToken"];
        if (reply["UID"].is<const char *>()) v.data["session"]["UID"] = reply["UID"];
        if (!saveVault(v)) {
            status = "Token refreshed; SD save failed";
            return false;
        }
    } else if (r != 200) {
        status = String("Proton connection failed (") + r + ")";
        return false;
    }
    reply.clear();
    JsonDocument incoming(&secrets);
    JsonArray entries = incoming.to<JsonArray>();
    String since;
    for (unsigned page = 0; page < 65; ++page) {
        reply.clear();
        r = request(
            v,
            String("/authenticator/v1/entry") + (since.length() ? String("?Since=") + encoded(since) : ""),
            reply
        );
        if (r != 200) {
            status = String("Sync failed (") + r + "); cache kept";
            return false;
        }
        JsonArrayConst batch = reply["Entries"].is<JsonArray>()
                                   ? reply["Entries"].as<JsonArrayConst>()
                                   : reply["Entries"]["Entries"].as<JsonArrayConst>();
        if (batch.isNull()) {
            status = "Unknown Proton response; cache kept";
            return false;
        }
        for (JsonVariantConst item : batch) {
            if (entries.size() >= 64) {
                status = "Vault exceeds 64 accounts; cache kept";
                return false;
            }
            authcore::Entry e;
            if (!parseEntry(v, item, e)) {
                status = "New key or entry format: pair again";
                return false;
            }
            JsonObject o = entries.add<JsonObject>();
            o["Content"] = item["Content"];
            o["ContentFormatVersion"] = item["ContentFormatVersion"];
            o["AuthenticatorKeyID"] = item["AuthenticatorKeyID"];
        }
        if (incoming.overflowed() || measureJson(incoming) > 36000) {
            status = "Vault too large; cache kept";
            return false;
        }
        String next = reply["Entries"].is<JsonObject>() ? reply["Entries"]["LastID"].as<String>() : String();
        if (next.isEmpty() || next == "null") {
            JsonDocument previous(&secrets);
            previous.set(v.data["entries"]);
            if (previous.overflowed()) {
                status = "Not enough memory; cache kept";
                return false;
            }
            uint64_t lastSync = v.data["lastSync"] | uint64_t(0);
            v.data["entries"] = entries;
            v.data["lastSync"] = uint64_t(time(nullptr));
            if (!saveVault(v)) {
                v.data["entries"] = previous.as<JsonArray>();
                v.data["lastSync"] = lastSync;
                status = "SD save failed; previous cache kept";
                return false;
            }
            status = String("Synced ") + entries.size() + " accounts";
            return true;
        }
        if (batch.size() == 0 || next == since) {
            status = "Invalid pagination; cache kept";
            return false;
        }
        since = next;
    }
    status = "Too many pages; cache kept";
    return false;
}
} // namespace
void cosmicAuthenticator() {
    if (!sdcardMounted && !setupSdCard()) {
        displayError("Authenticator needs the SD card", true);
        return;
    }
    if (!SDM.exists(path)) {
        if (SDM.exists(backup)) SDM.rename(backup, path);
        if (!SDM.exists(path)) {
            displayMsg("Pair on PC, copy vault.cav into /authenticator. See AUTHENTICATOR.md", true);
            return;
        }
    }
    String password = passwordInput();
    if (password.isEmpty()) return;
    busy("Unlocking encrypted vault...");
    Vault vault;
    bool ok = decryptVault(vault, password);
    eraseString(password);
    if (!ok) {
        displayError("Wrong passphrase or damaged vault", true);
        resetGlobals();
        return;
    }
    int selected = 0;
    bool actions = false, dirty = true;
    unsigned action = 0;
    unsigned long lastInput = millis();
    time_t shown = -1;
    String status = "Offline codes / direct Wi-Fi sync";
    resetGlobals();
    while (millis() - lastInput < 60000) {
        if (check(EscPress)) break;
        bool prev = check(PrevPress) || check(UpPress), next = check(NextPress) || check(DownPress),
             enter = check(SelPress);
        auto key = _getKeyPress();
        bool s = false;
        for (char c : key.word)
            if (c == 's' || c == 'S') s = true;
        if (prev || next || enter || key.pressed) {
            lastInput = millis();
            dirty = true;
        }
        int count = vault.data["entries"].size();
        if (actions) {
            if (prev) action = (action + 3) % 4;
            if (next) action = (action + 1) % 4;
            if (enter) {
                if (action == 0) {
                    sync(vault, status);
                    selected = 0;
                }
                if (action == 1) status = clockSync() ? "Clock set over Wi-Fi" : "Clock sync failed";
                if (action == 2) {
                    String value = keyboard("", 10, "Unix time in UTC (10 digits)");
                    bool valid = value.length() == 10;
                    uint64_t epoch = 0;
                    for (char c : value) {
                        if (c < '0' || c > '9') valid = false;
                        epoch = epoch * 10 + c - '0';
                    }
                    if (valid && epoch >= 1704067200 && epoch <= 4102444800ULL) {
                        timeval tv = {time_t(epoch), 0};
                        settimeofday(&tv, nullptr);
                        status = "Clock set manually";
                    } else status = "Clock unchanged";
                }
                if (action == 3) break;
                actions = false;
                lastInput = millis();
                resetGlobals();
                dirty = true;
            }
        } else {
            if (count) {
                if (prev) selected = (selected + count - 1) % count;
                if (next) selected = (selected + 1) % count;
            }
            if (enter) {
                actions = true;
                action = 0;
            }
            if (s) {
                sync(vault, status);
                selected = 0;
                lastInput = millis();
                resetGlobals();
                dirty = true;
            }
        }
        time_t now = time(nullptr);
        if (now != shown) {
            shown = now;
            dirty = true;
        }
        if (dirty) {
            tft->fillScreen(cosmic::background);
            cosmic::header("Authenticator");
            if (actions) {
                const char *labels[] = {
                    "Sync from Proton", "Set clock over Wi-Fi", "Set time manually", "Lock vault"
                };
                for (int i = 0; i < 4; ++i) {
                    uint16_t bg = action == unsigned(i) ? cosmic::panel : cosmic::surface;
                    tft->fillRoundRect(7, 22 + i * 22, 226, 20, 5, bg);
                    cosmic::label(labels[i], 15, 28 + i * 22, bg);
                }
                cosmic::footer("ENTER Choose   ESC Lock");
            } else {
                authcore::Entry entry;
                count = vault.data["entries"].size();
                bool valid = count && parseEntry(vault, vault.data["entries"][selected], entry);
                std::string code;
                if (valid) {
                    cosmic::label(String(entry.issuer.c_str()).substring(0, 33), 9, 27);
                    cosmic::label(String(entry.name.c_str()).substring(0, 37), 9, 41);
                    if (now >= 1704067200 && authcore::code(entry, now, code)) {
                        tft->fillRoundRect(7, 56, 226, 40, 8, cosmic::surface);
                        cosmic::label(code.c_str(), 22, 66, cosmic::surface, 3);
                        authcore::wipe(&code[0], code.size());
                        unsigned remaining = entry.period - uint64_t(now) % entry.period;
                        tft->fillRoundRect(9, 99, 222, 3, 1, cosmic::surface);
                        tft->fillRect(9, 99, 222 * remaining / entry.period, 3, cosmic::accent);
                    } else cosmic::label("Set clock: ENTER > Set clock", 9, 70);
                } else
                    cosmic::label(
                        count ? "Entry unavailable: pair again" : "No accounts. Press S to sync", 9, 56
                    );
                cosmic::label(status.substring(0, 37), 7, 108);
                cosmic::footer("Arrows Account   ENTER Menu   ESC Lock");
            }
            tft->display(false);
            dirty = false;
        }
        launcherDelayMs(15);
    }
    // Closing or switching away always destroys the unlocked document and keys.
    tft->fillScreen(cosmic::background);
    tft->display(false);
    resetGlobals();
}
#endif
