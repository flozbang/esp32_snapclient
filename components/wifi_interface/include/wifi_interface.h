/*
 * wifi_interface.h
 *
 *  Created on: 19.10.2024
 *      Author: florian
 */

#ifndef COMPONENTS_WIFI_INTERFACE_INCLUDE_WIFI_INTERFACE_H_
#define COMPONENTS_WIFI_INTERFACE_INCLUDE_WIFI_INTERFACE_H_


#ifndef WIFI_INTERFACE_H
#define WIFI_INTERFACE_H

#include "esp_err.h"
#include "device_data.h"
// Wi-Fi Initialisierungsfunktion
esp_err_t wifi_init(void);

// Funktion für den Access Point
esp_err_t wifi_start_ap(void);

// Funktion für den Wi-Fi-Client
esp_err_t wifi_connect_sta(device_data_t *device_data);
esp_err_t wifi_scan(void);
esp_err_t wifi_scan_to_json(char *out_json, size_t out_json_len);
esp_err_t wifi_interface_reconnect(const char *ssid, const char *password);
#endif // WIFI_INTERFACE_H

#ifdef __cplusplus
}
#endif



#endif /* COMPONENTS_WIFI_INTERFACE_INCLUDE_WIFI_INTERFACE_H_ */
