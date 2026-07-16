#include <trainer_platform.h>

#include <console.h>
#include <delayutil.h>
#include <led.h>
#include <randomutil.h>
#include <save.h>
#include <systick_handler.h>

#include <string.h>

extern int snake(void);

bool trainer_memory_read16(uint32_t address, uint16_t *value)
{
	*value = *(const uint16_t *)(uintptr_t)address;
	return true;
}

bool trainer_memory_write16(uint32_t address, uint16_t value)
{
	*(uint16_t *)(uintptr_t)address = value;
	return true;
}

bool trainer_memory_read(uint32_t address, void *destination, size_t size)
{
	memcpy(destination, (const void *)(uintptr_t)address, size);
	return true;
}

bool trainer_memory_write(uint32_t address, const void *source, size_t size)
{
	memcpy((void *)(uintptr_t)address, source, size);
	return true;
}

void trainer_platform_led_toggle(void) { led_toggle(); }
void trainer_platform_led_set(bool on) { led_set(on); }
void trainer_platform_delay_ms(uint32_t milliseconds) { delay_ms(milliseconds); }
uint64_t trainer_platform_random_next(void) { return random_next(); }
void trainer_platform_random_blend(uint32_t value) { random_blendseed(value); }
uint32_t trainer_platform_ticks(void) { return systick_cnt; }
uint32_t trainer_platform_millis(void) {
	return systick_cnt * 1000 / SYSTICK_HZ;
}
uint32_t trainer_platform_read_number(uint8_t base) {
	return console_read_number(base);
}
bool trainer_platform_save_write(int slot) { return save_writesave(slot); }
bool trainer_platform_save_read(int slot) { return save_readsave(slot); }
int trainer_platform_snake(void) { return snake(); }
