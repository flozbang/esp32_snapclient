/*
 * json_rpc.h
 *
 *  Created on: 11.09.2025
 *      Author: florian
 */

#ifndef COMPONENTS_JSON_RPC_INCLUDE_JSON_RPC_H_
#define COMPONENTS_JSON_RPC_INCLUDE_JSON_RPC_H_

#include "device_data.h"

#define TCP_PORT 25438
#define BUF_SIZE 128

typedef enum {
    GET_VOLUME = 1,
    SET_VOLUME,

    GET_MUTE,
    SET_MUTE,

    GET_BALANCE,
    SET_BALANCE,

    GET_MODE,
    SET_MODE,

    GET_EQ_1,
    SET_EQ_1,

    GET_EQ_2,
    SET_EQ_2,

    GET_EQ_3,
    SET_EQ_3,

    GET_EQ_4,
    SET_EQ_4,

    GET_EQ_5,
    SET_EQ_5,

    GET_EQ_6,
    SET_EQ_6,

    GET_EQ_7,
    SET_EQ_7,

    GET_EQ_8,
    SET_EQ_8,

    GET_EQ_9,
    SET_EQ_9,

    GET_EQ_10,
    SET_EQ_10,

    GET_DELAY,
    SET_DELAY
} rpc_method_t;

typedef struct {
    char method[32];
    int value; // -1, falls nicht vorhanden
    int id;
	int is_set;
} rpc_request_t;

typedef void (*rpc_event_handler_cb)(rpc_method_t state, void *event);

typedef struct  {
	device_data_t *device_data;
	int64_t       *time_delay;
} json_rpc_config_t;


void tcp_server_start();
void tcp_server_task(void *pvParameters);
void json_rpc_init(json_rpc_config_t *config , rpc_event_handler_cb cb);


#endif /* COMPONENTS_JSON_RPC_INCLUDE_JSON_RPC_H_ */
