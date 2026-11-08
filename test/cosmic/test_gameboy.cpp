#include "gameboy_input.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
extern "C" {
#include <minigb_apu.h>
}
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define WALNUT_GB_HIGH_LCD_ACCURACY 0
#define WGB_IRAM_ATTR
#include <walnut_cgb.h>
static std::array<uint8_t,32768> rom;
static unsigned lines;
static uint8_t read(gb_s *, uint_fast32_t a) { return a < rom.size() ? rom[a] : 255; }
static uint16_t read16(gb_s *g,uint_fast32_t a) { return read(g,a) | uint16_t(read(g,a+1)) << 8; }
static uint32_t read32(gb_s *g,uint_fast32_t a) { return read16(g,a) | uint32_t(read16(g,a+2)) << 16; }
static uint8_t ramRead(gb_s *,uint_fast32_t) { return 255; }
static void ramWrite(gb_s *,uint_fast32_t,uint8_t) {}
static void fail(gb_s *,gb_error_e,uint16_t) { assert(false && "Unexpected core error"); }
static void lcd(gb_s *,const uint8_t *,uint_fast8_t y) { assert(y < 144); ++lines; }
static void header(bool color) {
    rom.fill(0);
    rom[0x100]=0xc3;rom[0x101]=0x50;rom[0x102]=1;
    rom[0x143]=color?0x80:0;
    // Original diagnostic program: enable LCD and loop; no copyrighted ROM data.
    uint8_t program[]={0x3e,0x91,0xe0,0x40,0x3e,0xe4,0xe0,0x47,0x18,0xfe};
    std::copy(program,program+sizeof(program),rom.begin()+0x150);
    uint8_t checksum=0;for(size_t i=0x134;i<=0x14c;++i)checksum=checksum-rom[i]-1;
    rom[0x14d]=checksum;
}
int main() {
    assert(gameboy::pad({})==255);
    ds4::State s; s.face=0x28; assert(gameboy::pad(s)==0xef);
    s={};s.face=0x48;assert(gameboy::pad(s)==0xdf);
    s={};s.buttons=0x30;assert(gameboy::pad(s)==0x3f);
    for(unsigned hat=0;hat<8;++hat){s={};s.face=hat;assert((gameboy::pad(s)&15)!=15);}
    s={};s.lx=0;assert((gameboy::pad(s)&2)==0);
    for (bool color : {false,true}) {
        header(color);
        auto *g=static_cast<gb_s *>(calloc(1,sizeof(gb_s)));assert(g);
        assert(gb_init(g,read,read16,read32,ramRead,ramWrite,fail,nullptr)==GB_INIT_NO_ERROR);
        assert(bool(g->cgb.cgbMode)==color);
        gb_init_lcd(g,lcd); lines=0;
        for(int frame=0;frame<4;++frame) {
            g->gb_frame=false; unsigned budget=200000;
            while(!g->gb_frame && budget--) __gb_step_cpu_x(g);
            assert(g->gb_frame);
        }
        assert(lines>=144);
        size_t size=99;assert(gb_get_save_size_s(g,&size)==0 && size==0);
        rom[0x149]=3;assert(gb_get_save_size_s(g,&size)==0 && size==32768);
        rom[0x149]=4;assert(gb_get_save_size_s(g,&size)==0 && size==131072);
        printf("%s diagnostic: CPU/LCD frames passed, core context %zu bytes\n",color?"GBC":"GB",sizeof(gb_s));
        free(g);
    }
    minigb_apu_ctx apu;minigb_apu_audio_init(&apu);
    std::array<audio_sample_t,AUDIO_SAMPLES_TOTAL+2> samples={};
    samples[AUDIO_SAMPLES_TOTAL]=1234;samples[AUDIO_SAMPLES_TOTAL+1]=2345;
    minigb_apu_audio_callback(&apu,samples.data());
    assert(samples[AUDIO_SAMPLES_TOTAL]==1234 && samples[AUDIO_SAMPLES_TOTAL+1]==2345);
    puts("Game Boy/Color core, USB pad mapping, RAM sizing and APU buffer tests passed");
}
