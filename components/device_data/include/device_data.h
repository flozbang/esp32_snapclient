/*
 * device_data.h
 *
 *  Created on: 04.01.2025
 *      Author: florian
 */

#ifndef COMPONENTS_DEVICE_DATA_INCLUDE_DEVICE_DATA_H_
#define COMPONENTS_DEVICE_DATA_INCLUDE_DEVICE_DATA_H_

#include <stdbool.h>
#include <stdint.h>

#define DEVICE_MAGIC       0xDEADDEAD
#define DEVICE_ID_LENGTH   13
#define DEVICE_NAME_LENGTH 40

typedef struct audio_data {
    int volume;
    int muted;
    int gain[20];
    int output;
    int balance;
    uint32_t first;
    
} audio_data_t;

typedef struct {
    char passwd[40];
    char ssid[40];
    char mac[40];
    int WifiMode;
    audio_data_t audio;
} device_data_t;

typedef struct {
    char device_id[DEVICE_ID_LENGTH];
    char device_name[DEVICE_NAME_LENGTH];
} device_identity_t;


/* Device configuration */
bool device_data_load(device_data_t *device_data);
void device_data_save(device_data_t *device_data);
void device_data_reset_defaults(device_data_t *device_data);


/* Device identity */
bool device_identity_init(device_identity_t *identity);
bool device_identity_load_name(device_identity_t *identity);
bool device_identity_save_name(const device_identity_t *identity);
bool device_identity_set_name(device_identity_t *identity, const char *name);


/* JSON */
char *generate_json_from_device_data(const device_data_t *data, const device_identity_t *identity);
char *generate_json_for_device_name(const device_identity_t *identity);

int extract_ssid_and_passwd_from_json(const char *json_string, device_data_t *data);
int extract_device_name_from_json(const char *json_string, device_identity_t *identity);

void process_received_audio_json(const char *json_str, device_data_t *device_data);

#endif /* COMPONENTS_DEVICE_DATA_INCLUDE_DEVICE_DATA_H_ */