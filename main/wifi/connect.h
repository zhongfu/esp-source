#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT      BIT0 // successful connection with IP
#define WIFI_FAIL_BIT           BIT1 // we're still not connected even after trying MAXIMUM_RETRY times

extern EventGroupHandle_t s_wifi_event_group;

void connect_wifi();