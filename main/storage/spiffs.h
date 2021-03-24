#include <esp_err.h>

esp_err_t spiffs_init(void);
int spiffs_write(char *partition, char *filename, char *data, size_t data_sz);
int spiffs_read(char *partition, char *filename, char *data, size_t *data_sz);
int spiffs_delete(char *partition, char *filename);