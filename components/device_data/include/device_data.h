/*
 * device_data.h
 *
 *  Created on: 04.01.2025
 *      Author: florian
 */

#ifndef COMPONENTS_DEVICE_DATA_INCLUDE_DEVICE_DATA_H_
#define COMPONENTS_DEVICE_DATA_INCLUDE_DEVICE_DATA_H_

#include <esp_types.h>

typedef struct audio_data{
	int			volume;
	int			muted;
	int 	 	gain[20];
	int 	 	output;
	int         balance;
	uint32_t    first;
}audio_data_t;


typedef struct{
	char passwd[40];
	char ssid[40];
	char device_name[40];
	audio_data_t audio;
}device_data_t;

char* generate_json_from_device_data(const device_data_t *data);
int extract_ssid_and_passwd_from_json(const char *json_string, device_data_t *data);
void process_received_audio_json(const char *json_str, device_data_t *device_data);


#endif /* COMPONENTS_DEVICE_DATA_INCLUDE_DEVICE_DATA_H_ */
