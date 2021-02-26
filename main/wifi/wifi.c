#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "connect.h"

static const char *TAG = "wifi_task";

void wifi_task (void *pvParams) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    esp_netif_create_default_wifi_sta();

    while (true) {
        // we're either here because we just started connecting, or we've lost connection
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        connect_wifi();

        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                    WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                    pdFALSE,
                    pdFALSE,
                    portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT) {
            // we're good!
            ESP_LOGI(TAG, "WiFi is up!");
        } else if (bits & WIFI_FAIL_BIT) {
            // start AP mode for configuration, I guess
            ESP_LOGW(TAG, "WiFi connection failed :(");
            // TODO AP mode
            continue; // retry... for now
        } else {
            ESP_LOGE(TAG, "Got a weird WiFi event group bit, this should never happen");
        }

        // assume we're successful; wait until it fails again
        // then repeat the loop again (or TODO AP mode)
        xEventGroupWaitBits(s_wifi_event_group,
                        WIFI_FAIL_BIT,
                        pdFALSE,
                        pdFALSE,
                        portMAX_DELAY);
    }
}