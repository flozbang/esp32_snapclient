idf_component_register(SRCS "snapcast.c"
                       INCLUDE_DIRS "include"
                       REQUIRES buffer json sntp_client audio_stream audio_pipeline )

