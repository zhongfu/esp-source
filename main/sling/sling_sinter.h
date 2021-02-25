#ifndef SLING_SINTER_H
#define SLING_SINTER_H

#include <sinter.h>

#include "sling_message.h"

struct sling_message_display *sling_sinter_value_to_message(sinter_value_t *value, size_t *message_size);

#endif
