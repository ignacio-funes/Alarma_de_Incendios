#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_http_server.h"

// Define pins
#define LED_GPIO GPIO_NUM_2
#define SENSOR1_CHANNEL ADC1_CHANNEL_6  // GPIO34
#define SENSOR2_CHANNEL ADC1_CHANNEL_7  // GPIO35

// WiFi credentials
#define WIFI_AP_SSID      "yourAP"
#define WIFI_AP_PASS      "yourPassword"
#define WIFI_AP_CHANNEL   1
#define MAX_STA_CONN      4

static const char *TAG = "wifi_softAP";
static bool led_state = false;

// HTML Page
static const char *HTML_PAGE = "<!DOCTYPE html><html>"
    "<head><title>ESP32 Control Panel</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 20px; text-align: center; }"
    ".button { background-color: #4CAF50; border: none; color: white; padding: 15px 32px; "
    "text-align: center; text-decoration: none; display: inline-block; font-size: 16px; "
    "margin: 4px 2px; cursor: pointer; border-radius: 4px; }"
    ".sensor-card { background-color: #f0f0f0; padding: 20px; margin: 10px; border-radius: 8px; }"
    "</style>"
    "<script>"
    "function updateSensors() {"
    "  fetch('/sensors')"
    "    .then(response => response.text())"
    "    .then(data => {"
    "      const values = data.split(',');"
    "      document.getElementById('sensor1').innerHTML = values[0];"
    "      document.getElementById('sensor2').innerHTML = values[1];"
    "    });"
    "}"
    "setInterval(updateSensors, 2000);"
    "</script></head>"
    "<body>"
    "<h1>ESP32 Control Panel</h1>"
    "<div class='sensor-card'>"
    "<h2>Sensor 1</h2>"
    "<p>Value: <span id='sensor1'>0</span></p>"
    "</div>"
    "<div class='sensor-card'>"
    "<h2>Sensor 2</h2>"
    "<p>Value: <span id='sensor2'>0</span></p>"
    "</div>"
    "<p>LED Control:</p>"
    "<a class='button' style='background-color: #4CAF50;' href='/H'>Turn ON</a>&nbsp;"
    "<a class='button' style='background-color: #f44336;' href='/L'>Turn OFF</a>"
    "</body></html>";

// HTTP GET Handler for main page
static esp_err_t main_page_handler(httpd_req_t *req)
{
    httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
    return ESP_OK;
}

// HTTP GET Handler for sensor data
static esp_err_t sensor_data_handler(httpd_req_t *req)
{
    char response[32];
    int sensor1_value = adc1_get_raw(SENSOR1_CHANNEL);
    int sensor2_value = adc1_get_raw(SENSOR2_CHANNEL);
    
    sprintf(response, "%d,%d", sensor1_value, sensor2_value);
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// HTTP GET Handler for LED control
static esp_err_t led_handler(httpd_req_t *req)
{
    if (strcmp(req->uri, "/H") == 0) {
        gpio_set_level(LED_GPIO, 1);
        led_state = true;
    } else if (strcmp(req->uri, "/L") == 0) {
        gpio_set_level(LED_GPIO, 0);
        led_state = false;
    }
    
    // Redirect back to main page
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// HTTP Server Configuration
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the server
    if (httpd_start(&server, &config) == ESP_OK) {
        // URI handlers
        httpd_uri_t main_page = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = main_page_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &main_page);

        httpd_uri_t sensors = {
            .uri       = "/sensors",
            .method    = HTTP_GET,
            .handler   = sensor_data_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &sensors);

        httpd_uri_t led_on = {
            .uri       = "/H",
            .method    = HTTP_GET,
            .handler   = led_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &led_on);

        httpd_uri_t led_off = {
            .uri       = "/L",
            .method    = HTTP_GET,
            .handler   = led_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &led_off);
    }
    return server;
}

// WiFi Event Handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

// WiFi Access Point Configuration
static void wifi_init_softap(void)
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
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL);
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Configure LED GPIO
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    // Configure ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(SENSOR1_CHANNEL, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(SENSOR2_CHANNEL, ADC_ATTEN_DB_11);

    // Initialize WiFi
    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    // Start HTTP Server
    start_webserver();
}
