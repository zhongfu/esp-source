#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_system.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_tls.h>
#include <nvs_flash.h>

#include "uuidgen.h"
#include "sling.h"

#define NVS_NAMESPACE "sling"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

static const char *TAG = "sling_setup";

// not sure why the *_end stuff is needed, but yolo
extern const uint8_t sfs_root_g2_crt_start[]        asm("_binary_sfs_root_g2_crt_start");
extern const uint8_t sfs_root_g2_crt_end[]          asm("_binary_sfs_root_g2_crt_end");
extern const uint8_t verisign_pca3_g5_crt_start[]   asm("_binary_verisign_pca3_g5_crt_start");
extern const uint8_t verisign_pca3_g5_crt_end[]     asm("_binary_verisign_pca3_g5_crt_end");

// handle must already be opened
static esp_err_t gen_secret(nvs_handle_t handle, char *uuid) {
    esp_err_t err;

    UUIDGen(uuid);
    
    err = nvs_set_str(handle, "secret", uuid);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Got error %s while saving Sling secret to NVS", esp_err_to_name(err));
    }

    return err;
}

// make sure char *uuid is at least 37 bytes long (incl null terminator)
void sling_get_secret(char *uuid) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Got error %s while opening NVS namespace", esp_err_to_name(err));
        uuid = NULL;
        nvs_close(nvs_handle);
        return;
    }

    size_t size = 37;
    err = nvs_get_str(nvs_handle, "secret", uuid, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = gen_secret(nvs_handle, uuid);

        if (err != ESP_OK) {
            uuid = NULL;
            nvs_close(nvs_handle);
            return;
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Got error %s while retrieving Sling secret from NVS", esp_err_to_name(err));
        uuid = NULL;
        nvs_close(nvs_handle);
        return;
    }
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                    output_len += evt->data_len;
                }
                
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (evt->user_data) {
                // make sure to null terminate the user_data buffer
                ((char *) evt->user_data)[output_len] = 0;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

static void sling_http_get(esp_http_client_handle_t client, char *secret, char *endpoint) {
    // /v1/devices/{secret}/{endpoint}
    // 13 + 36 (secret) + strlen(endpoint) + 1 (null)
    size_t path_len = 13 + 36 + strlen(endpoint) + 1;
    char *path = malloc(path_len);
    snprintf(path, path_len, "/v1/devices/%s/%s", secret, endpoint);

    esp_http_client_set_url(client, path);
    free(path);

    esp_err_t err;
    uint16_t status;
    while (1) {
        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            status = esp_http_client_get_status_code(client);
            if (status != HttpStatus_Ok) {
                ESP_LOGE(TAG, "Unexpected HTTP response code %d in HTTP GET %s, retrying in 5s...", status, endpoint);
            } else {
                break;
            }
        } else {
            ESP_LOGE(TAG, "ESP error %s in HTTP GET %s, retrying in 5s...", esp_err_to_name(err), endpoint);
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

struct sling_config *sling_init() {
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

    char secret[37] = {0};
    sling_get_secret(secret);
    if (secret == NULL) {
        ESP_LOGE(TAG, "Couldn't get Sling secret. Giving up and rebooting.");
        esp_restart();
    } else {
        ESP_LOGI(TAG, "Sling secret: %s", secret);
    }

    esp_http_client_config_t config = {
        .host = "d1ygrvunq94rou.cloudfront.net", // TODO don't hardcode it, maybe
        .path = "/",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .cert_pem = (const char *) verisign_pca3_g5_crt_start, // TODO change this too, if using prod
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,        // Pass address of local buffer to get response
        .disable_auto_redirect = false, // TODO temp, was true
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    struct sling_config *sling_conf = malloc(sizeof(struct sling_config));
    sling_conf->server_cert_ptr = (const char *) sfs_root_g2_crt_start;
    
    sling_http_get(client, secret, "mqtt_endpoint");
    // shh, it's all good now
    // silence the trunc warning since we don't really care if it gets truncated... i think...
    #if __GNUC__ >= 8
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-truncation"
    #endif
    snprintf(sling_conf->broker_uri, sizeof(sling_conf->broker_uri), "mqtts://%s:8883", local_response_buffer);
    #if __GNUC__ >= 8
    #pragma GCC diagnostic pop
    #endif

    sling_http_get(client, secret, "client_id");
    strlcpy(sling_conf->client_id, local_response_buffer, sizeof(sling_conf->client_id));

    sling_http_get(client, secret, "cert");
    strlcpy(sling_conf->client_cert, local_response_buffer, sizeof(sling_conf->client_cert));

    sling_http_get(client, secret, "key");
    strlcpy(sling_conf->client_key, local_response_buffer, sizeof(sling_conf->client_key));

    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Broker URI: %s", sling_conf->broker_uri);
    ESP_LOGI(TAG, "Client ID: %s", sling_conf->client_id);

    return sling_conf;
}