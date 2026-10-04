set(COMPONENT_SRCS "")
set(COMPONENT_ADD_INCLUDEDIRS "include")

if(CONFIG_CUSTOM_AUDIO_BOARD_AI_THINKER_A1S)
    message(STATUS "Current board name is AI-Thinker ESP32 Audio Kit (ES8388)")
    list(APPEND COMPONENT_ADD_INCLUDEDIRS "ai_thinker_esp32_a1s")
    list(APPEND COMPONENT_SRCS "ai_thinker_esp32_a1s/board.c")
    list(APPEND COMPONENT_SRCS "ai_thinker_esp32_a1s/board_pins_config.c")
endif()

if(CONFIG_CUSTOM_AUDIO_BOARD_PCM5102)
    message(STATUS "Current board name is PCM5102 I2S DAC")
    list(APPEND COMPONENT_ADD_INCLUDEDIRS "pcm5102")
    list(APPEND COMPONENT_SRCS "pcm5102/board.c")
    list(APPEND COMPONENT_SRCS "pcm5102/board_pins_config.c")
    list(APPEND COMPONENT_SRCS "pcm5102/pcm5102.c")
endif()

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
    PRIV_REQUIRES
        esp_peripherals
        audio_sal
        audio_hal
        esp_dispatcher
        display_service
)
