#include "pcm5102.h"

esp_err_t pcm5102_init(audio_hal_codec_config_t *cfg)
{
    (void)cfg;
    return ESP_OK;
}

esp_err_t pcm5102_deinit(void)
{
    return ESP_OK;
}

int pcm5102_ctrl_state(audio_hal_codec_mode_t mode, audio_hal_ctrl_t ctrl_state)
{
    (void)mode;
    (void)ctrl_state;
    return ESP_OK;
}

esp_err_t pcm5102_config_i2s(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface)
{
    (void)mode;
    (void)iface;
    return ESP_OK;
}

esp_err_t pcm5102_set_voice_mute(bool enable)
{
    (void)enable;
    return ESP_OK;
}

esp_err_t pcm5102_get_voice_mute(void)
{
    return ESP_OK;
}

esp_err_t pcm5102_set_voice_volume(int volume)
{
    (void)volume;
    return ESP_OK;
}

esp_err_t pcm5102_get_voice_volume(int *volume)
{
    if (volume == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *volume = 100;
    return ESP_OK;
}

esp_err_t pcm5102_pa_power(bool enable)
{
    (void)enable;
    return ESP_OK;
}

audio_hal_func_t AUDIO_CODEC_PCM5102_DEFAULT_HANDLE = {
    .audio_codec_initialize = pcm5102_init,
    .audio_codec_deinitialize = pcm5102_deinit,
    .audio_codec_ctrl = pcm5102_ctrl_state,
    .audio_codec_config_iface = pcm5102_config_i2s,
    .audio_codec_set_mute = pcm5102_set_voice_mute,
    .audio_codec_set_volume = pcm5102_set_voice_volume,
    .audio_codec_get_volume = pcm5102_get_voice_volume,
    .audio_codec_enable_pa = pcm5102_pa_power,
    .audio_hal_lock = NULL,
    .handle = NULL,
};