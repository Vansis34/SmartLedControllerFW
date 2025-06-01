/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "protocol_examples_utils.h"
#include <esp_http_server.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/ledc.h"
// #include "esp_cpu.h"

#include "Config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#if !CONFIG_IDF_TARGET_LINUX
#include <esp_wifi.h>
#include <esp_system.h>
#include "nvs_flash.h"
#include "esp_eth.h"
#endif  // !CONFIG_IDF_TARGET_LINUX

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN  (64)
#define LED_1CH_PIN (14)
#define LED_2CH_PIN (12)


/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *TAG = "example";

typedef enum ledMode_e
{
    ALLTIME = 1,
    FADE,
    FADE_ALTERNATELY,
    HALF_FADE_ALTERNATELY,
}ledMode_t;

typedef struct device_status_s {
    int led_state; // 0 = выключено, 1 = включено
    uint8_t ledBrightness;
    ledMode_t mode;
    uint16_t brightness_1ch;
    uint16_t brightness_2ch;

} device_status_t;

static device_status_t device = {0, 100, 1, 1023, 1023}; // Начальное состояние лампы

void Init()
{   
    // gpio_reset_pin(LED_1CH_PIN);
    // gpio_reset_pin(LED_2CH_PIN);

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_HIGH_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = 3000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel_1 = {
        .channel    = LEDC_CHANNEL_1,
        .duty       = 0,
        .gpio_num   = LED_1CH_PIN,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel_1);

    ledc_channel_config_t ledc_channel_2 = {
        .channel    = LEDC_CHANNEL_2,
        .duty       = 0,
        .gpio_num   = LED_2CH_PIN,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel_2);
    ledc_fade_func_install(0);
}




// --------------- HTTP Server --------------------------------------
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_OK;
}
/* An HTTP GET handler */
esp_err_t get_status_handler(httpd_req_t *req) {
    char response[70];
    device_status_t *device = (device_status_t *)req->user_ctx; // Получаем указатель
    // на структуру device_status_t из req->user_ctx
    snprintf(response, sizeof(response), "{\"lamp\":%d, \"brigh\":%d, \"mode\":%d}", device->led_state, device->ledBrightness, device->mode); // Получаем состояние лампы

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

httpd_uri_t uri_get_status = {
    .uri       = "/status",
    .method    = HTTP_GET,
    .handler   = get_status_handler,
    .user_ctx  = &device
};


esp_err_t get_page_handler(httpd_req_t *req) {

    extern const unsigned char index_html_gz_start[] asm("_binary_index_html_gz_start");
    extern const unsigned char index_html_gz_end[] asm("_binary_index_html_gz_end");
    size_t index_html_gz_len = (index_html_gz_end - index_html_gz_start);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char*)index_html_gz_start, index_html_gz_len);
}

httpd_uri_t uri_get_page = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = get_page_handler,
    .user_ctx  = NULL
};



// функция включения лампы
void setLedCtrl(uint8_t lampState)
{   
    ESP_LOGI("URI","LED_state_chng, %d", lampState );
    device.led_state = lampState;
    // ESP_ERROR_CHECK(gpio_set_level(RELEY_PIN, lampState));
    if (lampState == 1)
    {
        switch(device.mode)
        {
            case ALLTIME:
            {
                ESP_LOGI("mode","ALLTIME");
                device.brightness_1ch = (1023 * device.ledBrightness) / 100;
                device.brightness_2ch = (1023 * device.ledBrightness) / 100;
                ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, device.brightness_1ch, 2000);
                ledc_fade_start(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT);
                ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, device.brightness_2ch, 2000);
                ledc_fade_start(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, LEDC_FADE_NO_WAIT);
            }
            break;
            case FADE:
            {
                
                ESP_LOGW("LED_CTRL", "mode is not avaible");
            }
            break;
            case FADE_ALTERNATELY:
            {
                
                ESP_LOGW("LED_CTRL", "mode is not avaible");
            }
            break;
            case HALF_FADE_ALTERNATELY:
            {
                
                ESP_LOGW("LED_CTRL", "mode is not avaible");
            }
            break;
            default:
            {
                ESP_LOGW("LED_CTRL", "mode is not avaible");
            }
        }
    }else
    {
        // остановвить режим моргания
        ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0, 1000);
        ledc_fade_start(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT);
        ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, 0, 1000);
        ledc_fade_start(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, LEDC_FADE_NO_WAIT);
    }
    
}


/* An HTTP PUT handler. This demonstrates realtime
 * registration and deregistration of URI handlers
 */
static esp_err_t led_ctrl_put_handler(httpd_req_t *req)
{
    char* buf = malloc(req->content_len + 1);
    ESP_LOGI("JSON", "json len: %d", req->content_len);
    int8_t ret = httpd_req_recv(req, buf, req->content_len);
    ESP_LOGI("JSON", "ret: %d",ret);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';
    ESP_LOGI("JSON", "Received JSON: %s", buf);
    int lamp_state;
    sscanf(buf, "{\"state\":%d}", &lamp_state); // Разбираем JSON
    setLedCtrl((uint8_t)lamp_state); // Функция для управления лампой

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}


static const httpd_uri_t uri_ctrl_led = {
    .uri       = "/led/ctrl",
    .method    = HTTP_PUT,
    .handler   = led_ctrl_put_handler,
    .user_ctx  = NULL
};


void updateBrighness()
{
    device.brightness_1ch = (1023 * device.ledBrightness) / 100;
    device.brightness_2ch = (1023 * device.ledBrightness) / 100;
    if (device.led_state == 1 && device.mode == ALLTIME)
    {
        ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, device.brightness_1ch, 2000);
        ledc_fade_start(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT);
        ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, device.brightness_2ch, 2000);
        ledc_fade_start(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, LEDC_FADE_NO_WAIT);
}
}


static esp_err_t led_settings_put_handler(httpd_req_t *req)
{
    char* buf = malloc(req->content_len + 1);
    ESP_LOGI("JSON", "json len: %d", req->content_len);
    int8_t ret = httpd_req_recv(req, buf, req->content_len);
    ESP_LOGI("JSON", "ret: %d",ret);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';
    ESP_LOGI("JSON", "Received JSON: %s", buf);
    uint8_t brighBuff;
    uint8_t modeBuff;

    sscanf(buf, "{\"brigh\":%d, \"mode\":%d}", (int *)&brighBuff, (int*)&modeBuff); // Разбираем JSON

    ESP_LOGI("SETTINGS", "Brightness: %d, Mode: %d", brighBuff, modeBuff);
    if(brighBuff != device.ledBrightness)
    {
        device.ledBrightness = brighBuff;
        updateBrighness(); // Функция изменения яркости
    }

    if(modeBuff != device.mode)
    {
        device.mode = (ledc_mode_t)modeBuff;
        //смена режима
    }


    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}


static const httpd_uri_t uri_settings_led = {
    .uri       = "/led/settings",
    .method    = HTTP_PUT,
    .handler   = led_settings_put_handler,
    .user_ctx  = NULL
};








static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#if CONFIG_IDF_TARGET_LINUX
    // Setting port as 8001 when building for Linux. Port 80 can be used only by a priviliged user in linux.
    // So when a unpriviliged user tries to run the application, it throws bind error and the server is not started.
    // Port 8001 can be used by an unpriviliged user as well. So the application will not throw bind error and the
    // server will be started.
    config.server_port = 8001;
#endif // !CONFIG_IDF_TARGET_LINUX
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &uri_get_page);
        httpd_register_uri_handler(server, &uri_ctrl_led);
        httpd_register_uri_handler(server, &uri_get_status);
        httpd_register_uri_handler(server, &uri_settings_led);
        // httpd_register_uri_handler(server, &ctrl);
        #if CONFIG_EXAMPLE_BASIC_AUTH
        httpd_register_basic_auth(server);
        #endif
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

#if !CONFIG_IDF_TARGET_LINUX
static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI("WIFI", "Подключено! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}
#endif // !CONFIG_IDF_TARGET_LINUX

void app_main(void)
{
    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    Init();
    // gpio_reset_pin(RELEY_PIN);
    // gpio_set_direction(RELEY_PIN, GPIO_MODE_OUTPUT);
    // gpio_set_level(RELEY_PIN, 0);

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
#if !CONFIG_IDF_TARGET_LINUX
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET
#endif // !CONFIG_IDF_TARGET_LINUX

    /* Start the server for the first time */
    server = start_webserver();

    while (server) {
        sleep(5);
    }
}
