#ifndef MICRO_FLAC_DECODER_H
#define MICRO_FLAC_DECODER_H

#include "audio_element.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int task_stack;
    int task_prio;
    int task_core;
    int out_rb_size;
} micro_flac_decoder_cfg_t;

#define MICRO_FLAC_DECODER_DEFAULT_CONFIG() { \
    .task_stack = 8192, \
    .task_prio = 5, \
    .task_core = 1, \
    .out_rb_size = 16384 \
}

audio_element_handle_t micro_flac_decoder_init(const micro_flac_decoder_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
#endif