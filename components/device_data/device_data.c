/*
 * device_data.c
 *
 *  Created on: 04.01.2025
 *      Author: florian
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_mac.h"

#include "cJSON.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "device_data.h"


static const char *TAG = "device_data";


char *generate_json_for_device_name(const device_identity_t *identity)
{
    cJSON *root;
    char *json_str;

    if (identity == NULL) {
        return NULL;
    }

    root = cJSON_CreateObject();

    if (root == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "type", 1);
    cJSON_AddStringToObject(root, "device-id", identity->device_id);
    cJSON_AddStringToObject(root, "device-name", identity->device_name);

    json_str = cJSON_Print(root);

    cJSON_Delete(root);

    return json_str;
}


char *generate_json_from_device_data(const device_data_t *data, const device_identity_t *identity)
{
    cJSON *root;
    char *json_string;

    if ((data == NULL) || (identity == NULL)) {
        return NULL;
    }

    root = cJSON_CreateObject();

    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return NULL;
    }

    cJSON_AddStringToObject(root, "device-id", identity->device_id);
    cJSON_AddStringToObject(root, "device-name", identity->device_name);

    json_string = cJSON_PrintUnformatted(root);

    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON string");
    }

    cJSON_Delete(root);

    return json_string;
}


int extract_device_name_from_json(const char *json_string, device_identity_t *identity)
{
    cJSON *root;
    cJSON *device_name;

    if ((json_string == NULL) || (identity == NULL)) {
        return -1;
    }

    root = cJSON_Parse(json_string);

    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return -1;
    }

    device_name = cJSON_GetObjectItem(root, "device");

    if (!cJSON_IsString(device_name) || (device_name->valuestring == NULL)) {
        ESP_LOGW(TAG, "No valid 'device-name' found in JSON");
        cJSON_Delete(root);
        return -1;
    }

    if (!device_identity_set_name(identity, device_name->valuestring)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);

    return 0;
}


int extract_ssid_and_passwd_from_json(const char *json_string, device_data_t *data)
{
    cJSON *root;
    cJSON *ssid_item;
    cJSON *passwd_item;

    if ((json_string == NULL) || (data == NULL)) {
        return -1;
    }

    root = cJSON_Parse(json_string);

    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return -1;
    }

    ssid_item = cJSON_GetObjectItem(root, "ssid");
    passwd_item = cJSON_GetObjectItem(root, "password");

    if ((ssid_item == NULL) || !cJSON_IsString(ssid_item)) {
        ESP_LOGE(TAG, "SSID field missing or invalid");
        cJSON_Delete(root);
        return -1;
    }

    if ((passwd_item == NULL) || !cJSON_IsString(passwd_item)) {
        ESP_LOGE(TAG, "Password field missing or invalid");
        cJSON_Delete(root);
        return -1;
    }

    strncpy(data->ssid, ssid_item->valuestring, sizeof(data->ssid) - 1);
    data->ssid[sizeof(data->ssid) - 1] = '\0';

    strncpy(data->passwd, passwd_item->valuestring, sizeof(data->passwd) - 1);
    data->passwd[sizeof(data->passwd) - 1] = '\0';

    cJSON_Delete(root);

    return 0;
}


void process_received_audio_json(const char *json_str, device_data_t *data)
{
    cJSON *root;
    cJSON *type_item;
    cJSON *audio_obj;

    if ((json_str == NULL) || (data == NULL)) {
        return;
    }

    root = cJSON_Parse(json_str);

    if (root == NULL) {
        ESP_LOGE(TAG, "Error parsing audio JSON");
        return;
    }

    if (!cJSON_HasObjectItem(root, "type")) {
        cJSON_Delete(root);
        return;
    }

    type_item = cJSON_GetObjectItem(root, "type");

    if ((type_item == NULL) || (type_item->valueint != 5)) {
        cJSON_Delete(root);
        return;
    }

    audio_obj = cJSON_GetObjectItem(root, "sliderValues");

    if (audio_obj == NULL) {
        cJSON_Delete(root);
        return;
    }

    for (int i = 0; i < 10; i++) {
        data->audio.gain[i] = cJSON_GetArrayItem(audio_obj, i)->valueint;
        data->audio.gain[i + 10] = cJSON_GetArrayItem(audio_obj, i)->valueint;
    }

    data->audio.volume = cJSON_GetArrayItem(audio_obj, 10)->valueint;
    data->audio.muted = cJSON_GetObjectItem(root, "muted")->valueint;
    data->audio.balance = cJSON_GetObjectItem(root, "balance")->valueint;
    data->audio.output = cJSON_GetObjectItem(root, "output")->valueint;

    cJSON_Delete(root);
}


void device_data_save(device_data_t *device_data)
{
    nvs_handle_t nvs;
    esp_err_t err;

    if (device_data == NULL) {
        return;
    }

    err = nvs_open("config", NVS_READWRITE, &nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open config NVS: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs, "devdata", device_data, sizeof(device_data_t));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save device data: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return;
    }

    err = nvs_commit(nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit device data: %s", esp_err_to_name(err));
    }

    nvs_close(nvs);
}


bool device_data_load(device_data_t *device_data)
{
    nvs_handle_t nvs;
    esp_err_t err;
    size_t stored_size;
    size_t size;

    if (device_data == NULL) {
        return false;
    }

    err = nvs_open("config", NVS_READONLY, &nvs);

    if (err != ESP_OK) {
        return false;
    }

    stored_size = 0;

    err = nvs_get_blob(nvs, "devdata", NULL, &stored_size);

    if ((err != ESP_OK) || (stored_size != sizeof(device_data_t))) {
        nvs_close(nvs);
        return false;
    }

    size = sizeof(device_data_t);

    err = nvs_get_blob(nvs, "devdata", device_data, &size);

    nvs_close(nvs);

    if ((err != ESP_OK) || (size != sizeof(device_data_t))) {
        return false;
    }

    if (device_data->audio.first != DEVICE_MAGIC) {
        return false;
    }

    return true;
}


void device_data_reset_defaults(device_data_t *device_data)
{
    if (device_data == NULL) {
        return;
    }

    memset(device_data, 0, sizeof(device_data_t));

    strncpy(device_data->passwd, "mega1720", sizeof(device_data->passwd) - 1);
    device_data->passwd[sizeof(device_data->passwd) - 1] = '\0';

    strncpy(device_data->ssid, "OpenWrt", sizeof(device_data->ssid) - 1);
    device_data->ssid[sizeof(device_data->ssid) - 1] = '\0';

    device_data->audio.volume = 76;
    device_data->audio.muted = 0;

    memset(device_data->audio.gain, 0, sizeof(device_data->audio.gain));

    device_data->audio.output = 3;
    device_data->audio.balance = 0;
    device_data->WifiMode = 0;
    device_data->audio.first = DEVICE_MAGIC;

    device_data_save(device_data);
}


bool device_identity_save_name(const device_identity_t *identity)
{
    nvs_handle_t nvs;
    esp_err_t err;

    if (identity == NULL) {
        return false;
    }

    err = nvs_open("identity", NVS_READWRITE, &nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open identity NVS: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(nvs, "device_name", identity->device_name);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save device name: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return false;
    }

    err = nvs_commit(nvs);

    nvs_close(nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit device name: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}


bool device_identity_load_name(device_identity_t *identity)
{
    nvs_handle_t nvs;
    esp_err_t err;
    size_t size;

    if (identity == NULL) {
        return false;
    }

    err = nvs_open("identity", NVS_READONLY, &nvs);

    if (err != ESP_OK) {
        return false;
    }

    size = sizeof(identity->device_name);

    err = nvs_get_str(nvs, "device_name", identity->device_name, &size);

    nvs_close(nvs);

    if (err != ESP_OK) {
        return false;
    }

    return true;
}


bool device_identity_set_name(device_identity_t *identity, const char *name)
{
    size_t len;

    if ((identity == NULL) || (name == NULL)) {
        return false;
    }

    len = strlen(name);

    if ((len == 0) || (len >= sizeof(identity->device_name))) {
        ESP_LOGE(TAG, "Invalid device name");
        return false;
    }

    strncpy(identity->device_name, name, sizeof(identity->device_name) - 1);
    identity->device_name[sizeof(identity->device_name) - 1] = '\0';

    return device_identity_save_name(identity);
}


bool device_identity_init(device_identity_t *identity)
{
    uint8_t mac[6];
    esp_err_t err;

    if (identity == NULL) {
        return false;
    }

    memset(identity, 0, sizeof(device_identity_t));

    err = esp_read_mac(mac, ESP_MAC_BASE);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ESP32 base MAC: %s", esp_err_to_name(err));
        return false;
    }

    snprintf(identity->device_id, sizeof(identity->device_id), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (!device_identity_load_name(identity)) {
        snprintf(identity->device_name, sizeof(identity->device_name), "esp-audio-%02X%02X%02X", mac[3], mac[4], mac[5]);

        if (!device_identity_save_name(identity)) {
            return false;
        }

        ESP_LOGI(TAG, "Created default device name");
    }

    ESP_LOGI(TAG, "Device ID   : %s", identity->device_id);
    ESP_LOGI(TAG, "Device name : %s", identity->device_name);

    return true;
}