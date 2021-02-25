#include "stdint.h"
#include "stdio.h"

#include "driver/uart.h"
#include "esp_system.h"
#include "esp_vfs_dev.h"
#include "esp_event.h"
#include "esp_log.h"

#include "wifi/connect.h"

#include "mqtt/mqtt.h"

static const char *TAG = "main";

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

  ESP_ERROR_CHECK(esp_event_loop_create_default());

  ESP_LOGI(TAG, "esp-source starting...");
  ESP_LOGI(TAG, "connecting to WiFi...");
  if (wifi_connect() != 0) { // successful connect
    ESP_LOGE(TAG, "error connecting to wifi, restarting...");
    app_exit();
  } else {
    ESP_LOGI(TAG, "connected to wifi!");
  }

  mqtt_app_start();
}
