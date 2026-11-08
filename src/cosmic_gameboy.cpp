#ifdef COSMIC_CARDPUTER
#include "cosmic_gameboy.h"
#include "controller_bridge.h"
#include "cosmic.h"
#include "cosmic_music.h"
#include "gameboy_input.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "powerSave.h"
#include "sd_functions.h"
#include <AudioOutputI2S.h>
#include <SD.h>
#include <algorithm>
#include <new>
#include <setjmp.h>
extern "C" {
#include <minigb_apu.h>
}
static minigb_apu_ctx *apu = nullptr;
extern "C" uint8_t audio_read(uint16_t address) { return apu ? minigb_apu_audio_read(apu, address) : 0xff; }
extern "C" void audio_write(uint16_t address, uint8_t value) {
    if (apu) minigb_apu_audio_write(apu, address, value);
}
#define ENABLE_SOUND 1
#define ENABLE_LCD 1
#define WALNUT_GB_HIGH_LCD_ACCURACY 0
// Keep the core in flash: putting a large emulator in IRAM wastes the ADV's shared SRAM.
#define WGB_IRAM_ATTR
#include <walnut_cgb.h>

namespace {
constexpr size_t pageSize = 512, pageCount = 48;
struct Page {
    uint32_t address = UINT32_MAX;
    uint32_t used = 0;
    uint8_t bytes[pageSize];
};
gb_s *core = nullptr;
Page *pages = nullptr;
Page *lastPage = nullptr;
uint8_t *ram = nullptr;
size_t ramSize = 0, activePages = 0;
uint32_t useCounter = 0, frameNumber = 0;
File rom;
size_t romSize = 0;
bool ramDirty = false, drawFrame = true, ioError = false;
jmp_buf abortFrame;
char coreError[64] = {};
AudioOutputI2S *audio = nullptr;
uint8_t readRom(gb_s *, uint_fast32_t address) {
    if (address >= romSize) return 0xff;
    uint32_t base = address & ~(pageSize - 1);
    if (lastPage && lastPage->address == base) {
        lastPage->used = ++useCounter;
        return lastPage->bytes[address - base];
    }
    Page *chosen = nullptr;
    for (size_t i = 0; i < activePages; ++i) {
        if (pages[i].address == base) {
            pages[i].used = ++useCounter;
            lastPage = &pages[i];
            return pages[i].bytes[address - base];
        }
        if (!chosen || pages[i].used < chosen->used) chosen = &pages[i];
    }
    if (!chosen) return 0xff;
    memset(chosen->bytes, 0xff, pageSize);
    size_t needed = std::min(pageSize, romSize - size_t(base));
    if (!rom.seek(base) || rom.read(chosen->bytes, needed) != needed) {
        ioError = true;
        return 0xff;
    }
    chosen->address = base;
    chosen->used = ++useCounter;
    lastPage = chosen;
    return chosen->bytes[address - base];
}
uint16_t read16(gb_s *g, uint_fast32_t address) {
    return uint16_t(readRom(g, address)) | uint16_t(readRom(g, address + 1)) << 8;
}
uint32_t read32(gb_s *g, uint_fast32_t address) {
    return uint32_t(read16(g, address)) | uint32_t(read16(g, address + 2)) << 16;
}
uint8_t readRam(gb_s *, uint_fast32_t address) { return ramSize ? ram[address % ramSize] : 0xff; }
void writeRam(gb_s *, uint_fast32_t address, uint8_t value) {
    if (ramSize && ram[address % ramSize] != value) {
        ram[address % ramSize] = value;
        ramDirty = true;
    }
}
void error(gb_s *, gb_error_e code, uint16_t address) {
    snprintf(coreError, sizeof(coreError), "Core error %d at %04X", int(code), address);
    longjmp(abortFrame, 1);
}
void line(gb_s *g, const uint8_t *pixels, uint_fast8_t number) {
    if (!drawFrame || number >= 144) return;
    // 160x144 -> 150x135, preserving aspect ratio and filling the LCD height.
    int y = number * 135 / 144;
    if (number && (number - 1) * 135 / 144 == y) return;
    static const uint16_t dmg[] = {0xffdf, 0xad55, 0x52aa, 0x0841};
    for (int x = 0; x < 150; ++x) {
        unsigned index = pixels[x * 160 / 150];
        uint16_t c = g->cgb.cgbMode ? g->cgb.fixPalette[index & 63] : dmg[index & 3];
        tft->drawPixel(45 + x, y, c);
    }
}
bool oneFrame() {
    if (setjmp(abortFrame)) return false;
    core->gb_frame = false;
    unsigned budget = 200000;
    while (!core->gb_frame && budget--) __gb_step_cpu_x(core);
    if (!core->gb_frame) {
        snprintf(coreError, sizeof(coreError), "Emulator frame budget exceeded");
        return false;
    }
    return true;
}
void cleanup() {
    if (audio) {
        audio->stop();
        delete audio;
        audio = nullptr;
    }
    free(apu);
    apu = nullptr;
    free(ram);
    ram = nullptr;
    ramSize = 0;
    free(core);
    core = nullptr;
    lastPage = nullptr;
    delete[] pages;
    pages = nullptr;
    activePages = 0;
    if (rom) rom.close();
}
bool save(const String &path) {
    if (!ramDirty || !ramSize) return true;
    String temp = path + ".sav.tmp", destination = path + ".sav", backup = path + ".sav.bak";
    File file = SD.open(temp, FILE_WRITE);
    if (!file || file.write(ram, ramSize) != ramSize) {
        if (file) file.close();
        return false;
    }
    file.flush();
    file.close();
    SD.remove(backup);
    if (SD.exists(destination) && !SD.rename(destination, backup)) return false;
    if (!SD.rename(temp, destination)) {
        if (SD.exists(backup)) SD.rename(backup, destination);
        return false;
    }
    ramDirty = false;
    return true;
}
bool open(const String &path) {
    cleanup();
    coreError[0] = 0;
    ioError = false;
    ramDirty = false;
    useCounter = 0;
    rom = SD.open(path, FILE_READ);
    uint8_t header[0x150];
    if (!rom || rom.size() < 32768 || rom.read(header, sizeof(header)) != sizeof(header)) {
        snprintf(coreError, sizeof(coreError), "ROM missing, truncated or unreadable");
        return false;
    }
    if (header[0x148] > 8 || header[0x149] > 5 || rom.size() < (32768u << header[0x148])) {
        snprintf(coreError, sizeof(coreError), "Unsupported size or truncated ROM");
        return false;
    }
    romSize = rom.size();
    core = static_cast<gb_s *>(calloc(1, sizeof(gb_s)));
    apu = static_cast<minigb_apu_ctx *>(calloc(1, sizeof(minigb_apu_ctx)));
    activePages = pageCount;
    pages = new (std::nothrow) Page[activePages];
    if (!core || !apu || !pages) {
        snprintf(coreError, sizeof(coreError), "Close Music/Lua apps: low memory");
        return false;
    }
    minigb_apu_audio_init(apu);
    if (setjmp(abortFrame)) return false;
    auto result = gb_init(core, readRom, read16, read32, readRam, writeRam, error, nullptr);
    if (result != GB_INIT_NO_ERROR) {
        snprintf(coreError, sizeof(coreError), "Invalid/unsupported ROM (error %d)", int(result));
        return false;
    }
    if (gb_get_save_size_s(core, &ramSize) != 0) {
        snprintf(coreError, sizeof(coreError), "Unsupported cartridge RAM size");
        return false;
    }
    if (ramSize) {
        if (esp_get_free_heap_size() < ramSize + 24000) {
            snprintf(coreError, sizeof(coreError), "Cartridge RAM does not fit; close apps");
            return false;
        }
        ram = static_cast<uint8_t *>(malloc(ramSize));
        if (!ram) {
            snprintf(coreError, sizeof(coreError), "No contiguous cartridge RAM memory");
            return false;
        }
        memset(ram, 0xff, ramSize);
        String saved = path + ".sav";
        if (!SD.exists(saved) && SD.exists(path + ".sav.bak")) saved = path + ".sav.bak";
        if (SD.exists(saved)) {
            File file = SD.open(saved);
            if (!file || file.size() != ramSize || file.read(ram, ramSize) != ramSize) {
                snprintf(coreError, sizeof(coreError), "Save size/read error; original kept");
                return false;
            }
        }
    }
    gb_init_lcd(core, line);
    frameNumber = 0;
    return true;
}
bool pauseMenu(const String &path, bool connected) {
    if (audio) audio->stop();
    bool quit = false;
    std::vector<Option> items = {
        {connected ? "Resume game" : "Resume (waiting for USB DS4)",     []() {}                      },
        {"Save battery RAM",
         [&]() { displayMsg(save(path) ? "Battery RAM saved" : "Save failed - check SD card", true); }},
        {audio ? "Mute game audio" : "Enable audio (if memory permits)",
         []() {
             if (audio) {
                 delete audio;
                 audio = nullptr;
             } else if (esp_get_free_heap_size() > 32000 && cosmicAudioCodecInit()) {
                 audio = new (std::nothrow) AudioOutputI2S();
                 if (audio) {
                     audio->SetBuffers(3, 1024);
                     audio->SetPinout(41, 43, 42);
                     audio->SetRate(AUDIO_SAMPLE_RATE);
                     audio->SetChannels(2);
                     audio->SetOutputModeMono(true);
                     audio->SetGain(0.15f);
                 }
             }
         }                                                                                            },
        {"Save and return to launcher",
         [&]() {
             if (save(path)) quit = true;
             else displayError("Save failed - retry or discard", true);
         }                                                                                            },
        {"Quit without saving",                                          [&]() {
             std::vector<Option> confirm = {
                 {"Keep playing", []() {}}, {"Discard unsaved progress", [&]() { quit = true; }}
             };
             resetGlobals();
             loopOptions(confirm);
         }                                                                       },
    };
    String previous = cosmic::setTitle("Game Boy / Pause");
    resetGlobals();
    controllerUsbMenuEnable(true);
    loopOptions(items);
    controllerUsbMenuEnable(false);
    returnToMenu = false;
    cosmic::setTitle(previous);
    if (audio && !quit && connected && !audio->begin()) {
        delete audio;
        audio = nullptr;
    }
    resetGlobals();
    tft->fillScreen(cosmic::background);
    return quit;
}
void play(const String &path) {
    if (!controllerUsbStart()) {
        displayError("USB host unavailable; reboot ADV", true);
        return;
    }
    if (!open(path)) {
        String reason(coreError);
        cleanup();
        displayError(reason, true);
        return;
    }
    tft->fillScreen(cosmic::background);
    resetGlobals();
    bool lastPs = false, lastConnected = false, audioPausedForUsb = false;
    uint32_t rtcAt = launcherMillis(), saveAt = rtcAt;
    while (true) {
        uint32_t start = micros();
        ds4::State state;
        bool connected = controllerUsbRead(state);
        bool ps = connected && (state.special & 1);
        if (check(EscPress) || (ps && !lastPs)) {
            if (pauseMenu(path, connected)) break;
            audioPausedForUsb = !connected;
        }
        lastPs = ps;
        if (!connected) {
            if (!audioPausedForUsb) {
                if (audio) audio->stop();
                audioPausedForUsb = true;
            }
            if (lastConnected || frameNumber == 0) {
                tft->fillScreen(cosmic::background);
                cosmic::header("Game Boy / USB controller");
                cosmic::label("Connect a wired DualShock 4", 14, 49);
                cosmic::footer("ESC Pause / return to launcher");
                tft->display(false);
            }
            lastConnected = false;
            launcherDelayMs(10);
            wakeUpScreen();
            continue;
        }
        if (audioPausedForUsb) {
            if (audio && !audio->begin()) {
                delete audio;
                audio = nullptr;
            }
            audioPausedForUsb = false;
        }
        if (!lastConnected) {
            tft->fillScreen(cosmic::background);
            lastConnected = true;
        }
        core->direct.joypad = gameboy::pad(state);
        drawFrame = (frameNumber % 2) == 0;
        if (!oneFrame() || ioError) {
            String reason = ioError ? "SD read failed; check card" : coreError;
            displayError(reason, true);
            save(path);
            break;
        }
        if (drawFrame) tft->display(false);
        if (audio) {
            audio_sample_t samples[AUDIO_SAMPLES_TOTAL];
            minigb_apu_audio_callback(apu, samples);
            unsigned long deadline = launcherMillis() + 50;
            for (size_t i = 0; i < AUDIO_SAMPLES; ++i) {
                while (!audio->ConsumeSample(samples + i * 2)) {
                    if (launcherMillis() >= deadline) break;
                    launcherDelayMs(1);
                }
            }
        }
        ++frameNumber;
        uint32_t now = launcherMillis();
        if (now - rtcAt >= 1000) {
            gb_tick_rtc(core);
            rtcAt = now;
        }
        if (now - saveAt >= 15000) {
            save(path);
            saveAt = now;
        }
        wakeUpScreen();
        uint32_t elapsed = micros() - start;
        if (elapsed < 16743) delayMicroseconds(16743 - elapsed);
        launcherDelayMs(1);
    }
    cleanup();
}
void scan(const String &directory, const String &query, std::vector<Option> &items) {
    File root = SD.open(directory);
    if (!root || !root.isDirectory()) return;
    for (File file = root.openNextFile(); file && items.size() < 128; file = root.openNextFile()) {
        String name = file.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        String lower = name;
        lower.toLowerCase();
        if (!file.isDirectory() && (lower.endsWith(".gb") || lower.endsWith(".gbc")) &&
            (query.isEmpty() || lower.indexOf(query) >= 0)) {
            String path = directory + "/" + name;
            items.push_back({name, [path]() { play(path); }});
        }
        file.close();
    }
}
} // namespace
void cosmicGameboy() {
    if (!sdcardMounted && !setupSdCard()) {
        displayError("Insert SD card with /roms/gb", true);
        return;
    }
    if (!controllerUsbStart()) {
        displayError("USB host unavailable; reboot ADV", true);
        return;
    }
    String query;
    String previous = cosmic::setTitle("Game Boy / Game Boy Color");
    while (true) {
        String lower = query;
        lower.toLowerCase();
        std::vector<Option> games;
        // Filter while scanning, before the 128-result cap, so a search can find
        // games beyond the first unfiltered page of a large SD collection.
        scan("/roms/gb", lower, games);
        scan("/roms/gbc", lower, games);
        std::sort(games.begin(), games.end(), [](const Option &a, const Option &b) {
            String left = a.label, right = b.label;
            left.toLowerCase();
            right.toLowerCase();
            return left < right;
        });
        bool rebuild = false;
        std::vector<Option> items;
        items.push_back({query.isEmpty() ? "Search games..." : String("Search: ") + query, [&]() {
                             controllerUsbMenuEnable(false);
                             String edited = keyboard(query, 48, "Search ROM filename");
                             if (edited != String(char(KEY_ESCAPE))) {
                                 edited.trim();
                                 query = edited;
                             }
                             rebuild = true;
                         }});
        if (!query.isEmpty())
            items.push_back({"Clear search", [&]() {
                                 query = "";
                                 rebuild = true;
                             }});
        if (games.empty())
            items.push_back(
                {query.isEmpty() ? "No ROMs in /roms/gb or /roms/gbc" : "No matching games",
                 [&]() { rebuild = true; }}
            );
        for (auto &game : games) items.push_back(std::move(game));
        items.push_back({"Back", []() {}});
        cosmic::setTitle(query.isEmpty() ? "Game Boy / Game Boy Color" : String("Games: ") + query);
        resetGlobals();
        controllerUsbMenuEnable(true);
        loopOptions(items);
        controllerUsbMenuEnable(false);
        returnToMenu = false;
        if (!rebuild) break;
    }
    cosmic::setTitle(previous);
}
#endif
