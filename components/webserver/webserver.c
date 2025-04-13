#include "webserver.h"
#include <esp_log.h>
#include <esp_http_server.h>
#include "esp_websocket_client.h"
#include "wifi_interface.h"
#include "device_data.h"
#include <stdint.h>
#include <string.h>
#include <cJSON.h> 


#include "config_html.h"
#include "wifi_html.h"
#include "amp_html.h"
#include "index_html.h"

static const char *TAG = "webserver";
static httpd_handle_t server = NULL;
static webserver_event_handler_cb _event_handler;
device_data_t _device_data;
struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

webserver_msg_type_t get_type_from_json(const char *json_string) {
    webserver_msg_type_t type = TYPE_0; // Standardwert für den Fehlerfall
	 ESP_LOGI(TAG, "Get Message Type");
    // JSON-String parsen
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        ESP_LOGE(TAG, "Fehler beim Parsen des JSON-Strings\n");
        return type; // Rückgabe des Standardwerts im Fehlerfall
    }
	
    // Typ aus dem JSON-Objekt extrahieren
    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (type_item != NULL && cJSON_IsNumber(type_item)) {
        int type_value = type_item->valueint;
		 ESP_LOGI(TAG, "Message Type %d", type_value);
        if (type_value >= TYPE_0 && type_value <= TYPE_10) {
            type = (webserver_msg_type_t)type_value;
        } else {
             ESP_LOGE(TAG,"Ungültiger Typ-Wert: %d\n", type_value);
        }
    } else {
        ESP_LOGE(TAG,"Fehler: Typ-Feld fehlt im JSON-String\n");
    }

    // JSON-Objekt freigeben
    cJSON_Delete(root);

    return type;
}




static void ws_async_send(void *arg)
{
    httpd_ws_frame_t ws_pkt;
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;

   
    char buff[4];
    memset(buff, 0, sizeof(buff));
    
    
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)buff;
    ws_pkt.len = strlen(buff);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    static size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients];

    esp_err_t ret = httpd_get_client_list(server, &fds, client_fds);

    if (ret != ESP_OK) {
        return;
    }

    for (int i = 0; i < fds; i++) {
        int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(hd, client_fds[i], &ws_pkt);
        }
    }
    free(resp_arg);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    return httpd_queue_work(handle, ws_async_send, resp_arg);
}

static esp_err_t handle_ws_req(httpd_req_t *req)
{
    uint8_t tmp=0;
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    webserver_msg_type_t type=get_type_from_json((const char*)ws_pkt.payload);
    char *json_string;
    switch(type){
			case TYPE_0:
				ESP_LOGE(TAG, "Typ 0 Message HUAARRAAARRARARA !!!");
				tmp=1;
				break;
			case TYPE_1:
				break;
			case TYPE_2:
				break;
			case TYPE_3:
				break;
			case TYPE_4:
				break;
			case TYPE_5:
				process_received_audio_json((const char*)ws_pkt.payload, &_device_data);
				_event_handler(&_device_data, NEW_AUDIO_DATA, NULL);
				break;
			case TYPE_6:
				json_string = generate_json_from_device_data(&_device_data);
				ws_pkt.payload = (uint8_t *)json_string;
           		ws_pkt.len = strlen(json_string);
            	ret = httpd_ws_send_frame(req, &ws_pkt);
				if (ret != ESP_OK) {
               		ESP_LOGE(TAG, "Fehler beim Senden des WebSocket-Frames: %d", ret);
                	if (json_string) {
                		free(json_string);
                	}
                	free(buf);
                	return ret;
            	}else{
					if (json_string) {
                		free(json_string);
                	}
                	free(buf);
				}
				break;
			case TYPE_7:
				break;
			case TYPE_8:
				ESP_LOGE(TAG, "Typ 8 Message HUAARRAAARRARARA !!!");
				if (extract_ssid_and_passwd_from_json((const char*)ws_pkt.payload, &_device_data) == 0) {
				    ESP_LOGI(TAG,"SSID: %s\n", _device_data.ssid);
				    ESP_LOGI(TAG,"Password: %s\n", _device_data.passwd);
				} else {
				    ESP_LOGE(TAG,"Fehler beim Extrahieren von SSID und Passwort\n");
				}
				_event_handler(&_device_data, NEW_SSID_DATA, NULL);
				break;
			case TYPE_9:
				break;
			case TYPE_10:
				break;
			default:
				break;										
		}
    
    
    
	// Wenn die Nachricht "scan" lautet, starte den Wi-Fi-Scan und sende das Ergebnis zurück
    if (tmp==1) {
		tmp=0;
        char json_buffer[1024];
        ret = wifi_scan_to_json(json_buffer, sizeof(json_buffer));
        if (ret == ESP_OK) {
            // Antwort über WebSocket senden
            ws_pkt.payload = (uint8_t *)json_buffer;
            ws_pkt.len = strlen(json_buffer);
            ret = httpd_ws_send_frame(req, &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Fehler beim Senden des WebSocket-Frames: %d", ret);
                free(buf);
                return ret;
            }
        } else {
            ESP_LOGE(TAG, "Fehler beim Scann: %d", ret);
        }
        free(buf);
    }else{
	/*	webserver_msg_type_t type=get_type_from_json((const char*)ws_pkt.payload);
		switch(type){
			case TYPE_1:
				break;
			case TYPE_2:
				break;
			case TYPE_3:
				break;
			case TYPE_4:
				break;
			case TYPE_5:
				break;
			case TYPE_6:
				break;
			case TYPE_7:
				break;
			case TYPE_8:
				ESP_LOGE(TAG, "Typ 8 Message HUAARRAAARRARARA !!!");
				break;
			case TYPE_9:
				break;
			case TYPE_10:
				break;
			default:
				break;										
		}*/
	}
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
        strcmp((char *)ws_pkt.payload, "toggle") == 0)
    {
        free(buf);
        return trigger_async_send(req->handle, req);
    }
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req) {
    // Dummy-Handler, wenn favicon.ico nicht benötigt wird
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}





esp_err_t root_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<html><body>");
    httpd_resp_send_chunk(req, (const char *)index_html_start, sizeof(index_html_start)-1);
    httpd_resp_send_chunk(req, (const char *)index_html_end, sizeof(index_html_end));
    httpd_resp_sendstr_chunk(req, "</body></html>");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t amp_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<html><body>");
    httpd_resp_send_chunk(req, (const char *)amp_html_start, sizeof(amp_html_start)-1);
    httpd_resp_send_chunk(req, (const char *)amp_html_end, sizeof(amp_html_end));
    httpd_resp_sendstr_chunk(req, "</body></html>");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t wifi_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<html><body>");
    httpd_resp_send_chunk(req, (const char *)wifi_html_start, sizeof(wifi_html_start)-1);
    httpd_resp_send_chunk(req, (const char *)wifi_html_end, sizeof(wifi_html_end));
    httpd_resp_sendstr_chunk(req, "</body></html>");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t config_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<html><body>");
    httpd_resp_send_chunk(req, (const char *)config_html_start, sizeof(config_html_start)-1);
    httpd_resp_send_chunk(req, (const char *)config_html_end, sizeof(config_html_end));
    httpd_resp_sendstr_chunk(req, "</body></html>");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}




 

// Funktion zum Starten des Webservers
esp_err_t start_webserver(webserver_event_handler_cb event_hanler, device_data_t data) {
    _event_handler=event_hanler;
    _device_data = data;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.core_id=0;
	
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handler for '/'");
		httpd_uri_t uri_general = {
			.uri = "/",
			.method = HTTP_GET,
			.handler = root_handler,  // Verwendet den allgemeinen Handler
			.user_ctx = NULL
		};
		httpd_uri_t uri_wifi = {
		    .uri = "/wifi",
		    .method = HTTP_GET,
		    .handler = wifi_handler,
		    .user_ctx = NULL
		};
		httpd_uri_t uri_config = {
		    .uri = "/config",
		    .method = HTTP_GET,
		    .handler = config_handler,
		    .user_ctx = NULL
		};
		httpd_uri_t uri_amp = {
		    .uri = "/amp",
		    .method = HTTP_GET,
		    .handler = amp_handler,
		    .user_ctx = NULL
		};
         httpd_uri_t ws = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = handle_ws_req,
            .user_ctx = NULL,
            .is_websocket = true
        };
         // Favicon-Handler registrieren
        httpd_uri_t favicon = {
            .uri = "/favicon.ico",
            .method = HTTP_GET,
            .handler = favicon_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_general);
		httpd_register_uri_handler(server, &uri_wifi);
		httpd_register_uri_handler(server, &uri_config);
		httpd_register_uri_handler(server, &uri_amp);
		httpd_register_uri_handler(server, &ws);
        httpd_register_uri_handler(server, &favicon);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return ESP_FAIL;
}

// Funktion zum Stoppen des Webservers
void stop_webserver(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}

// Funktion zur Initialisierung von LittleFS und Start des Webservers
void start_filesystem_and_webserver(void) {
   
       // start_webserver();
    
}
