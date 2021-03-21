/* MQTT over SSL Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_ota_ops.h"

#include "sinter.h"
#include "../sinter/sinter_task.h"
#include "../sling/sling_message.h"
#include "../sling/sling_sinter.h"
#include "../sling/sling.h"

static const char *TAG = "mqtt";

static MessageBufferHandle_t mbuf;

static xTaskHandle sinter_task_handle;

static uint32_t msg_no = 0;
static uint32_t display_start_counter = 0;
static bool needs_flush = false;

struct sling_config *config;

void uint32_to_char4(char buf[static 4], uint32_t val) {
    // little endian, big endian...
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

void uint16_to_char2(char buf[static 2], uint16_t val) {
    // little endian, big endian...
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

void send_raw(esp_mqtt_client_handle_t client, char *msg_type, char *payload, size_t payload_size) {
    size_t topic_sz = strlen(config->client_id) + strlen(msg_type) + 2; // uhh, null terminator?
    char *topic = malloc(topic_sz);
    snprintf(topic, topic_sz, "%s/%s", config->client_id, msg_type);

    printf("send_raw to %s:\n", topic);
    for (int i = 0; i < payload_size; i++) {
        printf("%02x ", *(payload+i));
    }
    printf("\n");

    esp_mqtt_client_publish(client, topic, payload, payload_size, 1, 0);
    free(topic);
}

void send_hello(esp_mqtt_client_handle_t client) {
    char buf[8];
    uint32_to_char4(buf, msg_no++);
    uint32_to_char4(buf+4, esp_random());

    send_raw(client, "hello", buf, 8);
}

void send_status(esp_mqtt_client_handle_t client, uint16_t status) {
    struct sling_message_status *to_send = malloc(sizeof(struct sling_message_status));
    to_send->message_counter = msg_no++;
    to_send->status = status;

    size_t buf_len = sizeof(*to_send);
    char *buf = malloc(buf_len);
    memcpy(buf, to_send, buf_len);
    send_raw(client, "status", buf, buf_len);
    free(to_send);
    free(buf);
}

// don't forget to free the returned char[]
char *get_msg_type(char *topic, size_t size) {
    char *buf = malloc(size + 1); // free?
    strlcpy(buf, topic, size + 1);

    char *msg_type;
    msg_type = strtok(buf, "/"); // client id. TODO potentially unsafe? Try strtok_r?
    msg_type = strtok(NULL, "/");

    if (msg_type == NULL) {
        return NULL;
    } else {
        char *ret = malloc(strlen(msg_type));
        strcpy(ret, msg_type); // msg_type is null terminated
        free(buf);
        return ret;
    }
}

void run_sinter(unsigned char *binary, size_t size) {
    size_t params_size = sizeof(struct sinter_run_params) + size;
    struct sinter_run_params *params = malloc(params_size); // freed in sinter_task
    params->buffer = mbuf;
    params->code_size = size;
    memcpy(params->code, binary, size);

    BaseType_t result = xTaskCreatePinnedToCore(sinter_task,
        "sinter_task",
        0x8000,
        (void*)params,
        2,
        sinter_task_handle,
        1);
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to start sinter_task with result %d. Out of heap space?", result);
        ESP_LOGE(TAG, "Free heap size: %d, min free heap size since boot: %d", xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize());
    }
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_no = 0;

            {
                size_t topic_sz = strlen(config->client_id) + 16;
                char *topic = malloc(topic_sz);

                snprintf(topic, topic_sz, "%s/run", config->client_id);
                esp_mqtt_client_subscribe(client, topic, 1);

                snprintf(topic, topic_sz, "%s/stop", config->client_id);
                esp_mqtt_client_subscribe(client, topic, 1);

                snprintf(topic, topic_sz, "%s/ping", config->client_id);
                esp_mqtt_client_subscribe(client, topic, 1);

                snprintf(topic, topic_sz, "%s/input", config->client_id);
                esp_mqtt_client_subscribe(client, topic, 1);

                ESP_LOGI(TAG, "subscribed to client topics");
                free(topic);

                send_hello(client);
                ESP_LOGI(TAG, "sent hello message");
            }

            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");

            {
                printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);

                //printf("topic %s, len %d", event->topic, event->topic_len);
                char *msg_type = get_msg_type(event->topic, event->topic_len);
                if (msg_type == NULL) {
                    ESP_LOGW(TAG, "couldn't get message type -- malformed topic?");
                    break;
                }
                ESP_LOGI(TAG, "message type %s", msg_type);
                size_t cmp_len = event->topic_len - strlen(config->client_id) - 2; // for the slash and null terminator
                if (strncmp(msg_type, "ping", cmp_len) == 0) {
                    ESP_LOGI(TAG, "got ping");

                    send_status(client, 0);
                    ESP_LOGI(TAG, "sent status");
                } else if (strncmp(msg_type, "run", cmp_len) == 0) {
                    ESP_LOGI(TAG, "got run");

                    size_t size = event->data_len - 4;
                    printf("data length is %d\n", size);
                    unsigned char *binary = malloc(size);
                    memcpy(binary, event->data + 4, size);
                    printf("received program:\n");
                    for (int i = 0; i < size; i++) {
                        printf("%02x ", *(binary+i));
                    }
                    printf("\n");

                    ESP_LOGI(TAG, "starting to run...");
                    run_sinter(binary, size);
                    free(binary);
                } else if (strncmp(msg_type, "stop", cmp_len) == 0) {
                    ESP_LOGI(TAG, "stopping sinter task...");
                    if (sinter_task_handle != NULL) { // don't accidentally kill the sling task
                        vTaskDelete(sinter_task_handle);
                    } else {
                        ESP_LOGW(TAG, "sinter task handle invalid -- already stopped?");
                    }
                    send_status(client, 0);
                }
                free(msg_type);
            }

            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
                ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            } else {
                ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
            break;

        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static void buffer_poll_loop(esp_mqtt_client_handle_t client) {
    size_t buffer_size = 0x800;
    char *buffer = malloc(buffer_size);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Error allocating mbuf buffer.");
        return;
    }

    while (1) {
        size_t recv_size = xMessageBufferReceive(mbuf, buffer, buffer_size, portMAX_DELAY);
        // recv_size == 0 if timeout or msg size > buffer_size
        // also skip if message is smaller than expected
        if (recv_size == 0 || recv_size < sizeof(struct sling_message_display_flush)) {
            // TODO figure out what to do if we get a message >2KiB...?
            continue;
        }

        struct sling_message_display *to_send = (struct sling_message_display *) buffer;
        to_send->message_counter = msg_no++;

        if (to_send->display_type & sling_message_display_type_flush) {
            struct sling_message_display_flush *to_send_flush = (struct sling_message_display_flush *) buffer;
            to_send_flush->starting_id = display_start_counter;
            display_start_counter = to_send_flush->message_counter;

            needs_flush = false;
        } else {
            // if not a self-flushing message...
            if ((to_send->display_type & sling_message_display_type_self_flushing) == 0) {
                if (!needs_flush) {
                    needs_flush = true;
                    display_start_counter = to_send->message_counter;
                }
            }
        }

        send_raw(client, "display", buffer, recv_size);
    }
}

void sling_mqtt_start(struct sling_config *conf) {
    config = conf;

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = config->broker_uri,
        .cert_pem = config->server_cert_ptr,
        .client_cert_pem = config->client_cert,
        .client_key_pem = config->client_key,
        .client_id = config->client_id,
        .buffer_size = 4096,
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);

    mbuf = xMessageBufferCreate(0x4000);
    esp_mqtt_client_start(client);
    buffer_poll_loop(client);
}
