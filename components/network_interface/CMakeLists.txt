idf_component_register(SRCS "network_interface.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_netif esp_eth esp_event lwip driver wifi_interface device_data json)
