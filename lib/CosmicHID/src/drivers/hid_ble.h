#pragma once
#include "../hal/i_hid_transport.h"

class NimBLEServer;
class NimBLECharacteristic;
class NimBLEHIDDevice;

// NimBLE-Arduino 2.x HOGP transport (guide §7.2). One HID service with
// keyboard input (id 1), mouse input (id 2) and keyboard LED output (id 1),
// plus battery and PnP. Multi-host = 3 static-random address slots (D5).
class BleHidTransport : public IHidTransport {
public:
    void begin(const Settings& s) override;
    void end();
    bool started() const { return server_ && hid_; }
    bool connected() const override { return connected_; }
    bool sendKeyboard(const KeyboardReport& r) override;
    bool sendMouse(const MouseReport& r) override;
    void onLed(void (*cb)(uint8_t leds, void* ctx), void* ctx) override;

    uint8_t hostSlot() const override { return slot_; }
    void    switchHostSlot(uint8_t slot) override;
    void    clearBonding() override;   // NimBLE cannot scope bonds per slot: clears all 3 (UI says so)

    // Extras used by the assembly root / UI (not part of IHidTransport).
    void setBatteryLevel(uint8_t pct);
    bool advertising() const;

private:
    friend class BleServerCallbacks;
    friend class BleLedCallbacks;

    void applySlotAddress() const;
    void startAdvertising() const;

    NimBLEServer*         server_ = nullptr;
    NimBLEHIDDevice*      hid_    = nullptr;
    NimBLECharacteristic* inKb_   = nullptr;
    NimBLECharacteristic* inMs_   = nullptr;

    volatile bool connected_ = false;
    uint16_t connHandle_ = 0xFFFF;
    uint8_t  slot_ = 0;

    void (*ledCb_)(uint8_t, void*) = nullptr;
    void* ledCtx_ = nullptr;

    char name_[32] = "Cardputer-Keyboard-Mouse";
};
