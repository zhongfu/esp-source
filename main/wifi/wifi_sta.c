#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_task.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi.h"

// TODO move config elsewhere
#define EXAMPLE_ESP_MAXIMUM_RETRY  10

static esp_event_handler_instance_t s_wifi_instance_any_id;
static esp_event_handler_instance_t s_wifi_instance_got_ip;

#define WIFI_CONNECTED_BIT      BIT0 // successful connection with IP
#define WIFI_FAIL_BIT           BIT1 // we're still not connected even after trying MAXIMUM_RETRY times

static const char *TAG = "wifi_sta";

static int s_retry_num = 0;

static void evgrp_set_bits(EventBits_t bits) {
    if (wifi_event_group != NULL) {
        xEventGroupSetBits(wifi_event_group, bits);
    } else {
        ESP_LOGI(TAG, "Event group is NULL, not setting bits");
    }
}

// TODO figure out what happens if we get connected but DHCP isn't working
// event_sta_connected but no event_sta_got_ip? or is there another ip_event for DHCP timeouts?
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            s_retry_num++;
            ESP_LOGI(TAG, "Disconnected :( Retrying, retry #%d", s_retry_num);
            esp_wifi_connect();
            
        } else {
            ESP_LOGI(TAG, "Connection failed %d times, giving up", s_retry_num);
            evgrp_set_bits(WIFI_FAIL_BIT);

            // it's out of our hands now
            ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_wifi_instance_got_ip));
            ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_instance_any_id));
            ESP_ERROR_CHECK(esp_wifi_stop());
            s_retry_num = 0;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        evgrp_set_bits(WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief Inits and starts connecting to WiFi.
 * 
 * Make sure to create an event group and assign it to `wifi_event_group` if
 * you want to know the status of the connection.
 */
void connect_wifi() {
    ESP_LOGI(TAG, "Init WiFi...");

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &s_wifi_instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &s_wifi_instance_got_ip));

    ESP_LOGI(TAG, "WiFi init finished, starting connect...");
    ESP_ERROR_CHECK(esp_wifi_start());
}