/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "lwip/err.h"
#include "lwip/sys.h"



#define EXAMPLE_ESP_WIFI_SSID      "yourAP"
#define EXAMPLE_ESP_WIFI_PASS      "yourPassword"
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

#define GAS_SENSOR_ADC_UNIT    ADC_UNIT_1
#define GAS_SENSOR_ADC_CHANNEL ADC_CHANNEL_6    // GPIO34
#define IR_SENSOR_ADC_CHANNEL  ADC_CHANNEL_7    // GPIO35
#define ADC_ATTEN             ADC_ATTEN_DB_11
#define ADC_BITWIDTH          ADC_BITWIDTH_12

#define RED_LED_GPIO               GPIO_NUM_32
#define GREEN_LED_GPIO             GPIO_NUM_33
#define BUZZER_GPIO               GPIO_NUM_5

#define GAS_THRESHOLD             300
#define IR_THRESHOLD              3900
#define SENSOR_CHECK_INTERVAL_MS  500



static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool do_calibration;

static bool alarm_state = false;

static const char *TAG = "wifi softAP";



static void init_gpio(void);
static bool init_adc(void);
static void update_outputs(bool green_state);
static int get_adc_reading_average(int channel);
static void check_sensors_task(void *pvParameters);
static httpd_handle_t start_webserver(void);



static void init_gpio(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RED_LED_GPIO) | (1ULL << GREEN_LED_GPIO) | (1ULL << BUZZER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    update_outputs(false);  // Initialize to red state
}

static bool init_adc(void)
{
    esp_err_t ret;
    bool cali_enable = false;

    // ADC1 Init
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // ADC1 Config
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, GAS_SENSOR_ADC_CHANNEL, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, IR_SENSOR_ADC_CHANNEL, &config));

    // Calibration Init
    #if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1, 
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle);
    if (ret == ESP_OK) {
        cali_enable = true;
    }
    #endif

    do_calibration = cali_enable;
    return true;
}

static void update_outputs(bool green_state)
{
    gpio_set_level(GREEN_LED_GPIO, green_state);
    gpio_set_level(RED_LED_GPIO, !green_state);
    gpio_set_level(BUZZER_GPIO, !green_state);
}

static int get_adc_reading_average(int channel)
{
    const int samples = 10;
    int sum = 0;
    int voltage;
    
    for (int i = 0; i < samples; i++) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, channel, &raw));
        
        if (do_calibration) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, raw, &voltage));
            sum += voltage;
        } else {
            sum += raw;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    
    return sum / samples;
}

static void check_sensors_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        int gas_value = get_adc_reading_average(GAS_SENSOR_ADC_CHANNEL);
        int ir_value = get_adc_reading_average(IR_SENSOR_ADC_CHANNEL);
        
        // If using calibrated values, adjust thresholds accordingly
        if (do_calibration) {
            alarm_state = (gas_value >= GAS_THRESHOLD * 3.3 / 4095) || 
                         (ir_value <= IR_THRESHOLD * 3.3 / 4095);
        } else {
            alarm_state = (gas_value >= GAS_THRESHOLD) || (ir_value <= IR_THRESHOLD);
        }
        
        update_outputs(!alarm_state);
        
        ESP_LOGI(TAG, "Gas: %d, IR: %d, Alarm: %d", gas_value, ir_value, alarm_state);
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SENSOR_CHECK_INTERVAL_MS));
    }
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char resp[32];
    int gas_value = get_adc_reading_average(GAS_SENSOR_ADC_CHANNEL);
    int ir_value = get_adc_reading_average(IR_SENSOR_ADC_CHANNEL);
    
    // If calibrated, convert to mV for display
    if (do_calibration) {
        snprintf(resp, sizeof(resp), "%d,%d,%d\n", 
                gas_value, // Already in mV
                ir_value,  // Already in mV
                alarm_state ? 1 : 0);
    } else {
        snprintf(resp, sizeof(resp), "%d,%d,%d\n", 
                gas_value,
                ir_value,
                alarm_state ? 1 : 0);
    }
    
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t root_handler(httpd_req_t *req)
{
    const char* html = get_html_content();  // Define this function with the HTML from previous example
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    httpd_uri_t root = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = root_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t status = {
        .uri       = "/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = NULL
    };

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &status);
        return server;
    }
    return NULL;
}

static void deinit_adc(void)
{
    if (do_calibration) {
        #if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(adc1_cali_handle);
        #endif
    }
    
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
    httpd_handle_t server = start_webserver();
        if (server == NULL) {
         ESP_LOGE(TAG, "Failed to start web server!");
    }
}



static const char* get_html_content(void)
{
    return "<!DOCTYPE html><html>"
           "<head><title>Alarm Monitor</title>"
           "<meta name='viewport' content='width=device-width, initial-scale=1'>"
           "<style>"
           "body { font-family: Arial, sans-serif; margin: 20px; text-align: center; }"
           ".sensor-card { background-color: #f0f0f0; padding: 20px; margin: 10px; border-radius: 8px; }"
           ".alarm-text { font-size: 24px; font-weight: bold; margin: 20px; padding: 10px; border-radius: 4px; }"
           ".alarm-active { background-color: #ffebee; color: #d32f2f; }"
           ".alarm-inactive { background-color: #e8f5e9; color: #2e7d32; }"
           "</style>"
           "<script>"
           "function updateValues() {"
           "  fetch('/status')"
           "    .then(response => response.text())"
           "    .then(data => {"
           "      const values = data.split(',');"
           "      document.getElementById('sensorGas').innerHTML = values[0];"
           "      document.getElementById('sensorIR').innerHTML = values[1];"
           "      const alarmState = values[2];"
           "      const alarmText = document.getElementById('alarmText');"
           "      if(alarmState.trim() === '1') {"
           "        alarmText.className = 'alarm-text alarm-active';"
           "        alarmText.innerHTML = 'ALARM ON';"
           "      } else {"
           "        alarmText.className = 'alarm-text alarm-inactive';"
           "        alarmText.innerHTML = 'ALARM OFF';"
           "      }"
           "    })"
           "    .catch(error => console.error('Error:', error));"
           "}"
           "updateValues();"
           "setInterval(updateValues, 2000);"
           "</script></head>"
           "<body>"
           "<h1>Alarm Monitor</h1>"
           "<div id='alarmText' class='alarm-text'>Loading alarm state...</div>"
           "<div class='sensor-card'>"
           "<h2>Sensor de Gas</h2>"
           "<p>Value: <span id='sensorGas'>0</span></p>"
           "</div>"
           "<div class='sensor-card'>"
           "<h2>Sensor IR</h2>"
           "<p>Value: <span id='sensorIR'>0</span></p>"
           "</div>"
           "</body></html>";
}



void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    init_gpio();
    if (!init_adc()) {
        ESP_LOGE(TAG, "ADC initialization failed");
        return;
    }
    
    xTaskCreate(check_sensors_task, "sensor_check", 4096, NULL, 5, NULL);
}
