#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "audio_element.h"

#include "micro_flac_bridge.h"
#include "micro_flac_decoder.h"

#define MICRO_FLAC_INPUT_BUFFER_INITIAL_SIZE 8192
#define MICRO_FLAC_INPUT_BUFFER_MAX_SIZE 131072

static const char *TAG = "micro_flac_decoder";

typedef struct {
    micro_flac_handle_t decoder;
    uint8_t *input_buffer;
    size_t input_size;
    size_t input_capacity;
    uint8_t *output_buffer;
    size_t output_buffer_size;
    micro_flac_info_t info;
    bool header_ready;
    bool input_eof;
    bool stream_start_logged;
} micro_flac_decoder_t;

static esp_err_t micro_flac_decoder_open(audio_element_handle_t self)
{
    micro_flac_decoder_t *ctx = audio_element_getdata(self);

    if (ctx == NULL) return ESP_ERR_INVALID_STATE;

    micro_flac_reset(ctx->decoder);

    ctx->input_size = 0;
    ctx->header_ready = false;
    ctx->input_eof = false;
    ctx->stream_start_logged = false;

    return ESP_OK;
}

static esp_err_t micro_flac_decoder_close(audio_element_handle_t self)
{
    micro_flac_decoder_t *ctx = audio_element_getdata(self);

    if (ctx == NULL) return ESP_ERR_INVALID_STATE;

    ctx->input_size = 0;
    ctx->header_ready = false;
    ctx->input_eof = false;
    ctx->stream_start_logged = false;

    return ESP_OK;
}

static esp_err_t micro_flac_decoder_destroy(audio_element_handle_t self)
{
    micro_flac_decoder_t *ctx = audio_element_getdata(self);

    if (ctx == NULL) return ESP_OK;

    if (ctx->decoder != NULL) micro_flac_destroy(ctx->decoder);

    if (ctx->input_buffer != NULL) free(ctx->input_buffer);

    if (ctx->output_buffer != NULL) free(ctx->output_buffer);

    free(ctx);

    return ESP_OK;
}

static int micro_flac_decoder_remove_input(micro_flac_decoder_t *ctx, size_t bytes)
{
    if (bytes == 0) return 0;

    if (bytes > ctx->input_size) return -1;

    ctx->input_size -= bytes;

    if (ctx->input_size > 0) memmove(ctx->input_buffer, ctx->input_buffer + bytes, ctx->input_size);

    return 0;
}

static int micro_flac_decoder_grow_input_buffer(micro_flac_decoder_t *ctx)
{
    size_t new_capacity;
    uint8_t *new_buffer;

    if (ctx->input_capacity >= MICRO_FLAC_INPUT_BUFFER_MAX_SIZE) return -1;

    new_capacity = ctx->input_capacity * 2;

    if (new_capacity > MICRO_FLAC_INPUT_BUFFER_MAX_SIZE) new_capacity = MICRO_FLAC_INPUT_BUFFER_MAX_SIZE;

    new_buffer = realloc(ctx->input_buffer, new_capacity);

    if (new_buffer == NULL) return -1;

    ctx->input_buffer = new_buffer;

    ctx->input_capacity = new_capacity;

    ESP_LOGW(TAG, "Input buffer increased to %u bytes", (unsigned int)new_capacity);

    return 0;
}

static int micro_flac_decoder_read_input(audio_element_handle_t self, micro_flac_decoder_t *ctx)
{
    size_t free_size;
    int read_bytes;

    if (ctx->input_eof) return AEL_IO_DONE;

    if (ctx->input_size >= ctx->input_capacity) {
        if (micro_flac_decoder_grow_input_buffer(ctx) != 0) {
            ESP_LOGE(TAG, "Input buffer maximum reached");
            return AEL_IO_FAIL;
        }
    }

    free_size = ctx->input_capacity - ctx->input_size;

    if (free_size == 0) return AEL_IO_FAIL;

    read_bytes = audio_element_input(self, (char *)(ctx->input_buffer + ctx->input_size), free_size);

    if (read_bytes > 0) {
        ctx->input_size += read_bytes;
        return read_bytes;
    }

    if (read_bytes == AEL_IO_DONE) {
        ctx->input_eof = true;
        ESP_LOGI(TAG, "Input EOF");
        return AEL_IO_DONE;
    }

    return read_bytes;
}

static int micro_flac_decoder_allocate_output(micro_flac_decoder_t *ctx)
{
    size_t required_size;
    uint8_t *new_buffer;

    if (micro_flac_get_info(ctx->decoder, &ctx->info) != 0) return -1;

    required_size = (size_t)ctx->info.max_block_size * (size_t)ctx->info.channels * (size_t)ctx->info.bytes_per_sample;

    if (required_size == 0) return -1;

    if (ctx->output_buffer_size >= required_size) return 0;

    new_buffer = realloc(ctx->output_buffer, required_size);

    if (new_buffer == NULL) return -1;

    ctx->output_buffer = new_buffer;

    ctx->output_buffer_size = required_size;

    ESP_LOGI(TAG, "Output buffer %u bytes", (unsigned int)required_size);

    return 0;
}

static int micro_flac_decoder_handle_header(audio_element_handle_t self, micro_flac_decoder_t *ctx)
{
    if (micro_flac_decoder_allocate_output(ctx) != 0) {
        ESP_LOGE(TAG, "Unable to allocate output buffer");
        return AEL_IO_FAIL;
    }

    ESP_LOGI(TAG, "FLAC %lu Hz, %lu channel, %lu bit, max block %lu", (unsigned long)ctx->info.sample_rate, (unsigned long)ctx->info.channels, (unsigned long)ctx->info.bits_per_sample, (unsigned long)ctx->info.max_block_size);

    audio_element_set_music_info(self, ctx->info.sample_rate, ctx->info.channels, ctx->info.bits_per_sample);

    audio_element_report_info(self);

    ctx->header_ready = true;

    return ESP_OK;
}

static int micro_flac_decoder_process(audio_element_handle_t self, char *buffer, int len)
{
    micro_flac_decoder_t *ctx = audio_element_getdata(self);
    int output_bytes_total = 0;

    (void)buffer;

    (void)len;

    if (ctx == NULL) return AEL_IO_FAIL;

    while (1) {
        micro_flac_result_t result;
        size_t bytes_consumed = 0;
        size_t samples_decoded = 0;
        size_t output_bytes;
        int read_result;
        int written;

        if (ctx->input_size == 0) {
            if (ctx->input_eof) return output_bytes_total > 0 ? output_bytes_total : AEL_IO_DONE;

            read_result = micro_flac_decoder_read_input(self, ctx);

            if (read_result > 0) continue;

            if (read_result == AEL_IO_DONE) continue;

            return output_bytes_total > 0 ? output_bytes_total : read_result;
        }

        if (!ctx->stream_start_logged && ctx->input_size >= 8) ESP_LOGI(TAG, "FLAC start: %02X %02X %02X %02X %02X %02X %02X %02X", ctx->input_buffer[0], ctx->input_buffer[1], ctx->input_buffer[2], ctx->input_buffer[3], ctx->input_buffer[4], ctx->input_buffer[5], ctx->input_buffer[6], ctx->input_buffer[7]);
        if (!ctx->stream_start_logged && ctx->input_size >= 8) ctx->stream_start_logged = true;

        result = micro_flac_decode(ctx->decoder, ctx->input_buffer, ctx->input_size, ctx->output_buffer, ctx->output_buffer_size, &bytes_consumed, &samples_decoded);

        if (bytes_consumed > 0) {
            if (micro_flac_decoder_remove_input(ctx, bytes_consumed) != 0) {
                ESP_LOGE(TAG, "Invalid bytes_consumed: %u", (unsigned int)bytes_consumed);
                return AEL_IO_FAIL;
            }
        }

        if (result == MICRO_FLAC_HEADER_READY) {
            if (micro_flac_decoder_handle_header(self, ctx) != ESP_OK) return AEL_IO_FAIL;

            continue;
        }

        if (result == MICRO_FLAC_SUCCESS) {
            if (!ctx->header_ready) {
                ESP_LOGE(TAG, "PCM received before FLAC header");
                return AEL_IO_FAIL;
            }

            output_bytes = samples_decoded * ctx->info.bytes_per_sample;

            if (output_bytes > ctx->output_buffer_size) {
                ESP_LOGE(TAG, "Decoder output too large: %u > %u", (unsigned int)output_bytes, (unsigned int)ctx->output_buffer_size);
                return AEL_IO_FAIL;
            }

            if (output_bytes > 0) {
                written = audio_element_output(self, (char *)ctx->output_buffer, output_bytes);

                if (written < 0) return written;

                output_bytes_total += written;
            }

            if ((bytes_consumed == 0) && (samples_decoded == 0)) {
                ESP_LOGE(TAG, "Decoder made no progress");
                return AEL_IO_FAIL;
            }

            continue;
        }

        if (result == MICRO_FLAC_NEED_MORE_DATA) {
            if (ctx->input_eof) {
                if (ctx->input_size == 0) return output_bytes_total > 0 ? output_bytes_total : AEL_IO_DONE;

                ESP_LOGE(TAG, "Unexpected EOF with %u undecoded bytes", (unsigned int)ctx->input_size);

                return AEL_IO_FAIL;
            }

            if (ctx->input_size >= ctx->input_capacity) {
                if (micro_flac_decoder_grow_input_buffer(ctx) != 0) {
                    ESP_LOGE(TAG, "FLAC frame exceeds input buffer limit (%u bytes)", (unsigned int)MICRO_FLAC_INPUT_BUFFER_MAX_SIZE);
                    return AEL_IO_FAIL;
                }
            }

            read_result = micro_flac_decoder_read_input(self, ctx);

            if (read_result > 0) continue;

            if (read_result == AEL_IO_DONE) continue;

            return output_bytes_total > 0 ? output_bytes_total : read_result;
        }

        if (result == MICRO_FLAC_END_OF_STREAM) {
            ESP_LOGI(TAG, "FLAC end of stream");

            return output_bytes_total > 0 ? output_bytes_total : AEL_IO_DONE;
        }

        ESP_LOGE(TAG, "FLAC decoder error: result=%ld (%s) input_size=%u consumed=%u samples=%u header=%d", (long)result, micro_flac_result_to_string(result), (unsigned int)ctx->input_size, (unsigned int)bytes_consumed, (unsigned int)samples_decoded, ctx->header_ready);

        return AEL_IO_FAIL;
    }
}

audio_element_handle_t micro_flac_decoder_init(const micro_flac_decoder_cfg_t *cfg)
{
    audio_element_cfg_t element_cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    audio_element_handle_t element;
    micro_flac_decoder_t *ctx;

    if (cfg == NULL) return NULL;

    ctx = calloc(1, sizeof(micro_flac_decoder_t));

    if (ctx == NULL) return NULL;

    ctx->decoder = micro_flac_create();

    if (ctx->decoder == NULL) goto error;

    ctx->input_buffer = malloc(MICRO_FLAC_INPUT_BUFFER_INITIAL_SIZE);

    if (ctx->input_buffer == NULL) goto error;

    ctx->input_capacity = MICRO_FLAC_INPUT_BUFFER_INITIAL_SIZE;

    ctx->input_size = 0;

    ctx->output_buffer = NULL;

    ctx->output_buffer_size = 0;

    ctx->header_ready = false;

    ctx->input_eof = false;
    ctx->stream_start_logged = false;

    element_cfg.open = micro_flac_decoder_open;

    element_cfg.close = micro_flac_decoder_close;

    element_cfg.process = micro_flac_decoder_process;

    element_cfg.destroy = micro_flac_decoder_destroy;

    element_cfg.task_stack = cfg->task_stack;

    element_cfg.task_prio = cfg->task_prio;

    element_cfg.task_core = cfg->task_core;

    element_cfg.out_rb_size = cfg->out_rb_size;

    element_cfg.buffer_len = 0;

    element_cfg.tag = "micro_flac_decoder";

    element = audio_element_init(&element_cfg);

    if (element == NULL) goto error;

    audio_element_setdata(element, ctx);

    return element;

    error:

    if (ctx->decoder != NULL) micro_flac_destroy(ctx->decoder);

    if (ctx->input_buffer != NULL) free(ctx->input_buffer);

    if (ctx->output_buffer != NULL) free(ctx->output_buffer);

    free(ctx);

    return NULL;
}
