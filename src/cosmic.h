#pragma once
#ifdef COSMIC_CARDPUTER
#include "display.h"
namespace cosmic {
constexpr uint16_t rgb(unsigned r, unsigned g, unsigned b) {
    return ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3);
}
constexpr uint16_t background = rgb(6, 6, 6);
constexpr uint16_t panel = rgb(105, 12, 12);
constexpr uint16_t accent = rgb(255, 4, 4);
constexpr uint16_t text = rgb(255, 223, 218);
constexpr uint16_t surface = rgb(38, 38, 38);
void applyTheme();
void header(const String &title);
void footer(const String &hint);
void label(const String &s, int x, int y, uint16_t bg = background, int size = 1);
void boot(bool force);
void desktop();
void drawIcon(const String &name, int x, int y);
String setTitle(const String &title);
Opt_Coord options(int index, std::vector<Option> &items, std::vector<MenuOptions> &hitboxes);
} // namespace cosmic
void controllerBridgeMenu();
#endif
