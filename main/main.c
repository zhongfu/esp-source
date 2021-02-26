#include "stdint.h"
#include "stdio.h"

#include "driver/uart.h"
#include "esp_system.h"
#include "esp_vfs_dev.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"

#include "wifi/wifi.h"
#include "wifi/connect.h"

#include "mqtt/mqtt.h"

static const char *TAG = "main";

static TaskHandle_t s_wifi_task_handle;

static _Noreturn void app_exit(void) {
  fflush(stdout);
  esp_restart();
  while (1) {
    (void)0;
  }
}

void app_main(void) {
  uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0);
  esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
  esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);

  //Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_event_loop_create_default());

  ESP_LOGI(TAG, "esp-source starting...");
  ESP_LOGI(TAG, "Starting WiFi task...");

  xTaskCreatePinnedToCore(&wifi_task,
                      "wifi_task",
                      10000,
                      NULL,
                      2,
                      &s_wifi_task_handle,
                      0);

  while (s_wifi_event_group == NULL) {
    ESP_LOGE(TAG, "WiFi event group not created yet, this should never happen! Sleeping 1s...");
    sleep(1);
  }

  // wait until connected, then start mqtt task
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                    WIFI_CONNECTED_BIT,
                    pdFALSE,
                    pdFALSE,
                    portMAX_DELAY);

  ESP_LOGI(TAG, "WiFi connected, starting MQTT task...");
  mqtt_app_start();
}
