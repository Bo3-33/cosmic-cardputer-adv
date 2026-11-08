#pragma once
#ifdef COSMIC_CARDPUTER
#include <cstdint>
void cosmicHidMenu();
bool cosmicHidUsbActive();
// Board keyboard capture uses the existing input task and its lock.
void cardputerHidCapture(bool enabled);
uint64_t cardputerHidKeys();
#endif
