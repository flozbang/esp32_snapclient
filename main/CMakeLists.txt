# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.




idf_component_register(
    SRCS main.c 
    ota_update.c        # list the source files of this component
    system_diagnostics.c
    INCLUDE_DIRS        # optional, add here public include directories
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES            esp_netif json_rpc esp_eth esp_event lwip driver esp_wifi network_interface device_data crossover sntp_client webserver snapcast_stream  app_update micro_flac_decoder micro-flac audio_board
        esp_http_client
    PRIV_REQUIRES       audio_pipeline audio_hal esp_peripherals audio_stream
)
