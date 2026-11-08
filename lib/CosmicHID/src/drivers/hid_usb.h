#pragma once
#include "../hal/i_hid_transport.h"

// TinyUSB composite CDC + HID keyboard + HID mouse (guide §7.1).
//
// The USBHIDKeyboard/USBHIDMouse objects live as file-level globals in
// hid_usb.cpp: TinyUSB interfaces must register during global construction,
// before the USB stack builds its descriptor. The keyboard is constructed
// first so the HID interface uses the keyboard boot protocol (BIOS support).
// TinyUSB itself is started by begin() below — the framework's app_main
// auto-start does not take effect on this stack (hardware-verified), so in
// BLE mode TinyUSB never runs and the USB port stays the chip's
// USB-Serial-JTAG console (flashing still works there, D1).
class UsbHidTransport : public IHidTransport {
public:
    void begin(const Settings& s) override;
    bool started() const { return started_; }
    bool connected() const override;
    bool sendKeyboard(const KeyboardReport& r) override;
    bool sendMouse(const MouseReport& r) override;
    void onLed(void (*cb)(uint8_t leds, void* ctx), void* ctx) override;

private:
    bool started_ = false;
    uint8_t lastButtons_ = 0;
};
