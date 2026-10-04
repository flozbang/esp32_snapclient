/*
 * network_interface.h
 *
 *  Created on: 26.08.2025
 *      Author: florian
 */

#ifndef MAIN_NETWORK_INTERFACE_H_
#define MAIN_NETWORK_INTERFACE_H_
#include "device_data.h"
#include <stdint.h>
#include "esp_netif.h"      
//#include "esp_eth.h"               // für Ethernet MAC/PHY
#include "esp_wifi.h"

#define PIN_NUM_MISO 18
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  19
#define PIN_NUM_CS   22
#define PIN_NUM_INT  27
#define PIN_NUM_RST  4   // Reset-Pin (manuell)

#define WIFI_CONNECT_MAX_RETRIES 5
#define WIFI_SETUP_AP_PASSWORD "snapcast"


/*#define PIN_NUM_MISO 12   // empfängt Daten vom W5500 (achte auf kein RC-Debounce)
#define PIN_NUM_MOSI 13   // ESP treibt diese Leitung (GPIO13 hat Pulldown, ok)
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   22   // CS sollte beim Boot HIGH sein (externen Pull-Up sicherstellen)
#define PIN_NUM_RST  21   // Reset (manuell)
#define PIN_NUM_INT  27   // falls du INT weiterhin auf 27 haben willst


*/
typedef struct {
    esp_netif_t *netif;
  //  esp_eth_handle_t eth_handle;
} network_watchdog_args_t;



typedef enum{
    NETWORK_DOWN,
    NETWORK_UP,
    NETWORK_AP_UP
} network_status_t;


typedef struct {
    void                          *source;          /*!< Element handle */
    void                          *data;            /*!< Data of input/output */
    int                           data_len;         /*!< Data length of input/output */
} network_event_msg_t;

typedef void (*network_event_handle_cb)(network_event_msg_t *msg, network_status_t state);

typedef struct{
	bool eth_link_up;
	bool eth_got_ip;
	bool wifi_got_ip;
	bool wifi_started;
	bool wifi_restart;
	bool ap_running;
	bool ap_fallback_active;
	uint8_t wifi_retry_count;
	network_event_handle_cb handle_cb;   
	device_data_t *device_data;
	device_identity_t *device_identity; 
}network_conf_t;

esp_err_t network_open(network_conf_t* conf);
void network_start_netif();
void network_init_eth_clock_pin();
void network_enable_eth_clock();
void network_disable_eth_clock();
void network_ethernet_init(network_conf_t *conf);
esp_err_t wifi_init(device_data_t *device_data, device_identity_t *device_identity);
esp_err_t wifi_connect_sta(device_data_t *device_data);
esp_err_t wifi_start_setup_ap(network_conf_t *conf);
esp_err_t wifi_stop_setup_ap(network_conf_t *conf);
esp_err_t network_wifi_sta_disconnect(void);
esp_err_t wifi_scan(void);
esp_err_t wifi_scan_to_json(char *out_json, size_t out_json_len);
#endif /* MAIN_NETWORK_INTERFACE_H_ */
