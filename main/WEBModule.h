#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief Start the HTTP server for the controller web interface.
 *
 * AppState_Init() must be called before starting the server because handlers
 * read and update the central state store.
 *
 * @return Server handle on success, otherwise NULL.
 */
httpd_handle_t WEB_start_webserver(void);

/**
 * @brief Stop a server previously returned by WEB_start_webserver().
 *
 * @param server Valid HTTP server handle.
 * @return ESP-IDF result from httpd_stop().
 */
esp_err_t WEB_stop_webserver(httpd_handle_t server);
