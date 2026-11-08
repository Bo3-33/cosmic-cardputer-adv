#if defined(COSMIC_CARDPUTER) || defined(COSMIC_HID_HOST)
#include "hid_usb.h"

#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>

extern "C" bool tud_mounted(void);    // TinyUSB device state (not re-exported by USB.h)
extern "C" bool tud_suspended(void);

namespace {

// Global construction order matters (see header): keyboard strictly first.
USBHIDKeyboard g_usbKeyboard;
USBHIDMouse    g_usbMouse;

void (*g_ledCb)(uint8_t, void*) = nullptr;
void* g_ledCtx = nullptr;

void usbEventHandler(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == ARDUINO_USB_HID_KEYBOARD_EVENTS && id == ARDUINO_USB_HID_KEYBOARD_LED_EVENT) {
        auto* led = static_cast<arduino_usb_hid_keyboard_event_data_t*>(data);
        if (g_ledCb != nullptr && led != nullptr) g_ledCb(led->leds, g_ledCtx);
    }
}

} // namespace

void UsbHidTransport::begin(const Settings&) {
    g_usbKeyboard.onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT, usbEventHandler);
    g_usbKeyboard.begin();
    g_usbMouse.begin();
    // Start TinyUSB explicitly: the framework's app_main auto-start
    // (ARDUINO_USB_ON_BOOT) does not take effect on this stack — verified on
    // hardware: without this call the device keeps enumerating as the
    // USB-Serial-JTAG unit and no HID appears.
    started_ = USB.begin();
}

bool UsbHidTransport::connected() const {
    // Query TinyUSB directly: the ARDUINO_USB_* event flags proved unreliable
    // on this stack (STARTED never delivered -> permanently "disconnected").
    return tud_mounted() && !tud_suspended();
}

bool UsbHidTransport::sendKeyboard(const KeyboardReport& r) {
    if (!connected()) return false;
    // KeyReport has an extra `reserved` byte: copy field by field, never memcpy.
    KeyReport kr;
    kr.modifiers = r.modifiers;
    kr.reserved = 0;
    for (int i = 0; i < 6; ++i) kr.keys[i] = r.keys[i];
    g_usbKeyboard.sendReport(&kr);  // returns void: connected() gate is our best signal
    return true;
}

bool UsbHidTransport::sendMouse(const MouseReport& r) {
    if (!connected()) return false;
    // Button edges via press/release, motion via move() — the real wire report
    // is 5 bytes (with pan), so raw sendReport of our 4-byte POD is forbidden.
    const uint8_t pressed = r.buttons & ~lastButtons_;
    const uint8_t released = lastButtons_ & ~r.buttons;
    if (pressed != 0) g_usbMouse.press(pressed);
    if (released != 0) g_usbMouse.release(released);
    lastButtons_ = r.buttons;
    if (r.dx != 0 || r.dy != 0 || r.wheel != 0) {
        g_usbMouse.move(r.dx, r.dy, r.wheel);
    }
    return true;
}

void UsbHidTransport::onLed(void (*cb)(uint8_t, void*), void* ctx) {
    g_ledCb = cb;
    g_ledCtx = ctx;
}

#endif
