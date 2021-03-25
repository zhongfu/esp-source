#include <string.h>

#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <nvs_flash.h>

#include "../storage/spiffs.h"

static const char *TAG = "wifi_config";

extern const uint8_t digicert_global_crt_start[]   asm("_binary_digicert_global_crt_start");
extern const uint8_t digicert_global_crt_end[]     asm("_binary_digicert_global_crt_end");

#define NVS_NAMESPACE "wifi"

/**
 * wifi storage in nvs, v0.1:
 * - conf_version       uint8_t                     version of config, in case we need to change it. let's just say it's 1 for now
 * 
 * - sta_ssid           uint8_t[32]                 SSID of target AP
 * - sta_authmode       wifi_auth_mode_t/uint8_t    auth mode
 * - sta_validate_ca    uint8_t                     whether or not to validate cacert (0 = no, 1 = yes). if can't read cacert, fall back to digicert (for auth01.nw.nus.edu.sg)
 * - sta_identity       uint8_t[64]                 identity. aka username?
 * - sta_password       uint8_t[64]                 passphrase for target AP, or for WPA2 enterprise
 * 
 * pem ca cert goes into spiffs partition "certs" as "wifi_8021x_ca_cert.pem"
 */

/**
 *  every char* should be a null-terminated string
 * even ca_cert, since we're taking PEM certs
 * 
 * if ca_cert is a nullptr, ignore
 * if ca_cert is an empty string, wipe
 * else write to spiffs
 * 
 * required parameters depends on authmode
 */
int wifi_update_config(char *ssid, wifi_auth_mode_t authmode, bool validate_cacert, char *ca_cert, char *identity, char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Got error %s while opening NVS namespace", esp_err_to_name(err));
        return 100;
    }

    switch (authmode) {
        case WIFI_AUTH_WPA2_ENTERPRISE:
            if (identity == NULL) {
                ESP_LOGE(TAG, "Got null identity in update_config!");
                nvs_close(nvs_handle);
                return 1;
            }
            // fallthrough
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
        case WIFI_AUTH_WPA2_PSK:
            if (password == NULL) {
                ESP_LOGE(TAG, "Got null password in update_config!");
                nvs_close(nvs_handle);
                return 1;
            }
            // fallthrough
        case WIFI_AUTH_OPEN:
            if (ssid == NULL) {
                ESP_LOGE(TAG, "Got null ssid in update_config!");
                nvs_close(nvs_handle);
                return 1;
            }
            break;
        default: // include open
            ESP_LOGE(TAG, "Got unknown authmode %d in update_config!", authmode);
            nvs_close(nvs_handle);
            return 1;
    }

    // do this again because we don't really want to save half of the settings before realizing that we're missing some stuff
    switch (authmode) {
        case WIFI_AUTH_WPA2_ENTERPRISE:
            err = nvs_set_u8(nvs_handle, "sta_validate_ca", validate_cacert ? 1 : 0);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error saving sta_validate_ca to NVS: %s", esp_err_to_name(err));
                nvs_close(nvs_handle);
                return 2;
            }

            if (validate_cacert) {
                if (ca_cert != NULL) {
                    if (strlen(ca_cert) == 0) {
                        spiffs_delete("certs", "wifi_8021x_ca_cert.pem"); // ignore if delete fails
                    } else {
                        int ret = spiffs_write("certs", "wifi_8021x_ca_cert.pem", ca_cert, strlen(ca_cert) + 1);
                        if (ret != 0) {
                            ESP_LOGE(TAG, "Error %d while saving /certs/wifi_8021x_ca_cert.pem", ret);
                            nvs_close(nvs_handle);
                            return 2;
                        }
                    }
                }
            }
            
            err = nvs_set_str(nvs_handle, "sta_identity", identity);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error saving sta_identity to NVS: %s", esp_err_to_name(err));
                nvs_close(nvs_handle);
                return 2;
            }
            // fallthrough
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
        case WIFI_AUTH_WPA2_PSK:
            err = nvs_set_str(nvs_handle, "sta_password", password);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error saving sta_password to NVS: %s", esp_err_to_name(err));
                nvs_close(nvs_handle);
                return 2;
            }
            // fallthrough
        case WIFI_AUTH_OPEN:
            err = nvs_set_str(nvs_handle, "sta_ssid", ssid);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error saving sta_ssid to NVS: %s", esp_err_to_name(err));
                nvs_close(nvs_handle);
                return 2;
            }
            
            err = nvs_set_u8(nvs_handle, "sta_authmode", (uint8_t) authmode);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error saving sta_authmode to NVS: %s", esp_err_to_name(err));
                nvs_close(nvs_handle);
                return 2;
            }
            break;
        default: // include open
            ESP_LOGE(TAG, "Got unknown authmode %d in update_config!", authmode);
            nvs_close(nvs_handle);
            return 1;
    }

    err = nvs_set_u8(nvs_handle, "conf_version", 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving conf_version to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return 2;
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return 0;
}

/**
 * load wifi config rightaway
 * 
 * returns 1 if config not found or invalid config
 * returns 2 if conf_version exists but failed getting other params
 * returns 100 if nvs namespace couldn't be opened
 * returns 0 if it's all good
 */
int wifi_load_config() {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Got error %s while opening NVS namespace", esp_err_to_name(err));
        return 100;
    }

    uint8_t version;
    err = nvs_get_u8(nvs_handle, "conf_version", &version);
    if (version != 1 || err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Config not found, or invalid config (wrong version?)");
        nvs_close(nvs_handle);
        return 1;
    }

    wifi_config_t conf = {
        .sta = {
            // .ssid = (unsigned char) ssid,
            // .threshold.authmode = authmode,
        },
    };
    
    err = nvs_get_u8(nvs_handle, "sta_authmode", (uint8_t *) &(conf.sta.threshold.authmode));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting sta_authmode: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return 2;
    }
    wifi_auth_mode_t authmode = conf.sta.threshold.authmode;

    size_t ssid_len = 32;
    err = nvs_get_str(nvs_handle, "sta_ssid", (char *) conf.sta.ssid, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error getting sta_ssid: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return 2;
    }

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    char password[64] = {0};
    size_t pw_len = 64;

    if (authmode != WIFI_AUTH_OPEN) {
        err = nvs_get_str(nvs_handle, "sta_password", password, &pw_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error getting sta_password: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return 2;
        }
    }

    if (authmode != WIFI_AUTH_WPA2_ENTERPRISE) {
        if (authmode == WIFI_AUTH_WPA3_PSK ||
            authmode == WIFI_AUTH_WPA2_WPA3_PSK ||
            authmode == WIFI_AUTH_WPA2_PSK) {
                strcpy((char *) conf.sta.password, password);
        } else if (authmode != WIFI_AUTH_OPEN) {
            ESP_LOGE(TAG, "Unknown or invalid authmode %d.", authmode);
            nvs_close(nvs_handle);
            return 1;
        }

        ESP_LOGI(TAG, "Connecting to WiFi with ssid: %s; authmode: %d; password: %s", conf.sta.ssid, authmode, password);
        esp_wifi_set_config(WIFI_IF_STA, &conf);
    } else { // 802.1x/wpa2 enterprise
        esp_wifi_set_config(WIFI_IF_STA, &conf);

        uint8_t validate_ca_int;
        err = nvs_get_u8(nvs_handle, "sta_validate_ca", &validate_ca_int);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error getting sta_validate_ca: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return 2;
        }

        if (validate_ca_int == 1) {
            size_t cacert_len;
            char *cacert = NULL;

            int res = spiffs_read("certs", "wifi_8021x_ca_cert.pem", cacert, &cacert_len);
            if (res == 1) { // not found
                esp_wifi_sta_wpa2_ent_set_ca_cert((const unsigned char *) digicert_global_crt_start, strlen((const char *) digicert_global_crt_start));
            } else if (res == 0) { // found!!
                esp_wifi_sta_wpa2_ent_set_ca_cert((const unsigned char *) cacert, cacert_len);
            } else {
                ESP_LOGE(TAG, "Error %d while reading /certs/wifi_802x_ca_cert.pem", res);
                nvs_close(nvs_handle);
                return 2;
            }
        }

        char identity[64] = {0};
        size_t id_len = 64;
        err = nvs_get_str(nvs_handle, "sta_identity", identity, &id_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error getting sta_identity: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return 2;
        }
        esp_wifi_sta_wpa2_ent_set_identity((uint8_t *) identity, 64);

        esp_wifi_sta_wpa2_ent_set_password((uint8_t *) password, 64);

        esp_wifi_sta_wpa2_ent_enable();

        ESP_LOGI(TAG, "Connecting to WiFi with ssid: %s; authmode: %d; validate: %d; identity: %s; password: %s", conf.sta.ssid, authmode, validate_ca_int, identity, password);
    }

    nvs_close(nvs_handle);
    return 0;
}