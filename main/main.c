//#include "esp_netif.h"
//#include "esp_eth.h"
//#include "esp_event.h"
#include "audio_hal.h"
#include "audio_mem.h"
#include "ota_update.h"
#include "board.h"
#include "esp_log.h"
//#include "driver/gpio.h"
//#include "ping/ping_sock.h"
//#include "lwip/ip4_addr.h"
#include "esp_ota_ops.h"


#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "network_interface.h"
#include "device_data.h"
#include "nvs_flash.h"
#include <sys/stat.h>
//#include "esp_netif_sntp.h"
#include "sntp_client.h"
#include "esp_timer.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"

#include "i2s_stream.h"
#include "esp_peripherals.h"
#include "flac_decoder.h"

#include "equalizer.h"
#include "crossover.h"
#include "webserver.h"
#include "device_data.h"
#include "snapcast_stream.h"
#include "json_rpc.h"
#include "system_diagnostics.h"
#include "micro_flac_decoder.h"
#include "es8388.h"
//#include "bluetooth_service.h"

//#define AI_THINKER_BOARD

int64_t time_delay;
static bool ota_requested = false;
static bool ota_running = false;
audio_pipeline_handle_t pipeline;
audio_element_handle_t i2s_stream_writer, i2s_stream_reader, equalizer, crossover, flac_decoder, snapcast_stream_reader, bt_stream_reader;

//device_data_t   device_data;
device_data_t   device_data_2;
device_identity_t device_identity;
network_conf_t  net_conf;
static const char* TAG="MAIN";
uint8_t pipline_is_playing=0;

esp_timer_handle_t timer_handle;



static void ota_task(void *arg);
void time_sync_event_handler(void);




static bool ota_pending_verify = false;

static esp_err_t ota_confirm_running_firmware(void)
{
    if (!ota_pending_verify) {
        return ESP_OK;
    }

    ESP_LOGI("OTA", "Firmware startup successful");
    ESP_LOGI("OTA", "Marking firmware as valid");

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();

    if (err != ESP_OK) {
        ESP_LOGE("OTA", "Failed to mark firmware valid: %s", esp_err_to_name(err));
        return err;
    }

    ota_pending_verify = false;

    ESP_LOGI("OTA", "Firmware marked as VALID");

    return ESP_OK;
}

static void ota_check_pending_verify(void)
{
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running_partition, &ota_state) != ESP_OK) {
        return;
    }

    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ota_pending_verify = true;

        ESP_LOGW("OTA", "Firmware is pending verification");
        ESP_LOGW("OTA", "Running partition: %s", running_partition->label);
    }
}


esp_err_t ota_start(const char *url)
{
    if (ota_running) {
        return ESP_ERR_INVALID_STATE;
    }

    ota_running = true;

    if (xTaskCreate(ota_task, "ota_task", 8192, (void *)url, 5, NULL) != pdPASS) {
        ota_running = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}


static void ota_task(void *arg)
{
    const char *url = (const char *)arg;

    ESP_LOGI(TAG, "Starting OTA update");

    esp_err_t err = ota_update_from_url(url);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    /* Bei Erfolg führt ota_update_from_url() esp_restart() aus. */

    vTaskDelete(NULL);
}

static void diagnostics_task(void *arg)
{
    while (1) {
        system_diagnostics_log();

        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

static void system_wdt_timer_callback(void* arg) {
    struct timeval now;
    gettimeofday(&now, NULL);  // Ruft die aktuelle Zeit ab

    // Epoch Sekunden aus dem struct timeval holen
    long epoch_seconds = now.tv_sec;
	//wifi_sta_list_t wifi_sta;
    wifi_ap_record_t ap_info;
    esp_wifi_sta_get_ap_info(&ap_info);
    // Prüfen, ob die aktuelle Sekunde eine volle Minute ist
    if (!(now.tv_sec % 60)) {
        ESP_LOGI(TAG, "Neue Minute erreicht! Epoch Sekunde: %ld", epoch_seconds);
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
			ESP_LOGI(TAG, "Der ESP32 ist mit einem Wi-Fi-Netzwerk verbunden.");
			 initialize_sntp(time_sync_event_handler); 
             set_system_time();
		}
    }
}

void time_sync_event_handler(void)
{
    ESP_LOGI(TAG, "Time sync event received.");
    setenv("TZ", MY_TZ, 1);
    tzset();  // Anwenden der Zeitzoneneinstellungen
     // Aktuelles Datum und Uhrzeit abrufen und anzeigen
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    ESP_LOGI(TAG, "Current date/time in Bielefeld: %02d-%02d-%04d %02d:%02d:%02d", 
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, 
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
	if(pipline_is_playing==0){
		ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
		audio_pipeline_run(pipeline);
		ESP_ERROR_CHECK(ota_confirm_running_firmware());
		pipline_is_playing=1;
	}
	     
}

void rpc_event_handler(rpc_method_t state, void *event){
	switch (state)
	{
    case GET_VOLUME:
        break;
    case SET_VOLUME:
        break;
    case GET_MUTE:
        break;
    case SET_MUTE:
        break;
    case GET_BALANCE:
        break;
    case SET_BALANCE:
        break;
    case GET_MODE:
        break;
    case SET_MODE:
        break;
    case SET_EQ_1:
    case SET_EQ_2:
    case SET_EQ_3:
    case SET_EQ_4:
    case SET_EQ_5:
    case SET_EQ_6:
    case SET_EQ_7:
    case SET_EQ_8:
    case SET_EQ_9:
    case SET_EQ_10:
    for (int i = 0; i < 10; i++) {
        ESP_LOGI(TAG, "Gain[%d]: %d", i, device_data_2.audio.gain[i]);
        equalizer_set_gain_info(equalizer, i, device_data_2.audio.gain[i], true);
    }
        break;
    case GET_EQ_1:
    case GET_EQ_2:
    case GET_EQ_3:
    case GET_EQ_4:
    case GET_EQ_5:
    case GET_EQ_6:
    case GET_EQ_7:
    case GET_EQ_8:
    case GET_EQ_9:
    case GET_EQ_10:
        break;
    case GET_DELAY:
        break;
    case SET_DELAY:
        break;
    default:
        break;
	}
}

void webserver_event_handler(void *data, webserver_state_t state, void *event){
	device_data_t *tmp=NULL;
	device_identity_t *ident=NULL;
	if(state==NEW_DEVICE_NAME){
		ident=(device_identity_t*)data;
	}else{
		tmp=(device_data_t*)data;
	}
	switch(state){
		case NEW_SSID_DATA:
			if(tmp!=NULL){
				ESP_LOGI(TAG, "New  SSID: %s PASSWD %s", tmp->ssid, tmp->passwd);
				strcpy(device_data_2.ssid, tmp->ssid);
			    strcpy(device_data_2.passwd, tmp->passwd);
			    device_data_2.WifiMode=0;
			    device_data_save(&device_data_2);
			   	device_data_2.audio.volume=0;
				esp_restart();
			}
			break;
		case NEW_AUDIO_DATA:
			if(tmp!=NULL){
				ESP_LOGI(TAG, "New Audio Data");
			    ESP_LOGI(TAG, "SSID: %s", tmp->ssid);
			    ESP_LOGI(TAG, "Password: %s", tmp->passwd);
			    ESP_LOGI(TAG, "Volume: %d", tmp->audio.volume);
			    ESP_LOGI(TAG, "Muted: %d", tmp->audio.muted);
			    ESP_LOGI(TAG, "Gain:");
			    for (int i = 0; i < 10; i++) {
			        ESP_LOGI(TAG, "Gain[%d]: %d", i, tmp->audio.gain[i]);
			        equalizer_set_gain_info(equalizer, i, tmp->audio.gain[i], true);
			    }
			    device_data_2.audio.balance=tmp->audio.balance * -1;
			    device_data_2.audio.volume=tmp->audio.volume;
			    device_data_2.audio.muted=tmp->audio.muted;
			    device_data_2.audio.output=tmp->audio.output;
			    device_data_save(&device_data_2);
				ESP_LOGI(TAG, "Volume: %d", device_data_2.audio.volume);
				ESP_LOGI(TAG, "Output: %d", device_data_2.audio.output);
				ESP_LOGI(TAG, "Balance: %d", tmp->audio.balance);
			}
			break;
		case NEW_DEVICE_NAME:
			if(ident!=NULL){
				ESP_LOGI(TAG, "New Device Name");
				ESP_LOGI(TAG, "Device Name: %s", ident->device_name);
				device_identity_set_name(&device_identity, ident->device_name);
			}
			break;
		case OTA_UPDATE:
			ota_start("http://192.168.1.156:8000/snapcast_client.bin");
			break;	
		default:
			break;			
	}
}




void network_event_handler(network_event_msg_t *msg, network_status_t state){
	if(state==NETWORK_DOWN){
		ESP_LOGE(TAG, "Network is down!!!!!!!!!!!");
	}else if(state==NETWORK_AP_UP){
		ESP_LOGW(TAG, "Wi-Fi setup AP is up");
		start_filesystem_and_webserver();
		start_webserver(webserver_event_handler, device_data_2);
	}else if(state==NETWORK_UP){
		ESP_LOGE(TAG, "Network is up!!!!!!!!!!!!!");
		start_filesystem_and_webserver();
        start_webserver(webserver_event_handler, device_data_2);
       // wifi_scan();
		initialize_sntp(time_sync_event_handler); 
		set_system_time();
		ESP_LOGI(TAG, "Timer started. It will trigger every 1000ms.");
		tcp_server_start();
	}
	
}

esp_err_t snapcast_stream_event_handler(snapcast_stream_event_msg_t *msg, snapcast_stream_status_t state, void *event_ctx){
	int *tmp;
	switch(state){
	case SNAPCAST_STREAM_STATE_NONE:
			ESP_LOGI(TAG,"Snapcast NONE");
		break;
	case SNAPCAST_STREAM_STATE_CONNECTED:
			ESP_LOGI(TAG,"Snapcast Connected");
		break;
	case SNAPCAST_STREAM_STATE_CHANGING_SONG_MESSAGE:
			ESP_LOGI(TAG,"Snapcast Changing Song");
			//audio_pipeline_reset_ringbuffer(pipeline);
		break;
	case SNAPCAST_STREAM_STATE_TCP_SOCKET_TIMEOUT_MESSAGE:
			ESP_LOGI(TAG,"Snapcast Restarting");
		//	tools_set_audio_volume(board_handle, 0, 0);
			esp_restart();
		break;
	case SNAPCAST_STREAM_STATE_SNTP_MESSAGE:
			ESP_LOGI(TAG,"Get Time");

		break;
	case SNAPCAST_STREAM_STATE_SERVER_SETTINGS_MESAGE:
			tmp=(int*)msg->data;
			ESP_LOGI(TAG,"!!!Snapcast Server Settings!!! Volume: %d Muted: %d",tmp[0], tmp[1]);
			
			device_data_2.audio.volume=tmp[0];
			device_data_2.audio.muted=tmp[1];
			//tools_set_audio_volume(board_handle, tmp[0], 0);
			//audio_hal_set_volume(board_handle->audio_hal,tmp[0]);
			if(tmp[1]==0){
				snapcast_stream_rinbuffer_reset(snapcast_stream_reader);
			}
		//	audio_data.volume=tmp[0];
		//	audio_data.muted=!tmp[1];
			//storage_save_audio_data(&audio_data);
		break;
	case SNAPCAST_STREAM_STATE_RUNNING:
		   // ESP_LOGI(TAG,"Snapcast is Running");
		//    snapcast_running =1;
		break;
	case SNAPCAST_STREAM_STATE_STOP:

		break;
	}
	return ESP_OK;
}
/************************************************************** */
/* Dinge die der Websrver können muss.							*/	
/* 1. Timezone													*/     
/* 2. Timezone													*/
/* 1. Timezone	                                                */
/* 3. Reset        												*/
/* 4. Snapcast Timediff                                         */
// ---------------- app_main ----------------
void app_main(void)
{
	
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
	    // NVS partition was truncated and needs to be erased
	    // Retry nvs_flash_init
	    ESP_ERROR_CHECK(nvs_flash_erase());
	    err = nvs_flash_init();
	}
	
	if (!device_data_load(&device_data_2)) {
	    device_data_reset_defaults(&device_data_2);
	}
	
	if (!device_identity_init(&device_identity)) {
	    ESP_LOGE("MAIN", "Failed to initialize device identity");
	}
	
	esp_log_level_set("*", ESP_LOG_INFO);
	esp_log_level_set(TAG, ESP_LOG_DEBUG);
	esp_log_level_set("CROSSOVER",   ESP_LOG_INFO);
	esp_log_level_set("DIAGNOSTICS", ESP_LOG_INFO);
	esp_log_level_set("SNAPCAST_STREAM", ESP_LOG_WARN);
	
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
	

	audio_hal_set_volume(board_handle->audio_hal, 100);
	ESP_LOGI(TAG, "[2.0] Device Data SSID: %s", device_data_2.ssid);
	ESP_LOGI(TAG, "[2.0] Device Data PASSWD: %s", device_data_2.passwd);
	ESP_LOGI(TAG, "[2.0] Device Data DEVICE NAME: %s", device_identity.device_name);
	ESP_LOGI(TAG, "[2.0] Device Data Wifi Mode: %d", device_data_2.WifiMode);
	ESP_LOGI(TAG, "[2.0] Device Data Volume : %d", device_data_2.audio.volume);
	net_conf.device_data=&device_data_2;
	net_conf.device_identity=&device_identity;
	net_conf.handle_cb=&network_event_handler;
	
	ota_check_pending_verify();
	system_diagnostics_init();
		
	ESP_LOGI(TAG, "[2.0] Create audio pipeline for playback");
	audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
	pipeline = audio_pipeline_init(&pipeline_cfg);
	mem_assert(pipeline);
	
	

	
	ESP_LOGI(TAG, "[2.3] Create flac decoder");
	
	
	device_data_2.audio.output=CROSSOVER_STATE_SUBWOOFER;
	device_data_2.audio.volume=100;
		
	equalizer_cfg_t eq_cfg = DEFAULT_EQUALIZER_CONFIG();
    int set_gain[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    eq_cfg.set_gain = set_gain; // The size of gain array should be the multiplication of NUMBER_BAND and number channels of audio stream data. The minimum of gain is -13 dB.
    eq_cfg.task_core = 1;
    equalizer = equalizer_init(&eq_cfg);
	crossover_cfg_t cr_cfg= CROSSOVER_CFG_DEFAULT();
	cr_cfg.task_core=1;
	cr_cfg.output  = &device_data_2.audio.output;
	cr_cfg.balance = &device_data_2.audio.balance;
	cr_cfg.volume = &device_data_2.audio.volume;
	cr_cfg.mute = &device_data_2.audio.muted;
	crossover = crossover_init(&cr_cfg);
	/*
	flac_decoder_cfg_t flac_cfg = DEFAULT_FLAC_DECODER_CONFIG();
	flac_cfg.task_core=0;
	flac_decoder = flac_decoder_init(&flac_cfg);
	*/
	
	micro_flac_decoder_cfg_t flac_cfg = MICRO_FLAC_DECODER_DEFAULT_CONFIG();
	flac_decoder = micro_flac_decoder_init(&flac_cfg);
	
	time_delay=720;
	
	snapcast_stream_cfg_t snapcast_cfg = SNAPCAST_STREAM_CFG_DEFAULT();
	snapcast_cfg.type = AUDIO_STREAM_READER;
	//snapcast_cfg.time_delay=&time_delay;
	snapcast_cfg.port = 1704;
	snapcast_cfg.host = "192.168.1.145";
	snapcast_cfg.state= SNAPCAST_STREAM_STATE_NONE;
	snapcast_cfg.hostname = device_identity.device_name;
	snapcast_cfg.task_core = 0;
	snapcast_cfg.event_handler=snapcast_stream_event_handler;
	snapcast_stream_reader = snapcast_stream_init(&snapcast_cfg);
	AUDIO_NULL_CHECK(TAG, snapcast_stream_reader, return);

/*#ifndef AI_THINKER_BOARD
//I2S0 -> DAC PACM5102
	i2s_stream_cfg_t i2s_stream_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_stream_cfg.chan_cfg.id=I2S_NUM_0;

	i2s_stream_cfg.std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
	i2s_stream_cfg.std_cfg.clk_cfg.sample_rate_hz=48000;
	i2s_stream_cfg.std_cfg.clk_cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;
	i2s_stream_cfg.std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
	i2s_stream_cfg.std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
	i2s_stream_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;  
	i2s_stream_cfg.std_cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_16BIT;
	i2s_stream_cfg.std_cfg.slot_cfg.ws_pol = false;
	i2s_stream_cfg.std_cfg.slot_cfg.bit_shift = false;
	i2s_stream_cfg.std_cfg.slot_cfg.msb_right = true;
	
	i2s_stream_cfg.std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED; 
	i2s_stream_cfg.std_cfg.gpio_cfg.bclk = GPIO_NUM_33;
	i2s_stream_cfg.std_cfg.gpio_cfg.ws   = GPIO_NUM_12;
	i2s_stream_cfg.std_cfg.gpio_cfg.dout = GPIO_NUM_5;
	i2s_stream_cfg.std_cfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
	i2s_stream_cfg.std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
	i2s_stream_cfg.std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
	i2s_stream_cfg.std_cfg.gpio_cfg.invert_flags.ws_inv = false;
	
	i2s_stream_cfg.type = AUDIO_STREAM_WRITER;  // Modus: Schreibe auf DAC
//	i2s_stream_cfg.port = I2S_NUM_0;   // Verwenden Sie den ersten I²S-Port
	i2s_stream_cfg.task_stack = 8192;
//	i2s_stream_cfg.    // Stapelgröße des Streams
	i2s_stream_cfg.task_core = 1;
	i2s_stream_cfg.task_prio = 7;
	i2s_stream_cfg.buffer_len = 12 * 682; // 8184 ≈ 8kB
	i2s_stream_cfg.out_rb_size = 12 * 682; // passend dazu
	i2s_stream_cfg.chan_cfg.dma_desc_num = 8;
	i2s_stream_cfg.chan_cfg.dma_frame_num = 512;
		         // CPU-Kern für diesen Task
	//	i2s_stream_cfg.std_cfg.dma_buf_len=300;
	//    i2s_stream_cfg.i2s_config.dma_buf_count=6;
		 // Größe des Ringbuffers für Audio-Daten
	i2s_stream_writer = i2s_stream_init(&i2s_stream_cfg);	
//I2S1 ->ADC PCM1808

	i2s_stream_cfg_t adc_cfg = I2S_STREAM_CFG_DEFAULT();
	
	
	adc_cfg.chan_cfg.id = I2S_NUM_1;
	
	adc_cfg.std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
	adc_cfg.std_cfg.clk_cfg.sample_rate_hz = 48000;
	adc_cfg.std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
	
	
	adc_cfg.std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;   // PCM1808 = 16 Bit
	adc_cfg.std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;    // ESP32 wählt selbst (meist 32)
	adc_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;               // Stereo
	adc_cfg.std_cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_16BIT;
	adc_cfg.std_cfg.slot_cfg.ws_pol = false;          // Standard I2S
	adc_cfg.std_cfg.slot_cfg.bit_shift = true;        // WICHTIG: PCM1808 benötigt 1-bit shift (I2S-Standard)
	adc_cfg.std_cfg.slot_cfg.msb_right = false;       // Standard: MSB zuerst
	
	adc_cfg.std_cfg.gpio_cfg.mclk = GPIO_NUM_0;       // MCLK
	adc_cfg.std_cfg.gpio_cfg.bclk = GPIO_NUM_14;      // BCK
	adc_cfg.std_cfg.gpio_cfg.ws   = GPIO_NUM_13;      // LRCK
	adc_cfg.std_cfg.gpio_cfg.din  = GPIO_NUM_32;      // PCM1808 OUT → ESP32
	adc_cfg.std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
	
	adc_cfg.type = AUDIO_STREAM_READER;
	
	
	adc_cfg.task_stack = 8192;
	adc_cfg.task_core = 1;
	adc_cfg.task_prio = 7;
	adc_cfg.buffer_len = 12 * 682;
	adc_cfg.out_rb_size = 12 * 682;
	
	adc_cfg.chan_cfg.dma_desc_num = 8;
	adc_cfg.chan_cfg.dma_frame_num = 512;
	
	i2s_stream_reader = i2s_stream_init(&adc_cfg);
*/

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
	i2s_stream_set_clk(i2s_stream_writer, 44100, 16, 2);
	
	json_rpc_config_t rpc_config;
	rpc_config.device_data=&device_data_2;
	rpc_config.time_delay=&time_delay;
	json_rpc_init(&rpc_config, rpc_event_handler);	
	
	
	ESP_LOGI(TAG, "[2.4] Register all elements to audio pipeline");
	
	audio_pipeline_register(pipeline, snapcast_stream_reader, "snapcast");
	audio_pipeline_register(pipeline, crossover,              "crossover");
	audio_pipeline_register(pipeline, equalizer,              "eq");
	audio_pipeline_register(pipeline, flac_decoder,           "flac");
	audio_pipeline_register(pipeline, i2s_stream_writer,      "i2s");
	
	
	const char *link_tag[5] = {
						    "snapcast",
						    "flac",
						    "eq",
						    "crossover",
						    "i2s"
						};

    audio_pipeline_link(pipeline, &link_tag[0], 5);
	//snapcast_stream_set_delay(snapcast_stream_reader, 720);
		
	// Example of using an audio event -- START
    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
	esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
	esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);
	network_open(&net_conf);
	
	const esp_timer_create_args_t timer_args = {
        .callback = system_wdt_timer_callback,      // Callback-Funktion
        .name = "periodic_timer"         // Name des Timers
    };

    esp_timer_handle_t timer_handle;

    // Timer erstellen
    err = esp_timer_create(&timer_args, &timer_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer");
        return;
    }
	err = esp_timer_start_periodic(timer_handle, 1000000);  // 1000000 µs = 1000 ms
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer");
        return;
    }
	//xTaskCreate(diagnostics_task, "diagnostics", 3072, NULL, 1, NULL);
	
	while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        
    }
    // Example of using an audio event -- END

    ESP_LOGI(TAG, "[ 6 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
   

    audio_pipeline_remove_listener(pipeline);

    /* Stop all peripherals before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(i2s_stream_writer);
  
    esp_periph_set_destroy(set);

}
