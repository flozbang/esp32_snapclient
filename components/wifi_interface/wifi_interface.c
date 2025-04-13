/*
 * wifi_interface.h
 *
 *  Created on: 19.10.2024
 *      Author: florian
 */
#include "wifi_interface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include <string.h>
#include <cJSON.h> 

#define WIFI_SSID "OpenWrt"
#define WIFI_PASS "mega1720"
#define MAX_RETRY 5

#include "wifi_interface.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wifi_interface";

// Gemeinsame Wi-Fi-Initialisierungsfunktion
esp_err_t wifi_init(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    return ESP_OK;
}

// Funktion für den STA-Modus (Wi-Fi-Client)
esp_err_t wifi_connect_sta(void)
{
    ESP_ERROR_CHECK(wifi_init());

    esp_netif_t *netif=esp_netif_create_default_wifi_sta();
    
    
    const char* hostname = "f_koch_audio_a";
    esp_err_t ret = esp_netif_set_hostname(netif, hostname);
    if (ret == ESP_OK) {
        ESP_LOGI("Wi-Fi", "Hostname erfolgreich auf '%s' gesetzt", hostname);
    } else {
        ESP_LOGE("Wi-Fi", "Fehler beim Setzen des Hostnamens");
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi STA initialized and started.");

    return ESP_OK;
}

// Funktion für den Access Point (AP-Modus)
esp_err_t wifi_start_ap(void)
{
    ESP_ERROR_CHECK(wifi_init());

    // MAC-Adresse abrufen
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    // SSID erstellen: "FKOCH_" + MAC-Adresse
    char ssid[32];
    sprintf(ssid, "FKOCH_%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Wi-Fi-Konfiguration für den Access Point
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "",
            .ssid_len = strlen(ssid),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid));

    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Access Point gestartet mit SSID: %s", ssid);

    return ESP_OK;
}

esp_err_t wifi_scan_to_json(char *out_json, size_t out_json_len) {
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    // Wi-Fi Scan starten
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    uint16_t number_of_networks = 10;
    wifi_ap_record_t ap_info[10];
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number_of_networks, ap_info));

    // JSON-Objekt erstellen
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();

    for (int i = 0; i < number_of_networks; i++) {
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (char *)ap_info[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", ap_info[i].rssi);
        cJSON_AddItemToArray(networks, network);
    }

    cJSON_AddItemToObject(root, "networks", networks);

    // Temporären String-Puffer initialisieren
    size_t json_len = out_json_len;  // Maximale Länge des Zielpuffers
    char *json_str = cJSON_PrintBuffered(root, 256, json_len);
    
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des JSON-Strings.");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    // Prüfen, ob der Zielpuffer ausreichend groß ist
    if (json_len > out_json_len) {
        ESP_LOGE(TAG, "Puffer für JSON-String zu klein, benötigt: %d", json_len);
        free(json_str);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    // Den JSON-String in den Ausgabepuffer kopieren
    memcpy(out_json, json_str, json_len);

    // Speicher des von cJSON_PrintBuffered verwendeten Puffers freigeben
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}


esp_err_t wifi_scan(void)
{
    

    // Scan-Parameter konfigurieren
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    // Scan starten
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    // Warten auf Scan-Ergebnis
    uint16_t number_of_networks = 10;
    wifi_ap_record_t ap_info[10];
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number_of_networks, ap_info));

    ESP_LOGI(TAG, "Gefundene Netzwerke: %d", number_of_networks);
    for (int i = 0; i < number_of_networks; i++) {
        ESP_LOGI(TAG, "SSID: %s, RSSI: %d", ap_info[i].ssid, ap_info[i].rssi);
    }

    return ESP_OK;
}



