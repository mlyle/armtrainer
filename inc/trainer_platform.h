#ifndef ARMTRAINER_PLATFORM_H
#define ARMTRAINER_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool trainer_memory_read16(uint32_t address, uint16_t *value);
bool trainer_memory_write16(uint32_t address, uint16_t value);
bool trainer_memory_read(uint32_t address, void *destination, size_t size);
bool trainer_memory_write(uint32_t address, const void *source, size_t size);

void trainer_platform_led_toggle(void);
void trainer_platform_led_set(bool on);
void trainer_platform_delay_ms(uint32_t milliseconds);
uint64_t trainer_platform_random_next(void);
void trainer_platform_random_blend(uint32_t value);
uint32_t trainer_platform_ticks(void);
uint32_t trainer_platform_millis(void);
uint32_t trainer_platform_read_number(uint8_t base);
bool trainer_platform_save_write(int slot);
bool trainer_platform_save_read(int slot);
void trainer_platform_single_step(bool enabled);
int trainer_platform_snake(void);

#endif
