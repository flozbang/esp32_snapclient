/*
 * sntp_handler.h
 *
 *  Created on: 12.08.2022
 *      Author: florian
 */

#ifndef MAIN_SNTP_CLIENT_H_
#define MAIN_SNTP_CLIENT_H_



#include "esp_err.h"
#define MY_TZ "CET-1CEST,M3.5.0/02,M10.5.0/03"
void initialize_sntp(void (*sync_callback)(void));
void set_system_time();
//void sntp_stop(void);

#endif /* MAIN_SNTP_CLIENT_H_ */
