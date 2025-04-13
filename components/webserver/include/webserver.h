#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "device_data.h"
#include <esp_http_server.h>

typedef enum{
	TYPE_0,
	TYPE_1,
	TYPE_2,
	TYPE_3,
	TYPE_4,
	TYPE_5,
	TYPE_6,
	TYPE_7,
	TYPE_8,
	TYPE_9,
	TYPE_10
}webserver_msg_type_t;


typedef enum{
	NEW_SSID_DATA,
	NEW_AUDIO_DATA,
	NEW_DEVICE_NAME
}webserver_state_t;

typedef void (*webserver_event_handler_cb)(device_data_t *device_data, webserver_state_t state, void *event);

// Initialisiert und startet den HTTP-Server
esp_err_t start_webserver(webserver_event_handler_cb event_handler, device_data_t device_data);

// Stoppt den HTTP-Server
void stop_webserver(void);
void start_filesystem_and_webserver(void);
#endif // WEBSERVER_H
