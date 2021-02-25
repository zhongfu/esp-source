#ifndef SLING_MESSAGE_H
#define SLING_MESSAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SLING_INTOPIC_RUN "run"
#define SLING_INTOPIC_STOP "stop"
#define SLING_INTOPIC_PING "ping"
#define SLING_INTOPIC_INPUT "input"

#define SLING_OUTTOPIC_STATUS "status"
#define SLING_OUTTOPIC_DISPLAY "display"
#define SLING_OUTTOPIC_HELLO "hello"

enum sling_message_status_type {
  sling_message_status_type_idle = 0,
  sling_message_status_type_running = 1,
  sling_message_status_type_prompt = 2
};

struct __attribute__((packed)) sling_message_status {
  uint32_t message_counter;
  uint16_t status;
};
_Static_assert(sizeof(struct sling_message_status) == 6, "Wrong sling_message_status size");

struct __attribute__((packed)) sling_message_status_prompt {
  uint32_t message_counter;
  uint16_t status;
  // Excluding null terminator
  uint32_t prompt_string_length;
  char prompt_string[];
};
_Static_assert(sizeof(struct sling_message_status_prompt) == 10, "Wrong sling_message_status_prompt size");

enum sling_message_display_type {
  sling_message_display_type_output = 0,
  sling_message_display_type_error = 1,
  sling_message_display_type_result = 2,
  sling_message_display_type_prompt_response = 4,
  sling_message_display_type_flush = 100,
  sling_message_display_type_self_flushing = 0x100
};

struct __attribute__((packed)) sling_message_display_flush {
  uint32_t message_counter;
  uint16_t message_type;
  uint32_t starting_id;
};
_Static_assert(sizeof(struct sling_message_display_flush) == 10,
               "Wrong sling_message_display_flush size");

struct __attribute__((packed)) sling_message_display {
  uint32_t message_counter;
  uint16_t display_type;
  uint16_t data_type;
  union {
    bool boolean;
    int32_t int32;
    float float32;
    // Excluding null terminator
    uint32_t string_length;
  };
  char string[];
};
_Static_assert(sizeof(struct sling_message_display) == 12, "Wrong sling_message_display size");

static inline char *sling_topic(const char *device_id, const char *topic) {
  char *ret = NULL;
  if (asprintf(&ret, "%s/%s", device_id, topic) == -1) {
    return NULL;
  }
  return ret;
}

#endif
