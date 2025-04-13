/*
 * device_data.c
 *
 *  Created on: 04.01.2025
 *      Author: florian
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include "cJSON.h"
#include "device_data.h"

static const char *TAG = "device_data";


char* generate_json_from_device_data(const device_data_t *data) {
    cJSON *root = cJSON_CreateObject();
	// Allgemeine Gerätedaten
    cJSON_AddNumberToObject(root, "type", 7);
    // Audio-Daten
    cJSON *audio = cJSON_CreateObject();
    cJSON_AddNumberToObject(audio, "volume", data->audio.volume);
    cJSON_AddNumberToObject(audio, "muted", data->audio.muted);

    // Gain-Array
    cJSON *gain_array = cJSON_CreateArray();
    for (int i = 0; i < 20; i++) {
        cJSON_AddItemToArray(gain_array, cJSON_CreateNumber(data->audio.gain[i]));
    }
    cJSON_AddItemToObject(audio, "gain", gain_array);

    cJSON_AddNumberToObject(audio, "output", data->audio.output);
    cJSON_AddNumberToObject(audio, "balance", data->audio.balance);
    cJSON_AddNumberToObject(audio, "first", data->audio.first);

    // Audio-Daten dem Root-Objekt hinzufügen
    cJSON_AddItemToObject(root, "audio", audio);

    // JSON in String umwandeln
    char *json_str = cJSON_Print(root);

    // Speicher freigeben
    cJSON_Delete(root);

    return json_str;
}

int extract_ssid_and_passwd_from_json(const char *json_string, device_data_t *data) {
    if (json_string == NULL || data == NULL) {
        return -1; // Fehler: ungültige Eingabe
    }

    // JSON-String parsen
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        printf("Fehler beim Parsen des JSON-Strings\n");
        return -1; // Fehler beim Parsen
    }

    // SSID und Passwort extrahieren
    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *passwd_item = cJSON_GetObjectItem(root, "password");

    if (ssid_item != NULL && cJSON_IsString(ssid_item)) {
        strncpy(data->ssid, ssid_item->valuestring, sizeof(data->ssid) - 1);
        data->ssid[sizeof(data->ssid) - 1] = '\0'; // Sicherstellen, dass der String null-terminiert ist
    } else {
        printf("Fehler: SSID-Feld fehlt oder ist nicht vom Typ string\n");
        cJSON_Delete(root);
        return -1; // Fehler
    }

    if (passwd_item != NULL && cJSON_IsString(passwd_item)) {
        strncpy(data->passwd, passwd_item->valuestring, sizeof(data->passwd) - 1);
        data->passwd[sizeof(data->passwd) - 1] = '\0'; // Sicherstellen, dass der String null-terminiert ist
    } else {
        printf("Fehler: Passwort-Feld fehlt oder ist nicht vom Typ string\n");
        cJSON_Delete(root);
        return -1; // Fehler
    }

    // JSON-Objekt freigeben
    cJSON_Delete(root);

    return 0; // Erfolg
}


// Funktion zur Verarbeitung des empfangenen JSON-Strings
void process_received_audio_json(const char *json_str, device_data_t *data) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE("JSON", "Error parsing JSON");
        return;
    }
    if (cJSON_HasObjectItem(root, "type")) {
        cJSON *typeItem = cJSON_GetObjectItem(root, "type");
        if (typeItem->valueint == 5) {
            cJSON *audioObj = cJSON_GetObjectItem(root, "sliderValues");
            if (audioObj) {
                // Die ersten 10 Werte für den rechten und linken Kanal
                for (int i = 0; i < 10; i++) {
                    data->audio.gain[i] = cJSON_GetArrayItem(audioObj, i)->valueint;
                    data->audio.gain[i + 10] = cJSON_GetArrayItem(audioObj, i)->valueint; // gleiche Werte für linke Kanäle
                }

                // Der elfte Wert ist für das Volume
                data->audio.volume = cJSON_GetArrayItem(audioObj, 10)->valueint;

                data->audio.muted = cJSON_GetObjectItem(root, "muted")->valueint;
                data->audio.balance = cJSON_GetObjectItem(root, "balance")->valueint;
            }
        }
    }

    cJSON_Delete(root);
}



