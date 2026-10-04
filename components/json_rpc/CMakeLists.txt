idf_component_register(SRCS "json_rpc.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sntp_client mdns device_data)
