#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"
#include "esp_log.h"

#include "sinter.h"
#include "sinter_task.h"
#include "../sling/sling_message.h"
#include "../sling/sling_sinter.h"

static const char *TAG = "sinter_task";

MessageBufferHandle_t mbuf;

static xTaskHandle sinter_task_handle;

static const char *fault_names[] = {"no fault",
                                    "out of memory",
                                    "type error",
                                    "divide by zero",
                                    "stack overflow",
                                    "stack underflow",
                                    "uninitialised load",
                                    "invalid load",
                                    "invalid program",
                                    "internal error",
                                    "incorrect function arity",
                                    "program called error()",
                                    "uninitialised heap"};

/*
 * how should this be done?
 * 
 * maybe yet another task on the mqtt side just to receive + publish to mqtt?
 * or publish directly from this task?
 * hmm...
 */

static void send_val(sinter_value_t *val, bool is_error, bool is_result) {
    if (mbuf == NULL) {
        ESP_LOGE(TAG, "Message buffer not available yet in send_val!");
        return;
    }

    struct sling_message_display *payload = sling_sinter_value_to_message(val, NULL);

    if (is_result) {
        payload->display_type = (is_error ? sling_message_display_type_error : sling_message_display_type_result)
                                | sling_message_display_type_self_flushing;
    } else {
        payload->display_type = (is_error ? sling_message_display_type_error : sling_message_display_type_output);
    }

    size_t payload_len = sizeof(*payload);
    size_t str_len = (val->type == sinter_type_string ? strlen(val->string_value) + 1 : 0);
    size_t buf_len = payload_len + str_len;
    char *buf = malloc(buf_len);
    memcpy(buf, payload, payload_len);
    if (val->type == sinter_type_string) {
        memcpy(buf + payload_len, val->string_value, str_len - 1);
    }

    xMessageBufferSend(mbuf, buf, buf_len, portMAX_DELAY);
    free(buf);
}

static void print_string(const char *s, bool is_error) {
    sinter_value_t val = {.type = sinter_type_string, .string_value = s};
    send_val(&val, is_error, false);
}

static void print_integer(int32_t v, bool is_error) {
    sinter_value_t val = {.type = sinter_type_integer, .integer_value = v};
    send_val(&val, is_error, false);
}

static void print_float(float v, bool is_error) {
    sinter_value_t val = {.type = sinter_type_float, .float_value = v};
    send_val(&val, is_error, false);
}

static void print_flush(bool is_error) {
    struct sling_message_display_flush *to_send = malloc(sizeof(struct sling_message_display_flush));
    to_send->message_type = sling_message_display_type_flush | (is_error ? sling_message_display_type_error : 0);

    size_t buf_len = sizeof(*to_send);
    char *buf = malloc(buf_len);
    memcpy(buf, to_send, buf_len);

    xMessageBufferSend(mbuf, buf, buf_len, portMAX_DELAY);
    free(to_send);
    free(buf);
}

static void sinter_task(void *pvParams) {
    // TODO is this safe...?
    struct sinter_run_params *params = (struct sinter_run_params *) pvParams;

    sinter_printer_float = print_float;
    sinter_printer_string = print_string;
    sinter_printer_integer = print_integer;
    sinter_printer_flush = print_flush;

    // TODO uuuuuuuuuuh it feels pretty dangerous to me, in case pvParams is smaller than sling_sinter_program?
    sinter_value_t result = {0};
    sinter_fault_t fault = sinter_run(params->code, params->code_size, &result);

    ESP_LOGI(TAG, "Program exited with fault %d and result type %d (%d, %d, %f)\n", fault, result.type, result.integer_value, result.boolean_value, result.float_value);

    if (fault != sinter_fault_none) {
        result.type = sinter_type_string;
        result.string_value = fault_names[fault];
    }

    send_val(&result, fault != sinter_fault_none, true);

    free(params);
    ESP_LOGI(TAG, "task high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
    sinter_task_handle = NULL;
    vTaskDelete(NULL);
}

int run_sinter(unsigned char *binary, size_t size) {
    size_t params_size = sizeof(struct sinter_run_params) + size;
    struct sinter_run_params *params = malloc(params_size); // freed in sinter_task
    params->code_size = size;
    memcpy(params->code, binary, size);

    BaseType_t result = xTaskCreatePinnedToCore(sinter_task,
        "sinter_task",
        0x8000,
        (void*)params,
        2,
        &sinter_task_handle,
        1);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to start sinter_task with result %d. Out of heap space?", result);
        ESP_LOGE(TAG, "Free heap size: %d, min free heap size since boot: %d", xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize());
        sinter_task_handle = NULL;
        return 1;
    }

    return 0;
}

int stop_sinter() {
    if (sinter_task_handle != NULL) { // don't accidentally kill the sling task
        vTaskDelete(sinter_task_handle);
        sinter_task_handle = NULL;
        return 0;
    } else {
        ESP_LOGW(TAG, "sinter task handle invalid -- already stopped?");
        return 1;
    }
}
