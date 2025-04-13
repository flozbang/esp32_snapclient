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

#ifndef _CROSSOVER_STREAM_H_
#define _CROSSOVER_STREAM_H_

#include "audio_error.h"
#include "audio_element.h"
#include "esp_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	CROSSOVER_STATE_DEFAULT,
    CROSSOVER_STATE_LEFT_CHANNEL,
    CROSSOVER_STATE_RIGHT_CHANNEL,
	CROSSOVER_STATE_STEREO,
	CROSSOVER_STATE_SUBWOOFER
} crossover_status_t;

/**
 * @brief   TCP Stream massage configuration
 */
typedef struct crossover_event_msg {
    void                          *source;          /*!< Element handle */
    void                          *data;            /*!< Data of input/output */
    int                           data_len;         /*!< Data length of input/output */
    esp_transport_handle_t        sock_fd;          /*!< handle of socket*/
} crossover_event_msg_t;

typedef esp_err_t (*crossover_event_handle_cb)(crossover_event_msg_t *msg, crossover_status_t state, void *event_ctx);

/**
 * @brief   TCP Stream configuration, if any entry is zero then the configuration will be set to default values
 */
typedef struct {
    audio_stream_type_t         type;               /*!< Type of stream */
    int                         timeout_ms;         /*!< time timeout for read/write*/
    int                         port;               /*!< TCP port> */
    char                        *host;              /*!< TCP host> */
    int                         task_stack;         /*!< Task stack size */
    int                         task_core;          /*!< Task running in core (0 or 1) */
    int                         task_prio;          /*!< Task priority (based on freeRTOS priority) */
    bool                        ext_stack;          /*!< Allocate stack on extern ram */
    crossover_event_handle_cb  event_handler;      /*!< TCP stream event callback*/
    void                        *event_ctx;         /*!< User context*/
    int							*output;
    int							*balance;
    int							*volume;
    int 						*mute;
} crossover_cfg_t;

/**
* @brief    TCP stream parameters
*/
#define CROSSOVER_DEFAULT_PORT             (8080)

#define CROSSOVER_TASK_STACK               (3072)
#define CROSSOVER_BUF_SIZE                 (4096)
#define CROSSOVER_TASK_PRIO                (5)
#define CROSSOVER_TASK_CORE                (0)
#define SUBWOOFER						   (1)
#define NOT_SUBWOOFER                      (0)
#define TCP_SERVER_DEFAULT_RESPONSE_LENGTH  (512)

#define CROSSOVER_CFG_DEFAULT() {              \
    .type          = AUDIO_STREAM_READER,       \
    .timeout_ms    = 30 *1000,                  \
    .port          = CROSSOVER_DEFAULT_PORT,   \
    .host          = NULL,                      \
    .task_stack    = CROSSOVER_TASK_STACK,     \
    .task_core     = CROSSOVER_TASK_CORE,      \
    .task_prio     = CROSSOVER_TASK_PRIO,      \
    .ext_stack     = true,                      \
    .event_handler = NULL,                      \
    .event_ctx     = NULL,                      \
}



struct iir_filt {
   float in_z1;
   float in_z2;
   float out_z1;
   float out_z2;
   float a0[4];
   float a1[4];
   float a2[4];
   float b1[4];
   float b2[4];
};




/**
 * @brief       Initialize a TCP stream to/from an audio element 
 *              This function creates a TCP stream to/from an audio element depending on the stream type configuration (e.g., 
 *              AUDIO_STREAM_READER or AUDIO_STREAM_WRITER). The handle of the audio element is the returned.
 *
 * @param      config The configuration
 *
 * @return     The audio element handle
 */
audio_element_handle_t crossover_init(crossover_cfg_t *config);

#ifdef __cplusplus
}
#endif

#endif
