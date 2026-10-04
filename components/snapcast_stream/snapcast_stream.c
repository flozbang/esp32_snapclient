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
#define SNAPCAST_RECONNECT_DELAY_MS 1000
#define SNAPCAST_SYNC_TOLERANCE_MS 30
#define SNAPCAST_RESYNC_THRESHOLD_MS 60
#define SNAPCAST_SYNC_LOG_INTERVAL 50
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
    audio_stream_type_t type;
    char first_start;
    bool startup_sync_done;
    bool reconnect_enabled;
    int sock;
    int port;
    char *host;
    char ip_addr[16];
    bool is_open;
    int timeout_ms;
    snapcast_stream_event_handle_cb hook;
    void *ctx;
    bool received_header;
    struct timeval last_sync;
    int id_counter;
    struct timeval server_uptime;
    char *audio_buffer;
    char *base_buffer;
    char *buffer;
    int diff_buffer_counter;
    char *hostname;
    /* Audio buffer control error: actual chunk age - target playback delay. */
    int64_t sync_error_ms;

    /* Current Snapcast server uptime corrected to the server time base. */
    int64_t now_us;

    /* Clock offset between the ESP32 and Snapserver, always in microseconds. */
    int64_t time_offset_us;
    int32_t playback_delay_ms;
    bool delay_changed;
    portMUX_TYPE delay_mux;
    uint32_t sync_log_counter;
    struct timeval now;
    struct timeval tv1;
    struct timeval tv2;
    base_message_t base_message;
    codec_header_message_t codec_header_message;
    wire_chunk_message_t wire_chunk_message;
    server_settings_message_t server_settings_message;
    time_message_t time_message;
    snapcast_stream_status_t state;
    snapcast_stream_ringbuffer_node_t *rb;
    snapcast_stream_ringbuffer_node_t *write_rb;
    snapcast_stream_ringbuffer_node_t *read_rb;
    SemaphoreHandle_t socket_mutex;
    TaskHandle_t timer_task;
    volatile bool timer_task_running;
    int *volume;
    int *muted;
} snapcast_stream_t;


char base_message_serialized[BASE_MESSAGE_SIZE];
char time_message_serialized[TIME_MESSAGE_SIZE];

int event_id;

esp_err_t snapcast_stream_set_delay(audio_element_handle_t self, int32_t delay_ms) {
    if (self == NULL)
        return ESP_ERR_INVALID_ARG;

    if (delay_ms < 0) {
        ESP_LOGW(TAG, "Invalid playback delay: %" PRId32 " ms", delay_ms);
        return ESP_ERR_INVALID_ARG;
    }

    snapcast_stream_t *tcp = audio_element_getdata(self);

    if (tcp == NULL)
        return ESP_ERR_INVALID_STATE;

    portENTER_CRITICAL(&tcp->delay_mux);

    if (tcp->playback_delay_ms != delay_ms) {
        tcp->playback_delay_ms = delay_ms;
        tcp->delay_changed = true;
    }

    portEXIT_CRITICAL(&tcp->delay_mux);

    ESP_LOGI(TAG, "Playback delay set to %" PRId32 " ms", delay_ms);

    return ESP_OK;
}

int32_t snapcast_stream_get_delay(audio_element_handle_t self) {
    if (self == NULL)
        return -1;

    snapcast_stream_t *tcp = audio_element_getdata(self);

    if (tcp == NULL)
        return -1;

    portENTER_CRITICAL(&tcp->delay_mux);
    int32_t delay_ms = tcp->playback_delay_ms;
    portEXIT_CRITICAL(&tcp->delay_mux);

    return delay_ms;
}

static int snapcast_stream_ringbuffer_distance(const snapcast_stream_t *tcp, const snapcast_stream_ringbuffer_node_t *from, const snapcast_stream_ringbuffer_node_t *to) {
    if (tcp == NULL || tcp->rb == NULL || from == NULL || to == NULL)
        return -1;

    const snapcast_stream_ringbuffer_node_t *node = from;
    int ringbuffer_size = tcp->rb->ringbuffer_size;

    for (int distance = 0; distance < ringbuffer_size; distance++) {
        if (node == to)
            return distance;

        node = node->next;
    }

    return -1;
}

static snapcast_stream_ringbuffer_node_t *snapcast_stream_find_best_chunk(snapcast_stream_t *tcp, int32_t target_delay_ms, int64_t *best_diff_ms) {
    if (tcp == NULL || tcp->rb == NULL || tcp->write_rb == NULL || target_delay_ms < 0)
        return NULL;

    snapcast_stream_ringbuffer_node_t *node = tcp->rb;
    snapcast_stream_ringbuffer_node_t *best = NULL;
    int64_t smallest_error_ms = INT64_MAX;
    int64_t selected_diff_ms = 0;
    int ringbuffer_size = tcp->rb->ringbuffer_size;

    for (int i = 0; i < ringbuffer_size; i++) {
        /* write_rb is the chunk currently being filled and is not selected. */
        if (node != tcp->write_rb && node->timestamp > 0 && node->data_size > 0 &&
            node->data_size <= SNAPCAST_STREAM_RINGBUFFER_SIZE) {
            int64_t chunk_age_ms = (tcp->now_us - node->timestamp) / 1000;
            int64_t diff_ms = chunk_age_ms - target_delay_ms;
            int64_t error_ms = diff_ms >= 0 ? diff_ms : -diff_ms;

            if (error_ms < smallest_error_ms) {
                smallest_error_ms = error_ms;
                selected_diff_ms = diff_ms;
                best = node;
            }
        }

        node = node->next;
    }

    if (best_diff_ms != NULL)
        *best_diff_ms = selected_diff_ms;

    return best;
}

static void snapcast_stream_log_ringbuffer(snapcast_stream_t *tcp, int32_t playback_delay_ms) {
    if (tcp == NULL || tcp->read_rb == NULL || tcp->write_rb == NULL)
        return;

    if (++tcp->sync_log_counter < SNAPCAST_SYNC_LOG_INTERVAL)
        return;

    tcp->sync_log_counter = 0;

    int read_to_write = snapcast_stream_ringbuffer_distance(tcp, tcp->read_rb, tcp->write_rb);
    int write_to_read = snapcast_stream_ringbuffer_distance(tcp, tcp->write_rb, tcp->read_rb);
    int64_t read_age_ms = tcp->read_rb->timestamp > 0 ? (tcp->now_us - tcp->read_rb->timestamp) / 1000 : -1;
    int64_t write_age_ms = tcp->write_rb->timestamp > 0 ? (tcp->now_us - tcp->write_rb->timestamp) / 1000 : -1;

    ESP_LOGI(TAG,
             "RB read=%d write=%d read->write=%d write->read=%d read_age=%" PRId64
             " ms write_age=%" PRId64 " ms target=%" PRId32 " ms sync_error=%" PRId64 " ms",
             tcp->read_rb->position,
             tcp->write_rb->position,
             read_to_write,
             write_to_read,
             read_age_ms,
             write_age_ms,
             playback_delay_ms,
             tcp->sync_error_ms);
}

static void snapcast_timer_task_stop(snapcast_stream_t *tcp) {
    if (tcp == NULL || tcp->timer_task == NULL)
        return;

    tcp->timer_task_running = false;
    xTaskNotifyGive(tcp->timer_task);

    while (tcp->timer_task != NULL)
        vTaskDelay(pdMS_TO_TICKS(10));
}

static void snapcast_socket_close(snapcast_stream_t *tcp) {
    if (tcp == NULL || tcp->socket_mutex == NULL)
        return;

    if (xSemaphoreTake(tcp->socket_mutex, portMAX_DELAY) != pdTRUE)
        return;

    if (tcp->sock >= 0) {
        shutdown(tcp->sock, SHUT_RDWR);
        close(tcp->sock);
        tcp->sock = -1;
    }

    xSemaphoreGive(tcp->socket_mutex);
}

void tools_get_mac(char *buffer){
	unsigned char base_mac[6];
	//esp_read_mac(base_mac, ESP_MAC_WIFI_STA);
	esp_base_mac_addr_get(base_mac); 
	sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X", base_mac[0], base_mac[1], base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
	ESP_LOGI(TAG, "%02X:%02X:%02X:%02X:%02X:%02X", base_mac[0], base_mac[1], base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
}

static esp_err_t connect_to_server(int sock, const char *ip_addr, int port) {
    if (sock < 0 || ip_addr == NULL || port <= 0 || port > UINT16_MAX)
        return ESP_ERR_INVALID_ARG;

    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port)
    };

    if (inet_pton(AF_INET, ip_addr, &server.sin_addr) != 1) {
        ESP_LOGE(TAG, "Invalid IPv4 address: %s", ip_addr);
        return ESP_ERR_INVALID_ARG;
    }

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        ESP_LOGW(TAG, "connect(%s:%d) failed: %s", ip_addr, port, strerror(errno));
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*

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
*/

static int receive_data(int sock, char *buffer, int buffer_size) {
    int total_bytes_received = 0;

    if (sock < 0 || buffer == NULL || buffer_size <= 1)
        return -1;

    const int bytes_to_receive = buffer_size - 1;

    while (total_bytes_received < bytes_to_receive) {
        fd_set readfds;
        struct timeval tv = {
            .tv_sec = TIMEOUT_SEC,
            .tv_usec = 0
        };

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        int ret = select(sock + 1, &readfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR)
                continue;

            ESP_LOGW(TAG, "select() failed: %s", strerror(errno));
            return -1;
        }

        if (ret == 0) {
            ESP_LOGW(TAG, "Receive timeout: %d/%d bytes", total_bytes_received, bytes_to_receive);
            return -2;
        }

        int len = recv(sock, buffer + total_bytes_received, bytes_to_receive - total_bytes_received, 0);

        if (len > 0) {
            total_bytes_received += len;
            continue;
        }

        if (len == 0) {
            ESP_LOGW(TAG, "Connection closed by remote host");

            if (total_bytes_received == 0)
                return 0;

            ESP_LOGW(TAG, "Connection closed during message: %d/%d bytes", total_bytes_received, bytes_to_receive);
            return -1;
        }

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            continue;

        ESP_LOGW(TAG, "recv() failed: %s", strerror(errno));
        return -1;
    }

    return total_bytes_received;
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

/*
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

*/

static int _snapcast_stream_socket_send(const char *tag, int sock, const char *data, uint32_t len) {
    uint32_t total_written = 0;

    if (sock < 0 || data == NULL || len == 0)
        return -1;

    while (total_written < len) {
        fd_set writefds;
        struct timeval tv = {
            .tv_sec = TIMEOUT_SEC,
            .tv_usec = 0
        };

        FD_ZERO(&writefds);
        FD_SET(sock, &writefds);

        int ret = select(sock + 1, NULL, &writefds, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR)
                continue;

            ESP_LOGW(tag, "send select() failed: %s", strerror(errno));
            return -1;
        }

        if (ret == 0) {
            ESP_LOGW(tag, "Send timeout: %" PRIu32 "/%" PRIu32 " bytes", total_written, len);
            return -1;
        }

        int written = send(sock, data + total_written, len - total_written, 0);

        if (written > 0) {
            total_written += written;
            continue;
        }

        if (written == 0) {
            ESP_LOGW(tag, "send() returned zero");
            return -1;
        }

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            continue;

        ESP_LOGW(tag, "send() failed: %s", strerror(errno));
        return -1;
    }

    return total_written;
}

static void snapcast_timer_task(void *pv_parameters) {
    snapcast_stream_t *tcp = pv_parameters;
    uint32_t counter = 0;

    if (tcp == NULL) {
        vTaskDelete(NULL);
        return;
    }

    while (tcp->timer_task_running) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));

        if (!tcp->timer_task_running)
            break;

        if (++counter < 10)
            continue;

        counter = 0;

        struct timeval now;
        struct timeval uptime;

        if (gettimeofday(&now, NULL) != 0) {
            ESP_LOGW(TAG, "gettimeofday() failed");
            continue;
        }

        timersub(&now, &tcp->server_uptime, &uptime);

        base_message_t base_message = {
            SNAPCAST_MESSAGE_TIME,
            tcp->id_counter++,
            0,
            {uptime.tv_sec, uptime.tv_usec},
            {uptime.tv_sec, uptime.tv_usec},
            TIME_MESSAGE_SIZE
        };

        time_message_t time_message = {0};

        time_message.latency.sec  = tcp->time_offset_us / 1000000;
        time_message.latency.usec = tcp->time_offset_us - time_message.latency.sec * 1000000;

        if (base_message_serialize(&base_message, base_message_serialized, BASE_MESSAGE_SIZE) != 0) {
            ESP_LOGW(TAG, "Failed to serialize time base message");
            continue;
        }

        if (time_message_serialize(&time_message, time_message_serialized, TIME_MESSAGE_SIZE) != 0) {
            ESP_LOGW(TAG, "Failed to serialize time message");
            continue;
        }

        if (xSemaphoreTake(tcp->socket_mutex, pdMS_TO_TICKS(TIMEOUT_SEC * 1000)) != pdTRUE) {
            ESP_LOGW(TAG, "Could not lock socket");
            continue;
        }

        int sock = tcp->sock;
        int result = 0;

        if (sock >= 0 && tcp->is_open) {
            result = _snapcast_stream_socket_send(TAG, sock, base_message_serialized, BASE_MESSAGE_SIZE);

            if (result >= 0)
                result = _snapcast_stream_socket_send(TAG, sock, time_message_serialized, TIME_MESSAGE_SIZE);
        }

        xSemaphoreGive(tcp->socket_mutex);

        if (result < 0)
            ESP_LOGW(TAG, "Sending time message failed");
    }

    tcp->timer_task = NULL;
    vTaskDelete(NULL);
}


snapcast_stream_ringbuffer_node_t *snapcast_stream_new_ringbuffer(){
	snapcast_stream_ringbuffer_node_t *node = (snapcast_stream_ringbuffer_node_t *)audio_calloc(1, sizeof(snapcast_stream_ringbuffer_node_t));
	AUDIO_MEM_CHECK(TAG, node, return NULL);
	node->position = 0;
	node->ringbuffer_size = 1;
	node->last  = node;
	node->next  = node;
	return node;
}

snapcast_stream_ringbuffer_node_t *snapcast_stream_ringbuffer_add_element(snapcast_stream_ringbuffer_node_t *last_node, int pos){
	snapcast_stream_ringbuffer_node_t *new_node = (snapcast_stream_ringbuffer_node_t *)audio_calloc(1, sizeof(snapcast_stream_ringbuffer_node_t));
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
	portENTER_CRITICAL(&tcp->delay_mux);
	tcp->delay_changed = true;
	portEXIT_CRITICAL(&tcp->delay_mux);
	tcp->sync_log_counter = 0;
	tcp->startup_sync_done = false;
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

static esp_err_t _snapcast_connect_to_server(audio_element_handle_t self) {
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);
    AUDIO_NULL_CHECK(TAG, tcp, return ESP_FAIL);

    esp_err_t ret = ESP_FAIL;
    int sockfd = -1;
    int result;
    struct timeval now;
    char mac_address[18];
    char base_message_serialized[BASE_MESSAGE_SIZE];
    char *hello_message_serialized = NULL;

    /*
     * Serveradresse bestimmen.
     *
     * Aktuell wird zuerst die konfigurierte Adresse verwendet. Falls du
     * zwingend mDNS verwenden möchtest, kann dieser Block später durch
     * eine separate Discovery-Funktion ersetzt werden.
     */
    const char *server_ip = tcp->ip_addr[0] != '\0' ? tcp->ip_addr : tcp->host;
    int server_port = tcp->port > 0 ? tcp->port : 1704;

    if (server_ip == NULL || server_ip[0] == '\0') {
        ESP_LOGE(TAG, "No Snapcast server address configured");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Connecting to Snapserver %s:%d", server_ip, server_port);

    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

    if (sockfd < 0) {
        ESP_LOGE(TAG, "socket() failed: %s", strerror(errno));
        return ESP_FAIL;
    }

    /*
     * Optional, aber sinnvoll: Send- und Empfangstimeout auch direkt
     * auf dem Socket setzen.
     */
    struct timeval socket_timeout = {
        .tv_sec = TIMEOUT_SEC,
        .tv_usec = 0
    };

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &socket_timeout, sizeof(socket_timeout)) < 0)
        ESP_LOGW(TAG, "Failed to set receive timeout: %s", strerror(errno));

    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout, sizeof(socket_timeout)) < 0)
        ESP_LOGW(TAG, "Failed to set send timeout: %s", strerror(errno));

    /*
     * TCP-Keepalive ist für einen dauerhaft verbundenen Snapcast-Client
     * empfehlenswert. Die konkreten Intervalle kannst du später anpassen.
     */
    int keepalive = 1;

    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0)
        ESP_LOGW(TAG, "Failed to enable TCP keepalive: %s", strerror(errno));

    ret = connect_to_server(sockfd, server_ip, server_port);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Connection to %s:%d failed", server_ip, server_port);
        goto cleanup;
    }

    if (gettimeofday(&now, NULL) != 0) {
        ESP_LOGE(TAG, "gettimeofday() failed: %s", strerror(errno));
        goto cleanup;
    }

    tools_get_mac(mac_address);

    base_message_t base_message = {
        SNAPCAST_MESSAGE_HELLO,
        0,
        0,
        {now.tv_sec, now.tv_usec},
        {0, 0},
        0
    };

    hello_message_t hello_message = {
        mac_address,
#ifdef AI_Thinker_Dev_Kit
        "AI_Thinker",
#else
        tcp->hostname != NULL ? tcp->hostname : "ESP32",
#endif
        "0.0.2",
        "ESP32",
        "32Bit",
        "xtensa",
        1,
        mac_address,
        2
    };

    ESP_LOGI(TAG, "Serializing Hello message");

    hello_message_serialized = hello_message_serialize(&hello_message, (uint32_t *)&base_message.size);

    if (hello_message_serialized == NULL) {
        ESP_LOGE(TAG, "Failed to serialize Hello message");
        goto cleanup;
    }

    result = base_message_serialize(&base_message, base_message_serialized, BASE_MESSAGE_SIZE);

    if (result != 0) {
        ESP_LOGE(TAG, "Failed to serialize Hello base message: %d", result);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Sending Hello base message");

    result = _snapcast_stream_socket_send(TAG, sockfd, base_message_serialized, BASE_MESSAGE_SIZE);

    if (result != BASE_MESSAGE_SIZE) {
        ESP_LOGE(TAG, "Failed sending Hello base message: %d/%d bytes", result, BASE_MESSAGE_SIZE);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Sending Hello payload, size=%" PRIu32, base_message.size);

    result = _snapcast_stream_socket_send(TAG, sockfd, hello_message_serialized, base_message.size);

    if (result != (int)base_message.size) {
        ESP_LOGE(TAG, "Failed sending Hello payload: %d/%" PRIu32 " bytes", result, base_message.size);
        goto cleanup;
    }

    /*
     * Den neuen Socket erst veröffentlichen, wenn Verbindung und
     * Hello-Nachricht vollständig erfolgreich waren.
     */
    if (tcp->socket_mutex == NULL) {
        ESP_LOGE(TAG, "Socket mutex is not initialized");
        goto cleanup;
    }

    if (xSemaphoreTake(tcp->socket_mutex, pdMS_TO_TICKS(TIMEOUT_SEC * 1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to lock socket mutex");
        goto cleanup;
    }

    if (tcp->sock >= 0) {
        shutdown(tcp->sock, SHUT_RDWR);
        close(tcp->sock);
    }

    tcp->sock = sockfd;
    tcp->is_open = true;
    sockfd = -1;

    xSemaphoreGive(tcp->socket_mutex);

    ESP_LOGI(TAG, "Connected to Snapserver %s:%d", server_ip, server_port);

    ret = ESP_OK;

cleanup:
    if (hello_message_serialized != NULL) {
        free(hello_message_serialized);
        hello_message_serialized = NULL;
    }

    if (sockfd >= 0) {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
    }

    if (ret != ESP_OK)
        tcp->is_open = false;

    return ret;
}

static void snapcast_prepare_reconnect(audio_element_handle_t self) {
    snapcast_stream_t *tcp = audio_element_getdata(self);

    if (tcp == NULL)
        return;

    snapcast_socket_close(tcp);

    codec_header_message_free(&tcp->codec_header_message);
    memset(&tcp->codec_header_message, 0, sizeof(tcp->codec_header_message));

    tcp->is_open = false;
    tcp->first_start = 0;
    tcp->startup_sync_done = false;
    tcp->received_header = false;
    tcp->now_us = 0;
    tcp->time_offset_us = 0;
    tcp->sync_error_ms = 0;

    memset(&tcp->server_uptime, 0, sizeof(tcp->server_uptime));

    snapcast_stream_rinbuffer_reset(self);
}

static esp_err_t snapcast_reconnect(audio_element_handle_t self) {
    snapcast_stream_t *tcp = audio_element_getdata(self);

    if (tcp == NULL)
        return ESP_FAIL;

    snapcast_prepare_reconnect(self);

    while (tcp->reconnect_enabled) {
        ESP_LOGI(TAG, "Reconnect to Snapserver...");

        if (_snapcast_connect_to_server(self) == ESP_OK) {
            ESP_LOGI(TAG, "Reconnect successful, waiting for Snapcast time sync");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Reconnect failed, retry in %d ms", SNAPCAST_RECONNECT_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(SNAPCAST_RECONNECT_DELAY_MS));
    }

    return ESP_FAIL;
}

static esp_err_t _snapcast_open(audio_element_handle_t self) {
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    snapcast_stream_t *tcp = audio_element_getdata(self);
    AUDIO_NULL_CHECK(TAG, tcp, return ESP_FAIL);

    tcp->reconnect_enabled = true;

    if (tcp->is_open) {
        ESP_LOGW(TAG, "Snapcast stream already open");
        return ESP_OK;
    }

    esp_err_t err = _snapcast_connect_to_server(self);

    if (err != ESP_OK) {
        tcp->is_open = false;
        return err;
    }

    return ESP_OK;
}

static esp_err_t _snapcast_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context) {
    static int err_cnt = 0;
    snapcast_stream_t *tcp = audio_element_getdata(self);

    if (tcp == NULL || buffer == NULL)
        return ESP_FAIL;

    while (true) {
        int result = receive_data(tcp->sock, tcp->base_buffer, BASE_MESSAGE_SIZE);

        if (result == -2) {
            err_cnt++;

            if (err_cnt <= ERROR_CNT)
                return -2;

            ESP_LOGW(TAG, "Base message timeout limit reached, reconnecting");
            err_cnt = 0;
            snapcast_reconnect(self);
            continue;
        }

        if (result <= 0) {
            ESP_LOGW(TAG, "Base message receive failed: %d", result);
            err_cnt = 0;
            snapcast_reconnect(self);
            continue;
        }

        if (result != BASE_MESSAGE_SIZE - 1) {
            ESP_LOGW(TAG, "Incomplete base message: %d/%d bytes", result, BASE_MESSAGE_SIZE - 1);
            err_cnt = 0;
            snapcast_reconnect(self);
            continue;
        }

        err_cnt = 0;
        result = base_message_deserialize(&tcp->base_message, tcp->base_buffer, BASE_MESSAGE_SIZE);

        if (result < 0) {
            ESP_LOGW(TAG, "Failed to deserialize base message: %d", result);
            snapcast_reconnect(self);
            continue;
        }

        uint32_t receive_buffer_size = tcp->base_message.size + 2;

        if (receive_buffer_size > SNAPCAST_STREAM_BUF_SIZE) {
            ESP_LOGE(TAG, "Payload too large: size=%" PRIu32 ", buffer=%d", tcp->base_message.size, SNAPCAST_STREAM_BUF_SIZE);
            snapcast_reconnect(self);
            continue;
        }

        result = receive_data(tcp->sock, buffer, receive_buffer_size);

        if (result == -2) {
            err_cnt++;

            if (err_cnt <= ERROR_CNT)
                return -2;

            ESP_LOGW(TAG, "Payload timeout limit reached, reconnecting");
            err_cnt = 0;
            snapcast_reconnect(self);
            continue;
        }

        if (result <= 0) {
            ESP_LOGW(TAG, "Payload receive failed: %d", result);
            err_cnt = 0;
            snapcast_reconnect(self);
            continue;
        }

        if (result != (int)receive_buffer_size - 1) {
            ESP_LOGW(TAG, "Incomplete payload: %d/%" PRIu32 " bytes", result, receive_buffer_size - 1);
            err_cnt = 0;
            snapcast_reconnect(self);
            continue;
        }

        err_cnt = 0;
        return result;
    }
}

static esp_err_t _snapcast_process(audio_element_handle_t self, char *in_buffer, int in_len) {
    (void)in_buffer;
    (void)in_len;

    int result;
    int volume[] = {0, 0};
    snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);

    if (tcp == NULL)
        return ESP_FAIL;

    int r_len = _snapcast_read(self, tcp->buffer, BASE_MESSAGE_SIZE, tcp->timeout_ms, NULL);

    if (r_len == -2)
        return 1;

    if (r_len <= 0)
        return ESP_FAIL;

    switch (tcp->base_message.type) {
        case SNAPCAST_MESSAGE_CODEC_HEADER: 
		    codec_header_message_free(&tcp->codec_header_message);
		    memset(&tcp->codec_header_message, 0, sizeof(tcp->codec_header_message));
		
		    result = codec_header_message_deserialize(&tcp->codec_header_message, tcp->buffer + 1, tcp->base_message.size);
		
		    if (result != 0) {
		        ESP_LOGE(TAG, "Failed to deserialize codec header: %d", result);
		        break;
		    }
		
		    ESP_LOGI(TAG, "Codec header: codec=%s size=%" PRIu32, tcp->codec_header_message.codec, tcp->codec_header_message.size);
		
		    if (tcp->codec_header_message.size >= 4) {
		        ESP_LOGI(TAG, "Codec header start: %02X %02X %02X %02X", (uint8_t)tcp->codec_header_message.payload[0], (uint8_t)tcp->codec_header_message.payload[1], (uint8_t)tcp->codec_header_message.payload[2], (uint8_t)tcp->codec_header_message.payload[3]);
		    }
		
		    if (strcmp(tcp->codec_header_message.codec, "flac") == 0) {
		        int output_len = audio_element_output(self, tcp->codec_header_message.payload, tcp->codec_header_message.size);
		
		        if (output_len < 0) {
		            ESP_LOGE(TAG, "Codec header output failed: %d", output_len);
		            return output_len;
		        }
		
		        tcp->received_header = true;
		        ESP_LOGI(TAG, "FLAC codec header forwarded: %" PRIu32 " bytes", tcp->codec_header_message.size);
		    }
		    break;
        case SNAPCAST_MESSAGE_WIRE_CHUNK: {
            result = wire_chunk_message_deserialize(&tcp->wire_chunk_message, tcp->buffer + 1, tcp->base_message.size);

            if (result < 0) {
                ESP_LOGW(TAG, "Failed to deserialize wire chunk: %d", result);
                return ESP_FAIL;
            }

            if (gettimeofday(&tcp->now, NULL) != 0) {
                ESP_LOGW(TAG, "gettimeofday() failed");
                return ESP_FAIL;
            }

            timersub(&tcp->now, &tcp->server_uptime, &tcp->tv1);
            tcp->now_us = (int64_t)tcp->tv1.tv_sec * 1000000LL + tcp->tv1.tv_usec + tcp->time_offset_us;

            if (tcp->write_rb == NULL || tcp->read_rb == NULL) {
                ESP_LOGE(TAG, "Ringbuffer pointers are invalid");
                return ESP_FAIL;
            }

            int data_size = tcp->wire_chunk_message.size;
			
			if (data_size <= 0 || (size_t)data_size > sizeof(tcp->write_rb->data)) {
			    ESP_LOGE(TAG, "Invalid wire chunk size: %d, destination capacity=%u",
			             data_size, (unsigned)sizeof(tcp->write_rb->data));
			    break;
			}
			
			tcp->write_rb->timestamp = (int64_t)tcp->wire_chunk_message.timestamp.sec * 1000000LL +
			                           tcp->wire_chunk_message.timestamp.usec;
			tcp->write_rb->data_size = data_size;
			memcpy(tcp->write_rb->data, tcp->buffer + 13, (size_t)data_size);

            if (tcp->first_start == 0) {
                tcp->write_rb = tcp->write_rb->next;
                break;
            }

            _snapcast_dispatch_event(self, tcp, NULL, 0, SNAPCAST_STREAM_STATE_RUNNING);
            int32_t playback_delay_ms;
            bool delay_changed;
            
            playback_delay_ms = tcp->playback_delay_ms;
            delay_changed = tcp->delay_changed;
            tcp->delay_changed = false;

            if (!tcp->startup_sync_done) {
                int64_t startup_diff_ms = 0;
                snapcast_stream_ringbuffer_node_t *startup_node = snapcast_stream_find_best_chunk(tcp, playback_delay_ms, &startup_diff_ms);

                if (startup_node == NULL) {
                    tcp->write_rb = tcp->write_rb->next;
                    break;
                }

                tcp->read_rb = startup_node;
                tcp->sync_error_ms = startup_diff_ms;
                tcp->startup_sync_done = true;
                tcp->delay_changed = false;
                ESP_LOGI(TAG, "Startup sync: target=%" PRId32 " ms node=%d sync_error=%" PRId64 " ms", playback_delay_ms, startup_node->position, startup_diff_ms);
            }

            if (delay_changed && tcp->startup_sync_done) {
                int64_t selected_diff_ms = 0;
                snapcast_stream_ringbuffer_node_t *best = snapcast_stream_find_best_chunk(tcp, playback_delay_ms, &selected_diff_ms);

                int64_t selected_abs_diff_ms = selected_diff_ms >= 0 ? selected_diff_ms : -selected_diff_ms;

               if (best != NULL && selected_abs_diff_ms <= SNAPCAST_RESYNC_THRESHOLD_MS) {
                    tcp->read_rb = best;
                    tcp->sync_error_ms = selected_diff_ms;
                    ESP_LOGI(TAG, "Delay resync: target=%" PRId32 " ms node=%d sync_error=%" PRId64 " ms",
                             playback_delay_ms, best->position, selected_diff_ms);
                } else {
                    
                    tcp->delay_changed = true;
                   
                    ESP_LOGD(TAG, "Delay resync deferred: target=%" PRId32 " ms best_diff=%" PRId64 " ms",
                             playback_delay_ms, selected_diff_ms);
               }
            }

            /*if (tcp->read_rb->timestamp > 0 && tcp->read_rb->data_size > 0) {
                tcp->sync_error_ms = (tcp->now_us - tcp->read_rb->timestamp) / 1000 - playback_delay_ms;


                if (tcp->read_rb != tcp->write_rb && tcp->read_rb->timestamp > 0 && tcp->read_rb->data_size > 0) {
                    int64_t abs_diff_after_resync = tcp->sync_error_ms >= 0 ? tcp->sync_error_ms : -tcp->sync_error_ms;

                    if (abs_diff_after_resync <= SNAPCAST_RESYNC_THRESHOLD_MS) {
                        memcpy(tcp->audio_buffer, tcp->read_rb->data, tcp->read_rb->data_size);
                        audio_element_output(self, tcp->audio_buffer, tcp->read_rb->data_size);
                        tcp->read_rb = tcp->read_rb->next;
                    }
                }
            }*/

            if (tcp->read_rb->timestamp > 0 && tcp->read_rb->data_size > 0 && tcp->read_rb != tcp->write_rb) {
                tcp->sync_error_ms =
                    (tcp->now_us - tcp->read_rb->timestamp) / 1000 -
                    playback_delay_ms;

                if (tcp->sync_error_ms < -SNAPCAST_SYNC_TOLERANCE_MS) {
                    /*
                     * Chunk ist zu jung: einen älteren Ringbuffer-Eintrag wählen.
                     * Es werden keine PCM-Daten wiederholt oder ausgegeben.
                     */
                    snapcast_stream_ringbuffer_node_t *older = tcp->read_rb->last;

                    if (older != tcp->write_rb &&
                        older->timestamp > 0 &&
                        older->data_size > 0) {
                        tcp->read_rb = older;

                        ESP_LOGD(TAG,
                                 "Sync early: move back to node=%d error=%" PRId64 " ms",
                                 tcp->read_rb->position,
                                 tcp->sync_error_ms);
                    }
                } else if (tcp->sync_error_ms > SNAPCAST_SYNC_TOLERANCE_MS) {
                    /*
                     * Chunk ist zu alt: einen Chunk überspringen.
                     * Normal wäre read_rb = read_rb->next; durch next->next
                     * wird effektiv genau ein alter Chunk verworfen.
                     */
                    snapcast_stream_ringbuffer_node_t *newer = tcp->read_rb->next->next;

                    if (newer != tcp->write_rb &&
                        newer->timestamp > 0 &&
                        newer->data_size > 0) {
                        tcp->read_rb = newer;

                        ESP_LOGD(TAG,
                                 "Sync late: skip to node=%d error=%" PRId64 " ms",
                                 tcp->read_rb->position,
                                 tcp->sync_error_ms);
                    }
                } else {
                    /* Chunk liegt im gültigen Zeitfenster: genau einmal ausgeben. */
                    memcpy(tcp->audio_buffer,
                           tcp->read_rb->data,
                           tcp->read_rb->data_size);

                    int output_len = audio_element_output(self,
                                                          tcp->audio_buffer,
                                                          tcp->read_rb->data_size);

                    if (output_len < 0) {
                        ESP_LOGW(TAG, "Audio output failed: %d", output_len);
                        return output_len;
                    }

                    tcp->read_rb = tcp->read_rb->next;
                }
            }

            snapcast_stream_log_ringbuffer(tcp, playback_delay_ms);
            tcp->write_rb = tcp->write_rb->next;
            break;
        }

        case SNAPCAST_MESSAGE_SERVER_SETTINGS:
            result = server_settings_message_deserialize(&tcp->server_settings_message, tcp->buffer + 5);

            if (result) {
                ESP_LOGW(TAG, "Failed to deserialize server settings: %d", result);
                break;
            }

            ESP_LOGI(TAG, "Buffer length: %" PRId32, tcp->server_settings_message.buffer_ms);
            ESP_LOGI(TAG, "Ringbuffer size: %" PRId32, tcp->server_settings_message.buffer_ms * 48 * 4);
            ESP_LOGI(TAG, "Latency: %" PRId32, tcp->server_settings_message.latency);

            volume[0] = tcp->server_settings_message.volume;
            volume[1] = tcp->server_settings_message.muted ? 1 : 0;
            _snapcast_dispatch_event(self, tcp, volume, sizeof(volume), SNAPCAST_STREAM_STATE_SERVER_SETTINGS_MESAGE);
            break;

        case SNAPCAST_MESSAGE_TIME: {
            result = time_message_deserialize(&tcp->time_message, tcp->buffer + 2, TIME_MESSAGE_SIZE);

            if (result) {
                ESP_LOGW(TAG, "Failed to deserialize time message: %d", result);
                break;
            }

            if (gettimeofday(&tcp->now, NULL) != 0) {
                ESP_LOGW(TAG, "gettimeofday() failed");
                break;
            }

            if (tcp->first_start == 0) {
                tcp->first_start = 1;
                tcp->tv2.tv_sec = tcp->base_message.received.sec;
                tcp->tv2.tv_usec = tcp->base_message.received.usec;
                timersub(&tcp->now, &tcp->tv2, &tcp->server_uptime);
                ESP_LOGI(TAG, "Initial Snapcast time synchronization complete");
            }

            timersub(&tcp->now, &tcp->server_uptime, &tcp->tv1);
            tcp->now_us = (int64_t)tcp->tv1.tv_sec * 1000000LL + tcp->tv1.tv_usec;

            int64_t sent_us = (int64_t)tcp->base_message.sent.sec * 1000000LL + tcp->base_message.sent.usec;
            int64_t received_us = (int64_t)tcp->base_message.received.sec * 1000000LL + tcp->base_message.received.usec;
            int64_t network_latency_us = (tcp->now_us - sent_us) / 2;
            int64_t latency_c2s_us = received_us - sent_us + network_latency_us;
            int64_t latency_s2c_us = tcp->now_us - sent_us + network_latency_us;
            int64_t time_difference_us = (latency_c2s_us - latency_s2c_us) / 2;

            if (time_difference_us < 200000 && time_difference_us > -200000) {
                tcp->time_offset_us = time_difference_us;
                ESP_LOGD(TAG, "Time offset updated: %" PRId64 " us", tcp->time_offset_us);
            }

            break;
        }

        case SNAPCAST_MESSAGE_STREAM_TAGS:
            ESP_LOGI(TAG, "SNAPCAST_MESSAGE_STREAM_TAGS");
            break;

        default:
            break;
    }

    return 1;
}

static esp_err_t _snapcast_close(audio_element_handle_t self) {
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    snapcast_stream_t *tcp = audio_element_getdata(self);
    AUDIO_NULL_CHECK(TAG, tcp, return ESP_FAIL);

    tcp->reconnect_enabled = false;
    tcp->first_start = 0;
    tcp->startup_sync_done = false;
    tcp->is_open = false;

    ESP_LOGI(TAG, "Close Snapcast Stream");
    snapcast_socket_close(tcp);

    if (AEL_STATE_PAUSED != audio_element_get_state(self))
        audio_element_set_byte_pos(self, 0);

    return ESP_OK;
}

void snapcast_reset_first_start(audio_element_handle_t self){
	snapcast_stream_t *tcp = (snapcast_stream_t *)audio_element_getdata(self);
	tcp->first_start = 0;
	tcp->startup_sync_done = false;
	tcp->is_open = false;
}

static esp_err_t _snapcast_destroy(audio_element_handle_t self) {
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    snapcast_stream_t *tcp = audio_element_getdata(self);

    if (tcp == NULL)
        return ESP_OK;

    ESP_LOGI(TAG, "Destroy Snapcast Stream");

    tcp->reconnect_enabled = false;
    tcp->is_open = false;

    snapcast_timer_task_stop(tcp);
    snapcast_socket_close(tcp);

    if (tcp->socket_mutex != NULL) {
        vSemaphoreDelete(tcp->socket_mutex);
        tcp->socket_mutex = NULL;
    }

    if (tcp->rb != NULL) {
        snapcast_stream_delete_ringbuffer(tcp->rb);
        tcp->rb = NULL;
    }
	codec_header_message_free(&tcp->codec_header_message);
    audio_free(tcp->audio_buffer);
    audio_free(tcp->base_buffer);
    audio_free(tcp->buffer);
    audio_free(tcp);

    audio_element_setdata(self, NULL);

    return ESP_OK;
}

audio_element_handle_t snapcast_stream_init(snapcast_stream_cfg_t *config) {
    AUDIO_NULL_CHECK(TAG, config, return NULL);

    audio_element_handle_t el = NULL;
    snapcast_stream_t *tcp = audio_calloc(1, sizeof(snapcast_stream_t));

    AUDIO_MEM_CHECK(TAG, tcp, return NULL);

    tcp->sock = -1;
    tcp->reconnect_enabled = true;
    tcp->timer_task = NULL;
    tcp->timer_task_running = false;

    tcp->socket_mutex = xSemaphoreCreateMutex();

    if (tcp->socket_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create socket mutex");
        goto cleanup_tcp;
    }

    tcp->audio_buffer = audio_malloc(SNAPCAST_STREAM_RINGBUFFER_SIZE);

    if (tcp->audio_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        goto cleanup_mutex;
    }

    tcp->base_buffer = audio_malloc(BASE_MESSAGE_SIZE + 4);

    if (tcp->base_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate base buffer");
        goto cleanup_audio_buffer;
    }

    tcp->buffer = audio_malloc(SNAPCAST_STREAM_BUF_SIZE);

    if (tcp->buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate stream buffer");
        goto cleanup_base_buffer;
    }

    tcp->rb = snapcast_stream_create_ringbuffer(SNAPCAST_STREAM_CUSTOM_RINGBUFFER_ELEMENTS);

    if (tcp->rb == NULL) {
        ESP_LOGE(TAG, "Failed to create Snapcast ringbuffer");
        goto cleanup_stream_buffer;
    }

    tcp->write_rb = snapcast_stream_ringbuffer_get_element(tcp->rb, SNAPCAST_STREAM_RINGBUFFER_FIRST);
    tcp->read_rb = snapcast_stream_ringbuffer_get_element(tcp->rb, SNAPCAST_STREAM_RINGBUFFER_FIRST);

    tcp->first_start = 0;
    tcp->startup_sync_done = false;
    tcp->state = config->state;
    tcp->type = config->type;
    tcp->port = config->port;
    tcp->host = config->host;
    tcp->volume = config->volume;
    tcp->muted = config->muted;
    tcp->hostname = config->hostname;
    tcp->playback_delay_ms = config->playback_delay_ms;
    tcp->delay_changed = true;
    tcp->delay_mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    tcp->sync_log_counter = 0;
    tcp->timeout_ms = config->timeout_ms;
    tcp->sync_error_ms = 0;
    tcp->time_offset_us = 0;

    if (config->event_handler != NULL) {
        tcp->hook = config->event_handler;
        tcp->ctx = config->event_ctx;
    }

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();

    cfg.open = _snapcast_open;
    cfg.close = _snapcast_close;
    cfg.process = _snapcast_process;
    cfg.destroy = _snapcast_destroy;
    cfg.read = _snapcast_read;
    cfg.write = NULL;
    cfg.task_stack = config->task_stack;
    cfg.task_prio = config->task_prio;
    cfg.task_core = config->task_core;
    cfg.stack_in_ext = config->ext_stack;
    cfg.tag = "snapcast_client";
    cfg.buffer_len = 0;

    el = audio_element_init(&cfg);

    if (el == NULL) {
        ESP_LOGE(TAG, "audio_element_init() failed");
        goto cleanup_ringbuffer;
    }

    audio_element_setdata(el, tcp);

    tcp->timer_task_running = true;

    BaseType_t task_result = xTaskCreatePinnedToCore(snapcast_timer_task, "snap_time", 3072, tcp, 10, &tcp->timer_task, 0);

    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Snapcast timer task");
        tcp->timer_task_running = false;
        audio_element_deinit(el);
        return NULL;
    }

    return el;

cleanup_ringbuffer:
    snapcast_stream_delete_ringbuffer(tcp->rb);

cleanup_stream_buffer:
    audio_free(tcp->buffer);

cleanup_base_buffer:
    audio_free(tcp->base_buffer);

cleanup_audio_buffer:
    audio_free(tcp->audio_buffer);

cleanup_mutex:
    vSemaphoreDelete(tcp->socket_mutex);

cleanup_tcp:
    audio_free(tcp);
    return NULL;
}
