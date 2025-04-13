idf_component_register(SRCS "wifi_interface.c"
                    INCLUDE_DIRS "include"
                     REQUIRES esp_wifi nvs_flash esp_event json)
