#ifdef COSMIC_CARDPUTER
#include "cosmic_apps.h"
#include "controller_bridge.h"
#include "cosmic.h"
#include "cosmic_lua.h"
#include "cosmic_music.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "powerSave.h"
#include "sd_functions.h"
#include <SD.h>
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include <new>

namespace {
constexpr int maxApps = 3;
struct Package {
    String id, name, directory;
};
struct Instance {
    Package package;
    CosmicLua lua;
    bool dirty = true;
    unsigned long savedAt = 0;
    explicit Instance(const Package &p) : package(p) {}
};
Instance *running[maxApps] = {};
int active = -1;
bool inAppLoop = false;

bool validId(const String &s) {
    if (s.isEmpty() || s.length() > 32) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}
Instance *instance(lua_State *L) { return static_cast<Instance *>(CosmicLua::from(L)->host); }
int bounded(lua_State *L, int arg, int min, int max) {
    lua_Integer n = luaL_checkinteger(L, arg);
    return n < min ? min : (n > max ? max : int(n));
}
uint16_t color(lua_State *L, int arg, uint16_t fallback = cosmic::text) {
    return lua_isnoneornil(L, arg) ? fallback : uint16_t(luaL_checkinteger(L, arg));
}
int apiText(lua_State *L) {
    int x = bounded(L, 1, 0, 239), y = bounded(L, 2, 0, 99);
    size_t len;
    const char *s = luaL_checklstring(L, 3, &len);
    int scale = lua_isnoneornil(L, 4) ? 1 : bounded(L, 4, 1, 3);
    uint16_t fg = color(L, 5), bg = color(L, 6, cosmic::background);
    if (y + 8 * scale > 100) return 0;
    int limit = (240 - x) / (6 * scale);
    tft->setTextSize(scale);
    tft->setTextColor(fg, bg);
    tft->setCursor(x, y + 18);
    for (size_t i = 0; i < len && i < size_t(limit); ++i) {
        unsigned char c = s[i];
        if (c < 32 || c > 126) c = ' ';
        tft->write(c);
    }
    return 0;
}
int apiRect(lua_State *L) {
    int x = bounded(L, 1, 0, 239), y = bounded(L, 2, 0, 99);
    int w = bounded(L, 3, 0, 240 - x), h = bounded(L, 4, 0, 100 - y);
    uint16_t c = color(L, 5);
    bool fill = lua_isnoneornil(L, 6) || lua_toboolean(L, 6);
    if (w && h) {
        if (fill) tft->fillRect(x, y + 18, w, h, c);
        else tft->drawRect(x, y + 18, w, h, c);
    }
    return 0;
}
int apiLine(lua_State *L) {
    int x1 = bounded(L, 1, 0, 239), y1 = bounded(L, 2, 0, 99);
    int x2 = bounded(L, 3, 0, 239), y2 = bounded(L, 4, 0, 99);
    tft->drawLine(x1, y1 + 18, x2, y2 + 18, color(L, 5));
    return 0;
}
int apiMillis(lua_State *L) {
    lua_pushnumber(L, launcherMillis());
    return 1;
}
int apiRedraw(lua_State *L) {
    instance(L)->dirty = true;
    return 0;
}
int apiStorageLoad(lua_State *L) {
    char bytes[1024];
    size_t n = 0;
    bool valid = false;
    {
        String path = "/appdata/" + instance(L)->package.id + "/state.txt";
        File file = SDM.open(path, FILE_READ);
        if (file && file.size() <= 1024) {
            n = file.readBytes(bytes, sizeof(bytes));
            valid = true;
            file.close();
        }
    }
    // No live C++ resources across a Lua allocation, which may longjmp on OOM.
    if (!valid) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, bytes, n);
    return 1;
}
int apiStorageSave(lua_State *L) {
    size_t len;
    const char *bytes = luaL_checklstring(L, 1, &len);
    if (len > 1024) return luaL_error(L, "storage limit is 1024 bytes");
    Instance *app = instance(L);
    unsigned long now = launcherMillis();
    if (app->savedAt && now - app->savedAt < 1000) {
        lua_pushboolean(L, false);
        return 1;
    }
    String dir = "/appdata/" + app->package.id;
    SDM.mkdir("/appdata");
    SDM.mkdir(dir);
    File file = SDM.open(dir + "/state.txt", FILE_WRITE);
    bool ok = file && file.write(reinterpret_cast<const uint8_t *>(bytes), len) == len;
    if (file) file.close();
    if (ok) app->savedAt = now;
    lua_pushboolean(L, ok);
    return 1;
}
int registerApi(lua_State *L) {
    const luaL_Reg ui[] = {
        {"text",  apiText},
        {"rect",  apiRect},
        {"line",  apiLine},
        {nullptr, nullptr}
    };
    luaL_newlib(L, ui);
    lua_setglobal(L, "ui");
    const luaL_Reg storage[] = {
        {"load",  apiStorageLoad},
        {"save",  apiStorageSave},
        {nullptr, nullptr       }
    };
    luaL_newlib(L, storage);
    lua_setglobal(L, "storage");
    lua_newtable(L);
    lua_pushcfunction(L, apiMillis);
    lua_setfield(L, -2, "millis");
    lua_pushcfunction(L, apiRedraw);
    lua_setfield(L, -2, "redraw");
    lua_pushinteger(L, 240);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, 100);
    lua_setfield(L, -2, "height");
    lua_setglobal(L, "app");
    lua_newtable(L);
    struct Color {
        const char *key;
        uint16_t value;
    };
    const Color colors[] = {
        {"background", cosmic::background},
        {"panel",      cosmic::panel     },
        {"accent",     cosmic::accent    },
        {"text",       cosmic::text      },
        {"surface",    cosmic::surface   }
    };
    for (const auto &c : colors) {
        lua_pushinteger(L, c.value);
        lua_setfield(L, -2, c.key);
    }
    lua_setglobal(L, "theme");
    return 0;
}
void closeApp(int slot) {
    if (slot < 0 || slot >= maxApps || !running[slot]) return;
    running[slot]->lua.call("suspend");
    delete running[slot];
    running[slot] = nullptr;
    if (active == slot) active = -1;
}
void errorApp(int slot) {
    if (!running[slot]) return;
    String message = running[slot]->lua.error.c_str();
    closeApp(slot);
    tft->fillScreen(cosmic::background);
    cosmic::header("App stopped");
    cosmic::label("The launcher is still running.", 7, 30);
    for (int line = 0; line < 4; ++line)
        cosmic::label(message.substring(line * 37, (line + 1) * 37), 7, 49 + line * 14);
    cosmic::footer("ENTER / ESC Continue");
    tft->display(false);
    resetGlobals();
    while (!check(SelPress) && !check(EscPress)) launcherDelayMs(10);
}
void selectApp(int slot) {
    if (slot == active) return;
    if (active >= 0 && running[active]) {
        if (!running[active]->lua.call("suspend")) errorApp(active);
    }
    active = slot;
    if (slot >= 0 && running[slot]) {
        running[slot]->dirty = true;
        if (!running[slot]->lua.call("resume")) errorApp(slot);
    }
}
void switcher() {
    std::vector<Option> items;
    for (int i = 0; i < maxApps; ++i)
        if (running[i]) {
            items.push_back(
                {"Resume " + running[i]->package.name + " " + String(running[i]->lua.memory() / 1024) + "K",
                 [i]() { selectApp(i); }}
            );
            items.push_back({"Close " + running[i]->package.name, [i]() { closeApp(i); }});
        }
    if (cosmicMusicLoaded()) {
        items.push_back({"Resume Music", []() { cosmicMusicMenu(); }});
        items.push_back({"Close Music", []() { cosmicMusicClose(); }});
    }
    if (controllerBridgeRunning())
        items.push_back({"USB bridge (background)", []() { controllerBridgeMenu(); }});
    if (items.empty()) {
        displayMsg("No running apps", true);
        return;
    }
    items.push_back({"Desktop", []() { selectApp(-1); }});
    items.push_back({"Close all Lua apps", []() {
                         for (int i = 0; i < maxApps; ++i) closeApp(i);
                     }});
    resetGlobals();
    String previous = cosmic::setTitle("App switcher");
    loopOptions(items);
    cosmic::setTitle(previous);
    returnToMenu = false;
    resetGlobals();
}
void appLoop() {
    if (inAppLoop) return;
    inAppLoop = true;
    unsigned long previous = launcherMillis(), paintAt = 0;
    int previousSlot = -2;
    resetGlobals();
    while (active >= 0 && running[active]) {
        int slot = active;
        Instance *app = running[slot];
        unsigned long now = launcherMillis();
        keyStroke keys = _getKeyPress();
        bool escape = check(EscPress);
        if (escape && !keys.del) {
            selectApp(-1);
            break;
        }
        bool switchKey = keys.pressed && !keys.word.empty() && keys.word[0] == '\t';
        for (auto hid : keys.hid_keys)
            if (hid == 0x2b) switchKey = true;
        if (switchKey) {
            switcher();
            previous = launcherMillis();
            previousSlot = -2;
            continue;
        }
        const char *key = nullptr;
        if (check(PrevPress)) key = "left";
        else if (check(NextPress)) key = "right";
        else if (check(UpPress)) key = "up";
        else if (check(DownPress)) key = "down";
        else if (check(SelPress)) key = "enter";
        char character[2] = {};
        if (!key && keys.pressed) {
            if (keys.del) key = "backspace";
            else if (!keys.word.empty()) {
                character[0] = keys.word[0];
                key = character;
            }
        }
        if (key) {
            if (!app->lua.call("key", key)) {
                errorApp(slot);
                break;
            }
            app->dirty = true;
        }
        if (now - previous >= 33) {
            double dt = std::min(0.1, double(now - previous) / 1000.0);
            previous = now;
            if (!app->lua.call("update", nullptr, dt)) {
                errorApp(slot);
                break;
            }
        }
        if (app->dirty || previousSlot != slot) {
            paintAt = now;
            previousSlot = slot;
            app->dirty = false;
            tft->fillRect(0, 18, 240, 100, cosmic::background);
            cosmic::header(app->package.name);
            cosmic::footer("TAB Switcher    ESC Desktop");
            if (!app->lua.call("draw")) {
                errorApp(slot);
                break;
            }
            tft->display(false);
        }
        launcherDelayMs(5);
    }
    inAppLoop = false;
    resetGlobals();
}
void launch(const Package &package) {
    for (int i = 0; i < maxApps; ++i)
        if (running[i] && running[i]->package.id == package.id) {
            selectApp(i);
            appLoop();
            return;
        }
    int slot = -1;
    for (int i = 0; i < maxApps; ++i)
        if (!running[i]) {
            slot = i;
            break;
        }
    if (slot < 0) {
        displayMsg("Close an app in the switcher first", true);
        return;
    }
    if (esp_get_free_heap_size() < 104 * 1024 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 32 * 1024) {
        displayError("Close an app to free memory", true);
        return;
    }
    File file = SDM.open(package.directory + "/main.lua", FILE_READ);
    if (!file || file.size() == 0 || file.size() > 24 * 1024) {
        displayError("main.lua missing or >24KB", true);
        return;
    }
    size_t length = file.size();
    char *source = static_cast<char *>(malloc(length));
    if (!source) {
        displayError("Not enough app memory", true);
        return;
    }
    size_t count = file.readBytes(source, length);
    file.close();
    running[slot] = new (std::nothrow) Instance(package);
    if (!running[slot]) {
        free(source);
        displayError("Not enough app memory", true);
        return;
    }
    bool ok = count == length && running[slot]->lua.open(source, length, registerApi, running[slot]);
    free(source);
    if (!ok) {
        errorApp(slot);
        return;
    }
    selectApp(slot);
    appLoop();
}
std::vector<Package> scan(unsigned &invalid) {
    std::vector<Package> packages;
    invalid = 0;
    File root = SDM.open("/apps");
    if (!root || !root.isDirectory()) return packages;
    for (File dir = root.openNextFile(); dir; dir = root.openNextFile()) {
        if (!dir.isDirectory()) {
            dir.close();
            continue;
        }
        String id = dir.name();
        int slash = id.lastIndexOf('/');
        if (slash >= 0) id = id.substring(slash + 1);
        String path = "/apps/" + id;
        dir.close();
        if (!validId(id)) {
            ++invalid;
            continue;
        }
        File file = SDM.open(path + "/app.json", FILE_READ);
        if (!file || file.size() > 2048) {
            ++invalid;
            continue;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, file);
        file.close();
        String name = doc["name"] | "";
        if (err || doc["format"] != "cosmic.app" || doc["version"] != 1 || doc["api"] != 1 ||
            doc["id"].as<String>() != id || doc["entry"] != "main.lua" || name.isEmpty() ||
            name.length() > 24 || !SDM.exists(path + "/main.lua")) {
            ++invalid;
            continue;
        }
        packages.push_back({id, name, path});
        if (packages.size() >= 32) break;
    }
    root.close();
    std::sort(packages.begin(), packages.end(), [](const Package &a, const Package &b) {
        return a.name < b.name;
    });
    return packages;
}
} // namespace
void cosmicAppSwitcher() {
    switcher();
    if (active >= 0) appLoop();
}
void cosmicAppsMenu() {
    if (!sdcardMounted && !setupSdCard()) {
        // Native apps remain available without SD storage.
    }
    bool done = false;
    while (!done) {
        unsigned invalid;
        auto packages = scan(invalid);
        std::vector<Option> items = {
            {"Music",
             [&]() {
                 cosmicMusicMenu();
                 done = true;
             }                            },
            {"Controller Bridge",
             [&]() {
                 controllerBridgeMenu();
                 done = true;
             }                            },
            {"App switcher",      [&]() {
                 cosmicAppSwitcher();
                 done = true;
             }}
        };
        for (const auto &package : packages)
            items.push_back({package.name, [package, &done]() {
                                 launch(package);
                                 done = true;
                             }});
        if (packages.empty())
            items.push_back({"Copy app packages into /apps", []() {
                                 displayMsg("See the included app format guide", true);
                             }});
        if (invalid)
            items.push_back({String(invalid) + " invalid package(s)", []() {
                                 displayMsg("Check app.json and main.lua", true);
                             }});
        items.push_back({"Back to desktop", [&]() { done = true; }});
        resetGlobals();
        String previous = cosmic::setTitle("Apps");
        if (loopOptions(items) < 0) done = true;
        cosmic::setTitle(previous);
        returnToMenu = false;
    }
}
#endif
