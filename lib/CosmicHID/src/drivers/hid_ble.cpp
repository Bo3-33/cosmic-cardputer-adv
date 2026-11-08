#if defined(COSMIC_CARDPUTER) || defined(COSMIC_HID_HOST)
#include "hid_ble.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include <cstdio>
#include <esp_mac.h>

namespace {

// HID report map (guide §7.2, byte-exact): keyboard id=1 boot layout + LEDs,
// mouse id=2 with 3 buttons + wheel.
const uint8_t kReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,              // Keyboard, Report ID 1
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,                          //   modifiers
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                          //   reserved
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,              //   6 keys
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
    0x91, 0x02,                                                  //   5 LED bits out
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,                          //   LED padding
    0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01,
    0xA1, 0x00,                                                  // Mouse, Report ID 2, Pointer
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,                          //   3 buttons
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,                          //   padding
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81,
    0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,              //   X, Y, Wheel rel
    0xC0, 0xC0,
};

constexpr uint16_t kAppearanceKeyboard = 0x03C1;  // iOS wants a keyboard appearance (D13)

// Static random address per slot: shown as C0:4B:4D:AD:00:0S. NimBLE takes
// the bytes little-endian, so 0xC0 goes into addr[5] (guide §7.2). The two
// MSBs of a static random address must be 1 — 0xC0 satisfies that.
void slotAddress(uint8_t slot, uint8_t out[6]) {
    out[0] = slot;
    out[1] = 0x00;
    out[2] = 0xAD;
    out[3] = 0x4D;
    out[4] = 0x4B;
    out[5] = 0xC0;
}

BleHidTransport* g_self = nullptr;

} // namespace

class BleServerCallbacks : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
        if (g_self == nullptr) return;
        g_self->connHandle_ = info.getConnHandle();
        g_self->connected_ = true;
        // 11.25-15 ms interval (Apple HID floor), latency 30, 3 s timeout (guide §7.2).
        server->updateConnParams(info.getConnHandle(), 9, 12, 30, 300);
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        if (g_self == nullptr) return;
        g_self->connected_ = false;
        g_self->connHandle_ = 0xFFFF;
        // advertiseOnDisconnect(true) restarts advertising automatically.
    }
};

class BleLedCallbacks : public NimBLECharacteristicCallbacks {
public:
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo&) override {
        if (g_self == nullptr || g_self->ledCb_ == nullptr) return;
        NimBLEAttValue v = chr->getValue();
        if (v.size() >= 1) g_self->ledCb_(v[0], g_self->ledCtx_);
    }
};

namespace {
BleServerCallbacks g_serverCbs;
BleLedCallbacks    g_ledCbs;
}

void BleHidTransport::begin(const Settings& s) {
    g_self = this;
    slot_ = s.bleSlot <= 2 ? s.bleSlot : 0;
    snprintf(name_, sizeof(name_), "%s", s.bleName);

    if(!NimBLEDevice::init(name_))return;
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    applySlotAddress();
    // Just Works bonding with Secure Connections (iOS-friendly, D13).
    NimBLEDevice::setSecurityAuth(true /*bond*/, false /*mitm*/, true /*sc*/);

    server_ = NimBLEDevice::createServer();
    server_->setCallbacks(&g_serverCbs);
    server_->advertiseOnDisconnect(true);  // NFR-5 auto re-advertise

    hid_ = new NimBLEHIDDevice(server_);
    hid_->setManufacturer("M5Stack");
    // PnP: VID source 0x02 = USB-IF, Espressif VID, project PID (guide §7.2).
    hid_->setPnp(0x02, 0x303A, 0x8211, 0x0100);
    hid_->setHidInfo(0x00, 0x03);  // v1.11 country 0, RemoteWake | NormallyConnectable
    hid_->setReportMap(const_cast<uint8_t*>(kReportMap), sizeof(kReportMap));

    inKb_ = hid_->getInputReport(1);
    inMs_ = hid_->getInputReport(2);
    NimBLECharacteristic* ledOut = hid_->getOutputReport(1);
    ledOut->setCallbacks(&g_ledCbs);

    hid_->setBatteryLevel(100);
    server_->start();
    startAdvertising();
}

void BleHidTransport::applySlotAddress() const {
    uint8_t addr[6];
    // Device-unique identity per slot; the reference's fixed MAC collides across units.
    esp_read_mac(addr, ESP_MAC_BT);
    addr[0] ^= uint8_t(0x51 + slot_);
    addr[5] |= 0xC0;
    NimBLEDevice::setOwnAddr(addr);
}

void BleHidTransport::startAdvertising() const {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    // Must precede setName(): NimBLE places the name in the scan response only
    // if it is already enabled — otherwise it tries the primary packet, where
    // appearance + UUID leave too little room, and the name is dropped.
    adv->enableScanResponse(true);
    adv->setAppearance(kAppearanceKeyboard);
    adv->addServiceUUID(hid_->getHidService()->getUUID());
    adv->setName(name_);
    adv->start();
}

bool BleHidTransport::sendKeyboard(const KeyboardReport& r) {
    if (!connected_ || inKb_ == nullptr) return false;
    uint8_t buf[8];
    buf[0] = r.modifiers;
    buf[1] = 0;  // reserved
    for (int i = 0; i < 6; ++i) buf[2 + i] = r.keys[i];
    inKb_->setValue(buf, sizeof(buf));
    return inKb_->notify();
}

void BleHidTransport::end() {
    g_self=nullptr;
    if(!NimBLEDevice::isInitialized())return;
    NimBLEDevice::getAdvertising()->stop();
    if(server_ && connected_)server_->disconnect(connHandle_);
    connected_=false;connHandle_=0xffff;
    delete hid_;hid_=nullptr;inKb_=inMs_=nullptr;server_=nullptr;
    NimBLEDevice::deinit(true);
}

bool BleHidTransport::sendMouse(const MouseReport& r) {
    if (!connected_ || inMs_ == nullptr) return false;
    uint8_t buf[4];
    buf[0] = r.buttons;
    buf[1] = static_cast<uint8_t>(r.dx);
    buf[2] = static_cast<uint8_t>(r.dy);
    buf[3] = static_cast<uint8_t>(r.wheel);
    inMs_->setValue(buf, sizeof(buf));
    return inMs_->notify();
}

void BleHidTransport::onLed(void (*cb)(uint8_t, void*), void* ctx) {
    ledCb_ = cb;
    ledCtx_ = ctx;
}

void BleHidTransport::switchHostSlot(uint8_t slot) {
    slot = slot % 3;
    if (slot == slot_) return;
    slot_ = slot;

    // Stop advertising, drop the current host, swap identity, re-advertise.
    NimBLEDevice::getAdvertising()->stop();
    if (connected_ && connHandle_ != 0xFFFF) {
        server_->disconnect(connHandle_);
    }
    applySlotAddress();
    NimBLEDevice::getAdvertising()->start();
}

void BleHidTransport::clearBonding() {
    NimBLEDevice::deleteAllBonds();
}

void BleHidTransport::setBatteryLevel(uint8_t pct) {
    if (hid_ != nullptr) hid_->setBatteryLevel(pct, connected_);
}

bool BleHidTransport::advertising() const {
    return NimBLEDevice::getAdvertising()->isAdvertising();
}

#endif
