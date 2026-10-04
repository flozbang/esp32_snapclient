#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include "esp_err.h"

esp_err_t ota_update_from_url(const char *url);

#endif