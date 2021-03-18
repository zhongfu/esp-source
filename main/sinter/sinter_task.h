#include"freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"

void sinter_task(void *pvParams);

struct __attribute__((packed)) sinter_run_params {
    MessageBufferHandle_t buffer;
    size_t code_size;
    unsigned char code[];
};
_Static_assert(sizeof(struct sinter_run_params) == 8, "Wrong sinter_run_params size");