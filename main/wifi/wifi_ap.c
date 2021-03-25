#include "string.h"
 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

#include "../sling/sling_setup.h"
#include "wifi_config.h"
#include "url_decode.h"
 
#define SERVER_PORT 80

static const char *TAG = "wifi_ap";

// TODO very cursed. sorry not sorry
static const char *html_header = "<html><head><title>esp-source config</title><meta chartset=\"UTF-8\"><style>form { display: table; } p { display: table-row; } label,input,select { display: table-cell; }</style></head><body>";
static const char *html_root_header = "<h1>welcome to esp-source</h1>";
static const char *html_root_secret_f = "<p>Secret: <input disabled value=\"%s\"></p><br>";
static const char *html_root_form_pt1 = "<form action=\"/set\" method=\"post\"><p><label>SSID: </label><input name=\"ssid\"></p><p><label>Auth type: </label><select name=\"authmode\" onchange=\"var id_p = document.getElementById('id_p').style; var pw_p = document.getElementById('pw_p').style; var val_p = document.getElementById('val_p').style; if (this.selectedIndex == 2) { val_p.display = 'table-row'; id_p.display = 'table-row'; } else { val_p.display = 'none'; id_p.display = 'none'; }; if (this.selectedIndex == 0) pw_p.display = 'none'; else pw_p.display = 'table-row';\">";
static const char *html_root_form_authmode_f = "<option value=\"%d\">Open</option><option value=\"%d\">WPA2-PSK</option><option value=\"%d\">WPA2-Enterprise</option>";
static const char *html_root_form_pt3 = "</select></p><p id=\"val_p\" style=\"display: none;\"><label>Validate CA: </label><input type=\"checkbox\" name=\"validate\" checked></p><p id=\"id_p\" style=\"display: none;\"><label>Identity: </label><input name=\"identity\"></p><p id=\"pw_p\" style=\"display: none;\"><label>Password: </label><input name=\"password\"></p><p><input type=\"submit\"></p></form>";
static const char *html_footer = "</body></html>";
 
static httpd_handle_t httpServerInstance = NULL;

static esp_err_t handler_get_root(httpd_req_t *req) {
    ESP_LOGI(TAG, "Got request to /");

    char outbuf[160] = {0};

    httpd_resp_send_chunk(req, html_header, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, html_root_header, HTTPD_RESP_USE_STRLEN);

    char uuid[37] = {0};
    sling_get_secret(uuid);
    snprintf(outbuf, 256, html_root_secret_f, uuid);
    httpd_resp_send_chunk(req, outbuf, HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req, html_root_form_pt1, HTTPD_RESP_USE_STRLEN);

    snprintf(outbuf, 256, html_root_form_authmode_f, WIFI_AUTH_OPEN, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA2_ENTERPRISE);
    httpd_resp_send_chunk(req, outbuf, HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req, html_root_form_pt3, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, html_footer, HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static httpd_uri_t uri_get_root = {
    .uri        = "/",
    .method     = HTTP_GET,
    .handler    = handler_get_root,
    .user_ctx   = NULL,
};

static esp_err_t handler_post_root(httpd_req_t *req) {
    ESP_LOGI(TAG, "Got request to /set");
    char buffer[256] = {0};
    httpd_req_recv(req, buffer, 256);
    char temp[128] = {0};

    char ssid[32] = {0};
    char authmode_c[8] = {0};
    wifi_auth_mode_t authmode = WIFI_AUTH_WPA2_PSK;
    char identity[64] = {0};
    char password[64] = {0};
    bool validate = true;

    if (httpd_query_key_value(buffer, "ssid", temp, 32) == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "400");
        httpd_resp_send(req, "SSID required", HTTPD_RESP_USE_STRLEN);
        ESP_LOGW(TAG, "SSID not received in request");
        return ESP_OK;
    }
    url_decode(ssid, temp);

    if (httpd_query_key_value(buffer, "authmode", authmode_c, 8) == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "400");
        httpd_resp_send(req, "Authmode required", HTTPD_RESP_USE_STRLEN);
        ESP_LOGW(TAG, "Authmode not received in request");
        return ESP_OK;
    } else {
        authmode = atoi(authmode_c);
        if (authmode != WIFI_AUTH_OPEN && authmode != WIFI_AUTH_WPA2_PSK && authmode != WIFI_AUTH_WPA2_ENTERPRISE) {
            httpd_resp_set_status(req, "400");
            httpd_resp_send(req, "Invalid authmode", HTTPD_RESP_USE_STRLEN);
            ESP_LOGE(TAG, "Got invalid authmode %d", authmode);
            return ESP_OK;
        }
    }

    if (authmode != WIFI_AUTH_OPEN) {
        if (httpd_query_key_value(buffer, "password", temp, 64) == ESP_ERR_NOT_FOUND) {
            httpd_resp_set_status(req, "400");
            httpd_resp_send(req, "Password required", HTTPD_RESP_USE_STRLEN);
            ESP_LOGW(TAG, "Password not received in request");
            return ESP_OK;
        }
        url_decode(password, temp);

        if (authmode == WIFI_AUTH_WPA2_ENTERPRISE) {
            if (httpd_query_key_value(buffer, "identity", temp, 64) == ESP_ERR_NOT_FOUND) {
                httpd_resp_set_status(req, "400");
                httpd_resp_send(req, "Identity required", HTTPD_RESP_USE_STRLEN);
                ESP_LOGW(TAG, "Identity not received in request");
                return ESP_OK;
            }
            url_decode(identity, temp);

            char validate_s[8] = {0}; // idk
            validate = (httpd_query_key_value(buffer, "validate", validate_s, 8) != ESP_ERR_NOT_FOUND); // if not found, then checkbox not checked... probably
        }
    }

    int ret = wifi_update_config(ssid, authmode, validate, NULL, identity, password);
    if (ret != 0) {
        httpd_resp_set_status(req, "500");
        httpd_resp_send(req, "Error setting config: %d", ret);
        ESP_LOGE(TAG, "Error setting config: %d", ret);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "New WiFi STA config! SSID: %s; Authmode: %d; Validate: %s; Identity: %s; Password: %s", ssid, authmode, validate ? "yes" : "no", identity, password);
    httpd_resp_send(req, "Set successfully! :)", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_uri_t uri_post_set = {
    .uri        = "/set",
    .method     = HTTP_POST,
    .handler    = handler_post_root,
    .user_ctx   = NULL,
};
 
static void startHttpServer(void){
    ESP_LOGI(TAG, "Starting httpd");
    httpd_config_t httpServerConfiguration = HTTPD_DEFAULT_CONFIG();
    httpServerConfiguration.server_port = SERVER_PORT;
    if (httpd_start(&httpServerInstance, &httpServerConfiguration) == ESP_OK) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(httpServerInstance, &uri_get_root));
        ESP_ERROR_CHECK(httpd_register_uri_handler(httpServerInstance, &uri_post_set));
    }
}
 
// static void stopHttpServer(void){
//     if (httpServerInstance != NULL) {
//         ESP_LOGI(TAG, "Stopping httpd");
//         ESP_ERROR_CHECK(httpd_stop(httpServerInstance));
//     }
// }

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}
 
void wifi_ap_start() {
    ESP_LOGI(TAG, "Starting AP mode");

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t apConfiguration = {
        .ap = {
            // .ssid = ap_ssid,
            // .password = ap_pw,
            .ssid_len = 0,
            //.channel = default,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .ssid_hidden = 0,
            .max_connection = 1,
            .beacon_interval = 150,
        },
    };

    uint8_t mac[6];
    esp_base_mac_addr_get(mac);

    snprintf((char *) apConfiguration.ap.ssid, 32, "esp-source %02x%02x", mac[4], mac[5]);
    snprintf((char *) apConfiguration.ap.password, 64, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "AP details: ssid %s, pw %s", apConfiguration.ap.ssid, apConfiguration.ap.password);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apConfiguration));
    
    ESP_ERROR_CHECK(esp_wifi_start());

    startHttpServer();

    while(1) vTaskDelay(10);
}