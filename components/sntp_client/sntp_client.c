/* LwIP SNTP example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "sntp_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "sntp_component";
static bool sntp_initialized = false;
static void (*time_sync_callback)(void) = NULL;

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "System time synchronized.");
    if (time_sync_callback) {
        time_sync_callback(); // Event-Handler aufrufen
    }
}


void set_system_time(void) {
    const int retry_count = 15;  // Anzahl der maximalen Versuche
    int retry = 0;

    ESP_LOGI(TAG, "Starting SNTP time sync...");
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }

    // Überprüfung, ob die Zeit erfolgreich gesetzt wurde
    if (retry < retry_count) {
        ESP_LOGI(TAG, "System time set successfully.");
        esp_netif_sntp_deinit();  // Ressourcen nach erfolgreicher Synchronisation freigeben
        sntp_initialized = false; // Setzt SNTP-Status zurück, um zukünftige Synchronisationen zu erlauben
    } else {
        ESP_LOGE(TAG, "Failed to set system time after multiple attempts.");
    }
}

void initialize_sntp(void (*sync_callback)(void)) {
	time_sync_callback = sync_callback;
    if (!sntp_initialized) {
        ESP_LOGI(TAG, "Initializing SNTP...");
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        config.server_from_dhcp = false;  // Server von DHCP ignorieren
        config.sync_cb=time_sync_notification_cb;
        esp_netif_sntp_init(&config);     // Initialisierung der SNTP-Komponente
        sntp_initialized = true;
    }
}


