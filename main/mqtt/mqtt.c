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
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_ota_ops.h"
#include "sinter.h"
#include "../sling/sling_message.h"
#include "../sling/sling_sinter.h"

static const char *TAG = "mqtt";

static const char *BROKER_URI = "mqtts://a2ymu7hue04vq7-ats.iot.ap-southeast-1.amazonaws.com:8883";
static const char *CLIENT_ID = "stg-sling:9";

// not sure why the *_end stuff is needed, but yolo
extern const uint8_t mqtt_server_cert_pem_start[]   asm("_binary_mqtt_server_cert_pem_start");
extern const uint8_t mqtt_server_cert_pem_end[]   asm("_binary_mqtt_server_cert_pem_end");
extern const uint8_t mqtt_client_cert_pem_start[]   asm("_binary_mqtt_client_cert_pem_start");
extern const uint8_t mqtt_client_cert_pem_end[]     asm("_binary_mqtt_client_cert_pem_end");
extern const uint8_t mqtt_client_key_pem_start[]   asm("_binary_mqtt_client_key_pem_start");
extern const uint8_t mqtt_client_key_pem_end[]     asm("_binary_mqtt_client_key_pem_end");

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

static uint32_t msg_no = 0;
static uint32_t display_start_counter = 0;
static bool needs_flush = false;

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
    size_t topic_sz = strlen(CLIENT_ID) + strlen(msg_type) + 2; // uhh, null terminator?
    char *topic = malloc(topic_sz);
    snprintf(topic, topic_sz, "%s/%s", CLIENT_ID, msg_type);

    printf("send_raw to %s:\n", topic);
    for (int i = 0; i < payload_size; i++) {
        printf("%02x ", *(payload+i));
    }
    printf("\n");

    esp_mqtt_client_publish(client, topic, payload, payload_size, 1, 0);
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
}

void send_display_flush(esp_mqtt_client_handle_t client, bool is_error) {
    struct sling_message_display_flush *to_send = malloc(sizeof(struct sling_message_display_flush));
    to_send->message_counter = msg_no++;
    to_send->message_type = sling_message_display_type_flush | (is_error ? sling_message_display_type_error : 0);
    to_send->starting_id = display_start_counter;
    display_start_counter = to_send->message_counter;

    size_t buf_len = sizeof(*to_send);
    char *buf = malloc(buf_len);
    memcpy(buf, to_send, buf_len);
    send_raw(client, "display", buf, buf_len);
    needs_flush = false;
}

void send_display(esp_mqtt_client_handle_t client, sinter_value_t *val, bool is_error, bool is_result) {
    struct sling_message_display *payload = sling_sinter_value_to_message(val, NULL);
    payload->message_counter = msg_no++;
    if (is_result) {
        payload->display_type = (is_error ? sling_message_display_type_error : sling_message_display_type_result)
                                | sling_message_display_type_self_flushing;
    } else {
        if (!needs_flush) {
            needs_flush = true;
            display_start_counter = payload->message_counter;
        }

        if (is_error) { // i.e. is_error && !is_result
            payload->display_type = sling_message_display_type_error;
        } else { // normal output message
            payload->display_type = sling_message_display_type_output;
        }
    }

    size_t payload_len = sizeof(*payload);
    size_t str_len = (val->type == sinter_type_string ? strlen(val->string_value) + 1 : 0);
    char *buf = malloc(payload_len + str_len);
    memcpy(buf, payload, payload_len);
    if (val->type == sinter_type_string) {
        memcpy(buf + payload_len, val->string_value, str_len - 1);
    }

    send_raw(client, "display", buf, payload_len + str_len);
}

char *get_msg_type(char *topic, size_t size) {
    char *buf = malloc(size + 1);
    strlcpy(buf, topic, size + 1);

    char *msg_type;
    msg_type = strtok(buf, "/"); // client id. TODO potentially unsafe? Try strtok_r?
    msg_type = strtok(NULL, "/");
    if (msg_type == NULL) {
        return "";
    } else {
        return msg_type;
    }
}

static esp_mqtt_client_handle_t mqtt_client;

static void print_string(const char *s, bool is_error) {
    sinter_value_t val = {.type = sinter_type_string, .string_value = s};
    send_display(mqtt_client, &val, is_error, false);
}

static void print_integer(int32_t v, bool is_error) {
    sinter_value_t val = {.type = sinter_type_integer, .integer_value = v};
    send_display(mqtt_client, &val, is_error, false);
}

static void print_float(float v, bool is_error) {
    sinter_value_t val = {.type = sinter_type_float, .float_value = v};
    send_display(mqtt_client, &val, is_error, false);
}

static void print_flush(bool is_error) {
  send_display_flush(mqtt_client, is_error);
}

sinter_fault_t run_sinter(esp_mqtt_client_handle_t client, const unsigned char *binary, const size_t binary_size) {
    send_status(client, 1);
    ESP_LOGI(TAG, "sent status running");

    mqtt_client = client;

    sinter_printer_float = print_float;
    sinter_printer_string = print_string;
    sinter_printer_integer = print_integer;
    sinter_printer_flush = print_flush;

    sinter_value_t result = {0};
    sinter_fault_t fault = sinter_run(binary, binary_size, &result);

    ESP_LOGI(TAG, "Program exited with fault %d and result type %d (%d, %d, %f)\n", fault, result.type, result.integer_value, result.boolean_value, result.float_value);

    if (fault != sinter_fault_none) {
        result.type = sinter_type_string;
        result.string_value = fault_names[fault];
    }

    send_display(client, &result, fault != sinter_fault_none, true);
    send_status(client, 0);
    ESP_LOGI(TAG, "sent status idle");
    return fault;
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
                size_t topic_sz = strlen(CLIENT_ID) + 16;
                char *topic = malloc(topic_sz);

                snprintf(topic, topic_sz, "%s/run", CLIENT_ID);
                esp_mqtt_client_subscribe(client, topic, 1);

                snprintf(topic, topic_sz, "%s/stop", CLIENT_ID);
                esp_mqtt_client_subscribe(client, topic, 1);

                snprintf(topic, topic_sz, "%s/ping", CLIENT_ID);
                esp_mqtt_client_subscribe(client, topic, 1);

                snprintf(topic, topic_sz, "%s/input", CLIENT_ID);
                esp_mqtt_client_subscribe(client, topic, 1);

                ESP_LOGI(TAG, "subscribed to client topics");

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
                ESP_LOGI(TAG, "message type %s", msg_type);
                size_t cmp_len = event->topic_len - strlen(CLIENT_ID) - 2; // for the slash and null terminator
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
                    run_sinter(client, binary, size);
                }
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

void mqtt_app_start(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = BROKER_URI,
        .cert_pem = (const char *) mqtt_server_cert_pem_start,
        .client_cert_pem = (const char *) mqtt_client_cert_pem_start,
        .client_key_pem = (const char *) mqtt_client_key_pem_start,
        .client_id = CLIENT_ID,
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}
