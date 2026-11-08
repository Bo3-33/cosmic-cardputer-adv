#include "ds4_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main() {
    uint8_t packet[20];
    ds4::encode({}, packet);
    const uint32_t neutral[] = {0xfff, 0x02000000, 0x007ff7ff, 0x80800081, 0};
    for (int i = 0; i < 5; ++i) assert(ds4::get32(packet + i * 4) == neutral[i]);
    assert(packet[0] == 0xff && packet[1] == 0x0f && packet[15] == 0x80);
    uint8_t report[64] = {1, 128, 128, 128, 128, 0x48, 0, 0};
    ds4::State sample;
    assert(!ds4::decode(report, 9, sample));
    report[0] = 0x11; assert(!ds4::decode(report, 64, sample));
    report[0] = 1;
    assert(ds4::decode(report, 64, sample));
    ds4::encode(sample, packet);
    assert(ds4::get32(packet) == 0xffe); // Circle -> A
    ds4::encode(sample, packet, 16, true);
    assert(ds4::get32(packet) == 0xffd); // Alternate Circle -> B
    for (int hat = 0; hat < 8; ++hat) {
        sample.face = hat;
        ds4::encode(sample, packet);
        uint32_t held = ~ds4::get32(packet) & 0xfff;
        const uint32_t dirs[] = {64, 80, 16, 144, 128, 160, 32, 96};
        assert(held == dirs[hat]);
    }
    sample = {};
    sample.buttons = 0x3f;
    sample.special = 0xff;
    ds4::encode(sample, packet);
    assert((~ds4::get32(packet) & 0xfff) == (4 | 8 | 256 | 512));
    assert(ds4::get32(packet + 12) == 0x80800681);
    assert(ds4::get32(packet + 16) == 1); // Never synthesize power events
    sample = {};
    sample.lx = 255; sample.ly = 0;
    sample.rx = 255; sample.ry = 0;
    ds4::encode(sample, packet);
    uint32_t circle = ds4::get32(packet + 8);
    assert((circle & 0xfff) == 3536 && (circle >> 12) == 3536);
    assert((ds4::get32(packet + 12) >> 16 & 255) == 255); // Rotated and saturated
    assert((ds4::get32(packet + 12) >> 24) == 128);
    for (int v = 112; v <= 144; ++v) { sample = {}; sample.lx = v; ds4::encode(sample, packet); assert(ds4::get32(packet + 8) == 0x007ff7ff); }
    for (int v = 0; v <= 255; ++v) { assert(ds4::axis(v, 16) >= -1 && ds4::axis(v, 16) <= 1); }
    puts("DS4 decoder, button mapping, sticks, triggers, HOME and neutral packets passed");
}
