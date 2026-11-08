#ifdef COSMIC_CARDPUTER
#include "cosmic_clock.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "onlineLauncher.h"
#include <esp_sntp.h>
#include <time.h>
bool cosmicSyncClock() {
    if (!launcherWifiIsConnected() && !connectWifi()) return false;
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
    for (int i = 0; i < 100; ++i) {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && time(nullptr) > 1704067200) return true;
        launcherDelayMs(100);
    }
    return false;
}
#endif
