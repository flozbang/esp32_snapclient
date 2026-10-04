/*
 * network_interface.c
 *
 *  Created on: 28.08.2025
 *      Author: florian
 */

#include "network_interface.h"

#include <stddef.h>                // für NULL
#include "device_data.h"
#include "esp_err.h"               // für ESP_ERROR_CHECK
#include "esp_log.h"               // für ESP_LOGI, TAG
#include "esp_netif.h"             // für esp_netif_*
#include "esp_event.h"             // für esp_event_*
#include "driver/gpio.h"           // für gpio_reset_pin etc.
//#include "esp_eth.h"               // für Ethernet MAC/PHY
#include "driver/spi_master.h"
#include "esp_wifi.h"
#include "network_interface.h"     // dein eigenes Header
#include "string.h"
#include <inttypes.h>
#include "cJSON.h"
//#include "esp_eth.h"


//include "esp_eth_phy.h"
static const char* TAG= "NETWORK_INTERFACE";

static TimerHandle_t network_watchdog_timer;

/* Timer callback */
void network_watchdog_cb(TimerHandle_t xTimer)
{
    network_conf_t *conf = (network_conf_t *)pvTimerGetTimerID(xTimer);
    static bool prev_network_up = false;
    bool network_is_up;

    if (conf == NULL) {
        return;
    }

    network_is_up = conf->eth_link_up || conf->wifi_got_ip;

    if (network_is_up != prev_network_up) {
        network_event_msg_t msg = { .source = conf, .data = NULL, .data_len = 0 };
        prev_network_up = network_is_up;

        if (conf->handle_cb != NULL) {
            conf->handle_cb(&msg, network_is_up ? NETWORK_UP : NETWORK_DOWN);
        }
    }

    if (!conf->eth_link_up && !conf->wifi_started) {
        if (wifi_connect_sta(conf->device_data) == ESP_OK) {
            conf->wifi_started = true;
        }
    }
}

// ---------------- Event Handler für IP ----------------
static void got_ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	network_conf_t *conf = (network_conf_t*)arg;
	if (event_base == IP_EVENT) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
	
	    if (event_id == IP_EVENT_STA_GOT_IP) {
	        ESP_LOGI(TAG, "WiFi hat IP: " IPSTR, IP2STR(&event->ip_info.ip));
	        conf->wifi_got_ip = (event->ip_info.ip.addr != 0);
			conf->eth_got_ip = false;
			esp_netif_set_default_netif(event->esp_netif);
			wifi_scan();
	    }
	    else if (event_id == IP_EVENT_ETH_GOT_IP) {
	        ESP_LOGI(TAG, "Ethernet hat IP: " IPSTR, IP2STR(&event->ip_info.ip));
	        conf->eth_got_ip = (event->ip_info.ip.addr != 0);
			conf->wifi_got_ip = false;
			conf->eth_link_up=true;
			esp_netif_set_default_netif(event->esp_netif);
	    }
	    else {
	        ESP_LOGW(TAG, "Unbekanntes IP-Event (%" PRId32 ")", event_id);
	    }
	}
}
/*
// ---------------- Ethernet Event Handler ----------------
static void eth_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    network_conf_t *conf = (network_conf_t*)arg;

    switch(event_id){
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet Link UP");
            conf->eth_link_up = true;
			// Callback aufrufen, falls registriert
	       if (conf->handle_cb) {
	           network_event_msg_t msg = {
	               .source = conf,
	               .data = NULL,
	               .data_len = 0
	           };
	           network_status_t state =  NETWORK_DOWN;
	           conf->handle_cb(&msg, state);
	       }
            // STA trennen wenn aktiv
            if(conf->wifi_started){
                ESP_LOGI(TAG, "Trenne STA Wi-Fi wegen Ethernet");
                esp_wifi_disconnect();
                conf->wifi_started = false;
                conf->wifi_restart = false;
            }

            // AP ggf. stoppen
            if(conf->ap_running){
                ESP_LOGI(TAG, "Stoppe AP wegen Ethernet");
                esp_wifi_stop();
                conf->ap_running = false;
            }
            break;

        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Ethernet Link DOWN");
            conf->eth_link_up = false;
            conf->eth_got_ip = false;
            break;

        default:
            ESP_LOGW(TAG, "Unbekanntes Ethernet Event: %" PRId32, event_id);
            break;
    }
}
*/
// ---------------- Wifi Station Event Handler ----------------
static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    network_conf_t *conf = (network_conf_t *)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA gestartet -> Verbinde");
                conf->wifi_started = true;
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                conf->wifi_got_ip = false;

                if (conf->wifi_retry_count < UINT8_MAX) {
                    conf->wifi_retry_count++;
                }

                ESP_LOGW(TAG, "STA getrennt, Retry %u/%u", conf->wifi_retry_count, WIFI_CONNECT_MAX_RETRIES);

                if (conf->wifi_retry_count < WIFI_CONNECT_MAX_RETRIES) {
                    esp_wifi_connect();
                } else if (!conf->ap_fallback_active) {
                    ESP_LOGW(TAG, "STA-Verbindung fehlgeschlagen -> Setup-AP wird aktiviert");
                    wifi_start_setup_ap(conf);
                } else {
                    esp_wifi_connect();
                }
                break;

            default:
                break;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA Wi-Fi IP: " IPSTR, IP2STR(&event->ip_info.ip));
        conf->wifi_got_ip = true;
        conf->wifi_started = true;
        conf->wifi_restart = false;
        conf->wifi_retry_count = 0;
        esp_netif_set_default_netif(event->esp_netif);

        if (conf->ap_fallback_active) {
            ESP_LOGI(TAG, "STA wieder online -> Setup-AP wird deaktiviert");
            wifi_stop_setup_ap(conf);
        }
    }
}

// ---------------- Wifi Access Point Event Handler ----------------
static void wifi_ap_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    network_conf_t *conf = (network_conf_t *)arg;

    if (event_base != WIFI_EVENT) {
        return;
    }

    switch (event_id) {
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "Setup-AP gestartet");
            conf->ap_running = true;
            conf->ap_fallback_active = true;

            if (conf->handle_cb != NULL) {
                network_event_msg_t msg = { .source = conf, .data = NULL, .data_len = 0 };
                conf->handle_cb(&msg, NETWORK_AP_UP);
            }
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "Setup-AP gestoppt");
            conf->ap_running = false;
            conf->ap_fallback_active = false;
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "Station mit Setup-AP verbunden");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "Station vom Setup-AP getrennt");
            break;

        default:
            break;
    }
}

/*
void lan8720_adjust_rx_delay(esp_eth_handle_t eth_handle)
{
    esp_err_t ret;
    uint32_t phy_data[3];

    // Wartezeit nach Start und Sicherstellung PHY bereit
    for (int i = 0; i < 50; i++) { // max 2,5s
        phy_data[0] = 1;    // PHY-Adresse
        phy_data[1] = 0x1D; // Register RGCR1
        phy_data[2] = 0;

        ret = esp_eth_ioctl(eth_handle, ETH_CMD_READ_PHY_REG, phy_data);
        if (ret == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PHY Read never succeeded!");
        return;
    }

    phy_data[2] |= (1 << 8); // RXD0 Edge Delay setzen

    ret = esp_eth_ioctl(eth_handle, ETH_CMD_WRITE_PHY_REG, phy_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PHY Write Error");
    } else {
        ESP_LOGI(TAG, "RXD0 Edge Delay angepasst");
    }
}
*/

void network_start_netif(){
	// TCP/IP Stack & Event Loop
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
}

void network_init_eth_clock_pin() {
    //gpio_reset_pin(PHY_CLK_EN_GPIO);
    //gpio_set_direction(PHY_CLK_EN_GPIO, GPIO_MODE_OUTPUT);
}

void network_enable_eth_clock() {
    //gpio_set_level(PHY_CLK_EN_GPIO, 1);
}

void network_disable_eth_clock() {
    //gpio_set_level(PHY_CLK_EN_GPIO, 0);
}


esp_err_t network_open(network_conf_t *conf){
	
	conf->eth_got_ip=false;
	conf->eth_link_up=false;
	conf->wifi_started=false;
	conf->wifi_restart=false;
	conf->wifi_got_ip=false;
	conf->ap_running=false;
	conf->ap_fallback_active=false;
	conf->wifi_retry_count=0;
	network_start_netif();
	network_ethernet_init(conf);
	wifi_init(conf->device_data, conf->device_identity);
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, conf, NULL));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_ap_event_handler, conf, NULL));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_event_handler, conf, NULL));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, conf, NULL));
	//ESP_ERROR_CHECK(esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, conf, NULL));
	if (wifi_connect_sta(conf->device_data) == ESP_OK) {
		conf->wifi_started = true;
	}
	network_watchdog_timer = xTimerCreate("EthWatchdog", pdMS_TO_TICKS(10000),
				                                  pdTRUE, conf, network_watchdog_cb);
	if (network_watchdog_timer != NULL) {
		xTimerStart(network_watchdog_timer, 0);
	}
	return ESP_OK;
}


void network_ethernet_init(network_conf_t *conf) {
/*	esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
   	esp_netif_t *eth_netif = esp_netif_new(&cfg);

    // SPI-Bus initialisieren
    spi_bus_config_t buscfg = {
       .miso_io_num = PIN_NUM_MISO,
       .mosi_io_num = PIN_NUM_MOSI,
       .sclk_io_num = PIN_NUM_CLK,
       .quadwp_io_num = -1,
       .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

   // SPI-Gerät konfigurieren
   spi_device_interface_config_t devcfg = {
       .command_bits = 16,
       .address_bits = 8,
       .mode = 0,
       .clock_speed_hz = 20 * 1000 * 1000,
       .spics_io_num = PIN_NUM_CS,
       .queue_size = 20,
   };

   // W5500 Reset-Pin
   gpio_reset_pin(PIN_NUM_RST);
   gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
   gpio_set_level(PIN_NUM_RST, 0);
   vTaskDelay(pdMS_TO_TICKS(100));
   gpio_set_level(PIN_NUM_RST, 1);
   vTaskDelay(pdMS_TO_TICKS(100));

   // MAC-Adresse festlegen
   uint8_t mac_addr[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};

   // W5500 Konfiguration
   eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &devcfg);
   w5500_config.int_gpio_num = -1;
   w5500_config.poll_period_ms = 10;

   // MAC-Konfiguration
   eth_mac_config_t mac_config = {
       .sw_reset_timeout_ms = 100,
       .rx_task_stack_size  = 4096,
       .rx_task_prio        = 15,
       .flags               = 0
   };
 
   // MAC erstellen
   esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
   if (!mac) {
       ESP_LOGE(TAG, "Failed to create W5500 MAC");
       return;
   }


   // PHY erstellen
   eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
   phy_config.phy_addr = 0;
   phy_config.reset_gpio_num = -1; // Reset schon gemacht
   esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
   if (!phy) {
       ESP_LOGE(TAG, "Failed to create W5500 PHY");
       return;
   }

   // Ethernet-Treiber installieren
   esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
   esp_eth_handle_t eth_handle = NULL;
   ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
   ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr));		
   // Netif anhängen und Ethernet starten
   ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
   ESP_ERROR_CHECK(esp_eth_start(eth_handle));

   // DHCP starten
   ESP_ERROR_CHECK(esp_netif_dhcpc_start(eth_netif));

   // MAC-Adresse ausgeben
   uint8_t read_mac[6];
   ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, read_mac));
   ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
            read_mac[0], read_mac[1], read_mac[2],
            read_mac[3], read_mac[4], read_mac[5]);*/
}

esp_err_t wifi_init(device_data_t *device_data, device_identity_t *device_identity)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    (void)ap_netif;

    // Hostname setzen
    esp_err_t ret = esp_netif_set_hostname(netif, device_identity->device_name);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wifi Hostname erfolgreich auf '%s' gesetzt", device_identity->device_name);
    } else {
        ESP_LOGE(TAG, "Fehler beim Setzen des Hostnamens");
    }
	
	
	
    return ESP_OK;
}

// Funktion für den STA-Modus (Wi-Fi-Client)
esp_err_t wifi_connect_sta(device_data_t *device_data)
{
    

    // WLAN-Konfiguration mit SSID & Passwort aus device_data
    wifi_config_t wifi_config = { 0 }; // wichtig: alles auf 0 setzen

    strncpy((char *)wifi_config.sta.ssid, device_data->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, device_data->passwd, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';     // Sicherheitshalber nullterminieren
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "Verbinde mit SSID: %s", wifi_config.sta.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi STA gestartet.");

    return ESP_OK;
}

esp_err_t wifi_start_setup_ap(network_conf_t *conf)
{
    wifi_config_t ap_config = { 0 };

    if (conf == NULL || conf->device_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "Snapcast-%02X%02X%02X", conf->device_data->mac[3], conf->device_data->mac[4], conf->device_data->mac[5]);
    strncpy((char *)ap_config.ap.password, WIFI_SETUP_AP_PASSWORD, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.ssid[sizeof(ap_config.ap.ssid) - 1] = '\0';
    ap_config.ap.password[sizeof(ap_config.ap.password) - 1] = '\0';
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "Aktiviere Setup-AP '%s'", ap_config.ap.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    conf->ap_fallback_active = true;
    return ESP_OK;
}

esp_err_t wifi_stop_setup_ap(network_conf_t *conf)
{
    if (conf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    conf->ap_fallback_active = false;
    conf->ap_running = false;
    return ESP_OK;
}

esp_err_t network_wifi_sta_disconnect(void)
{
    // WLAN trennen
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Fehler beim Trennen: %d", ret);
    } else {
        ESP_LOGI(TAG, "WLAN getrennt");
    }
    return ESP_OK;
}
	
esp_err_t wifi_scan_to_json(char *out_json, size_t out_json_len)
{
    wifi_scan_config_t scan_config = { .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = true };
    wifi_ap_record_t ap_info[10];
    uint16_t number_of_networks = 10;
    cJSON *root;
    cJSON *networks;
    char *json_str;
    size_t json_len;
    esp_err_t ret;

    if (out_json == NULL || out_json_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi Scan konnte nicht gestartet werden: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_scan_get_ap_records(&number_of_networks, ap_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi Scan-Ergebnisse konnten nicht gelesen werden: %s", esp_err_to_name(ret));
        return ret;
    }

    root = cJSON_CreateObject();
    networks = cJSON_CreateArray();
    if (root == NULL || networks == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(networks);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < number_of_networks; i++) {
        cJSON *network = cJSON_CreateObject();
        if (network == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(network, "ssid", (char *)ap_info[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", ap_info[i].rssi);
        cJSON_AddItemToArray(networks, network);
    }

    cJSON_AddItemToObject(root, "networks", networks);
    json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    json_len = strlen(json_str);
    if (json_len + 1 > out_json_len) {
        ESP_LOGE(TAG, "Puffer für Wi-Fi JSON zu klein: %u benötigt, %u vorhanden", (unsigned)(json_len + 1), (unsigned)out_json_len);
        free(json_str);
        return ESP_ERR_NO_MEM;
    }

    memcpy(out_json, json_str, json_len + 1);
    free(json_str);
    return ESP_OK;
}
  
esp_err_t wifi_scan(void)
{
    wifi_scan_config_t scan_config = { .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = true };
    wifi_ap_record_t ap_info[10];
    uint16_t number_of_networks = 10;
    esp_err_t ret;

    ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi Scan konnte nicht gestartet werden: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_scan_get_ap_records(&number_of_networks, ap_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi Scan-Ergebnisse konnten nicht gelesen werden: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Gefundene Netzwerke: %u", number_of_networks);
    for (int i = 0; i < number_of_networks; i++) {
        ESP_LOGI(TAG, "SSID: %s, RSSI: %d", ap_info[i].ssid, ap_info[i].rssi);
    }

    return ESP_OK;
}
