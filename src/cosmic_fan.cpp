#ifdef COSMIC_CARDPUTER
#include "cosmic_fan.h"
#include "cosmic.h"
#include "honeywell_signals.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include <driver/gpio.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>

void cosmicHoneywellFan() {
    rmt_channel_handle_t channel = nullptr;
    rmt_encoder_handle_t encoder = nullptr;
    bool enabled = false;
    auto cleanup = [&]() {
        if (channel && enabled) rmt_disable(channel);
        if (encoder) rmt_del_encoder(encoder);
        if (channel) rmt_del_channel(channel);
        gpio_set_direction(GPIO_NUM_44, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_44, 0);
    };
    rmt_tx_channel_config_t config = {};
    config.gpio_num = GPIO_NUM_44;
    config.clk_src = RMT_CLK_SRC_DEFAULT;
    config.resolution_hz = 1000000;
    config.mem_block_symbols = 64;
    config.trans_queue_depth = 1;
    esp_err_t result = rmt_new_tx_channel(&config, &channel);
    rmt_copy_encoder_config_t copy = {};
    if (result == ESP_OK) result = rmt_new_copy_encoder(&copy, &encoder);
    rmt_carrier_config_t carrier = {};
    carrier.frequency_hz = honeywell::frequency;
    carrier.duty_cycle = honeywell::duty;
    if (result == ESP_OK) result = rmt_apply_carrier(channel, &carrier);
    if (result == ESP_OK) {
        result = rmt_enable(channel);
        enabled = result == ESP_OK;
    }
    if (result != ESP_OK) {
        cleanup();
        displayError(String("IR unavailable: ") + esp_err_to_name(result), true);
        return;
    }
    int selected = 0;
    bool dirty = true;
    String status = "Point the ADV IR emitter at the fan";
    resetGlobals();
    while (true) {
        if (check(EscPress)) break;
        if (check(PrevPress) || check(UpPress)) {
            selected = (selected + 2) % 3;
            dirty = true;
        }
        if (check(NextPress) || check(DownPress)) {
            selected = (selected + 1) % 3;
            dirty = true;
        }
        if (dirty) {
            tft->fillScreen(cosmic::background);
            cosmic::header("Honeywell Fan");
            for (int i = 0; i < 3; ++i) {
                uint16_t bg = i == selected ? cosmic::panel : cosmic::surface;
                tft->fillRoundRect(7, 24 + i * 26, 226, 23, 7, bg);
                if (i == selected) tft->fillRoundRect(10, 29 + i * 26, 3, 13, 1, cosmic::accent);
                cosmic::label(honeywell::signals[i].name, 43, 32 + i * 26, bg);
                if (i == 0) {
                    tft->drawCircle(26, 35, 6, cosmic::text);
                    tft->drawLine(26, 27, 26, 35, cosmic::text);
                } else if (i == 1) {
                    tft->drawLine(19, 61, 33, 61, cosmic::text);
                    tft->drawLine(19, 61, 23, 57, cosmic::text);
                    tft->drawLine(33, 61, 29, 65, cosmic::text);
                } else {
                    for (int bar = 0; bar < 3; ++bar)
                        tft->fillRect(19 + bar * 5, 92 - bar * 3, 3, 3 + bar * 3, cosmic::text);
                }
            }
            cosmic::label(status.substring(0, 38), 7, 106);
            cosmic::footer("ENTER Send   Arrows Choose   ESC Back");
            tft->display(false);
            dirty = false;
        }
        if (check(SelPress)) {
            const auto &signal = honeywell::signals[selected];
            rmt_symbol_word_t symbols[240] = {};
            const size_t symbolCount = (signal.count + 1) / 2;
            for (size_t i = 0; i < symbolCount; ++i) {
                symbols[i].val = 0;
                symbols[i].level0 = 1;
                symbols[i].duration0 = signal.durations[i * 2];
                symbols[i].level1 = 0;
                // Raw capture ends on a mark; add only a 1us inactive tail when required by RMT.
                symbols[i].duration1 = i * 2 + 1 < signal.count ? signal.durations[i * 2 + 1] : 1;
            }
            rmt_transmit_config_t tx = {};
            tx.loop_count = 0; // Captures already contain their repeats: transmit the full recording once.
            result = rmt_transmit(channel, encoder, symbols, symbolCount * sizeof(rmt_symbol_word_t), &tx);
            if (result == ESP_OK) {
                // Keep the symbol storage alive until the hardware finishes, even on a timeout.
                result = rmt_tx_wait_all_done(channel, 2000);
                if (result != ESP_OK) { rmt_tx_wait_all_done(channel, -1); }
            }
            status = result == ESP_OK ? String("Sent: ") + signal.name
                                      : String("IR error: ") + esp_err_to_name(result);
            resetGlobals(); // Do not turn a held Enter into extra toggle transmissions.
            dirty = true;
        }
        launcherDelayMs(10);
    }
    cleanup();
    resetGlobals();
}
#endif
