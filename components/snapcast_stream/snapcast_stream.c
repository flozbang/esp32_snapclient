/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2020 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <stdio.h>
#include <string.h>

#include "lwip/sockets.h"
#include "esp_transport_tcp.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "audio_mem.h"
#include "snapcast_stream.h"
#include "sntp_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include <inttypes.h>
#include "mdns.h"
static const char *TAG = "SNAPCAST_STREAM";

#define ERROR_CNT 5
#define TIMEOUT_SEC 5
/*
typedef struct snapcast_stream_ringbuffer_bits{
	unsigned new_wire_chunk:1;
	unsigned enabled:1;
	unsigned sync:1;
	unsigned :13;
}snapcast_stream_ringbuffer_bits_t;
*/

typedef struct snapcast_stream_ringbuffer_node{
	int position;
	int data_size;
	int ringbuffer_size;
	int64_t timestamp;
	char data[SNAPCAST_STREAM_RINGBUFFER_SIZE];
	struct snapcast_stream_ringbuffer_node *last;
	struct snapcast_stream_ringbuffer_node *next;
}snapcast_stream_ringbuffer_node_t;


typedef struct snapcast_stream {
    audio_stream_type_t           		type;
    char 								first_start;
    int                           		sock;
    int                           		port;
    char                          		*host;
    char								ip_addr[16];
    bool                          		is_open;
    int                           		timeout_ms;
    snapcast_stream_event_handle_cb     hook;
    void                                *ctx;
    bool  								received_header;
   	struct timeval 						last_sync;
   	int 								id_counter;
	struct timeval 						server_uptime;
	char 								*audio_buffer;
	char 								*base_buffer;
	char								*buffer;
	int                                 diff_buffer_counter;
	//int64_t 							avrange_diff;
	int64_t								diff_us;
	int64_t								now_us;
	int64_t         					timeDiff;
	struct timeval 						now;
	struct timeval                      tv1;
	struct timeval                      tv2;
	base_message_t 						base_message;
   	codec_header_message_t 				codec_header_message;
   	wire_chunk_message_t 				wire_chunk_message;
   	server_settings_message_t 			server_settings_message;
   	time_message_t 						time_message;
   	snapcast_stream_status_t            state;
   	snapcast_stream_ringbuffer_node_t   *rb;
   	snapcast_stream_ringbuffer_node_t   *write_rb;
   	snapcast_stream_ringbuffer_node_t   *read_rb;
   	SemaphoreHandle_t   				mutex;
   	int 								*volume;
   	int									*muted;
//   	audio_board_handle_t                bh;
//   	xQueueHandle 						event_queue;
   //	wifi_data_t							*wifi_data;
} snapcast_stream_t;



char base_message_serialized[BASE_MESSAGE_SIZE];
char time_message_serialized[TIME_MESSAGE_SIZE];

int event_id;


void tools_get_mac(char *buffer){
	unsigned char base_mac[6];
	//esp_read_mac(base_mac, ESP_MAC_WIFI_STA);
	esp_base_mac_addr_get(base_mac); 
	sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X", base_mac[0], base_mac[1], base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
	ESP_LOGI(TAG, "%02X:%02X:%02X:%02X:%02X:%02X", base_mac[0], base_mac[1], base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
}

esp_err_t connect_to_server(int sock, const char *ip_addr, int port) {
    struct sockaddr_in server;
    server.sin_addr.s_addr = inet_addr(ip_addr);
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    int result = connect(sock, (struct sockaddr *)&server, sizeof(server));
    if (result == -1 && errno != EINPROGRESS) {
        ESP_LOGW(TAG,"Connect failed: %s\n", strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

int receive_data(int sock, char* buffer, int buffer_size) {
    int bytes_received = 0;
    int total_bytes_received = 0;
    int ret;
    struct timeval tv;
    fd_set readfds;

    while (total_bytes_received < buffer_size - 1) {
        // Set the timeout value
        tv.tv_sec = TIMEOUT_SEC;
        tv.tv_usec = 0;

        // Set up the file descriptor set for select()
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        // Wait until there is data to read or timeout occurs
        ret = select(sock+1, &readfds, NULL, NULL, &tv);
        if (ret == -1) {
            // Error occurred while waiting for data
            ESP_LOGW(TAG, "Error occurred while waiting for data: %s\n", strerror(errno));
            return -1;
        } else if (ret == 0) {
            // Timeout occurred
            ESP_LOGW(TAG, "Timeout occurred while waiting for data\n");
          //  _snapcast_dispatch_event(self, tcp, NULL, 0x00, SNAPCAST_STREAM_STATE_TCP_SOCKET_TIMEOUT_MESSAGE);
            return -2;
        }

        // Try to receive some data
        int len = recv(sock, buffer + total_bytes_received , buffer_size - total_bytes_received - 1, 0);
        if (len == -1) {
        	ESP_LOGW(TAG, "Error receiving data: %s\n", strerror(errno));
            return -1;
        } else if (len == 0) {
            // Connection closed by remote host
        	ESP_LOGW("TAG", "Connection closed by remote host\n");
            return bytes_received;
        } else {
            // Data received
            total_bytes_received += len;
            bytes_received = total_bytes_received;
        }
    }
    return bytes_received;
}


static int _get_socket_error_code_reason(const char *str, int sockfd)
{
    uint32_t optlen = sizeof(int);
    int result;
    int err;

    err = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &result, &optlen);
    if (err == -1) {
        ESP_LOGE(TAG, "%s, getsockopt failed", str);
        return -1;
    }
    if (result != 0) {
        ESP_LOGW(TAG, "%s error, error code: %d, reason: %s", str, err, strerror(result));
    }
    return result;
}




/*

static void log_socket_error(const char *tag, const int sock, const int err, const char *message){
    ESP_LOGE(tag, "[sock=%d]: %s\n error=%d: %s", sock, message, err, strerror(err));
}*/


static int _snapcast_stream_socket_send(const char *tag, const int sock, const char * data, const uint32_t len){
	int to_write = len;
	while (to_write > 0) {
		int written = send(sock, data + (len - to_write), to_write, 0);
		if (written < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
			//log_socket_error(tag, sock, errno, "Error occurred during sending");
			return -1;
		}
		to_write -= written;
	}
	return len;
}


void myTimerTask(void *pvParameters) {
	struct timeval now, tv;
	uint64_t counter=0;

	snapcast_stream_t *tcp = (snapcast_stream_t*)pvParameters;
	if (tcp == NULL) {
		ESP_LOGE(TAG, "Something went wrong");
		return;
	}
	while (1) {

		if(counter>10){

			if (gettimeofday(&now, NULL)) {
				ESP_LOGI(TAG, "Failed to gettimeofday\r\n");
				return;
			}
			/*if (now.tv_sec % 3600 == 0) {
				tcp->first_start=0;
				if (xQueueSend(tcp->event_queue, &sntp_update, portMAX_DELAY) == pdTRUE) {
					printf("Task 1: Daten gesendet: %d\n", sntp_update);
				}
			}*/
			timersub(&now,(&tcp->server_uptime) ,&tv);
			base_message_t base_message = {
					SNAPCAST_MESSAGE_TIME,      // type
					tcp->id_counter++,                         // id
					0x0,                         // refersTo
					{ tv.tv_sec, tv.tv_usec }, // sent
					{ tv.tv_sec, tv.tv_usec },                // received
					TIME_MESSAGE_SIZE,                         // size
				};
			time_message_t time_message;
			time_message.latency.sec=tcp->timeDiff /1000000;
			time_message.latency.usec=tcp->timeDiff - time_message.latency.sec;
			if (base_message_serialize( &base_message, base_message_serialized, BASE_MESSAGE_SIZE)) {
				ESP_LOGE(TAG, "Failed to serialize base message for time\r\n");
				return;
			}
			if (time_message_serialize( &time_message, time_message_serialized, TIME_MESSAGE_SIZE)) {
				ESP_LOGI(TAG, "Failed to serialize time message\r\b");
				return;
			}
			_snapcast_stream_socket_send(TAG, tcp->sock, base_message_serialized, BASE_MESSAGE_SIZE);
			_snapcast_stream_socket_send(TAG, tcp->sock, time_message_serialized, TIME_MESSAGE_SIZE);

		} else {

		}
        counter++;
        vTaskDelay(pdMS_TO_TICKS(200));  // Hier wird der Thread für 1000 Millisekunden pausiert
    }
}

snapcast_stream_ringbuffer_node_t *snapcast_stream_new_ringbuffer(){
	snapcast_stream_ringbuffer_node_t *node = (snapcast_stream_ringbuffer_node_t*) audio_malloc(sizeof(snapcast_stream_ringbuffer_node_t));
	AUDIO_MEM_CHECK(TAG, node, return NULL);
	node->position = 0;
	node->ringbuffer_size = 1;
	node->last  = node;
	node->next  = node;
	return node;
}

snapcast_stream_ringbuffer_node_t *snapcast_stream_ringbuffer_add_element(snapcast_stream_ringbuffer_node_t *last_node, int pos){
	snapcast_stream_ringbuffer_node_t *new_node = (snapcast_stream_ringbuffer_node_t*) audio_malloc(sizeof(snapcast_stream_ringbuffer_node_t));
	AUDIO_MEM_CHECK(TAG, new_node, return NULL);
//	new_node->data        = data;
	new_node->position	  = pos;
	new_node->last        = last_node;
	new_node->next        = last_node->next;
	new_node->next->ringbuffer_size += 1;
	last_node->next       = new_node;
	new_node->next->last  = new_node;
	return new_node;
}

snapcast_stream_ringbuffer_node_t *snapcast_stream_ringbuffer_get_element(snapcast_stream_ringbuffer_node_t *rb, int pos){
	while(rb->position!=pos){
		rb = rb->next;
	}
	return rb;
}

snapcast_stream_ringbuffer_node_t *snapcast_stream_ringbuffer_delete_element(snapcast_stream_ringbuffer_node_t  *node, int pos){
	snapcast_stream_ringbuffer_node_t *tmp = node;
	snapcast_stream_ringbuffer_node_t *el = snapcast_stream_ringbuffer_get_element(node, pos);
	snapcast_stream_ringbuffer_node_t *first = snapcast_stream_ringbuffer_get_element(tmp, SNAPCAST_STREAM_RINGBUFFER_FIRST);
	snapcast_stream_ringbuffer_node_t *rb = el->next;
	int size = first->ringbuffer_size;
	first->ringbuffer_size -= 1;
	el->next->last = el->last;
	el->last->next = el->next;
	int y = rb->position;
	for(int x = y;x<size;x++){
		rb->position-=1;
		rb=rb->next;
	}
	size=first->ringbuffer_size;
	rb = first;
	for(int x=0;x<size;x++){
		rb=rb->next;
	}
	free(el);
	return node->last;
}


esp_err_t snapcast_stream_rinbuffer_reset(audio_element_handle_t self){
	snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);
	snapcast_stream_ringbuffer_node_t *node = snapcast_stream_ringbuffer_get_element(tcp->rb, SNAPCAST_STREAM_RINGBUFFER_FIRST);
	tcp->write_rb=node;
	tcp->read_rb=node;
	int size=node->ringbuffer_size;
	for(int x=0;x<size;x++){
		node->timestamp=0;
		node->data_size=0;
		memset(node->data, 0x00, SNAPCAST_STREAM_RINGBUFFER_SIZE);
		node=node->next;
	}
	return ESP_OK;
}

snapcast_stream_ringbuffer_node_t *snapcast_stream_create_ringbuffer(int size){
	snapcast_stream_ringbuffer_node_t *head    = snapcast_stream_new_ringbuffer();
	snapcast_stream_ringbuffer_node_t *current = head;
	for(int i = 1; i < size; i++){
		current=snapcast_stream_ringbuffer_add_element(current, i);
	}
	return head;
}

void snapcast_stream_delete_ringbuffer(snapcast_stream_ringbuffer_node_t *head) {
    snapcast_stream_ringbuffer_node_t *current = head;
    snapcast_stream_ringbuffer_node_t *next;

    do {
        next = current->next;
        free(current);
        current = next;
    } while (current != head);
}

static esp_err_t _snapcast_dispatch_event(audio_element_handle_t el, snapcast_stream_t *tcp, void *data, int len, snapcast_stream_status_t state)
{
    if (el && tcp && tcp->hook) {
        snapcast_stream_event_msg_t msg = { 0 };
        msg.data = data;
        msg.data_len = len;
        msg.source = el;
        return tcp->hook(&msg, state, tcp->ctx);
    }
    return ESP_FAIL;
}

static esp_err_t _snapcast_connect_to_server(audio_element_handle_t self){
	char base_message_serialized[BASE_MESSAGE_SIZE];
	char *hello_message_serialized;
	char mac_address[18];
	struct timeval now;
	int result;
	int sockfd=0;
	int err=0;
  	int err_cnt=0;
  	snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);
	//int port;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd<-1){
		ESP_LOGE(TAG, "Could not create socket: %s\n", strerror(errno));
	}
	
	esp_err_t _err = mdns_init();
    if (_err != ESP_OK) {
        ESP_LOGE("mDNS", "mDNS Init Fehlgeschlagen: %s", esp_err_to_name(err));
        
    }

    // mDNS-Service Suche
    mdns_result_t *mdns_results = NULL;
    err = mdns_query_ptr("_snapcast", "_tcp", 3000, 10, &mdns_results);  // Beispiel für HTTP-Service auf TCP
    if (err != ESP_OK) {
        ESP_LOGE("mDNS", "mDNS Service Suche Fehlgeschlagen: %s", esp_err_to_name(err));
        
    }

    if (mdns_results) {
        mdns_result_t *r = mdns_results;
        while (r) {
            ESP_LOGI("mDNS", "Service gefunden: %s", r->instance_name);
	        ESP_LOGI("mDNS", "Hostname: %s", r->hostname);
	        // Port zuweisen
	        tcp->port = r->port;
	        // IP-Adresse in einen String umwandeln
	        char ip_str[IP4ADDR_STRLEN_MAX];
	        // Cast von esp_ip4_addr_t auf ip4_addr_t
        	ip4addr_ntoa_r((const ip4_addr_t *)&(r->addr->addr.u_addr.ip4), ip_str, IP4ADDR_STRLEN_MAX);
	       
	        // IP-Adresse in tcp->ip_addr kopieren
	        strncpy(tcp->ip_addr, ip_str, sizeof(tcp->ip_addr) - 1);
	        tcp->ip_addr[sizeof(tcp->ip_addr) - 1] = '\0';  // Sicherstellen, dass es nullterminiert ist
	        ESP_LOGI("mDNS", "TCP IP-Adresse: %s", tcp->ip_addr);
	        ESP_LOGI("mDNS", "TCP Port: %u", tcp->port);
	       	r = r->next;
        }
        mdns_query_results_free(mdns_results);  // Ergebnisse freigeben
    } else {
        ESP_LOGI("mDNS", "Keine mDNS Services gefunden");
    }

    // Optional: mDNS beenden, falls nicht mehr benötigt
    mdns_free();
	
	//strcpy(tcp->ip_addr, "192.168.1.145");
	err=0;
	err_cnt=0;
	ESP_LOGI(TAG, "connecting with snapserver.....");
	//do{
		ESP_LOGI(TAG, "connecting .....");
		err=connect_to_server(sockfd, "192.168.1.145", 1704);
	//}while(err!=0);
	ESP_LOGI(TAG, "Host is %s, port is %d\n", tcp->ip_addr, tcp->port);
	tcp->sock=sockfd;
	if (tcp->sock < 0) {
		_get_socket_error_code_reason(__func__,  tcp->sock);
		return ESP_FAIL;
	}
	result = gettimeofday(&now, NULL);
	if (result) {
		ESP_LOGI(TAG, "Failed to gettimeofday\r\n");
		return ESP_FAIL;
	}
	tools_get_mac(mac_address);
	base_message_t base_message = {
		SNAPCAST_MESSAGE_HELLO,      // type
		0x0,                         // id
		0x0,                         // refersTo
		{ now.tv_sec, now.tv_usec }, // sent
		{ 0x0, 0x0 },                // received
		0x0,                         // size
	};

	hello_message_t hello_message = {
		mac_address,
#ifdef AI_Thinker_Dev_Kit
		"IA_Thinker",          // hostname
#else
		"PCM5102",          // hostname
#endif		
		"0.0.2",               // client version
		"ESP32",               // client name
		"32Bit",               // os name
		"xtensa",              // arch
		1,                     // instance
		mac_address,           // id
		2,                     // protocol version
	};
	ESP_LOGI(TAG, "Sending Hello Message\r\n");
	hello_message_serialized = hello_message_serialize(&hello_message, (uint32_t *)&(base_message.size));
	if (!hello_message_serialized) {
			ESP_LOGI(TAG, "Failed to serialize hello message\r\b");
			return ESP_FAIL;
	}
	if (result) {
		ESP_LOGI(TAG, "Failed to serialize base message\r\n");
		return ESP_FAIL;
	}
	result=base_message_serialize(&base_message, base_message_serialized, BASE_MESSAGE_SIZE);
	if (result) {
		ESP_LOGI(TAG, "Failed to serialize base message\r\n");
		return ESP_FAIL;
	}
	if(_snapcast_stream_socket_send(TAG, sockfd, base_message_serialized, BASE_MESSAGE_SIZE)<0){
		ESP_LOGI(TAG, "Failed sending base message\r\n");
	   free(hello_message_serialized);
	   return ESP_FAIL;
	}
	if(_snapcast_stream_socket_send(TAG, sockfd, hello_message_serialized, base_message.size)<0){
		ESP_LOGI(TAG, "Failed sending hello message\r\n");
	   free(hello_message_serialized);
	   return ESP_FAIL;
	}
	//_snapcast_dispatch_event(self, tcp, NULL, 0, SNAPCAST_STREAM_STATE_CONNECTED);
	free(hello_message_serialized);
	return ESP_OK;
}

static esp_err_t _snapcast_open(audio_element_handle_t self)
{
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);
    snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);
    if (tcp->is_open) {
		ESP_LOGE(TAG, "Already opened");
		return ESP_FAIL;
	}
	tcp->is_open = true;
	_snapcast_connect_to_server(self);
    return ESP_OK;
}

static esp_err_t _snapcast_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context){
	int rlen = 0;
	int result = 0;
	static int err_cnt =0;
	snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);
start:
	result=receive_data(tcp->sock, tcp->base_buffer, BASE_MESSAGE_SIZE);
	if(result < 0){
		if(result != -2){
			close(tcp->sock);
			_snapcast_connect_to_server(self);
			ESP_LOGW(TAG, "Socket Error");
			goto start;
		}else{
			if(err_cnt>ERROR_CNT){
				err_cnt=0;
				_snapcast_dispatch_event(self, tcp, 0, 0, SNAPCAST_STREAM_STATE_TCP_SOCKET_TIMEOUT_MESSAGE);
			}
			err_cnt++;
		}
	}

	result = base_message_deserialize(&(tcp->base_message), tcp->base_buffer, BASE_MESSAGE_SIZE);
	if(result < 0){
		ESP_LOGW(TAG, "Failed to read base message: %d\r\n", result);
	}
	rlen = receive_data(tcp->sock, buffer, tcp->base_message.size+2);
	if(rlen < 0){
		if(rlen != -2){
			close(tcp->sock);
			_snapcast_connect_to_server(self);
			ESP_LOGW(TAG, "Socket Error");
			goto start;
		}else{
			if(err_cnt>ERROR_CNT){
				err_cnt=0;
				_snapcast_dispatch_event(self, tcp, 0, 0, SNAPCAST_STREAM_STATE_TCP_SOCKET_TIMEOUT_MESSAGE);
			}
			err_cnt++;
		}
		}
	return rlen;
}

static esp_err_t _snapcast_process(audio_element_handle_t self, char *in_buffer, int in_len){

	//struct timeval now, tv1;// tv2; //, last_time_sync;
	int result;
	int r_len = 1;
	int volume[] ={0, 0};
	//static int64_t diff_us=0, now_us;
	snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);
	if (gettimeofday(&tcp->now, NULL)) {
		ESP_LOGI(TAG, "Failed to gettimeofday\r\n");
	}
	r_len=_snapcast_read(self, tcp->buffer, BASE_MESSAGE_SIZE, tcp->timeout_ms, NULL);
	if(r_len > 0) {
		if(r_len > 4096){
			ESP_LOGI(TAG, "Failed Write Size  %"PRIu32, tcp->wire_chunk_message.size);
			r_len=4096;
		}else if(r_len < 0){
			return ESP_FAIL;
		}
		switch(tcp->base_message.type){
			case SNAPCAST_MESSAGE_CODEC_HEADER:
				ESP_LOGI(TAG, "SNAPCAST_MESSAGE_CODEC_HEADER");
				//tcp->bits.enabled=0;
				break;
			case SNAPCAST_MESSAGE_WIRE_CHUNK:
					// Decode wire_chunk message
					
					result = wire_chunk_message_deserialize(&(tcp->wire_chunk_message), tcp->buffer+1, tcp->base_message.size);
					if(result < 0){
						ESP_LOGI(TAG, "Failed to read wire chunk message: %d\r\n", result);
					}
					// Get the current Unix epoch time and store it in "now"
					int result = gettimeofday(&tcp->now, NULL);
					if (result) {
						ESP_LOGI(TAG, "Failed to gettimeofday\r\n");
						return ESP_FAIL;
					}
					// Server Uptime is a constant. It represents the upTime of the Snapcast server at the time of client connection.
					// The time basis of the Snapcast system is not the Unix Epoch Time but rather the uptime of the Snap Server.
					//To obtain the current uptime, the server uptime is subtracted from the current Unix Epoch time.
					//tv1 = now - tcp->server_uptime where now represents the Unix epoch time of this client system,
					// and server_uptime is the uptime of the Snap server provided to the client at boot time.
					timersub(&tcp->now,(&tcp->server_uptime) ,&tcp->tv1);
					tcp->now_us = tcp->tv1.tv_sec * 1000000;
					tcp->now_us +=tcp->tv1.tv_usec + tcp->timeDiff;

					//The audio buffer of the client is a ring buffer implemented with a linked list.
					//There are two pointers, write_rb and read_rb. write_rb points to the current list element where data can be written,
					//and read_rb points to the current element from which data can be read.

					// If tcp->write_rb exists,  the timestamp of the wire chunk message stored will be current in write_rb?
					//The timestamp represents the server uptime of the Snap server at the time the message was sent from the Snap server.
					//
					if(tcp->write_rb!=0x00){
						tcp->write_rb->timestamp  = tcp->wire_chunk_message.timestamp.sec * 1000000;
						tcp->write_rb->timestamp += tcp->wire_chunk_message.timestamp.usec;
						tcp->write_rb->data_size  = tcp->wire_chunk_message.size;

					}
					// If write_rb does not exist, something has gone seriously wrong.
					else{
						ESP_LOGE(TAG, "Something is not good with write_rb");
					}

					//The size of the message indicates whether a track has been changed
					//or if the audio track has been stopped or paused( size<20).
					//It can also happen that a message larger than the receiving buffer (SNAPCAST_STREAM_RINGBUFFER_SIZE -13) is sent by accident,
					//this needs to be handled.
					// Why -13, because the first 13 bytes of the wire_chunk messages does not content audio data
					if(tcp->write_rb->data_size<SNAPCAST_STREAM_RINGBUFFER_SIZE){
						_snapcast_dispatch_event(self, tcp, NULL, 0x00, SNAPCAST_STREAM_STATE_RUNNING);
						//tools_set_audio_volume(tcp->bh, *tcp->volume, *tcp->muted);
						if(tcp->write_rb->data_size > SNAPCAST_STREAM_BUF_SIZE - 13){
							ESP_LOGE(TAG, "Package to big, Data Size=%d", tcp->write_rb->data_size);
							return ESP_FAIL;
						}
						//writes received audio chunk to current write_rb.
						for(int x=0;x<tcp->write_rb->data_size;x++){
							tcp->write_rb->data[x]=tcp->buffer[x+13];

						}
		    			//memcpy(tcp->write_rb->data, in_buffer+13, tcp->write_rb->data_size);
						//The theory is that there should be a difference of one second or 1000000 microseconds between the timestamp of read_rb and now.
						//All snap clients are supposed to play the wire chunk message 1 second after receiving it.
						//Since all clients share the same time base (server uptime), they will play simultaneously.

						//We calculate the difference between the timestamp of the current read_rb and the current time (now) in microseconds.
						tcp->diff_us = (tcp->now_us - tcp->read_rb->timestamp)/1000 - 730;
						//ESP_LOGI(TAG, "time Diff =%lld", (tcp->diff_us));
						//If the timestamp of the current read_rb is within a certain time window, read_rb should be played.
						//There is a lower limit, which is set at 700ms.
						//Why not 1000ms? Because audio processing on the ESP32 takes approximately 200 to 300ms.
						//Why an upper limit? Quite simply, to catch chunks that are too old. Since jumping can only occur chunk by chunk (20ms),
						//an absolute limit cannot be set; hence, a time window is needed.
						if(tcp->diff_us < SNAPCAST_STREAM_LOWER_SYNC && tcp->diff_us > SNAPCAST_STREAM_UPPER_SYNC){
							//read_rb and write_rb must not be equal.
							if(tcp->read_rb!=tcp->write_rb){
								for(int x=0;x<tcp->read_rb->data_size;x++){
									tcp->audio_buffer[x]=tcp->read_rb->data[x];
								}
							}
							//send audio data to next pipline element.
							audio_element_output(self, tcp->audio_buffer, tcp->read_rb->data_size);
							// set next ring buffer read element.
							tcp->read_rb=tcp->read_rb->next;
						}
						// If current read_rb timestamp is not ithin time window.
						// Audio stream must be synchronized with the Snap server.
						else{
							//ESP_LOGW(TAG, "sync..........");
						/*	ESP_LOGE(TAG, "time Diff =%lld", tcp->diff_us /1000);
							ESP_LOGE(TAG, "now =%lld", (uint64_t)tcp->now_us /1000);
							ESP_LOGE(TAG, "Timestamp =%lld", tcp->read_rb->timestamp /1000);*/
//							//current read_rb timestamp is to new. older read buffer element is needed.
							if(tcp->diff_us < SNAPCAST_STREAM_LOWER_SYNC){
								//Need older ringbuffer element, so set the current read_rb pointer
								//to the next older ringbuffer element.
								//ESP_LOGI(TAG, "sync down.........");
								tcp->read_rb=tcp->read_rb->last;
								//current read_rb timestamp is to new. older read buffer element is needed.
							}else if(tcp->diff_us > SNAPCAST_STREAM_UPPER_SYNC){
								//ESP_LOGI(TAG, "sync up........");
								tcp->read_rb=tcp->read_rb->next->next;
							}
							//ESP_LOGI(TAG, "..........\r\n");
						}
						tcp->write_rb=tcp->write_rb->next;
					}
				break;
			case SNAPCAST_MESSAGE_SERVER_SETTINGS:
					result = server_settings_message_deserialize(&(tcp->server_settings_message), tcp->buffer+5);
					if (result) {
						ESP_LOGI(TAG, "Failed to read server settings: %d\r\n", result);
					}
					ESP_LOGI(TAG, "Buffer length: %" PRId32, tcp->server_settings_message.buffer_ms);
					ESP_LOGI(TAG, "Ringbuffer size:%" PRId32, tcp->server_settings_message.buffer_ms*48*4);
	    			ESP_LOGI(TAG, "Latency:        %"PRId32 , tcp->server_settings_message.latency);
					volume[0]=tcp->server_settings_message.volume;
					if(tcp->server_settings_message.muted==0){
						volume[1]=0;
					}else{
						volume[1]=1;
					}
					ESP_LOGI(TAG, "Mute:           %d", volume[1]);
					ESP_LOGI(TAG, "Setting volume: %d", volume[0]);
	//				ESP_LOGI(TAG, "Latency in ms:  %d", tcp->server_settings_message.latency);
					_snapcast_dispatch_event(self, tcp, (void*)volume, 4, SNAPCAST_STREAM_STATE_SERVER_SETTINGS_MESAGE);
				break;
			case SNAPCAST_MESSAGE_TIME:
					//ESP_LOGI(TAG, "SNAPCAST_MESSAGE_TIME: Buffer Read=%" PRId32, tcp->base_message.size);
					result = time_message_deserialize(&(tcp->time_message), tcp->buffer+2, TIME_MESSAGE_SIZE);
					if (result) {
						ESP_LOGI(TAG, "Failed to deserialize time message\r\n");
						break;
					}
					if (gettimeofday(&tcp->now, NULL)) {
						ESP_LOGI(TAG, "Failed to gettimeofday\r\n");
					}
					if(tcp->first_start==0){
						ESP_LOGE(TAG, "FIRST RUN !!!!!!!!!!!!!!!!");
						tcp->first_start=1;
						tcp->tv2.tv_sec = tcp->base_message.received.sec;
						tcp->tv2.tv_usec= tcp->base_message.received.usec;
						timersub(&tcp->now,&tcp->tv2,&tcp->server_uptime);
					}
					timersub(&tcp->now,(&tcp->server_uptime) ,&tcp->tv1);
					tcp->now_us = tcp->tv1.tv_sec * 1000000;
					tcp->now_us +=tcp->tv1.tv_usec;
					int64_t currentUptime = tcp->now_us;
					int64_t networkLatency = (tcp->now_us - tcp->base_message.sent.sec * 1000000 - tcp->base_message.sent.usec) / 2;
					int64_t latency_c2s = tcp->base_message.received.sec * 1000000 + tcp->base_message.received.usec -
					                      tcp->base_message.sent.sec * 1000000 - tcp->base_message.sent.usec + networkLatency;
					int64_t latency_s2c = tcp->now_us - tcp->base_message.sent.sec * 1000000 - tcp->base_message.sent.usec + networkLatency;
					int64_t timeDifference = (latency_c2s - latency_s2c) / 2;
					tcp->timeDiff = timeDifference;
					//ESP_LOGI(TAG, "Server Up Time: %" PRId64,tcp->server_uptime.tv_sec);
					//ESP_LOGI(TAG, "Timediff %" PRId64, tcp->timeDiff );
					if (timeDifference < 200000 && timeDifference > -200000) {

					}
				break;
			case SNAPCAST_MESSAGE_STREAM_TAGS:
				ESP_LOGI(TAG, "SNAPCAST_MESSAGE_STREAM_TAGS ");
				break;
			}

		/*if(size>1){

		}*/
	}/* else {
		w_size = r_len;
	}*/
	//memset(in_buffer, 0x00, 4096);
	return 1;
}

static esp_err_t _snapcast_close(audio_element_handle_t self)
{
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);
    AUDIO_NULL_CHECK(TAG, tcp, return ESP_FAIL);
    if (!tcp->is_open) {
        ESP_LOGE(TAG, "Already closed");
        return ESP_FAIL;
    }
    tcp->first_start=0;
    ESP_LOGE(TAG, "Close Snapcast Stream");
    close(tcp->sock);
    tcp->is_open = false;
    if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
        audio_element_set_byte_pos(self, 0);
    }
    return ESP_OK;
}

void snapcast_reset_first_start(audio_element_handle_t self){
	snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);
	tcp->is_open = false;
}

static esp_err_t _snapcast_destroy(audio_element_handle_t self)
{
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);
    ESP_LOGE(TAG, "Destroy Snapcast Stream");
    snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);
    snapcast_stream_delete_ringbuffer(tcp->write_rb);
    audio_free(tcp->audio_buffer);
    audio_free(tcp->base_buffer);
    audio_free(tcp->buffer);
    audio_free(tcp);
    return ESP_OK;
}

audio_element_handle_t snapcast_stream_init(snapcast_stream_cfg_t *config)
{
    AUDIO_NULL_CHECK(TAG, config, return NULL);

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    audio_element_handle_t el;
    cfg.open = _snapcast_open;
    cfg.close   = _snapcast_close;
    cfg.process = _snapcast_process;
    cfg.destroy = _snapcast_destroy;
    cfg.task_stack = config->task_stack;
    cfg.task_prio = config->task_prio;
    cfg.task_core = config->task_core;
    cfg.stack_in_ext = config->ext_stack;
    cfg.tag = "snapcast_client";
    cfg.buffer_len =0;// SNAPCAST_STREAM_BUF_SIZE;


    snapcast_stream_t *tcp = audio_calloc(1, sizeof(snapcast_stream_t));
    AUDIO_MEM_CHECK(TAG, tcp, return NULL);
    char *audio_buffer = audio_malloc(SNAPCAST_STREAM_RINGBUFFER_SIZE);
    AUDIO_MEM_CHECK(TAG, audio_buffer, return NULL);
    tcp->audio_buffer=audio_buffer;
    tcp->base_buffer = audio_malloc(BASE_MESSAGE_SIZE +4);
    tcp->audio_buffer=audio_buffer;
    tcp->buffer = audio_malloc(SNAPCAST_STREAM_BUF_SIZE);
    tcp->first_start=0;
    AUDIO_MEM_CHECK(TAG, tcp->buffer, return NULL);
    tcp->state= config->state;
    tcp->type = config->type;
    tcp->port = config->port;
    tcp->host = config->host;
    tcp->volume = config->volume;
    tcp->muted  = config->muted;
 //   tcp->bh		= config->bh;
    //tcp->event_queue = config->event_queue;
    tcp->timeout_ms = config->timeout_ms;
    if (config->event_handler) {
        tcp->hook = config->event_handler;
        if (config->event_ctx) {
            tcp->ctx = config->event_ctx;
        }
    }
    //tcp->wifi_data = config->wifi_data;
    tcp->rb=snapcast_stream_create_ringbuffer(SNAPCAST_STREAM_CUSTOM_RINGBUFFER_ELEMENTS);
    tcp->write_rb=snapcast_stream_ringbuffer_get_element(tcp->rb, SNAPCAST_STREAM_RINGBUFFER_FIRST);
    tcp->read_rb= snapcast_stream_ringbuffer_get_element(tcp->rb, SNAPCAST_STREAM_RINGBUFFER_FIRST);

    tcp->diff_buffer_counter=0;
    //tcp->mutex=xSemaphoreCreateMutex();
    //tcp->audio_buffer=0;
    tcp->timeDiff=0;
    cfg.read = _snapcast_read;
    cfg.write = NULL;
    el = audio_element_init(&cfg);
    xTaskCreatePinnedToCore(myTimerTask, "My Task", 2048, tcp, 10, NULL, 0);
    AUDIO_MEM_CHECK(TAG, el, goto _snapcast_init_exit);
    audio_element_setdata(el, tcp);

    return el;
_snapcast_init_exit:
	snapcast_stream_delete_ringbuffer(tcp->write_rb);
	audio_free(tcp->audio_buffer);
    audio_free(tcp);
    return NULL;
}
