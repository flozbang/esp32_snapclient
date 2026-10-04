/*
 * tcp_handler.c
 *
 *  Created on: 14.01.2024
 *      Author: florian
 */
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_err.h>
#include <esp_spiffs.h>
#include <nvs_flash.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <lwip/sockets.h>



#define TAG "SOCKET_HANDLER"

// Function to create a socket and connect
int tcp_handler_connect_socket(const char *host, int port) {
   return 0;
}




