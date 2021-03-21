#include <stdio.h>
#include <string.h>

#include <esp_system.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "sling_setup.h"
#include "sling_mqtt.h"

const char *TAG = "sling";

void sling_task(void *pvParams) {
    struct sling_config *config = sling_init();
    
    sling_mqtt_start(config);
}