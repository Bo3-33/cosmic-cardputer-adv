#ifdef COSMIC_CARDPUTER
#include "cosmic.h"
#include "app_registry.h"
#include "controller_bridge.h"
#include "cosmic_apps.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "partitioner.h"
#include "powerSave.h"
#include "sd_functions.h"
#include "settings.h"
#include "webInterface.h"
#include <algorithm>

namespace cosmic {
static String menuTitle = "Launcher";
String setTitle(const String &title) {
    String previous = menuTitle;
    menuTitle = title;
    return previous;
}
void applyTheme() {
    rotation = 1;
    FGCOLOR = text;
    BGCOLOR = background;
    ALCOLOR = accent;
    odd_color = panel;
    even_color = surface;
    _fp = _fm = 1;
    _fg = 2;
}
void label(const String &s, int x, int y, uint16_t bg, int size) {
    tft->setTextSize(size);
    tft->setTextColor(text, bg);
    tft->setCursor(x, y);
    tft->print(s);
}
void header(const String &title) {
    tft->fillRect(0, 0, tftWidth, 18, panel);
    label(title.substring(0, 26), 7, 5, panel);
    label(launcherWifiIsConnected() ? "W" : "-", tftWidth - 65, 5, panel);
    int battery = std::max(0, std::min(100, getBattery()));
    tft->drawRoundRect(tftWidth - 47, 4, 39, 11, 2, text);
    tft->fillRect(tftWidth - 8, 7, 2, 5, text);
    tft->fillRect(tftWidth - 44, 7, battery * 33 / 100, 5, text);
}
void footer(const String &hint) {
    tft->fillRect(0, tftHeight - 16, tftWidth, 16, panel);
    label(hint.substring(0, 38), 6, tftHeight - 12, panel);
}
static void wallpaper() {
    tft->fillScreen(background);
    // Static ribbons: no framebuffer or continuous animation needed.
    for (int y = 20; y < 117; ++y) {
        int x = 210 - (y - 20) * 2;
        tft->drawLine(std::max(0, x), y, std::min(239, x + 39), y, rgb(33, 6, 6));
        if (y > 75) tft->drawLine(0, y, (y - 75) * 2, y, rgb(58, 7, 7));
    }
}
void boot(bool force) {
    static unsigned long painted = 0;
    if (!force && painted) return;
    painted = launcherMillis();
    applyTheme();
    wallpaper();
    header("COSMIC");
    label("COSMIC", 48, 42, background, 4);
    label("CARDPUTER ADV", 81, 80);
    footer("ENTER Launcher   Other key Boot app");
    tft->display(false);
}
void drawIcon(const String &name, int x, int y) {
    if (name.indexOf("Controller") >= 0 || name.indexOf("bridge") >= 0) {
        tft->drawRoundRect(x, y + 3, 18, 12, 4, text);
        tft->drawLine(x + 3, y + 9, x + 9, y + 9, text);
        tft->drawLine(x + 6, y + 6, x + 6, y + 12, text);
        tft->fillCircle(x + 14, y + 8, 1, accent);
    } else if (name.indexOf("Stopwatch") >= 0) {
        tft->drawCircle(x + 9, y + 9, 7, text);
        tft->drawLine(x + 9, y + 9, x + 9, y + 4, accent);
        tft->drawLine(x + 9, y + 9, x + 13, y + 9, accent);
    } else if (name.indexOf("Notes") >= 0) {
        tft->drawRoundRect(x + 2, y, 14, 18, 2, text);
        for (int row = 0; row < 3; ++row)
            tft->drawLine(x + 5, y + 5 + row * 4, x + 13, y + 5 + row * 4, text);
    } else if (name.indexOf("Orbit") >= 0) {
        tft->drawCircle(x + 9, y + 9, 7, text);
        tft->fillCircle(x + 9, y + 9, 2, accent);
        tft->fillCircle(x + 15, y + 5, 2, text);
    } else {
        tft->fillRoundRect(x, y, 18, 18, 4, panel);
        label(name.substring(0, 1), x + 6, y + 5, panel);
    }
}
Opt_Coord options(int index, std::vector<Option> &items, std::vector<MenuOptions> &hitboxes) {
    Opt_Coord coord;
    hitboxes.clear();
    tft->fillScreen(background);
    header(menuTitle);
    if (items.empty()) return coord;
    index = std::max(0, std::min(index, int(items.size()) - 1));
    int start = std::max(0, index - 1);
    start = std::min(start, std::max(0, int(items.size()) - 3));
    for (int row = 0; row < 3 && start + row < int(items.size()); ++row) {
        int i = start + row, y = 25 + row * 29;
        uint16_t bg = i == index ? panel : surface;
        tft->fillRoundRect(7, y, tftWidth - 14, 25, 7, bg);
        if (i == index) tft->fillRoundRect(10, y + 5, 3, 15, 1, accent);
        drawIcon(items[i].label, 20, y + 5);
        label(items[i].label.substring(0, 29), 45, y + 9, bg);
        MenuOptions hit(String(i), "", nullptr);
        hit.setCoords(7, y, tftWidth - 14, 25);
        hitboxes.push_back(hit);
        if (i == index) {
            coord.x = 45;
            coord.y = y + 9;
            coord.size = 30;
            coord.fgcolor = text;
            coord.bgcolor = bg;
        }
    }
    tft->fillRoundRect(236, 25, 3, 83, 1, surface);
    int thumb = std::max(8, 83 * std::min(3, int(items.size())) / int(items.size()));
    tft->fillRoundRect(
        236, 25 + (83 - thumb) * index / std::max(1, int(items.size()) - 1), 3, thumb, 1, accent
    );
    footer("ENTER Open  ESC Back    " + String(index + 1) + "/" + String(items.size()));
    tft->display(false);
    return coord;
}
static void menu(const String &title, std::vector<Option> items) {
    String previous = menuTitle;
    menuTitle = title;
    resetGlobals();
    loopOptions(items);
    menuTitle = previous;
    returnToMenu = false;
}
static void firmware() {
    std::vector<Option> entries = {
        {"SD card",        []() { loopSD(false); }     },
        {"Online install", []() { ota_function(); }    },
        {"Web upload",     []() { loopOptionsWebUi(); }},
    };
    for (const auto &app : launcherListInstalledApps()) {
        String id = app.label;
        entries.push_back({"Boot: " + (app.name.isEmpty() ? id : app.name), [id]() {
                               launcherShowAppActions(id.c_str());
                           }});
    }
    menu("Firmware", entries);
}
static void settings() {
    menu(
        "Settings",
        {
            {"Brightness",        []() { setBrightnessMenu(); }},
            {"Wi-Fi",             []() { connectWifi(); }      },
            {"Launcher settings",
             []() {
                 settings_menu();
                 applyTheme();
             }                                                 },
            {"Partition manager", []() { partList(); }         },
            {"Power off",         []() {
                 controllerBridgeStop();
                 powerOff();
             }                        },
    }
    );
}
static void icon(int i, int x, int y) {
    if (i == 0) {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) tft->fillRoundRect(x + c * 8, y + r * 7 + 1, 5, 5, 1, text);
        return;
    }
    --i;
    switch (i) {
        case 0: // Firmware chip
            tft->drawRoundRect(x + 4, y + 3, 15, 15, 2, text);
            for (int j = 0; j < 3; ++j) {
                tft->drawLine(x, y + 6 + j * 4, x + 4, y + 6 + j * 4, text);
                tft->drawLine(x + 19, y + 6 + j * 4, x + 23, y + 6 + j * 4, text);
            }
            break;
        case 1:
            tft->drawRoundRect(x + 1, y + 6, 22, 14, 2, text);
            tft->drawRect(x + 3, y + 2, 8, 5, text);
            break;
        case 2: // Controller
            tft->drawRoundRect(x, y + 4, 24, 14, 5, text);
            tft->drawLine(x + 4, y + 11, x + 10, y + 11, text);
            tft->drawLine(x + 7, y + 8, x + 7, y + 14, text);
            tft->fillCircle(x + 17, y + 9, 1, text);
            tft->fillCircle(x + 20, y + 13, 1, text);
            break;
        case 3:
            tft->drawLine(x + 1, y + 7, x + 12, y + 2, text);
            tft->drawLine(x + 12, y + 2, x + 23, y + 7, text);
            tft->drawLine(x + 6, y + 12, x + 12, y + 9, text);
            tft->drawLine(x + 12, y + 9, x + 18, y + 12, text);
            tft->fillCircle(x + 12, y + 17, 2, text);
            break;
        default:
            tft->drawCircle(x + 12, y + 11, 8, text);
            tft->drawCircle(x + 12, y + 11, 3, text);
            tft->drawLine(x + 12, y, x + 12, y + 3, text);
            tft->drawLine(x + 12, y + 19, x + 12, y + 22, text);
            tft->drawLine(x, y + 11, x + 4, y + 11, text);
            tft->drawLine(x + 20, y + 11, x + 24, y + 11, text);
    }
}
void desktop() {
    applyTheme();
    // This shell targets the approved landscape layout.
    tft->setRotation(1);
    tftWidth = 240;
    tftHeight = 135;
    const char *names[] = {"Apps", "Firmware", "SD files", "Wi-Fi", "Settings"};
    int selected = 0;
    bool redraw = true;
    unsigned long statusAt = 0;
    resetGlobals();
    while (true) {
        if (redraw) {
            wallpaper();
            header("Apps");
            label("COSMIC", 66, 30, background, 3);
            label(names[selected], (240 - strlen(names[selected]) * 6) / 2, 63);
            tft->fillRoundRect(15, 79, 210, 37, 8, surface);
            for (int i = 0; i < 5; ++i) {
                int x = 36 + i * 36;
                if (i == selected) tft->fillRoundRect(x - 3, 81, 30, 29, 5, panel);
                icon(i >= 3 ? i + 1 : i, x, 83);
                if (i == selected) tft->fillRoundRect(x + 4, 110, 16, 3, 1, accent);
            }
            label("TAB Switcher   ENTER Open", 45, 123);
            tft->display(false);
            redraw = false;
        }
        if (launcherMillis() - statusAt > 3000) {
            statusAt = launcherMillis();
            redraw = true;
        }
        if (check(PrevPress) || check(UpPress)) {
            selected = (selected + 4) % 5;
            redraw = true;
        }
        if (check(NextPress) || check(DownPress)) {
            selected = (selected + 1) % 5;
            redraw = true;
        }
        if (check(SelPress)) {
            switch (selected) {
                case 0: cosmicAppsMenu(); break;
                case 1: firmware(); break;
                case 2: loopSD(false); break;
                case 3: connectWifi(); break;
                case 4: settings(); break;
            }
            applyTheme();
            tft->setRotation(1);
            tftWidth = 240;
            tftHeight = 135;
            returnToMenu = false;
            resetGlobals();
            redraw = true;
        }
        keyStroke key = _getKeyPress();
        bool tab = false;
        for (auto hid : key.hid_keys)
            if (hid == 0x2b) tab = true;
        if (!key.word.empty() && key.word[0] == '\t') tab = true;
        if (tab) {
            cosmicAppSwitcher();
            redraw = true;
            resetGlobals();
        }
        check(EscPress);
        checkReboot();
        launcherDelayMs(10);
    }
}
} // namespace cosmic
#endif
