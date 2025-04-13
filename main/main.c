/* Play an MP3 file from HTTP

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "main.h"
#include <stdint.h>
#include <string.h>
#include <sys/_timeval.h>
#include <sys/time.h>
#include <sys/time.h>
#include "audio_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "flac_decoder.h"
#include "mp3_decoder.h"
#include "sys/time.h"
#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"
#include "esp_netif_sntp.h"
#include "sntp_client.h"
#include "snapcast_stream.h"
#include "wifi_interface.h"
#include "esp_timer.h"
#include "equalizer.h"
#include "crossover.h"
#include "webserver.h"
#include "device_data.h"
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"

#else
#include "tcpip_adapter.h"
#endif

static const char *TAG = "MAIN";
uint8_t pipline_is_playing=0;

audio_pipeline_handle_t pipeline;
audio_element_handle_t i2s_stream_writer, flac_decoder, equalizer, snapcast_stream_reader, crossover;
device_data_t   device_data;

esp_timer_handle_t timer_handle;

void webserver_event_handler(device_data_t *device_data, webserver_state_t state, void *event);

void time_sync_event_handler(void);
static void system_wdt_timer_callback(void* arg) {
    struct timeval now;
    gettimeofday(&now, NULL);  // Ruft die aktuelle Zeit ab

    // Epoch Sekunden aus dem struct timeval holen
    long epoch_seconds = now.tv_sec;
	//wifi_sta_list_t wifi_sta;
    wifi_ap_record_t ap_info;
    esp_wifi_sta_get_ap_info(&ap_info);
    // Prüfen, ob die aktuelle Sekunde eine volle Minute ist
    if (!(now.tv_sec % 1800)) {
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
		pipline_is_playing=1;
	}
	     
}


// STA Event-Handler
static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Wi-Fi disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        start_webserver(webserver_event_handler, device_data);
        wifi_scan();
        initialize_sntp(time_sync_event_handler); 
        
        set_system_time();
        

    	ESP_LOGI(TAG, "Timer started. It will trigger every 1000ms.");
    }
}

// AP Event-Handler
static void wifi_ap_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "Access Point started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
        ESP_LOGI(TAG, "Access Point stopped");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "Station connected: AID=%d", event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "Station disconnected: AID=%d", event->aid);
    }
}

void webserver_event_handler(device_data_t *data, webserver_state_t state, void *event){
	switch(state){
		case NEW_SSID_DATA:
			ESP_LOGI(TAG, "New  SSID: %s PASSWD %s", data->ssid, data->passwd);
			break;
		case NEW_AUDIO_DATA:
			ESP_LOGI(TAG, "New Audio Data");
			ESP_LOGI(TAG, "Device Name: %s", data->device_name);
		    ESP_LOGI(TAG, "SSID: %s", data->ssid);
		    ESP_LOGI(TAG, "Password: %s", data->passwd);
		    ESP_LOGI(TAG, "Volume: %d", data->audio.volume);
		    ESP_LOGI(TAG, "Muted: %d", data->audio.muted);
		    ESP_LOGI(TAG, "Gain:");
		    for (int i = 0; i < 10; i++) {
		        ESP_LOGI(TAG, "Gain[%d]: %d", i, data->audio.gain[i]);
		        equalizer_set_gain_info(equalizer, i, data->audio.gain[i], true);
		    }
		    ESP_LOGI(TAG, "Output: %d", data->audio.output);
		    ESP_LOGI(TAG, "Balance: %d", data->audio.balance);
		    device_data.audio.balance=data->audio.balance * -1;
		    device_data.audio.volume=data->audio.volume;
		    device_data.audio.muted=data->audio.muted;
			break;
		case NEW_DEVICE_NAME:
			break;
		default:
			break;			
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
			
			device_data.audio.volume=tmp[0];
			device_data.audio.muted=tmp[1];
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


void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

   

    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("SNAPCAST_STREAM", ESP_LOG_INFO);
    esp_log_level_set("webserver", ESP_LOG_INFO);
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_ap_event_handler, NULL, NULL));
  /* ESP_LOGI(TAG, "[ 1 ] Start audio codec chip");*/
   ESP_LOGI(TAG, "[ 1 ] Start audio codec chip");
#ifdef AI_Thinker_Dev_Kit
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
#endif
 
  

    ESP_LOGI(TAG, "[2.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

   

    ESP_LOGI(TAG, "[2.2] Create i2s stream to write data to codec chip");
   // I²S-Stream initialisieren
#ifndef AI_Thinker_Dev_Kit 
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
	i2s_stream_cfg.task_stack = 4096;
//	i2s_stream_cfg.    // Stapelgröße des Streams
	i2s_stream_cfg.task_core = 1;
	i2s_stream_cfg.chan_cfg.dma_desc_num = 8;
	i2s_stream_cfg.chan_cfg.dma_frame_num = 256;
	         // CPU-Kern für diesen Task
//	i2s_stream_cfg.std_cfg.dma_buf_len=300;
//    i2s_stream_cfg.i2s_config.dma_buf_count=6;
	 // Größe des Ringbuffers für Audio-Daten
	
	i2s_stream_writer = i2s_stream_init(&i2s_stream_cfg);
#else
ESP_LOGW(TAG, "[2.4] Create i2s stream to write data to codec chip");
	 ESP_LOGI(TAG, "[2.2] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
	i2s_stream_set_clk(i2s_stream_writer, 48000, 16, 2);
#endif	
	snapcast_stream_cfg_t snapcast_cfg = SNAPCAST_STREAM_CFG_DEFAULT();
	snapcast_cfg.type = AUDIO_STREAM_READER;
	snapcast_cfg.port = 1704;
	snapcast_cfg.host = "192.168.1.145";
	snapcast_cfg.state= SNAPCAST_STREAM_STATE_NONE;
	//snapcast_cfg.volume =&audio_data.volume;
	//snapcast_cfg.muted  =&audio_data.muted;
	snapcast_cfg.task_core = 0;
	//snapcast_cfg.wifi_data=&wifi_data;
	//snapcast_cfg.bh     =board_handle;
	//snapcast_cfg.event_queue=event_queue;
	snapcast_cfg.event_handler=snapcast_stream_event_handler;
	snapcast_stream_reader = snapcast_stream_init(&snapcast_cfg);
	AUDIO_NULL_CHECK(TAG, snapcast_stream_reader, return);
	
	equalizer_cfg_t eq_cfg = DEFAULT_EQUALIZER_CONFIG();
    int set_gain[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    eq_cfg.set_gain = set_gain; // The size of gain array should be the multiplication of NUMBER_BAND and number channels of audio stream data. The minimum of gain is -13 dB.
    eq_cfg.task_core = 1;
    equalizer = equalizer_init(&eq_cfg);
  
	crossover_cfg_t cr_cfg= CROSSOVER_CFG_DEFAULT();
	cr_cfg.task_core=0;
	//cr_cfg.output  = &audio_data.output;
	cr_cfg.balance = &device_data.audio.balance;
	cr_cfg.volume = &device_data.audio.volume;
	cr_cfg.mute = &device_data.audio.muted;
	crossover = crossover_init(&cr_cfg);
	
	flac_decoder_cfg_t flac_cfg = DEFAULT_FLAC_DECODER_CONFIG();
	flac_cfg.out_rb_size =16384;
	flac_cfg.task_core=1;
	
	flac_decoder = flac_decoder_init(&flac_cfg);
	
	
    ESP_LOGI(TAG, "[2.4] Register all elements to audio pipeline");
 	audio_pipeline_register(pipeline, crossover,  "crossover");
    audio_pipeline_register(pipeline, equalizer,  "eq");
    audio_pipeline_register(pipeline, snapcast_stream_reader, "snapcast");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");
    audio_pipeline_register(pipeline, flac_decoder,        "flac");



    ESP_LOGI(TAG, "[2.5] Link it together snapcast_stream-->flac_decoder-->crossover-->equalizer-->i2s_stream-->[codec_chip]");
    //const char *link_tag[3] = {"snapcast", "flac", "eq", "i2s"};
    //audio_pipeline_link(pipeline, &link_tag[0], 3);
	audio_pipeline_link(pipeline, (const char *[]) {"snapcast","flac", "eq", "crossover","i2s"}, 5);
#ifdef AI_Thinker_Dev_Kit
    audio_hal_set_volume(board_handle->audio_hal, 100);
#endif   
    //audio_hal_set_volume(board_handle->audio_hal, 100);
    ESP_LOGI(TAG, "[ 3 ] Start and wait for Wi-Fi network");
    ESP_ERROR_CHECK(wifi_connect_sta());

	esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
	esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
	
	
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
    
    // Example of using an audio event -- START
    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    

    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
           
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            continue;
        }

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            break;
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
