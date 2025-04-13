/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2020 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "dsps_biquad_platform.h"
#include "dsps_biquad.h"
#include "dsps_biquad_gen.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_err.h"
#include "audio_mem.h"
#include "crossover.h"

static const char *TAG = "CROSSOVER";
#define CONNECT_TIMEOUT_MS        100
#define MULTIPLIER                1.0/(float)(INT16_MAX + 1)
typedef struct tcp_stream {
    audio_stream_type_t           	type;
    char							*out_buffer;
    char                          	*left_buffer_in;
    char                          	*right_buffer_in;
    char                          	*left_buffer_out;
    char                        	*right_buffer_out;
    int 							*balance;
    int								*output;
    int								*volume;
    int                             *mute;
    crossover_event_handle_cb    	hook;
    void                          	*ctx;
    bool                          	is_open;
} crossover_t;
uint8_t fs;



//bass-speaker, 45Hz Hipass
static struct iir_filt conf_45_hp = {
	//index0 = 16k, index1 = 32k, index 2 = 44k1, index 3 = 48k
	.a0 = { 0.9875820178250215,  0.9937716134506484 , 0.9954766638878052,   0.9958434200204267 },
	.a1 = { -1.975164035650043, -1.9875432269012967, -1.9909533277756104,  -1.9916868400408534 },
	.a2 = { 0.9875820178250215,  0.9937716134506484,  0.9954766638878052,   0.9958434200204267 },
	.b1 = { -1.975009826344679, -1.9875044344654942, -1.9909328674920315,  -1.9916695631391037 },
	.b2 = { 0.9753182449554073,  0.9875820193370991,  0.9909737880591895,   0.991704116942603 },

};


//bass-speaker, 2500Hz Lowpass
static struct iir_filt conf_2k5_lp = {
	//index0 = 16k, index1 = 32k, index 2 = 44k1, index 3 = 48k
	.a0 = { 0.13993146179027674, 0.044278036805267616, 0.025175362450974036, 0.021620113635254866 },
	.a1 = { 0.2798629235805535,  0.08855607361053523,  0.05035072490194807,  0.04324022727050973 },
	.a2 = { 0.13993146179027674, 0.044278036805267616, 0.025175362450974036, 0.021620113635254866 } ,
	.b1 = { -0.699698900564656, -1.3228374096880198,  -1.50365042037159,    -1.5430779694435248 },
	.b2 = { 0.259424747725763,   0.4999495569090904,   0.6043518701754859, 	 0.6295584239845442}

};

//bass-speaker, 2500Hz Lowpass
static struct iir_filt conf_230_lp = {
	//index0 = 16k, index1 = 32k, index 2 = 44k1, index 3 = 48k
	.a0 = {0.0019158798296087405, 0.0004940085822727755, 0.00026235697011624086, 0.00022186706755321744},
	.a1 = {0.003831759659217481,  0.000988017164545551,  0.0005247139402324817,  0.0004437341351064349},
	.a2 = {0.0019158798296087405, 0.0004940085822727755, 0.00026235697011624086, 0.00022186706755321744},
	.b1 = {-1.8724240200960798,  -1.9361536826675452,   -1.9536645911191763,    -1.9574282114678947},
	.b2 = {0.8800875394145148,    0.9381297169966364,    0.9547140189996416,     0.9583156797381076}

};


//tweeter 2800 Hz Hipass
static struct iir_filt conf_2k8_hp = {
	//index0 = 16k, index1 = 32k, index 2 = 44k1, index 3 = 48k
	.a0 = {  0.44599764558093963, 0.6764097852300075,  0.753716633131342,   0.7713299340241907},
	.a1 = { -0.8919952911618793, -1.352819570460015,  -1.507433266262684,  -1.5426598680483814},
	.a2 = { 0.44599764558093963,  0.6764097852300075,  0.753716633131342,   0.7713299340241907},
	.b1 = { -0.5570289325445305, -1.2452156906579934, -1.4458299168752424, -1.489668635259956},
	.b2 = { 0.2269616497792281,   0.4604234502620365,  0.5690366156501254,  0.595651100836807}

};



typedef struct {
    float a0, a1, a2, b0, b1, b2;
} biquad_coeffs;

biquad_coeffs calculate_highpass_coeffs(float sample_rate, float cutoff_frequency, float q) {
    biquad_coeffs coeffs;
    float omega = 2.0f * M_PI * cutoff_frequency / sample_rate;
    float alpha = sinf(omega) / (2.0f * q);

    coeffs.a0 = (1.0f + cosf(omega)) / 2.0f;
    coeffs.a1 = -(1.0f + cosf(omega));
    coeffs.a2 = (1.0f + cosf(omega)) / 2.0f;
    coeffs.b0 = (1.0f + alpha);
    coeffs.b1 = -2.0f * cosf(omega);
    coeffs.b2 = (1.0f - alpha);
    return coeffs;
}

biquad_coeffs calculate_lowpass_coeffs(float sample_rate, float cutoff_frequency, float q) {
    biquad_coeffs coeffs;
    float omega = 2.0f * M_PI * cutoff_frequency / sample_rate;
    float alpha = sinf(omega) / (2.0f * q);
    coeffs.a0 = (1.0f - cosf(omega)) / 2.0f;
    coeffs.a1 = 1.0f - cosf(omega);
    coeffs.a2 = (1.0f - cosf(omega)) / 2.0f;
    coeffs.b0 = (1.0f + alpha);
    coeffs.b1 = -2.0f * cosf(omega);
    coeffs.b2 = (1.0f - alpha);

    return coeffs;
}

static float process_iir (float inSampleF, struct iir_filt * config) {
	float outSampleF =
	(* config).a0[fs] * inSampleF
	+ (* config).a1[fs] * (* config).in_z1
	+ (* config).a2[fs] * (* config).in_z2
	- (* config).b1[fs] * (* config).out_z1
	- (* config).b2[fs] * (* config).out_z2;
	(* config).in_z2 = (* config).in_z1;
	(* config).in_z1 = inSampleF;
	(* config).out_z2 = (* config).out_z1;
	(* config).out_z1 = outSampleF;
	return outSampleF;
}

	static void process_crossover_data (uint8_t * data, size_t item_size, uint8_t subwoofer) {

		int16_t * samples = (int16_t *) data;
		int16_t * outsample = (int16_t *) data;

		for (int i=0; i<item_size; i=i+4) {
			//restore input samples and make monosum
			float insample = (float) *samples;
			samples++;
			insample += *samples;
			samples++;
			//monosum now available in insample
			float lowsample=0;
			float highsample=0;
			if(!subwoofer){
				//process bass speaker
				lowsample = process_iir(insample, &conf_45_hp);
				lowsample = process_iir(lowsample, &conf_2k5_lp);
				//lowsample = process_iir(lowsample, &conf_60_eq);
				//process tweeter
				highsample = process_iir(insample, &conf_2k8_hp);
				*outsample = (int16_t) lowsample;
				outsample++;
				*outsample = (int16_t) highsample;
				outsample++;
			}else{
				//process bass speaker
				lowsample = process_iir(insample, &conf_45_hp);
				lowsample = process_iir(lowsample, &conf_230_lp);
				*outsample = (int16_t) lowsample;
				outsample++;
				*outsample = (int16_t) lowsample;
				outsample++;
			}
		}
	}

	static void set_sample_rate (uint8_t samplerate) {
		fs=samplerate;
	}



static esp_err_t _dispatch_event(audio_element_handle_t el, crossover_t *crossover, void *data, int len, crossover_status_t state)
{
   /* if (el && crossover && crossover->hook) {
        crossover_event_msg_t msg = { 0 };
        msg.data = data;
        msg.data_len = len;
        msg.sock_fd = crossover->t;
        msg.source = el;
        return crossover->hook(&msg, state, crossover->ctx);
    }
    return ESP_FAIL;*/
	return ESP_OK;

}

static esp_err_t crossover_open(audio_element_handle_t self)
{
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    crossover_t *crossover = (crossover_t *)audio_element_getdata(self);
    crossover->is_open = true;
    return ESP_OK;
}

static esp_err_t crossover_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    crossover_t *crossover = (crossover_t *)audio_element_getdata(self);
    return 0;
}

static esp_err_t crossover_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    crossover_t *crossover = (crossover_t *)audio_element_getdata(self);

    return 0;
}




void fill_Left_channel(char *l_buffer, char *audio_buffer, int size) {
    int x=0;
	for (int i = 1; i < size; i += 4) {
        l_buffer[x++] = audio_buffer[i];
        l_buffer[x++] = audio_buffer[i + 1];
    }
}

void fill_right_channel(uint8_t *r_buffer, char *audio_buffer, int size) {
    int x=0;
	for (int i = 0; i < size; i += 4) {

		r_buffer[x++] = audio_buffer[i];
		r_buffer[x++] = audio_buffer[i + 1];
    }
}


void fill_LR_buffers(crossover_t *crossover, int16_t* audio_buffer, int size) {
    int x = 0;
    int16_t* r_buffer = (int16_t*)crossover->right_buffer_in;
    int16_t* l_buffer = (int16_t*)crossover->left_buffer_in;
    for (int i = 0; i < size; i += 2) {
    	r_buffer[x]   = audio_buffer[i];
    	l_buffer[x]   = audio_buffer[i+1];
    	x++;
    }
}

void fill_L_buffers(crossover_t *crossover, int16_t* audio_buffer, int size) {
    for (int i = 0; i < size; i += 2) {
    	audio_buffer[i]   = audio_buffer[i+1];
    }
}

void fill_R_buffers(crossover_t *crossover, int16_t* audio_buffer, int size) {
    //int16_t* r_buffer = (int16_t*)crossover->right_buffer_in;
    //int16_t* l_buffer = (int16_t*)crossover->left_buffer_in;
    for (int i = 0; i < size; i += 2) {
    	audio_buffer[i+1] = audio_buffer[i];
    }
}

void fill_out_buffer(crossover_t *crossover, int16_t * out_buffer, int size) {
    int x = 0;
    int16_t *r_buffer = (int16_t*)crossover->right_buffer_in;
    int16_t *l_buffer = (int16_t*)crossover->left_buffer_in;
    for(int i=0; i < size; i+=2){
    	out_buffer[i]   = r_buffer[x];
    	out_buffer[i+1] = l_buffer[x];
    	x++;
    }

}

void set_volume(crossover_t *crossover, int size) {
    int32_t buffer;
    int16_t v_q15 = 0;
    
    // Accessing balance value
    int16_t v = *crossover->volume;
    int16_t mute = *crossover->mute;
    if(mute==1){
		v_q15 =0;
	}
    int16_t *l_buffer = (int16_t*)crossover->left_buffer_in;
    int16_t *r_buffer = (int16_t*)crossover->right_buffer_in;
    v_q15 =(int16_t)(v * 327.67);
    if(mute==1){
		v_q15 =0;
	}
    for (int i = 0; i < size; i++) {
        buffer = r_buffer[i] * v_q15;
        r_buffer[i] = buffer >> 15;
        buffer = l_buffer[i] * v_q15;
        l_buffer[i] = buffer >> 15;
    }
}

void set_balance(crossover_t *crossover, int size) {
    int32_t buffer;
    int16_t bl_r_q15 = 32767;
    int16_t bl_l_q15 = 32767;

    // Accessing balance value
    int16_t bl = *crossover->balance;
    int16_t *l_buffer = (int16_t*)crossover->left_buffer_in;
    int16_t *r_buffer = (int16_t*)crossover->right_buffer_in;
    if(bl>0){
    	bl_r_q15 = 32767 - (int16_t)(bl * 327.67);
    }else{
    	bl *=-1;
    	bl_l_q15 = 32767 - (int16_t)(bl * 327.67);
    }
    for (int i = 0; i < size; i++) {
        buffer = r_buffer[i] * bl_r_q15;
        r_buffer[i] = buffer >> 15;
        buffer = l_buffer[i] * bl_l_q15;
        l_buffer[i] = buffer >> 15;
    }
}





static esp_err_t crossover_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
	crossover_t *crossover = (crossover_t *)audio_element_getdata(self);
	audio_element_input(self, in_buffer, in_len);
	set_sample_rate (3);
	char tmp = 3;
	switch(tmp){
		case	CROSSOVER_STATE_LEFT_CHANNEL:
				fill_L_buffers(crossover, (int16_t*)in_buffer, in_len/2);
				process_crossover_data((uint8_t*)in_buffer, in_len, NOT_SUBWOOFER);
			break;
		case	CROSSOVER_STATE_RIGHT_CHANNEL:
				fill_R_buffers(crossover, (int16_t*)in_buffer, in_len/2);
				process_crossover_data((uint8_t*)in_buffer, in_len, NOT_SUBWOOFER);
			break;
		case	CROSSOVER_STATE_STEREO:
				fill_LR_buffers(crossover, (int16_t*)in_buffer, in_len/2);
				set_balance(crossover, in_len/4);
				set_volume(crossover, in_len/4);
				fill_out_buffer(crossover, (int16_t*)in_buffer, in_len/2);
			break;
		case	CROSSOVER_STATE_SUBWOOFER:
			process_crossover_data((uint8_t*)in_buffer, in_len, SUBWOOFER);
			break;
	}
	audio_element_output(self, in_buffer, in_len);
    return 1;
}

static esp_err_t crossover_close(audio_element_handle_t self)
{
	AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);
	crossover_t *crossover = (crossover_t *)audio_element_getdata(self);
	AUDIO_NULL_CHECK(TAG, crossover, return ESP_FAIL);
	if (!crossover->is_open) {
		ESP_LOGE(TAG, "Already closed");
		return ESP_FAIL;
	}
	ESP_LOGE(TAG, "Close Crossover");
	crossover->is_open = false;
	if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
		audio_element_set_byte_pos(self, 0);
	}
	return ESP_OK;
}

static esp_err_t crossover_destroy(audio_element_handle_t self)
{
	crossover_t *crossover = (crossover_t *)audio_element_getdata(self);
	AUDIO_NULL_CHECK(TAG, crossover, return ESP_FAIL);
	audio_free(crossover->left_buffer_in);
	audio_free(crossover->left_buffer_out);
	audio_free(crossover->right_buffer_in);
	audio_free(crossover->right_buffer_out);
	audio_free(crossover->out_buffer);
	audio_free(crossover);
    return ESP_OK;
}

audio_element_handle_t crossover_init(crossover_cfg_t *config)
{
    AUDIO_NULL_CHECK(TAG, config, return NULL);

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    audio_element_handle_t el;
    cfg.open = crossover_open;
    cfg.close = crossover_close;
    cfg.process = crossover_process;
    cfg.destroy = crossover_destroy;
    cfg.task_stack = config->task_stack;
    cfg.task_prio = config->task_prio;
    cfg.task_core = config->task_core;
    cfg.stack_in_ext = config->ext_stack;
    cfg.tag = "crossover";
    if (cfg.buffer_len == 0) {
        cfg.buffer_len = CROSSOVER_BUF_SIZE;
    }

    crossover_t *crossover = audio_calloc(1, sizeof(crossover_t));
    crossover->left_buffer_in   = audio_malloc(CROSSOVER_BUF_SIZE/2);
    crossover->right_buffer_in  = audio_malloc(CROSSOVER_BUF_SIZE/2);
    crossover->left_buffer_out  = audio_malloc(CROSSOVER_BUF_SIZE/2);
    crossover->right_buffer_out = audio_malloc(CROSSOVER_BUF_SIZE/2);
    /*crossover->coeffs_lpf   = (float*)audio_malloc(20);
    crossover->coeffs_hpf   = (float*)audio_malloc(20);
    crossover->w_lpf        = (float*)audio_malloc(20);
    crossover->w_hpf        = (float*)audio_malloc(20);*/
    crossover->out_buffer   = audio_malloc(4096);
    AUDIO_MEM_CHECK(TAG, crossover, return NULL);
    AUDIO_MEM_CHECK(TAG, crossover->left_buffer_in, return NULL);
    AUDIO_MEM_CHECK(TAG, crossover->right_buffer_in, return NULL);
    AUDIO_MEM_CHECK(TAG, crossover->left_buffer_out, return NULL);
    AUDIO_MEM_CHECK(TAG, crossover->right_buffer_out, return NULL);
    crossover->type     = config->type;
    crossover->output	= config->output;
    crossover->balance	= config->balance;
    crossover->volume   = config->volume;
    crossover->mute   = config->mute;
    if (config->event_handler) {
        crossover->hook = config->event_handler;
        if (config->event_ctx) {
            crossover->ctx = config->event_ctx;
        }
    }
  //  dsps_biquad_gen_lpf_f32(crossover->coeffs_lpf, crossover->lpf_freq, crossover->lpf_qFactor);
  //  dsps_biquad_gen_hpf_f32(crossover->coeffs_hpf, crossover->hpf_freq, crossover->hpf_qFactor);
    el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, goto crossover_init_exit);
    audio_element_setdata(el, crossover);

    return el;
crossover_init_exit:
	audio_free(crossover);
    return NULL;
}
