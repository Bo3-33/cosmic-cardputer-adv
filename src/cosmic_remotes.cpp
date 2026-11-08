#ifdef COSMIC_CARDPUTER
#include "cosmic_remotes.h"
#include "cosmic.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "remote_protocol.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <freertos/queue.h>
#include <lwip/inet.h>
#include <new>
#include <webos/WakeOnLan.h>
#include <webos/WebOsClient.h>

namespace {
using lgremote::RemoteKey;
lgremote::WebOsClient *lg = nullptr;
String lgIp, rokuIp, lgKey, lgMac;
bool pinPairing = false;
QueueHandle_t rokuQueue = nullptr;
portMUX_TYPE messageLock = portMUX_INITIALIZER_UNLOCKED;
char rokuMessage[64] = "Ready - choose a Roku address";
struct RokuCommand {
    char ip[16];
    char path[192];
};
void rokuStatus(const String &s) {
    portENTER_CRITICAL(&messageLock);
    snprintf(rokuMessage, sizeof(rokuMessage), "%s", s.c_str());
    portEXIT_CRITICAL(&messageLock);
}
void rokuTask(void *) {
    RokuCommand c;
    while (true)
        if (xQueueReceive(rokuQueue, &c, portMAX_DELAY) == pdTRUE) {
            if (!launcherWifiIsConnected()) {
                rokuStatus("Wi-Fi disconnected");
                continue;
            }
            HTTPClient http;
            http.setConnectTimeout(1200);
            http.setTimeout(1200);
            int code = -1;
            if (http.begin(String("http://") + c.ip + ":8060" + c.path)) code = http.POST("");
            http.end();
            if (code >= 200 && code < 300) rokuStatus(String("Sent: ") + c.path + " (" + String(code) + ")");
            else if (code == 403) rokuStatus("Roku denied control: check TV settings");
            else rokuStatus("Roku request failed: " + String(code));
        }
}
bool ipv4(const String &ip) { return remoteProtocol::validIp(ip.c_str()); }
void save() {
    Preferences p;
    if (!p.begin("cosmic_remote", false)) return;
    p.putString("lg_ip", lgIp);
    p.putString("roku_ip", rokuIp);
    p.putString("lg_key", lgKey);
    p.putString("lg_mac", lgMac);
    p.putBool("pin", pinPairing);
    p.end();
}
void load() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    Preferences p;
    if (!p.begin("cosmic_remote", true)) return;
    lgIp = p.getString("lg_ip");
    rokuIp = p.getString("roku_ip");
    lgKey = p.getString("lg_key");
    lgMac = p.getString("lg_mac");
    pinPairing = p.getBool("pin", false);
    p.end();
}
void persistLg() {
    if (!lg) return;
    String value;
    if (lg->takeNewClientKey(value)) {
        lgKey = value;
        save();
    }
    if (lg->takeLearnedMac(value)) {
        lgMac = value;
        save();
    }
}
void discovery(bool isLg) {
    if (!launcherWifiIsConnected() && !connectWifi()) return;
    cosmic::header(isLg ? "Find LG TV" : "Find Roku");
    cosmic::label("Searching local network...", 9, 42);
    cosmic::footer("ESC Cancel");
    tft->display(false);
    WiFiUDP udp;
    if (!udp.begin(0)) {
        displayError("Cannot start discovery", true);
        return;
    }
    const char *st = isLg ? "urn:lge-com:service:webos-second-screen:1" : "roku:ecp";
    String search =
        String(
            "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: "
        ) +
        st + "\r\n\r\n";
    udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
    udp.print(search);
    udp.endPacket();
    std::vector<String> addresses;
    unsigned long start = launcherMillis();
    resetGlobals();
    while (launcherMillis() - start < 3500 && !check(EscPress)) {
        if (udp.parsePacket() > 0) {
            char buffer[1025];
            int n = udp.read(buffer, 1024);
            if (n < 0) n = 0;
            buffer[n] = 0;
            String reply(buffer);
            reply.toLowerCase();
            if (reply.indexOf(isLg ? "webos" : "roku:ecp") >= 0) {
                String ip = udp.remoteIP().toString();
                bool exists = false;
                for (const auto &a : addresses)
                    if (a == ip) exists = true;
                if (!exists && addresses.size() < 16) addresses.push_back(ip);
            }
        }
        launcherDelayMs(10);
    }
    udp.stop();
    if (addresses.empty()) {
        displayMsg("No devices found. Enter IP manually.", true);
        return;
    }
    std::vector<Option> choices;
    for (const auto &ip : addresses)
        choices.push_back({ip, [ip, isLg]() {
                               if (isLg) {
                                   if (lgIp != ip) {
                                       lgKey = "";
                                       lgMac = "";
                                   }
                                   lgIp = ip;
                               } else rokuIp = ip;
                               save();
                           }});
    resetGlobals();
    loopOptions(choices);
    returnToMenu = false;
}
void configure(bool isLg) {
    bool done = false;
    while (!done) {
        persistLg();
        std::vector<Option> options = {
            {"IP: " + (isLg ? lgIp : rokuIp),
             [isLg]() {
                 String value = keyboard(isLg ? lgIp : rokuIp, 15, "TV local IPv4 address");
                 if (value == String(char(KEY_ESCAPE))) return;
                 if (!ipv4(value)) {
                     displayError("Enter a valid IPv4 address", true);
                     return;
                 }
                 if (isLg) {
                     if (lgIp != value) {
                         lgKey = "";
                         lgMac = "";
                     }
                     lgIp = value;
                 } else rokuIp = value;
                 save();
             }                                                             },
            {"Discover devices",              [isLg]() { discovery(isLg); }},
            {"Wi-Fi network",                 []() { connectWifi(); }      },
        };
        if (isLg) {
            options.push_back({pinPairing ? "Pairing: PIN" : "Pairing: TV Allow prompt", []() {
                                   pinPairing = !pinPairing;
                                   save();
                               }});
            options.push_back({"Forget pairing", []() {
                                   lgKey = "";
                                   save();
                               }});
            options.push_back({"Wake MAC: " + lgMac, []() {
                                   String v = keyboard(lgMac, 17, "TV MAC for Wake-on-LAN");
                                   uint8_t mac[6];
                                   if (v != String(char(KEY_ESCAPE)) && lgremote::parseMacAddress(v, mac)) {
                                       lgMac = v;
                                       save();
                                   }
                               }});
        }
        options.push_back({"Open remote", [&]() { done = true; }});
        resetGlobals();
        if (loopOptions(options) < 0) done = true;
        returnToMenu = false;
    }
}
void sendRoku(const String &path) {
    if (!ipv4(rokuIp)) {
        rokuStatus("Set the Roku IP in settings");
        return;
    }
    RokuCommand c = {};
    snprintf(c.ip, sizeof(c.ip), "%s", rokuIp.c_str());
    snprintf(c.path, sizeof(c.path), "%s", path.c_str());
    if (xQueueSend(rokuQueue, &c, 0) != pdTRUE) rokuStatus("Remote busy - try again");
}
String escaped(const String &text) { return String(remoteProtocol::encodeText(text.c_str()).c_str()); }
void remote(bool isLg) {
    load();
    if (!launcherWifiIsConnected() && !connectWifi()) return;
    if ((isLg ? lgIp : rokuIp).isEmpty()) configure(isLg);
    if (!ipv4(isLg ? lgIp : rokuIp)) return;
    if (isLg) {
        if (!lg) {
            if (esp_get_free_heap_size() < 100 * 1024) {
                displayError("Close Music or Lua apps first", true);
                return;
            }
            lg = new (std::nothrow) lgremote::WebOsClient();
            if (!lg) {
                displayError("No memory for LG remote", true);
                return;
            }
            if (!lg->begin()) {
                delete lg;
                lg = nullptr;
                displayError("Cannot start LG remote", true);
                return;
            }
        }
        lg->configure(
            lgIp, lgKey, pinPairing ? lgremote::PairingMethod::Pin : lgremote::PairingMethod::Prompt
        );
        lg->setNetworkAvailable(launcherWifiIsConnected());
        lg->setSuspended(false);
    } else if (!rokuQueue) {
        rokuQueue = xQueueCreate(4, sizeof(RokuCommand));
        if (!rokuQueue) {
            displayError("No memory for Roku remote", true);
            return;
        }
        if (xTaskCreate(rokuTask, "RokuECP", 6144, nullptr, 1, nullptr) != pdPASS) {
            vQueueDelete(rokuQueue);
            rokuQueue = nullptr;
            displayError("Cannot start Roku remote", true);
            return;
        }
    }
    if (!isLg) {
        xQueueReset(rokuQueue);
        rokuStatus("Configured - send a key to test");
    }
    String shown;
    bool dirty = true;
    unsigned long statusAt = 0;
    resetGlobals();
    while (true) {
        if (isLg) {
            persistLg();
            lg->setNetworkAvailable(launcherWifiIsConnected());
        }
        if (launcherMillis() - statusAt > 250) {
            statusAt = launcherMillis();
            String state;
            if (isLg) {
                state = lgremote::webOsStatusText(lg->status());
                String reason = lg->lastError();
                if (!reason.isEmpty()) state = reason;
            } else {
                char status[64];
                portENTER_CRITICAL(&messageLock);
                memcpy(status, rokuMessage, sizeof(status));
                portEXIT_CRITICAL(&messageLock);
                state = status;
            }
            if (!launcherWifiIsConnected()) state = "Wi-Fi disconnected";
            if (state != shown) {
                shown = state;
                dirty = true;
            }
        }
        if (dirty) {
            tft->fillScreen(cosmic::background);
            cosmic::header(isLg ? "LG TV Remote" : "Roku Remote");
            cosmic::label((isLg ? lgIp : rokuIp), 8, 25);
            tft->fillRoundRect(7, 40, 226, 25, 7, cosmic::surface);
            cosmic::label(shown.substring(0, 35), 13, 49, cosmic::surface);
            cosmic::label("Arrows Navigate   ENTER OK", 9, 73);
            cosmic::label("H Home B Back +/- Volume P Play", 9, 88);
            cosmic::label("S Settings T Text  I Info M Mute", 9, 103);
            cosmic::footer(isLg ? "ESC Desktop  N PIN  O Off  W Wake" : "ESC Desktop  O Off  R Rev  F Fwd");
            tft->display(false);
            dirty = false;
        }
        keyStroke key = _getKeyPress();
        bool escape = check(EscPress);
        if (escape && !key.del) break;
        RemoteKey lgButton = RemoteKey::None;
        const char *rokuButton = nullptr;
        if (check(UpPress)) {
            lgButton = RemoteKey::Up;
            rokuButton = "Up";
        } else if (check(DownPress)) {
            lgButton = RemoteKey::Down;
            rokuButton = "Down";
        } else if (check(PrevPress)) {
            lgButton = RemoteKey::Left;
            rokuButton = "Left";
        } else if (check(NextPress)) {
            lgButton = RemoteKey::Right;
            rokuButton = "Right";
        } else if (check(SelPress) || key.enter) {
            lgButton = RemoteKey::Ok;
            rokuButton = "Select";
        } else if (key.del) {
            lgButton = RemoteKey::Back;
            rokuButton = "Back";
        } else if (key.pressed && !key.word.empty()) {
            char c = key.word[0];
            if (c >= 'A' && c <= 'Z') c += 32;
            switch (c) {
                case 'h':
                    lgButton = RemoteKey::Home;
                    rokuButton = "Home";
                    break;
                case 'b':
                    lgButton = RemoteKey::Back;
                    rokuButton = "Back";
                    break;
                case '+':
                case '=':
                    lgButton = RemoteKey::VolumeUp;
                    rokuButton = "VolumeUp";
                    break;
                case '-':
                    lgButton = RemoteKey::VolumeDown;
                    rokuButton = "VolumeDown";
                    break;
                case 'm':
                    lgButton = RemoteKey::Mute;
                    rokuButton = "VolumeMute";
                    break;
                case 'p':
                    lgButton = RemoteKey::PlayPause;
                    rokuButton = "Play";
                    break;
                case 'r':
                    lgButton = RemoteKey::Rewind;
                    rokuButton = "Rev";
                    break;
                case 'f':
                    lgButton = RemoteKey::FastForward;
                    rokuButton = "Fwd";
                    break;
                case 'i':
                    lgButton = RemoteKey::Info;
                    rokuButton = "Info";
                    break;
                case 'o':
                    lgButton = RemoteKey::PowerOff;
                    rokuButton = "PowerOff";
                    break;
                case 'w':
                    if (isLg && !lgremote::sendWakeOnLan(lgMac))
                        displayMsg("Set TV wake MAC in Settings", true);
                    break;
                case 'n':
                    if (isLg) {
                        String pin = keyboard("", 8, "PIN shown on LG TV");
                        if (pin != String(char(KEY_ESCAPE))) lg->submitPin(pin);
                    }
                    dirty = true;
                    break;
                case 's':
                    if (isLg) lg->setSuspended(true);
                    configure(isLg);
                    if (isLg) {
                        lg->configure(
                            lgIp,
                            lgKey,
                            pinPairing ? lgremote::PairingMethod::Pin : lgremote::PairingMethod::Prompt
                        );
                        lg->setSuspended(false);
                    }
                    dirty = true;
                    resetGlobals();
                    break;
                case 't': {
                    String value = keyboard("", 32, "Text for focused TV field");
                    if (value != String(char(KEY_ESCAPE))) {
                        if (isLg) lg->typeText(value.c_str());
                        else sendRoku("/keypress/Lit_" + escaped(value));
                    }
                    dirty = true;
                    resetGlobals();
                    break;
                }
                default:
                    if (c >= '0' && c <= '9') {
                        lgButton = lgremote::digitKey(c - '0');
                        static char digit[2];
                        digit[0] = c;
                        digit[1] = 0;
                        rokuButton = digit;
                    }
                    break;
            }
        }
        if (isLg && lgButton != RemoteKey::None) {
            if (!lg->sendKey(lgButton)) {
                shown = "TV not paired / command unavailable";
                dirty = true;
            }
        }
        if (!isLg && rokuButton)
            sendRoku(String("/keypress/") + (strlen(rokuButton) == 1 ? "Lit_" : "") + rokuButton);
        launcherDelayMs(10);
    }
    if (isLg) {
        persistLg();
        lg->setSuspended(true);
    } else xQueueReset(rokuQueue);
    resetGlobals();
}
} // namespace
void cosmicLgRemote() {
    String title = cosmic::setTitle("LG TV Remote");
    remote(true);
    cosmic::setTitle(title);
}
void cosmicRokuRemote() {
    String title = cosmic::setTitle("Roku Remote");
    remote(false);
    cosmic::setTitle(title);
}
#endif
