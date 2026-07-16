#ifndef ARMTRAINER_WEB_PLATFORM_H
#define ARMTRAINER_WEB_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void web_platform_set_memory(uint8_t *memory, size_t size);
void web_platform_set_time(uint32_t now_ms);
bool web_platform_led(void);

#endif
