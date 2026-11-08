#ifdef COSMIC_CARDPUTER
#include "cosmic_music.h"
#include "cosmic.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "sd_functions.h"
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutputI2S.h>
#include <Wire.h>
#include <algorithm>
#include <new>

namespace {
struct Track {
    String path, title, art;
};
std::vector<Track> tracks;
AudioGenerator *decoder = nullptr;
AudioFileSourceSD *source = nullptr;
AudioOutputI2S *output = nullptr;
int selected = 0, playing = -1, volume = 30;
bool paused = true;
String message;
void release() {
    if (decoder) {
        decoder->stop();
        delete decoder;
        decoder = nullptr;
    }
    delete source;
    source = nullptr;
    delete output;
    output = nullptr;
    playing = -1;
}
void scan(const String &directory, int depth) {
    File root = SD.open(directory);
    if (!root || !root.isDirectory()) return;
    for (File f = root.openNextFile(); f && tracks.size() < 128; f = root.openNextFile()) {
        String name = f.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        String path = directory + "/" + name;
        bool folder = f.isDirectory();
        f.close();
        if (folder) {
            if (depth < 2) scan(path, depth + 1);
            continue;
        }
        String lower = name;
        lower.toLowerCase();
        if (!lower.endsWith(".mp3") && !lower.endsWith(".wav")) continue;
        String stem = path.substring(0, path.lastIndexOf('.'));
        String art = stem + ".rgb565";
        if (!SD.exists(art)) art = directory + "/cover.rgb565";
        tracks.push_back({path, name.substring(0, name.lastIndexOf('.')), art});
    }
}
bool codec() {
    // Cardputer ADV ES8311, BCLK as master-clock source; register values follow M5Unified.
    // Copyright M5Stack, MIT: https://github.com/m5stack/M5Unified (license in music guide).
    const uint8_t regs[][2] = {
        {0x00, 0x80},
        {0x01, 0xB5},
        {0x02, 0x18},
        {0x0D, 0x01},
        {0x12, 0   },
        {0x13, 0x10},
        {0x32, 0xBF},
        {0x37, 0x08}
    };
    launcherInputLock();
    bool ok = true;
    for (const auto &reg : regs) {
        Wire.beginTransmission(0x18);
        Wire.write(reg, 2);
        if (Wire.endTransmission() != 0) ok = false;
    }
    launcherInputUnlock();
    return ok;
}
bool play(int index) {
    release();
    if (esp_get_free_heap_size() < 90000) {
        message = "Close another app to play music";
        return false;
    }
    if (!codec()) {
        message = "ADV audio codec not responding";
        return false;
    }
    output = new (std::nothrow) AudioOutputI2S();
    if (!output) {
        message = "Not enough audio memory";
        return false;
    }
    output->SetPinout(41, 43, 42);
    output->SetOutputModeMono(true);
    output->SetGain(volume / 100.0f);
    source = new (std::nothrow) AudioFileSourceSD(tracks[index].path.c_str());
    String lower = tracks[index].path;
    lower.toLowerCase();
    if (lower.endsWith(".mp3")) decoder = new (std::nothrow) AudioGeneratorMP3();
    else decoder = new (std::nothrow) AudioGeneratorWAV();
    if (!source || !decoder || !source->isOpen() || !decoder->begin(source, output)) {
        release();
        message = "Cannot decode track / low memory";
        return false;
    }
    playing = index;
    paused = false;
    message = "";
    return true;
}
void cover(int index, int x, int width, int height, bool side) {
    if (index < 0 || index >= int(tracks.size())) return;
    uint16_t pixels[64 * 64];
    File art = SD.open(tracks[index].art);
    bool valid = art && art.size() == sizeof(pixels) &&
                 art.read(reinterpret_cast<uint8_t *>(pixels), sizeof(pixels)) == sizeof(pixels);
    art.close();
    for (int col = 0; col < width; ++col) {
        int inset = side ? (col * 10 / width) : 0;
        int h = height - inset;
        for (int row = 0; row < h; ++row) {
            uint16_t color = valid ? pixels[(row * 64 / h) * 64 + col * 64 / width]
                                   : (col % 9 < 3 ? cosmic::panel : cosmic::surface);
            tft->drawPixel(x + col, 29 + inset / 2 + row, color);
            if (row < 10) tft->drawPixel(x + col, 95 + row, (color >> 2) & 0x39e7);
        }
    }
    if (!valid) cosmic::label(tracks[index].title.substring(0, width / 6 - 1), x + 3, 51, cosmic::surface);
}
void draw() {
    tft->fillScreen(cosmic::background);
    cosmic::header("Music / Cover Flow");
    if (tracks.empty()) {
        cosmic::label("Add MP3 or WAV files to /music", 8, 45);
        cosmic::label("Optional: 64x64 cover.rgb565", 8, 65);
        cosmic::footer("ESC Desktop");
    } else {
        cover(selected - 1, 14, 40, 55, true);
        cover(selected + 1, 186, 40, 55, true);
        cover(selected, 88, 64, 64, false);
        cosmic::label(tracks[selected].title.substring(0, 32), 8, 107);
        cosmic::label(paused ? "||" : ">", 8, 19);
        cosmic::label("VOL " + String(volume), 188, 19);
        if (!message.isEmpty()) {
            tft->fillRect(0, 94, 240, 24, cosmic::panel);
            cosmic::label(message.substring(0, 38), 5, 101, cosmic::panel);
        }
        cosmic::footer("L/R Browse ENTER Play  U/D Volume");
    }
    tft->display(false);
}
} // namespace
void cosmicMusicClose() {
    release();
    paused = true;
}
bool cosmicAudioCodecInit() { return codec(); }
bool cosmicMusicLoaded() { return decoder != nullptr; }
void cosmicMusicMenu() {
    if (!sdcardMounted && !setupSdCard()) {
        displayError("Insert SD card for music", true);
        return;
    }
    if (tracks.empty()) {
        scan("/music", 0);
        std::sort(tracks.begin(), tracks.end(), [](const Track &a, const Track &b) {
            return a.path < b.path;
        });
    }
    resetGlobals();
    bool dirty = true;
    while (true) {
        if (check(EscPress)) {
            if (output) output->stop();
            paused = true;
            break;
        }
        if (!tracks.empty()) {
            if (check(PrevPress)) {
                selected = (selected + tracks.size() - 1) % tracks.size();
                dirty = true;
                message = "";
            }
            if (check(NextPress)) {
                selected = (selected + 1) % tracks.size();
                dirty = true;
                message = "";
            }
            if (check(UpPress)) {
                volume = std::min(100, volume + 5);
                if (output) output->SetGain(volume / 100.0f);
                dirty = true;
            }
            if (check(DownPress)) {
                volume = std::max(0, volume - 5);
                if (output) output->SetGain(volume / 100.0f);
                dirty = true;
            }
            if (check(SelPress)) {
                if (playing != selected || !decoder) play(selected);
                else {
                    paused = !paused;
                    if (paused) output->stop();
                    else if (!output->begin()) {
                        paused = true;
                        message = "Audio output unavailable";
                    }
                }
                dirty = true;
            }
            if (!paused && decoder && !decoder->loop()) {
                int next = playing + 1;
                if (next < int(tracks.size())) {
                    selected = next;
                    play(next);
                } else {
                    release();
                    paused = true;
                }
                dirty = true;
            }
        }
        if (dirty) {
            draw();
            dirty = false;
        }
        launcherDelayMs(1);
    }
    resetGlobals();
}
#endif
