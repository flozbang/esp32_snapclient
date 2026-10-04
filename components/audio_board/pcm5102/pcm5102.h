#ifndef _PCM5102_H_
#define _PCM5102_H_

#include "audio_hal.h"

extern audio_hal_func_t AUDIO_CODEC_PCM5102_DEFAULT_HANDLE;

esp_err_t pcm5102_init(audio_hal_codec_config_t *cfg);
esp_err_t pcm5102_deinit(void);
int pcm5102_ctrl_state(audio_hal_codec_mode_t mode, audio_hal_ctrl_t ctrl_state);
esp_err_t pcm5102_config_i2s(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface);
esp_err_t pcm5102_set_voice_mute(bool enable);
esp_err_t pcm5102_get_voice_mute(void);
esp_err_t pcm5102_set_voice_volume(int volume);
esp_err_t pcm5102_get_voice_volume(int *volume);
esp_err_t pcm5102_pa_power(bool enable);

#endif