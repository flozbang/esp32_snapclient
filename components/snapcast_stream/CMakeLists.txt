idf_component_register(SRCS "snapcast_stream.c" "timeprovider.c" "tcp_handler.c"
                       INCLUDE_DIRS "include"
                       REQUIRES audio_stream snapcast audio_sal  audio_board sntp_client mdns)
