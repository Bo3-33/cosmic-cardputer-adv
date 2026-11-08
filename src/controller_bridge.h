#pragma once
#ifdef COSMIC_CARDPUTER
#include "ds4_protocol.h"
#include <WString.h>
bool controllerBridgeRunning();
void controllerBridgeStop();
bool controllerUsbStart();
bool controllerUsbHostActive();
bool controllerUsbRead(ds4::State &sample);
void controllerUsbMenuEnable(bool enabled);
void controllerUsbMenuPoll();
void controllerBridgeMenu();
#endif
