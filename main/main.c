#include "AppState.h"
#include "Button.h"
#include "LEDDriver.h"
#include "WEBModule.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

#if !CONFIG_IDF_TARGET_LINUX
#include "esp_eth.h"
#include "esp_wifi.h"
#endif

static const char *TAG = "MAIN";

/**
 * @brief Initialize NVS and recover the two documented incompatible states.
 *
 * ESP-IDF can require erasing NVS after its internal format changes or when no
 * free pages remain. Other failures are returned without erasing user data.
 *
 * @return ESP_OK or the NVS initialization/erase error.
 */
static esp_err_t initialize_nvs(void)
{
    esp_err_t error = nvs_flash_init();
    if (error != ESP_ERR_NVS_NO_FREE_PAGES &&
        error != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        return error;
    }

    ESP_LOGW(TAG, "NVS layout is incompatible, erasing and retrying");
    error = nvs_flash_erase();
    if (error != ESP_OK) {
        return error;
    }
    return nvs_flash_init();
}

/**
 * @brief Initialize state consumers that do not depend on network connectivity.
 *
 * The button receives no factory-reset callback yet because persistent
 * configuration reset belongs to backlog task N-03. Short presses are already
 * fully functional and toggle AppState power.
 *
 * @return ESP_OK or the first module initialization error.
 */
static esp_err_t initialize_application_modules(void)
{
    esp_err_t error = AppState_Init(NULL);
    if (error != ESP_OK) {
        return error;
    }
    error = LED_Init();
    if (error != ESP_OK) {
        return error;
    }
    return Button_Init(NULL);
}

#if !CONFIG_IDF_TARGET_LINUX
/**
 * @brief Stop the HTTP server after the active network link is disconnected.
 * @param arg Pointer to the shared HTTP server handle.
 * @param event_base Event base supplied by ESP-IDF.
 * @param event_id Event identifier supplied by ESP-IDF.
 * @param event_data Event payload supplied by ESP-IDF.
 */
static void disconnect_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)event_base;
    (void)event_id;
    (void)event_data;

    httpd_handle_t *server = arg;
    if (*server == NULL) {
        return;
    }

    ESP_LOGI(TAG, "network disconnected, stopping web server");
    if (WEB_stop_webserver(*server) == ESP_OK) {
        *server = NULL;
    } else {
        ESP_LOGE(TAG, "failed to stop web server");
    }
}

/**
 * @brief Start the HTTP server after the station receives an IPv4 address.
 * @param arg Pointer to the shared HTTP server handle.
 * @param event_base Event base supplied by ESP-IDF.
 * @param event_id Event identifier supplied by ESP-IDF.
 * @param event_data Pointer to ip_event_got_ip_t.
 */
static void connect_handler(void *arg,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            void *event_data)
{
    (void)event_base;
    (void)event_id;

    const ip_event_got_ip_t *event = event_data;
    ESP_LOGI(TAG, "Wi-Fi connected, IPv4: " IPSTR,
             IP2STR(&event->ip_info.ip));

    httpd_handle_t *server = arg;
    if (*server == NULL) {
        *server = WEB_start_webserver();
    }
}
#endif

/**
 * @brief Firmware entry point.
 *
 * For this test iteration protocol_examples_common connects in STA mode using
 * CONFIG_EXAMPLE_WIFI_SSID and CONFIG_EXAMPLE_WIFI_PASSWORD compiled from
 * sdkconfig. The password is intentionally never written to the log.
 */
void app_main(void)
{
    static httpd_handle_t server;

    ESP_ERROR_CHECK(initialize_nvs());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(initialize_application_modules());

#if !CONFIG_IDF_TARGET_LINUX
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, disconnect_handler, &server));
    ESP_LOGI(TAG, "connecting with compile-time Wi-Fi SSID: %s",
             CONFIG_EXAMPLE_WIFI_SSID);
#endif
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_ETH_GOT_IP, connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(
        ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, disconnect_handler, &server));
#endif
#endif

    /*
     * protocol_examples_common is intentionally kept unchanged for the test
     * iteration. It owns STA initialization, authentication and retry policy.
     */
    ESP_ERROR_CHECK(example_connect());
}
