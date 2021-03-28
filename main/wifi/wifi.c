#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "driver/gpio.h"

#include "wifi.h"
#include "wifi_sta.h"
#include "wifi_ap.h"
#include "wifi_config.h"

static const char *TAG = "wifi_task";

// FreeRTOS event group to signal when we are connected
EventGroupHandle_t wifi_event_group;

void wifi_task(void *pvParams) {
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ull << GPIO_NUM_39); // button A
    gpio_config(&io_conf);

    if (gpio_get_level(GPIO_NUM_39) == 0) {
        ESP_LOGW(TAG, "Button A pressed, entering AP mode for config");
        wifi_ap_start(); // and don't ever come back here
    } else if (wifi_load_config() != 0) { // failed to load config, what's up?
        ESP_LOGW(TAG, "Error loading WiFi config, entering AP mode");
        wifi_ap_start();
    } // else we're all good, yeah?

    while (true) {
        // we're either here because we just started connecting, or we've lost connection
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        connect_wifi();

        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                    WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                    pdFALSE,
                    pdFALSE,
                    portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT) {
            // we're good!
            ESP_LOGI(TAG, "WiFi is up!");
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGW(TAG, "WiFi connection failed :(");
            ESP_LOGW(TAG, "Starting AP mode...");
            wifi_ap_start();
        } else {
            ESP_LOGE(TAG, "Got a weird WiFi event group bit, this should never happen");
        }

        // assume we're successful; wait until it fails again
        // then repeat the loop again (or TODO AP mode)
        xEventGroupWaitBits(wifi_event_group,
                        WIFI_FAIL_BIT,
                        pdFALSE,
                        pdFALSE,
                        portMAX_DELAY);
    }
}
