#ifdef COSMIC_CARDPUTER
#include "cosmic_ollee.h"
#include "cosmic.h"
#include "cosmic_clock.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "ollee_protocol.h"
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sys/time.h>

namespace {
struct Settings {
    int32_t latitude = 0, longitude = 0;
    int offsetMinutes = -420, tide = 0;
    bool denver = true, latitudeSet = false, longitudeSet = false;
} watchSettings;
portMUX_TYPE replyLock = portMUX_INITIALIZER_UNLOCKED;
ollee::Receiver receiver;
uint8_t expected = 0, response[249] = {};
size_t responseSize = 0;
bool received = false;
void notify(NimBLERemoteCharacteristic *, uint8_t *p, size_t n, bool) {
    portENTER_CRITICAL(&replyLock);
    for (size_t i = 0; i < n; ++i)
        if (receiver.byte(p[i]) && receiver.buffer[7] == expected) {
            responseSize = receiver.buffer[1] - 6;
            memcpy(response, receiver.buffer + 8, responseSize);
            received = true;
        }
    portEXIT_CRITICAL(&replyLock);
}
void page(const String &text) {
    tft->fillScreen(cosmic::background);
    cosmic::header("Ollee Watch Clock");
    cosmic::label(text.substring(0, 37), 8, 42);
    cosmic::footer("ESC Cancel");
    tft->display(false);
}
void load() {
    Preferences p;
    if (!p.begin("cosmic_ollee", true)) return;
    watchSettings.latitude = p.getInt("lat", 0);
    watchSettings.longitude = p.getInt("lon", 0);
    watchSettings.tide = p.getInt("tide", 0);
    watchSettings.offsetMinutes = p.getInt("offset", -420);
    watchSettings.denver = p.getBool("denver", true);
    watchSettings.latitudeSet = p.getBool("lat_set", false);
    watchSettings.longitudeSet = p.getBool("lon_set", false);
    p.end();
}
void save() {
    Preferences p;
    if (!p.begin("cosmic_ollee", false)) return;
    p.putInt("lat", watchSettings.latitude);
    p.putInt("lon", watchSettings.longitude);
    p.putInt("tide", watchSettings.tide);
    p.putInt("offset", watchSettings.offsetMinutes);
    p.putBool("denver", watchSettings.denver);
    p.putBool("lat_set", watchSettings.latitudeSet);
    p.putBool("lon_set", watchSettings.longitudeSet);
    p.end();
}
bool number(const String &prompt, double lo, double hi, double &out) {
    String text = keyboard("", 16, prompt);
    if (text == String(char(KEY_ESCAPE))) return false;
    text.trim();
    char *end = nullptr;
    double value = strtod(text.c_str(), &end);
    if (end == text.c_str() || *end || !std::isfinite(value) || value < lo || value > hi) {
        displayError("Invalid value; unchanged", true);
        return false;
    }
    out = value;
    return true;
}
int offset(time_t now) {
    if (!watchSettings.denver) return watchSettings.offsetMinutes * 60;
    const char *tz = getenv("TZ");
    String previous = tz ? tz : "";
    bool had = tz != nullptr;
    setenv("TZ", "MST7MDT,M3.2.0,M11.1.0", 1);
    tzset();
    tm local = {};
    localtime_r(&now, &local);
    char formatted[8] = {};
    strftime(formatted, sizeof(formatted), "%z", &local);
    if (had) setenv("TZ", previous.c_str(), 1);
    else unsetenv("TZ");
    tzset();
    if (strlen(formatted) != 5) return -420 * 60;
    int seconds = ((formatted[1] - '0') * 10 + formatted[2] - '0') * 3600 +
                  ((formatted[3] - '0') * 10 + formatted[4] - '0') * 60;
    return formatted[0] == '-' ? -seconds : seconds;
}
bool request(
    NimBLEClient *client, NimBLERemoteCharacteristic *write, uint8_t command, const uint8_t *data, size_t size
) {
    uint8_t packet[32];
    size_t n = ollee::frame(command, data, size, packet, sizeof(packet));
    if (!n) return false;
    portENTER_CRITICAL(&replyLock);
    receiver.used = 0;
    received = false;
    responseSize = 0;
    expected = command + 0x20;
    portEXIT_CRITICAL(&replyLock);
    for (size_t i = 0; i < n; i += 20) {
        if (!client->isConnected() ||
            !write->writeValue(packet + i, std::min(size_t(20), n - i), write->canWrite()))
            return false;
    }
    for (unsigned long start = millis(); millis() - start < 25000;) {
        portENTER_CRITICAL(&replyLock);
        bool done = received;
        portEXIT_CRITICAL(&replyLock);
        if (done) return true;
        if (!client->isConnected() || check(EscPress)) return false;
        launcherDelayMs(10);
    }
    return false;
}
bool confirm() {
    bool yes = false;
    time_t now = time(nullptr);
    int delta = offset(now);
    time_t wall = now + delta;
    tm local = {};
    gmtime_r(&wall, &local);
    char clock[32];
    strftime(clock, sizeof(clock), "%Y-%m-%d %H:%M:%S", &local);
    String previous = cosmic::setTitle("Confirm clock + location");
    std::vector<Option> items = {
        {clock, []() {}},
        {String("UTC offset: ") + String(delta / 60) + " min", []() {}},
        {String("Lat: ") + String(watchSettings.latitude / 1000.0, 3), []() {}},
        {String("Lon: ") + String(watchSettings.longitude / 1000.0, 3), []() {}},
        {String("Tide interval: ") + watchSettings.tide + " min", []() {}},
        {"Send clock + these location fields", [&]() { yes = true; }},
        {"Cancel", []() {}}
    };
    resetGlobals();
    loopOptions(items);
    returnToMenu = false;
    cosmic::setTitle(previous);
    resetGlobals();
    return yes;
}
void syncWatchClock() {
    if (!watchSettings.latitudeSet || !watchSettings.longitudeSet) {
        displayMsg("Set home coordinates or explicitly choose No location first", true);
        return;
    }
    page("Getting current time over Wi-Fi...");
    if (!cosmicSyncClock()) {
        displayError("Could not obtain fresh network time", true);
        return;
    }
    if (ESP.getFreeHeap() < 100000) {
        displayError("Close other apps before Bluetooth", true);
        return;
    }
    if (NimBLEDevice::isInitialized()) {
        displayError("Exit Bluetooth keyboard app first", true);
        return;
    }
    page("Enable watch BT; close phone app");
    launcherDelayMs(600);
    if (!NimBLEDevice::init("COSMIC Watch Clock")) {
        displayError("Bluetooth could not start", true);
        return;
    }
    NimBLEClient *client = nullptr;
    auto cleanup = [&]() {
        portENTER_CRITICAL(&replyLock);
        expected = 0;
        receiver.used = 0;
        portEXIT_CRITICAL(&replyLock);
        if (client) {
            client->disconnect();
            NimBLEDevice::deleteClient(client);
            client = nullptr;
        }
        NimBLEDevice::deinit(true);
        resetGlobals();
    };
    page("Scanning for your watch (5 seconds)");
    auto *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setMaxResults(24);
    auto results = scan->getResults(5000);
    if (check(EscPress)) {
        cleanup();
        return;
    }
    int chosen = -1;
    std::vector<Option> watches;
    for (int i = 0; i < results.getCount(); ++i) {
        const auto *d = results.getDevice(i);
        String name = d->getName().c_str();
        String lower = name;
        lower.toLowerCase();
        if (!d->isAdvertisingService(NimBLEUUID(ollee::service)) && lower.indexOf("ollee") < 0) continue;
        String address = d->getAddress().toString().c_str();
        watches.push_back(
            {(name.isEmpty() ? String("UART watch") : name) + " " + address.substring(12),
             [&, i]() { chosen = i; }}
        );
    }
    if (watches.empty()) {
        cleanup();
        displayMsg(
            "No Ollee found. Hold ALARM on the watch clock face until LAP blinks. Close its phone app and "
            "retry.",
            true
        );
        return;
    }
    watches.push_back({"Cancel", []() {}});
    String previous = cosmic::setTitle("Choose your Ollee watch");
    resetGlobals();
    loopOptions(watches);
    returnToMenu = false;
    cosmic::setTitle(previous);
    if (chosen < 0) {
        cleanup();
        return;
    }
    page("Connecting to selected watch...");
    client = NimBLEDevice::createClient();
    if (!client) {
        cleanup();
        displayError("No memory for watch connection", true);
        return;
    }
    client->setConnectTimeout(10000);
    client->setConnectRetries(0);
    if (!client->connect(results.getDevice(chosen), true, false, false)) {
        cleanup();
        displayError("Connection failed; close phone app", true);
        return;
    }
    auto *service = client->getService(ollee::service);
    auto *write = service ? service->getCharacteristic(ollee::writeCharacteristic) : nullptr;
    auto *read = service ? service->getCharacteristic(ollee::notifyCharacteristic) : nullptr;
    if (!write || (!write->canWrite() && !write->canWriteNoResponse()) || !read || !read->canNotify() ||
        !read->subscribe(true, notify)) {
        cleanup();
        displayError("Watch UART service unavailable", true);
        return;
    }
    page("Checking watch firmware...");
    if (!request(client, write, 0x2a, nullptr, 0)) {
        cleanup();
        displayError("No valid version reply; unchanged", true);
        return;
    }
    bool supported = false;
    portENTER_CRITICAL(&replyLock);
    supported = ollee::supportedVersion(response, responseSize);
    portEXIT_CRITICAL(&replyLock);
    if (!supported) {
        cleanup();
        displayError("Requires HW 01.05.00 / FW 00.01.11; clock unchanged", true);
        return;
    }
    if (!confirm()) {
        cleanup();
        return;
    }
    timeval tv = {};
    gettimeofday(&tv, nullptr);
    uint8_t payload[20];
    if (!ollee::timePayload(
            uint32_t(tv.tv_sec),
            offset(tv.tv_sec),
            watchSettings.latitude,
            watchSettings.longitude,
            tv.tv_usec / 1000,
            watchSettings.tide,
            payload
        )) {
        cleanup();
        displayError("Invalid clock or home settings", true);
        return;
    }
    page("Sending clock + reviewed fields...");
    bool acknowledged = request(client, write, 0x23, payload, 20);
    portENTER_CRITICAL(&replyLock);
    acknowledged = acknowledged && responseSize == 0;
    portEXIT_CRITICAL(&replyLock);
    cleanup();
    if (acknowledged) displayMsg("Watch acknowledged clock update", true);
    else displayError("No acknowledgement. Check watch; it may have received the update.", true);
}
} // namespace
void cosmicOllee() {
    load();
    String previous = cosmic::setTitle("Ollee Watch Clock");
    bool done = false;
    while (!done) {
        std::vector<Option> items = {
            {"Sync watch clock (Wi-Fi + BT)", []() { syncWatchClock(); }},
            {String("Time zone: ") + (watchSettings.denver ? "Denver (auto DST)" : "Fixed UTC offset"),
             []() {
                 watchSettings.denver = !watchSettings.denver;
                 save();
             }},
            {String("Fixed UTC offset: ") + watchSettings.offsetMinutes + " min",
             []() {
                 double n;
                 if (number("UTC offset minutes (-720..840)", -720, 840, n) && std::floor(n) == n) {
                     watchSettings.offsetMinutes = int(n);
                     watchSettings.denver = false;
                     save();
                 }
             }},
            {String("Home latitude: ") + String(watchSettings.latitude / 1000.0, 3),
             []() {
                 double n;
                 if (number("Latitude (-90..90)", -90, 90, n)) {
                     watchSettings.latitude = int32_t(n * 1000);
                     watchSettings.latitudeSet = true;
                     save();
                 }
             }},
            {String("Home longitude: ") + String(watchSettings.longitude / 1000.0, 3),
             []() {
                 double n;
                 if (number("Longitude (-180..180)", -180, 180, n)) {
                     watchSettings.longitude = int32_t(n * 1000);
                     watchSettings.longitudeSet = true;
                     save();
                 }
             }},
            {String("Tide interval: ") + watchSettings.tide + " min",
             []() {
                 double n;
                 if (number("Tide interval minutes (0 = off)", 0, 1440, n) && std::floor(n) == n) {
                     watchSettings.tide = int(n);
                     save();
                 }
             }},
            {"No location: explicitly use 0,0",
             []() {
                 bool accept = false;
                 std::vector<Option> confirmation = {
                     {"Replace watch home with 0,0?", []() {}},
                     {"Yes, use 0,0 and tide off", [&]() { accept = true; }},
                     {"Cancel", []() {}}
                 };
                 resetGlobals();
                 loopOptions(confirmation);
                 returnToMenu = false;
                 if (accept) {
                     watchSettings.latitude = watchSettings.longitude = watchSettings.tide = 0;
                     watchSettings.latitudeSet = watchSettings.longitudeSet = true;
                     save();
                 }
             }},
            {"Back", [&]() { done = true; }}
        };
        resetGlobals();
        int choice = loopOptions(items);
        if (choice < 0 || returnToMenu) done = true;
        returnToMenu = false;
    }
    cosmic::setTitle(previous);
    resetGlobals();
}
#endif
