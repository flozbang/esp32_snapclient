idf_component_register(SRCS "sntp_client.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event esp_system nvs_flash esp_netif)

