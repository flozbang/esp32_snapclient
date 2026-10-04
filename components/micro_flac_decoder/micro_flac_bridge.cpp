#include "micro_flac_bridge.h"
#include "micro_flac/flac_decoder.h"
#include "esp_log.h"
#include "esp_timer.h"
static const char *TAG = "micro_flac_bridge";
using namespace micro_flac;

extern "C" micro_flac_handle_t micro_flac_create(void)
{
    return new FLACDecoder();
}

extern "C" void micro_flac_destroy(micro_flac_handle_t handle)
{
    FLACDecoder *decoder = static_cast<FLACDecoder *>(handle);
    delete decoder;
}

extern "C" void micro_flac_reset(micro_flac_handle_t handle)
{
    FLACDecoder *decoder = static_cast<FLACDecoder *>(handle);
    if (decoder != nullptr) decoder->reset();
}

extern "C" micro_flac_result_t micro_flac_decode(micro_flac_handle_t handle, const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *bytes_consumed, size_t *samples_decoded)
{
    FLACDecoder *decoder = static_cast<FLACDecoder *>(handle);

    if (decoder == nullptr) return FLAC_DECODER_ERROR_INVALID_ARGUMENT;

   // int64_t start_us = esp_timer_get_time();
	FLACDecoderResult result = decoder->decode(input, input_size, output, output_size, *bytes_consumed, *samples_decoded);
//	int64_t decode_us = esp_timer_get_time() - start_us;

//ESP_LOGI(TAG, "decode: result=%d input=%u consumed=%u samples=%u time=%lld us", (int)result, (unsigned int)input_size, (unsigned int)*bytes_consumed, (unsigned int)*samples_decoded, decode_us);

    return static_cast<micro_flac_result_t>(result);
}

extern "C" int micro_flac_get_info(micro_flac_handle_t handle, micro_flac_info_t *info)
{
    FLACDecoder *decoder = static_cast<FLACDecoder *>(handle);
    if (decoder == nullptr) return -1;
    if (info == nullptr) return -1;
    const auto &stream_info = decoder->get_stream_info();
    if (!stream_info.is_valid()) return -1;
    info->sample_rate = stream_info.sample_rate();
    info->channels = stream_info.num_channels();
    info->bits_per_sample = stream_info.bits_per_sample();
    info->bytes_per_sample = stream_info.bytes_per_sample();
    info->max_block_size = stream_info.max_block_size();
    return 0;
}

extern "C" const char *micro_flac_result_to_string(micro_flac_result_t result)
{
    switch (result) {
        case FLAC_DECODER_SUCCESS: return "SUCCESS";
        case FLAC_DECODER_HEADER_READY: return "HEADER_READY";
        case FLAC_DECODER_END_OF_STREAM: return "END_OF_STREAM";
        case FLAC_DECODER_NEED_MORE_DATA: return "NEED_MORE_DATA";
        case FLAC_DECODER_ERROR_BAD_HEADER: return "BAD_HEADER";
        case FLAC_DECODER_ERROR_SYNC_NOT_FOUND: return "SYNC_NOT_FOUND";
        case FLAC_DECODER_ERROR_CRC_MISMATCH: return "CRC_MISMATCH";
        case FLAC_DECODER_ERROR_MEMORY_ALLOCATION: return "MEMORY_ALLOCATION";
        case FLAC_DECODER_ERROR_BAD_MAGIC_NUMBER: return "BAD_MAGIC_NUMBER";
        case FLAC_DECODER_ERROR_BAD_SAMPLE_DEPTH: return "BAD_SAMPLE_DEPTH";
        case FLAC_DECODER_ERROR_RESERVED_CHANNEL_ASSIGNMENT: return "RESERVED_CHANNEL_ASSIGNMENT";
        case FLAC_DECODER_ERROR_RESERVED_SUBFRAME_TYPE: return "RESERVED_SUBFRAME_TYPE";
        case FLAC_DECODER_ERROR_RESERVED_RESIDUAL_CODING_METHOD: return "RESERVED_RESIDUAL_CODING_METHOD";
        case FLAC_DECODER_ERROR_BAD_BLOCK_SIZE: return "BAD_BLOCK_SIZE";
        case FLAC_DECODER_ERROR_BLOCK_SIZE_NOT_DIVISIBLE_RICE: return "BLOCK_SIZE_NOT_DIVISIBLE_RICE";
        case FLAC_DECODER_ERROR_BAD_SAMPLE_RATE: return "BAD_SAMPLE_RATE";
        case FLAC_DECODER_ERROR_BAD_LPC_PARAMS: return "BAD_LPC_PARAMS";
        case FLAC_DECODER_ERROR_OGG_DEMUX: return "OGG_DEMUX";
        case FLAC_DECODER_ERROR_OGG_BAD_HEADER: return "OGG_BAD_HEADER";
        case FLAC_DECODER_ERROR_INPUT_INVALID: return "INPUT_INVALID";
        case FLAC_DECODER_ERROR_OUTPUT_TOO_SMALL: return "OUTPUT_TOO_SMALL";
        case FLAC_DECODER_ERROR_FRAME_MISMATCH: return "FRAME_MISMATCH";
        case FLAC_DECODER_ERROR_INTERNAL: return "INTERNAL";
        case FLAC_DECODER_ERROR_BAD_RICE_PARTITION: return "BAD_RICE_PARTITION";
        case FLAC_DECODER_ERROR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        default: return "UNKNOWN";
    }
}
