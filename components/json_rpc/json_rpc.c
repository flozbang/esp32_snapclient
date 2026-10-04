/*
 * json_rpc.c
 *
 *  Created on: 11.09.2025
 *      Author: florian
 */
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "esp_log.h"
#include "json_rpc.h"


static rpc_event_handler_cb _event_handler;
static const char *TAG_ = "tcp_server";

typedef struct  {
	device_data_t *device_data;
	int64_t       *time_delay;
} json_rpc_t;

static json_rpc_t rpc;



int parse_json_rpc(const char *json, rpc_request_t *req) {
    if (!json || !req) return -1;

    req->value = -1; // Default, falls kein "value"
    req->id = 0;

    // Methode extrahieren
    const char *pm = strstr(json, "\"method\":\"");
    if (!pm) return -1;
    pm += strlen("\"method\":\"");
    int i = 0;
    while (*pm && *pm != '"' && i < sizeof(req->method)-1) {
        req->method[i++] = *pm++;
    }
    req->method[i] = '\0';

    // Value extrahieren (optional, nur bei case SET_*)
    const char *pv = strstr(json, "\"value\":");
    if (pv) {
        pv += strlen("\"value\":");
        sscanf(pv, "%d", &req->value);
    }

    // ID extrahieren
    const char *pid = strstr(json, "\"id\":");
    if (pid) {
        pid += strlen("\"id\":");
        sscanf(pid, "%d", &req->id);
    }

    return 0; // Erfolg
}


char* json_rpc_process(rpc_request_t *req, json_rpc_t *p){
	 char *resp=calloc(1, 128);
	 int value=0;
	 int id=0;
	 switch(req->id){
		case GET_VOLUME:
			value=p->device_data->audio.volume;
			id=req->id;
		break;
		case SET_VOLUME:
			p->device_data->audio.volume=req->value;
			value=req->value;
			id=req->id;
		break;
		case GET_MUTE:
			value=p->device_data->audio.muted;
			id=req->id;
		break;
		case SET_MUTE:
			p->device_data->audio.muted=req->value;
			value=req->value;
			id=req->id;
		break;
		case GET_BALANCE:
			value=p->device_data->audio.balance;
			id=req->id;
		break;
		case SET_BALANCE:
			p->device_data->audio.balance=req->value;
			value=req->value;
			id=req->id;
		break;
		case GET_MODE:
		ESP_LOGI(TAG_, "GET_MODE: Value: %d, ID: %d\n", p->device_data->audio.output, req->id);
			value=p->device_data->audio.output;
			id=req->id;
		break;
		case SET_MODE:
		ESP_LOGI(TAG_, "SET_MODE: Value: %d, ID: %d\n", req->value, req->id);
			p->device_data->audio.output=req->value;
			value=req->value;
			id=req->id;
		break;
		case GET_EQ_1:
    		value = p->device_data->audio.gain[0];
    		id = req->id;
    		break;
		case SET_EQ_1:
		    p->device_data->audio.gain[0] = req->value;
		    value = req->value;
		    id = req->id;
		    _event_handler(SET_EQ_1, NULL);
		    break;
		case GET_EQ_2:
		    value = p->device_data->audio.gain[1];
		    id = req->id;
		    break;
		case SET_EQ_2:
		    p->device_data->audio.gain[1] = req->value;
		    value = req->value;
		    id = req->id;
		    _event_handler(SET_EQ_2, NULL);
		    break;
		case GET_EQ_3:
		    value = p->device_data->audio.gain[2];
		    id = req->id;
		    break;
			case SET_EQ_3:
		    p->device_data->audio.gain[2] = req->value;
		    value = req->value;
		    id = req->id;
		    _event_handler(SET_EQ_3, NULL);
		    break;
		case GET_EQ_4:
		    value = p->device_data->audio.gain[3];
		    id = req->id;
		    break;
		case SET_EQ_4:
		    p->device_data->audio.gain[3] = req->value;
		    value = req->value;
		    id = req->id;
		    _event_handler(SET_EQ_4, NULL);
		    break;
		case GET_EQ_5:
		    value = p->device_data->audio.gain[4];
		    id = req->id;
		    break;
		case SET_EQ_5:
		    p->device_data->audio.gain[4] = req->value;
		    value = req->value;
		    id = req->id;
		    _event_handler(SET_EQ_5, NULL);
		    break;
		case GET_EQ_6:
		    value = p->device_data->audio.gain[5];
		    id = req->id;
		    break;
		case SET_EQ_6:
		    p->device_data->audio.gain[5] = req->value;
		    value = req->value;
		    id = req->id;
		    _event_handler(SET_EQ_6, NULL);
		    break;
		case GET_EQ_7:
		    value = p->device_data->audio.gain[6];
		    id = req->id;
		    break;
		case SET_EQ_7:
		    p->device_data->audio.gain[6] = req->value;
		    value = req->value;
		    id = req->id;
		    _event_handler(SET_EQ_7, NULL);
		    break;
		case GET_EQ_8:
		    value = p->device_data->audio.gain[7];
		    id = req->id;
		    break;
		case SET_EQ_8:
		    p->device_data->audio.gain[7] = req->value;
		    value = req->value;
		    id = req->id;
		    _event_handler(SET_EQ_8, NULL);
		    break;
		case GET_EQ_9:
		    value = p->device_data->audio.gain[8];
		    id = req->id;
		    break;
		case SET_EQ_9:
		    p->device_data->audio.gain[8] = req->value;
		    value = req->value;
		    id = req->id;
		    _event_handler(SET_EQ_9, NULL);
		    break;
		case GET_EQ_10:
		    value = p->device_data->audio.gain[9];
		    id = req->id;
		    break;
		case SET_EQ_10:
		    p->device_data->audio.gain[9] = req->value;
		    value = req->value;
		    id = req->id;
		    _event_handler(SET_EQ_10, NULL);
		    break;
		case GET_DELAY: value = *p->time_delay; id = req->id; break;
		case SET_DELAY: *p->time_delay = req->value; value = req->value; id = req->id; break;
	}
	snprintf(resp, 128, "{\"jsonrpc\":\"2.0\",\"result\":{\"status\":\"ok\",\"value\":%d},\"id\":%d}\n", value, id);
	return resp;
}


void tcp_server_task(void *pvParameters)
{
	rpc_request_t req;
	json_rpc_t *rpc= (json_rpc_t*)pvParameters;
	char rx_buffer[BUF_SIZE];
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
     // Socket erstellen
    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG_, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG_, "Socket created");
     // Socket an Port binden
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TCP_PORT);
    if (bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        ESP_LOGE(TAG_, "Socket unable to bind: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG_, "Socket bound, port %d", TCP_PORT);
    // Socket auf Verbindungen lauschen
    if (listen(listen_sock, 5) < 0) {
        ESP_LOGE(TAG_, "Error during listen: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
       return;
   }
   ESP_LOGI(TAG_, "Socket listening");
   while (1) {
       ESP_LOGI(TAG_, "Waiting for a client...");
       struct sockaddr_in client_addr;
       socklen_t addr_len = sizeof(client_addr);
       int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
       if (client_sock < 0) {
           ESP_LOGE(TAG_, "Unable to accept connection: errno %d", errno);
           continue;
       }
       inet_ntoa_r(client_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
       ESP_LOGI(TAG_, "Client connected from %s", addr_str);
       // Daten empfangen
       int len;
       while ((len = recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0)) > 0) {
           rx_buffer[len] = 0; // Null-terminieren
		   parse_json_rpc(rx_buffer, &req);
		   ESP_LOGI(TAG_, "Method: %s, Value: %d, ID: %d\n", req.method, req.value, req.id);
           char *resp = json_rpc_process( &req, rpc);
           send(client_sock, resp, strlen(resp), 0);
		   free(resp);
       }
       if (len < 0) {
           ESP_LOGE(TAG_, "recv failed: errno %d", errno);
       }
       ESP_LOGI(TAG_, "Client disconnected");
       close(client_sock);
   }
    // Listening-Socket schließen (normalerweise nie erreicht)
   close(listen_sock);
   vTaskDelete(NULL);
} 




void tcp_server_start(){
	xTaskCreate(tcp_server_task, "tcp_server", 4096, &rpc, 5, NULL);
}


void json_rpc_init(json_rpc_config_t *config, rpc_event_handler_cb cb){
	_event_handler=cb;
	rpc.device_data=config->device_data;
	rpc.time_delay=config->time_delay;
}

