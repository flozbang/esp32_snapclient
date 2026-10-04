#include "system_diagnostics.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "DIAGNOSTICS";

static system_diagnostics_t diagnostics;

static const char *reset_reason_to_string(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "UNKNOWN";

        case ESP_RST_POWERON:
            return "POWERON";

        case ESP_RST_EXT:
            return "EXTERNAL";

        case ESP_RST_SW:
            return "SOFTWARE";

        case ESP_RST_PANIC:
            return "PANIC";

        case ESP_RST_INT_WDT:
            return "INT_WDT";

        case ESP_RST_TASK_WDT:
            return "TASK_WDT";

        case ESP_RST_WDT:
            return "WDT";

        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";

        case ESP_RST_BROWNOUT:
            return "BROWNOUT";

        case ESP_RST_SDIO:
            return "SDIO";

        default:
            return "OTHER";
    }
}

void system_diagnostics_init(void)
{
    diagnostics.wifi_reconnects = 0;
    diagnostics.snapcast_reconnects = 0;
    diagnostics.rb_underflows = 0;
    diagnostics.rb_overflows = 0;
    diagnostics.stream_errors = 0;

    esp_reset_reason_t reason = esp_reset_reason();

    ESP_LOGI(TAG, "Reset reason: %s (%d)", reset_reason_to_string(reason), reason);
}

void system_diagnostics_log(void)
{
    uint32_t heap_free = esp_get_free_heap_size();
    uint32_t heap_min = esp_get_minimum_free_heap_size();

    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    ESP_LOGI(TAG,
             "heap=%lu min=%lu internal=%u internal_min=%u largest=%u wifi_rec=%lu snap_rec=%lu rb_under=%lu rb_over=%lu stream_err=%lu",
             (unsigned long)heap_free,
             (unsigned long)heap_min,
             (unsigned int)internal_free,
             (unsigned int)internal_min,
             (unsigned int)internal_largest,
             (unsigned long)diagnostics.wifi_reconnects,
             (unsigned long)diagnostics.snapcast_reconnects,
             (unsigned long)diagnostics.rb_underflows,
             (unsigned long)diagnostics.rb_overflows,
             (unsigned long)diagnostics.stream_errors);
}

void system_diagnostics_wifi_reconnect(void)
{
    diagnostics.wifi_reconnects++;
}

void system_diagnostics_snapcast_reconnect(void)
{
    diagnostics.snapcast_reconnects++;
}

void system_diagnostics_rb_underflow(void)
{
    diagnostics.rb_underflows++;
}

void system_diagnostics_rb_overflow(void)
{
    diagnostics.rb_overflows++;
}

void system_diagnostics_stream_error(void)
{
    diagnostics.stream_errors++;
}

const system_diagnostics_t *system_diagnostics_get(void)
{
    return &diagnostics;
}