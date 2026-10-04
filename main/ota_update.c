#include "ota_update.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

static const char *TAG = "OTA";

#define OTA_BUFFER_SIZE 4096

esp_err_t ota_update_from_url(const char *url)
{
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);

    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP connection failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);

    if (content_length < 0) {
        ESP_LOGE(TAG, "Failed to fetch HTTP headers");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP status: %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA update partition found");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Running partition: %s @ 0x%08lx", running_partition->label, (unsigned long)running_partition->address);
    ESP_LOGI(TAG, "Update partition : %s @ 0x%08lx", update_partition->label, (unsigned long)update_partition->address);
    ESP_LOGI(TAG, "Firmware size    : %d bytes", content_length);

    if ((content_length > 0) && ((size_t)content_length > update_partition->size)) {
        ESP_LOGE(TAG, "Firmware image is too large");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t ota_handle = 0;

    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    uint8_t buffer[OTA_BUFFER_SIZE];
    int total_written = 0;

    while (1) {
        int received = esp_http_client_read(client, (char *)buffer, sizeof(buffer));

        if (received < 0) {
            ESP_LOGE(TAG, "HTTP read failed");
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        if (received == 0) {
            break;
        }

        err = esp_ota_write(ota_handle, buffer, received);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return err;
        }

        total_written += received;

        ESP_LOGI(TAG, "Written %d bytes", total_written);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    err = esp_ota_end(ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA successful");
    ESP_LOGI(TAG, "Next boot partition: %s", update_partition->label);

    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_restart();

    return ESP_OK;
}