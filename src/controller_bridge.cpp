#ifdef COSMIC_CARDPUTER
#include "controller_bridge.h"
#include "cosmic.h"
#include "cosmic_hid.h"
#include "ds4_protocol.h"
#include "gameboy_input.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "powerSave.h"
#include <cstring>
#include <esp_intr_alloc.h>
#include <fcntl.h>
#include <freertos/semphr.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <nvs.h>
#include <usb/usb_host.h>

namespace {
usb_host_client_handle_t client = nullptr;
usb_device_handle_t device = nullptr;
usb_transfer_t *transfer = nullptr;
uint8_t newAddress = 0, endpoint = 0;
int interfaceNumber = -1;
bool gone = false, pending = false;
bool usbInstalled = false;
bool usbAttempted = false;
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
ds4::State state;
unsigned long lastReport = 0;
uint32_t reports = 0, sent = 0;
bool enabled = false, usbReady = false, crossIsA = false;
uint8_t deadzone = 16;
int sock = -1;
sockaddr_in target = {};
char usbStatus[48] = "USB host not started";
String consoleIp;
SemaphoreHandle_t networkLock;

void status(const char *s) {
    portENTER_CRITICAL(&stateLock);
    snprintf(usbStatus, sizeof(usbStatus), "%s", s);
    portEXIT_CRITICAL(&stateLock);
}
void setReady(bool ready) {
    portENTER_CRITICAL(&stateLock);
    usbReady = ready;
    if (!ready) state = ds4::State{};
    portEXIT_CRITICAL(&stateLock);
}
void received(usb_transfer_t *t) {
    pending = false;
    if (t->status == USB_TRANSFER_STATUS_COMPLETED && !gone) {
        ds4::State next;
        if (ds4::decode(t->data_buffer, t->actual_num_bytes, next)) {
            portENTER_CRITICAL(&stateLock);
            state = next;
            lastReport = launcherMillis();
            ++reports;
            usbReady = true;
            portEXIT_CRITICAL(&stateLock);
            status("DS4 USB input active");
        }
        if (usb_host_transfer_submit(t) == ESP_OK) pending = true;
        else {
            gone = true;
            status("USB submit failed; replug");
            setReady(false);
        }
    } else {
        gone = true;
        setReady(false);
        status("USB disconnected/error; replug");
    }
}
void event(const usb_host_client_event_msg_t *msg, void *) {
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV && !device) newAddress = msg->new_dev.address;
    if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE && msg->dev_gone.dev_hdl == device) {
        gone = true;
        setReady(false);
        status("Controller unplugged");
    }
}
void closeDevice() {
    if (!device) return;
    if (pending) {
        usb_host_endpoint_halt(device, endpoint);
        usb_host_endpoint_flush(device, endpoint);
        return; // Drain callbacks before releasing the interface or transfer.
    }
    if (interfaceNumber >= 0) usb_host_interface_release(client, device, interfaceNumber);
    if (transfer) usb_host_transfer_free(transfer);
    usb_host_device_close(client, device);
    transfer = nullptr;
    device = nullptr;
    interfaceNumber = -1;
    gone = false;
    setReady(false);
}
void openDevice(uint8_t address) {
    if (usb_host_device_open(client, address, &device) != ESP_OK) {
        status("Cannot open USB device");
        return;
    }
    const usb_device_desc_t *desc = nullptr;
    const usb_config_desc_t *config = nullptr;
    if (usb_host_get_device_descriptor(device, &desc) != ESP_OK || desc->idVendor != 0x054c ||
        (desc->idProduct != 0x05c4 && desc->idProduct != 0x09cc)) {
        status("Use a Sony DualShock 4 via USB");
        gone = true;
        return;
    }
    if (usb_host_get_active_config_descriptor(device, &config) != ESP_OK) {
        gone = true;
        return;
    }
    const uint8_t *p = reinterpret_cast<const uint8_t *>(config);
    int candidate = -1;
    uint16_t packetSize = 0;
    for (size_t off = 0; off + 2 <= config->wTotalLength;) {
        uint8_t len = p[off], type = p[off + 1];
        if (len < 2 || off + len > config->wTotalLength) break;
        if (type == 4 && len >= 9) candidate = p[off + 5] == 3 && p[off + 3] == 0 ? p[off + 2] : -1;
        if (type == 5 && len >= 7 && candidate >= 0 && (p[off + 2] & 0x80) && (p[off + 3] & 3) == 3) {
            endpoint = p[off + 2];
            packetSize = uint16_t(p[off + 4]) | uint16_t(p[off + 5]) << 8;
            interfaceNumber = candidate;
            break;
        }
        off += len;
    }
    if (interfaceNumber < 0 || packetSize < 10 || packetSize > 64) {
        interfaceNumber = -1;
        gone = true;
        status("Unsupported DS4 USB descriptors");
        return;
    }
    int claimed = interfaceNumber;
    if (usb_host_interface_claim(client, device, claimed, 0) != ESP_OK) {
        interfaceNumber = -1;
        gone = true;
        status("Cannot claim DS4 interface");
        return;
    }
    if (usb_host_transfer_alloc(packetSize, 0, &transfer) != ESP_OK) {
        gone = true;
        status("Not enough USB memory");
        return;
    }
    transfer->device_handle = device;
    transfer->bEndpointAddress = endpoint;
    transfer->callback = received;
    transfer->num_bytes = packetSize;
    if (usb_host_transfer_submit(transfer) == ESP_OK) {
        pending = true;
        status("DS4 attached; waiting for input");
    } else {
        gone = true;
        status("Cannot read DS4; replug");
    }
}
void sendPacket(const ds4::State &sample) {
    uint8_t packet[20];
    ds4::encode(sample, packet, deadzone, crossIsA);
    if (sock >= 0 &&
        sendto(sock, packet, sizeof(packet), 0, reinterpret_cast<sockaddr *>(&target), sizeof(target)) ==
            sizeof(packet)) {
        portENTER_CRITICAL(&stateLock);
        ++sent;
        portEXIT_CRITICAL(&stateLock);
    }
}
void worker(void *) {
    unsigned long nextSend = 0;
    while (true) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(0, &flags);
        usb_host_client_handle_events(client, pdMS_TO_TICKS(1));
        if (gone) closeDevice();
        if (!device && newAddress) {
            uint8_t address = newAddress;
            newAddress = 0;
            openDevice(address);
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
        unsigned long now = launcherMillis();
        if (now - nextSend >= 16) {
            nextSend = now;
            ds4::State sample;
            portENTER_CRITICAL(&stateLock);
            if (usbReady && now - lastReport < 250) sample = state;
            portEXIT_CRITICAL(&stateLock);
            xSemaphoreTake(networkLock, portMAX_DELAY);
            if (enabled && launcherWifiIsConnected()) sendPacket(sample);
            xSemaphoreGive(networkLock);
        }
        launcherDelayMs(1);
    }
}
bool startUsb() {
    if (cosmicHidUsbActive()) {
        status("USB HID active; reboot for DS4");
        return false;
    }
    if (usbInstalled) return true;
    if (usbAttempted) {
        status("USB init failed; reboot required");
        return false;
    }
    usbAttempted = true;
    networkLock = xSemaphoreCreateMutex();
    if (!networkLock) {
        status("No memory for USB host");
        return false;
    }
    usb_host_config_t cfg = {};
    cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
    esp_err_t err = usb_host_install(&cfg);
    if (err != ESP_OK) {
        status("USB host unavailable; reboot");
        vSemaphoreDelete(networkLock);
        networkLock = nullptr;
        return false;
    }
    usb_host_client_config_t cc = {};
    cc.max_num_event_msg = 5;
    cc.async.client_event_callback = event;
    if (usb_host_client_register(&cc, &client) != ESP_OK) {
        // Host resources remain until reboot if client registration fails.
        status("USB client failed; reboot");
        return false;
    }
    if (xTaskCreate(worker, "DS4Bridge", 6144, nullptr, 2, nullptr) != pdPASS) {
        status("USB task failed; reboot");
        return false;
    }
    usbInstalled = true;
    status("Plug in DS4 using USB data cable");
    return true;
}
void stop() {
    if (!networkLock) return;
    xSemaphoreTake(networkLock, portMAX_DELAY);
    enabled = false;
    for (int i = 0; i < 3; ++i) sendPacket(ds4::State{});
    if (sock >= 0) close(sock);
    sock = -1;
    xSemaphoreGive(networkLock);
}
bool start() {
    if (consoleIp.isEmpty()) {
        displayError("Set 2DS IP first", true);
        return false;
    }
    sockaddr_in next = {};
    next.sin_family = AF_INET;
    next.sin_port = htons(4950);
    if (inet_pton(AF_INET, consoleIp.c_str(), &next.sin_addr) != 1 || next.sin_addr.s_addr == 0 ||
        next.sin_addr.s_addr == INADDR_BROADCAST) {
        displayError("Invalid console IPv4", true);
        return false;
    }
    if (!launcherWifiIsConnected() && !connectWifi()) return false;
    if (!startUsb()) {
        displayError("USB host failed; reboot", true);
        return false;
    }
    stop();
    xSemaphoreTake(networkLock, portMAX_DELAY);
    target = next;
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock >= 0) {
        fcntl(sock, F_SETFL, O_NONBLOCK);
        enabled = true;
    }
    xSemaphoreGive(networkLock);
    return enabled;
}
void loadSettings() {
    nvs_handle_t h;
    if (nvs_open("cosmic_ds4", NVS_READONLY, &h) != ESP_OK) return;
    char ip[16] = {};
    size_t len = sizeof(ip);
    if (nvs_get_str(h, "ip", ip, &len) == ESP_OK) consoleIp = ip;
    nvs_get_u8(h, "deadzone", &deadzone);
    uint8_t mapping = 0;
    nvs_get_u8(h, "cross_a", &mapping);
    crossIsA = mapping != 0;
    if (deadzone > 64) deadzone = 16;
    nvs_close(h);
}
void saveSettings() {
    nvs_handle_t h;
    if (nvs_open("cosmic_ds4", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ip", consoleIp.c_str());
    nvs_set_u8(h, "deadzone", deadzone);
    nvs_set_u8(h, "cross_a", crossIsA);
    nvs_commit(h);
    nvs_close(h);
}
void monitor() {
    resetGlobals();
    unsigned long painted = 0;
    while (!check(EscPress)) {
        if (launcherMillis() - painted >= 200) {
            painted = launcherMillis();
            char message[48];
            uint32_t count, packets;
            bool ready;
            portENTER_CRITICAL(&stateLock);
            snprintf(message, sizeof(message), "%s", usbStatus);
            count = reports;
            packets = sent;
            ready = usbReady && launcherMillis() - lastReport < 250;
            portEXIT_CRITICAL(&stateLock);
            tft->fillScreen(cosmic::background);
            cosmic::header("Controller bridge");
            cosmic::label(ready ? "DUALSHOCK 4 / USB" : "WAITING FOR USB INPUT", 9, 28);
            cosmic::label(String(message).substring(0, 36), 9, 47);
            cosmic::label("2DS " + consoleIp + ":4950", 9, 65);
            cosmic::label("Reports " + String(count) + "  Sent " + String(packets), 9, 82);
            cosmic::label(
                launcherWifiIsConnected() ? "UDP only - no console acknowledgement" : "Wi-Fi disconnected",
                9,
                100
            );
            cosmic::footer("ESC Desktop - bridge stays running");
            tft->display(false);
        }
        wakeUpScreen();
        launcherDelayMs(10);
    }
}
} // namespace
bool controllerBridgeRunning() {
    if (!networkLock) return false;
    xSemaphoreTake(networkLock, portMAX_DELAY);
    bool running = enabled;
    xSemaphoreGive(networkLock);
    return running;
}
void controllerBridgeStop() { stop(); }
bool controllerUsbHostActive() { return usbAttempted; }
bool controllerUsbStart() {
    stop();
    return startUsb();
}
bool controllerUsbRead(ds4::State &sample) {
    sample = ds4::State{};
    portENTER_CRITICAL(&stateLock);
    bool valid = usbReady && launcherMillis() - lastReport < 250;
    if (valid) sample = state;
    portEXIT_CRITICAL(&stateLock);
    return valid;
}
namespace {
bool usbMenuEnabled = false;
uint8_t usbMenuPrevious = 0;
unsigned long usbMenuRepeat = 0;
} // namespace
void controllerUsbMenuEnable(bool enabled) {
    usbMenuEnabled = enabled;
    usbMenuPrevious = 0;
    usbMenuRepeat = 0;
}
void controllerUsbMenuPoll() {
    if (!usbMenuEnabled) return;
    ds4::State sample;
    uint8_t held = controllerUsbRead(sample) ? uint8_t(~gameboy::pad(sample)) : 0;
    uint8_t pressed = held & ~usbMenuPrevious;
    unsigned long now = launcherMillis();
    if (held != usbMenuPrevious) usbMenuRepeat = now + 350;
    else if (held && now >= usbMenuRepeat) {
        pressed |= held & 15;
        usbMenuRepeat = now + 150;
    }
    usbMenuPrevious = held;
    if (pressed & 1) NextPress = true;
    if (pressed & 2) PrevPress = true;
    if (pressed & 4) UpPress = true;
    if (pressed & 8) DownPress = true;
    if (pressed & (16 | 128)) SelPress = true;
    if (pressed & 32) EscPress = true;
}
void controllerBridgeMenu() {
    String previous = cosmic::setTitle("Controller bridge");
    static bool loaded = false;
    if (!loaded) {
        loadSettings();
        loaded = true;
    }
    bool done = false;
    while (!done) {
        std::vector<Option> items = {
            {controllerBridgeRunning() ? "Resume bridge monitor" : "Start USB bridge",
             []() {
                 if (controllerBridgeRunning() || start()) monitor();
             }                                                                                               },
            {"2DS IP: " + (consoleIp.isEmpty() ? String("not set") : consoleIp),
             []() {
                 stop();
                 consoleIp = keyboard(consoleIp, 15, "2DS IPv4 address");
                 saveSettings();
             }                                                                                               },
            {"Dead zone: " + String(deadzone),
             []() {
                 if (networkLock) xSemaphoreTake(networkLock, portMAX_DELAY);
                 deadzone = deadzone >= 24 ? 8 : deadzone + 8;
                 if (networkLock) xSemaphoreGive(networkLock);
                 saveSettings();
             }                                                                                               },
            {crossIsA ? "Mapping: Cross=A" : "Mapping: Circle=A",
             []() {
                 if (networkLock) xSemaphoreTake(networkLock, portMAX_DELAY);
                 crossIsA = !crossIsA;
                 if (networkLock) xSemaphoreGive(networkLock);
                 saveSettings();
             }                                                                                               },
            {"Stop bridge / release buttons",                                          []() { stop(); }      },
            {"Back to desktop",                                                        [&]() { done = true; }},
        };
        resetGlobals();
        if (loopOptions(items) < 0) done = true;
        returnToMenu = false;
    }
    cosmic::setTitle(previous);
}
#endif
