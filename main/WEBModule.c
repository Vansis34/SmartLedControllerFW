#include "WEBModule.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "AppState.h"
#include "app_config.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "WEB";

/**
 * @brief Send a JSON error response with the requested HTTP status.
 *
 * @param req Active HTTP request.
 * @param status HTTP status string understood by esp_http_server.
 * @param message Short diagnostic intended for the API caller.
 * @return Result returned by httpd_resp_send().
 */
static esp_err_t send_json_error(httpd_req_t *req,
                                 const char *status,
                                 const char *message)
{
    char response[160];

    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    snprintf(response, sizeof(response), "{\"error\":\"%s\"}", message);
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief Receive a complete, size-limited HTTP request body.
 *
 * httpd_req_recv() may return fewer bytes than requested because TCP is a
 * byte stream. The loop therefore continues until Content-Length bytes have
 * been collected. The returned buffer is NUL-terminated for cJSON.
 *
 * @param req Active HTTP request containing Content-Length.
 * @param[out] body Heap buffer owned by the caller on success.
 * @return ESP_OK, ESP_ERR_INVALID_SIZE, ESP_ERR_NO_MEM, or ESP_FAIL.
 */
static esp_err_t receive_body(httpd_req_t *req, char **body)
{
    if (body == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *body = NULL;

    if (req->content_len == 0U || req->content_len > APP_HTTP_MAX_BODY_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *buffer = malloc(req->content_len + 1U);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0U;
    while (received < req->content_len) {
        int result = httpd_req_recv(req,
                                    buffer + received,
                                    req->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            free(buffer);
            return ESP_FAIL;
        }
        received += (size_t)result;
    }

    buffer[received] = '\0';
    *body = buffer;
    return ESP_OK;
}

/**
 * @brief Parse a complete request body as a JSON object.
 *
 * @param req Active HTTP request.
 * @param[out] root Parsed cJSON object owned by the caller on success.
 * @return ESP_OK or an error describing receive/allocation/JSON failure.
 */
static esp_err_t receive_json_object(httpd_req_t *req, cJSON **root)
{
    char *body = NULL;
    esp_err_t error = receive_body(req, &body);
    if (error != ESP_OK) {
        return error;
    }

    cJSON *parsed = cJSON_ParseWithLength(body, req->content_len);
    free(body);

    if (parsed == NULL || !cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        return ESP_ERR_INVALID_ARG;
    }

    *root = parsed;
    ESP_LOGD(TAG,
             "JSON request received: uri=%s body_bytes=%d",
             req->uri,
             req->content_len);
    return ESP_OK;
}

/**
 * @brief Send the appropriate response for a JSON receive/parse failure.
 *
 * Size and syntax errors are caused by the request and receive an HTTP 400.
 * Allocation and socket failures are internal/server failures and receive 500.
 *
 * @param req Active HTTP request.
 * @param error Error returned by receive_json_object().
 * @return Result of sending the response.
 */
static esp_err_t send_json_receive_error(httpd_req_t *req, esp_err_t error)
{
    if (error == ESP_ERR_INVALID_SIZE) {
        return send_json_error(req, "400 Bad Request", "body_size_invalid");
    }
    if (error == ESP_ERR_INVALID_ARG) {
        return send_json_error(req, "400 Bad Request", "invalid_json");
    }

    ESP_LOGE(TAG, "Failed to receive request body: %s", esp_err_to_name(error));
    return send_json_error(req,
                           "500 Internal Server Error",
                           "request_receive_failed");
}

/**
 * @brief Read an integer-valued JSON number without silently rounding it.
 *
 * cJSON stores numbers as double. Checking both finiteness and floor() avoids
 * accepting values such as 50.5 for integer application fields.
 *
 * @param object Parent JSON object.
 * @param name Field name.
 * @param[out] value Parsed integer.
 * @return true only when the field exists and is an exact int value.
 */
static bool json_get_int(const cJSON *object, const char *name, int *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < INT_MIN || item->valuedouble > INT_MAX) {
        return false;
    }

    *value = item->valueint;
    return true;
}

/**
 * @brief Convert AppState validation errors into a stable HTTP response.
 *
 * @param req Active HTTP request.
 * @param error Result returned by AppState_Apply().
 * @return Result of sending the response.
 */
static esp_err_t send_apply_result(httpd_req_t *req, esp_err_t error)
{
    if (error == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    }
    if (error == ESP_ERR_INVALID_ARG) {
        return send_json_error(req, "400 Bad Request", "value_out_of_range");
    }

    ESP_LOGE(TAG, "AppState_Apply failed: %s", esp_err_to_name(error));
    return send_json_error(req,
                           "500 Internal Server Error",
                           "state_update_failed");
}

/**
 * @brief Handle requests for an unknown URI.
 *
 * @param req Active HTTP request.
 * @param err HTTP server error code (unused because this handler is for 404).
 * @return ESP_OK after sending the error response.
 */
static esp_err_t http_404_error_handler(httpd_req_t *req,
                                        httpd_err_code_t err)
{
    (void)err;
    send_json_error(req, "404 Not Found", "not_found");
    return ESP_OK;
}

/**
 * @brief Return the current central lighting state.
 *
 * @param req Active GET /status request.
 * @return Result of obtaining or sending the snapshot.
 */
static esp_err_t get_status_handler(httpd_req_t *req)
{
    app_state_snapshot_t snapshot;
    esp_err_t error = AppState_Get(&snapshot);
    if (error != ESP_OK) {
        return send_json_error(req,
                               "500 Internal Server Error",
                               "state_unavailable");
    }

    char response[128];
    snprintf(response,
             sizeof(response),
             "{\"lamp\":%d,\"brigh\":%u,\"mode\":%d,\"revision\":%lu}",
             snapshot.power ? 1 : 0,
             snapshot.brightness,
             snapshot.mode,
             (unsigned long)snapshot.revision);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

/**
 * @brief Return the embedded gzip-compressed web page.
 *
 * @param req Active GET / request.
 * @return Result of sending the embedded file.
 */
static esp_err_t get_page_handler(httpd_req_t *req)
{
    extern const unsigned char index_html_gz_start[]
        asm("_binary_index_html_gz_start");
    extern const unsigned char index_html_gz_end[]
        asm("_binary_index_html_gz_end");
    size_t page_size = (size_t)(index_html_gz_end - index_html_gz_start);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_html_gz_start, page_size);
}

/**
 * @brief Apply power and optional mode received from PUT /led/ctrl.
 *
 * @param req Active HTTP request.
 * @return Result of sending the validation or success response.
 */
static esp_err_t led_ctrl_put_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    esp_err_t error = receive_json_object(req, &root);
    if (error != ESP_OK) {
        return send_json_receive_error(req, error);
    }

    app_state_patch_t patch = {0};
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (cJSON_IsBool(state)) {
        patch.values.power = cJSON_IsTrue(state);
        patch.mask |= APP_STATE_FIELD_POWER;
    } else {
        int state_value;
        if (!json_get_int(root, "state", &state_value) ||
            (state_value != 0 && state_value != 1)) {
            cJSON_Delete(root);
            return send_json_error(req, "400 Bad Request", "state_must_be_bool");
        }
        patch.values.power = state_value == 1;
        patch.mask |= APP_STATE_FIELD_POWER;
    }

    const cJSON *mode_item = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (mode_item != NULL) {
        int mode;
        if (!json_get_int(root, "mode", &mode)) {
            cJSON_Delete(root);
            return send_json_error(req, "400 Bad Request", "mode_must_be_int");
        }
        patch.values.mode = (led_mode_t)mode;
        patch.mask |= APP_STATE_FIELD_MODE;
    }

    cJSON_Delete(root);
    return send_apply_result(req, AppState_Apply(&patch, NULL, NULL));
}

/**
 * @brief Apply brightness and/or mode from PUT /led/settings.
 *
 * @param req Active HTTP request.
 * @return Result of sending the validation or success response.
 */
static esp_err_t led_settings_put_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    esp_err_t error = receive_json_object(req, &root);
    if (error != ESP_OK) {
        return send_json_receive_error(req, error);
    }

    app_state_patch_t patch = {0};
    const cJSON *brightness_item =
        cJSON_GetObjectItemCaseSensitive(root, "brigh");
    if (brightness_item != NULL) {
        int brightness;
        if (!json_get_int(root, "brigh", &brightness) ||
            brightness < 0 || brightness > 100) {
            cJSON_Delete(root);
            return send_json_error(req,
                                   "400 Bad Request",
                                   "brigh_out_of_range");
        }
        patch.values.brightness = (uint8_t)brightness;
        patch.mask |= APP_STATE_FIELD_BRIGHTNESS;
    }

    const cJSON *mode_item = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (mode_item != NULL) {
        int mode;
        if (!json_get_int(root, "mode", &mode)) {
            cJSON_Delete(root);
            return send_json_error(req, "400 Bad Request", "mode_must_be_int");
        }
        patch.values.mode = (led_mode_t)mode;
        patch.mask |= APP_STATE_FIELD_MODE;
    }

    cJSON_Delete(root);
    if (patch.mask == 0U) {
        return send_json_error(req, "400 Bad Request", "no_supported_fields");
    }

    return send_apply_result(req, AppState_Apply(&patch, NULL, NULL));
}

/**
 * @brief Start the HTTP server and register the legacy web API.
 *
 * @return Server handle on success, otherwise NULL.
 */
httpd_handle_t WEB_start_webserver(void)
{
    const httpd_uri_t uri_get_status = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = get_status_handler,
    };
    const httpd_uri_t uri_get_page = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_page_handler,
    };
    const httpd_uri_t uri_ctrl_led = {
        .uri = "/led/ctrl",
        .method = HTTP_PUT,
        .handler = led_ctrl_put_handler,
    };
    const httpd_uri_t uri_settings_led = {
        .uri = "/led/settings",
        .method = HTTP_PUT,
        .handler = led_settings_put_handler,
    };

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting server on port %d", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    esp_err_t error = httpd_register_uri_handler(server, &uri_get_page);
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(server, &uri_ctrl_led);
    }
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(server, &uri_get_status);
    }
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(server, &uri_settings_led);
    }
    if (error == ESP_OK) {
        httpd_register_err_handler(server,
                                   HTTPD_404_NOT_FOUND,
                                   http_404_error_handler);
        return server;
    }

    ESP_LOGE(TAG, "Failed to register URI handler: %s", esp_err_to_name(error));
    httpd_stop(server);
    return NULL;
}

/**
 * @brief Stop a running HTTP server.
 *
 * @param server Server handle returned by WEB_start_webserver().
 * @return ESP-IDF result from httpd_stop().
 */
esp_err_t WEB_stop_webserver(httpd_handle_t server)
{
    return httpd_stop(server);
}
