#ifndef MICRO_FLAC_BRIDGE_H
#define MICRO_FLAC_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *micro_flac_handle_t;
typedef int32_t micro_flac_result_t;

#define MICRO_FLAC_SUCCESS 0
#define MICRO_FLAC_HEADER_READY 1
#define MICRO_FLAC_END_OF_STREAM 2
#define MICRO_FLAC_NEED_MORE_DATA 3

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t bytes_per_sample;
    uint32_t max_block_size;
} micro_flac_info_t;

micro_flac_handle_t micro_flac_create(void);
void micro_flac_destroy(micro_flac_handle_t handle);
void micro_flac_reset(micro_flac_handle_t handle);
micro_flac_result_t micro_flac_decode(micro_flac_handle_t handle, const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *bytes_consumed, size_t *samples_decoded);
int micro_flac_get_info(micro_flac_handle_t handle, micro_flac_info_t *info);
const char *micro_flac_result_to_string(micro_flac_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* MICRO_FLAC_BRIDGE_H */
