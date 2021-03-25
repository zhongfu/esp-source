#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"

extern MessageBufferHandle_t mbuf;

int run_sinter(unsigned char *binary, size_t size);

int stop_sinter();

struct __attribute__((packed)) sinter_run_params {
    size_t code_size;
    unsigned char code[];
};
_Static_assert(sizeof(struct sinter_run_params) == 4, "Wrong sinter_run_params size");