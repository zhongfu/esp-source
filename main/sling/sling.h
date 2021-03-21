#include "freertos/FreeRTOS.h"

void sling_task(void *pvParams);

// TODO dunno. too much memory used here?
struct sling_config {
    char broker_uri[96];
    char client_id[16];
    char client_cert[2048];
    char client_key[2048];
    const char *server_cert_ptr;
};
_Static_assert(sizeof(struct sling_config) == 4212, "Wrong sling_config size");