#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <sinter.h>

#include "sling_message.h"
#include "sling_sinter.h"

struct sling_message_display *sling_sinter_value_to_message(sinter_value_t *value, size_t *message_size) {
  // TODO handle array
  const size_t extra_size = value->type == sinter_type_string ? strlen(value->string_value) + 1 : 0;
  const size_t message_len = sizeof(struct sling_message_display) + extra_size;
  struct sling_message_display *payload = malloc(message_len);
  if (!payload) {
    return payload;
  }

  payload->data_type = value->type;

  switch (value->type) {
  case sinter_type_boolean:
    payload->boolean = value->boolean_value;
    break;
  case sinter_type_integer:
    payload->int32 = value->integer_value;
    break;
  case sinter_type_float:
    payload->float32 = value->float_value;
    break;
  case sinter_type_string: {
    payload->string_length = extra_size - 1;
    memcpy(&payload->string, value->string_value, extra_size);
    break;
  }
  case sinter_type_array:
    // TODO
    break;
  case sinter_type_null:
  case sinter_type_undefined:
  case sinter_type_function:
  default:
    break;
  }

  if (message_size) {
    *message_size = message_len;
  }

  return payload;
}
