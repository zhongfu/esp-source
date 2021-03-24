#include <esp_wifi_types.h>

int wifi_update_config(char *ssid, wifi_auth_mode_t authmode, bool validate_cacert, char *ca_cert, char *identity, char *password);

int wifi_load_config();