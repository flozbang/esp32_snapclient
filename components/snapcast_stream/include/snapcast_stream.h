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

#ifndef _SNAPCAST_CLIENT_STREAM_H_
#define _SNAPCAST_CLIENT_STREAM_H_

#include "audio_error.h"
#include "audio_element.h"
#include "esp_transport.h"
#include "snapcast.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
//#include "mDNS.h"
//#include "webserver.h"
//#include "board.h"
//#include "tools.h"
#ifdef __cplusplus
extern "C" {
#endif





typedef enum {
    SNAPCAST_STREAM_STATE_NONE,
    SNAPCAST_STREAM_STATE_CONNECTED,
	SNAPCAST_STREAM_STATE_CHANGING_SONG_MESSAGE,
	SNAPCAST_STREAM_STATE_TCP_SOCKET_TIMEOUT_MESSAGE,
	SNAPCAST_STREAM_STATE_SNTP_MESSAGE,
	SNAPCAST_STREAM_STATE_SERVER_SETTINGS_MESAGE,
	SNAPCAST_STREAM_STATE_RUNNING,
	SNAPCAST_STREAM_STATE_STOP
} snapcast_stream_status_t;




/**
 * @brief   TCP Stream massage configuration
 */
typedef struct snapcast_stream_event_msg {
    void                          *source;          /*!< Element handle */
    void                          *data;            /*!< Data of input/output */
    int                           data_len;         /*!< Data length of input/output */
    esp_transport_handle_t        sock_fd;          /*!< handle of socket*/
} snapcast_stream_event_msg_t;

typedef esp_err_t (*snapcast_stream_event_handle_cb)(snapcast_stream_event_msg_t *msg, snapcast_stream_status_t state, void *event_ctx);

/**
 * @brief   TCP Stream configuration, if any entry is zero then the configuration will be set to default values
 */
typedef struct {
    audio_stream_type_t         type;               /*!< Type of stream */
    int                         out_rb_size;        /*!< Size of output ringbuffer */
    int                         timeout_ms;         /*!< time timeout for read/write*/
    int                         port;               /*!< TCP port> */
    char                        *host;              /*!< TCP host> */
    int                         task_stack;         /*!< Task stack size */
    int                         task_core;          /*!< Task running in core (0 or 1) */
    int                         task_prio;          /*!< Task priority (based on freeRTOS priority) */
    bool                        ext_stack;          /*!< Allocate stack on extern ram */
    snapcast_stream_event_handle_cb  event_handler;      /*!< TCP stream event callback*/
    void                        *event_ctx;         /*!< User context*/
    int							state;
    int 						*volume;
    int 						*muted;
   // wifi_data_t                 *wifi_data;
    audio_element_handle_t      i2s_streamer;
//   audio_board_handle_t        bh;
    xQueueHandle 				event_queue;
} snapcast_stream_cfg_t;

/**
* @brief    TCP stream parameters
*/
#define SNAPCAST_STREAM_DEFAULT_PORT                 (8080)
#define SNAPCAST_STREAM_RINGBUFFER_SIZE          	 (4096)
#define SNAPCAST_STREAM_CUSTOM_RINGBUFFER_ELEMENTS   50
#define SNAPCAST_STREAM_CUSTOM_RINGBUFFER_SIZE   	 (SNAPCAST_STREAM_CUSTOM_RINGBUFFER_ELEMENTS * SNAPCAST_STREAM_RINGBUFFER_SIZE)
#define SNAPCAST_STREAM_TASK_STACK                   (4096)
#define SNAPCAST_STREAM_BUF_SIZE                     (4192)
#define SNAPCAST_STREAM_TASK_PRIO                	 (5)
#define SNAPCAST_STREAM_TASK_CORE                	 (1)
#define SNAPCAST_STREAM_CLIENT_NAME              	 "esp32"
#define SNAPCAST_SERVER_DEFAULT_RESPONSE_LENGTH      (512)
#define SNAPCAST_CONNECT_TIMEOUT_MS        		 	 1000
#define ENABLE									 	 1
#define DISABLE                                      0
#define SNAPCAST_STREAM_RINGBUFFER_FIRST             0
#define SNAPCAST_STREAM_LOWER_SYNC                   40
#define SNAPCAST_STREAM_UPPER_SYNC                   -40

#define SNAPCAST_STREAM_CFG_DEFAULT() {              \
    .type          = AUDIO_STREAM_READER,            \
	.out_rb_size   = SNAPCAST_STREAM_BUF_SIZE,\
    .timeout_ms    = SNAPCAST_CONNECT_TIMEOUT_MS,    \
    .port          = SNAPCAST_STREAM_DEFAULT_PORT,   \
    .host          = NULL,                           \
    .task_stack    = SNAPCAST_STREAM_TASK_STACK,     \
    .task_core     = SNAPCAST_STREAM_TASK_CORE,      \
    .task_prio     = SNAPCAST_STREAM_TASK_PRIO,      \
    .ext_stack     = true,                           \
    .event_handler = NULL,                           \
    .event_ctx     = NULL,                           \
}

/**
 * @brief       Initialize a SNAPCAST stream to/from an audio element
 *              This function creates a SNAPCAST stream to/from an audio element depending on the stream type configuration (e.g.,
 *              AUDIO_STREAM_READER or AUDIO_STREAM_WRITER). The handle of the audio element is the returned.
 *
 * @param      config The configuration
 *
 * @return     The audio element handle
 */
audio_element_handle_t snapcast_stream_init(snapcast_stream_cfg_t *config);
esp_err_t snapcast_stream_rinbuffer_reset(audio_element_handle_t self);
void snapcast_reset_first_start(audio_element_handle_t self);
#ifdef __cplusplus
}
#endif

#endif
