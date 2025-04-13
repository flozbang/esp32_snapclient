idf_component_register(SRCS "webserver.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server esp_websocket_client json audio_sal esp_wifi wifi_interface device_data)
