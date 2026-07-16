#include <matrix.h>
#include <systick_handler.h>
#include <trainer_platform.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PROGRAM_BASE UINT32_C(0x20000000)
#define SAVE_SIZE UINT32_C(0x4000)

volatile uint32_t systick_cnt;

static matrix_cb_t matrix_callback;
static uint8_t *program_memory;
static size_t program_memory_size;
static uint8_t save_slots[3][SAVE_SIZE];
static uint32_t now_ms;
static uint32_t random_state = UINT32_C(0x6d2b79f5);
static bool led_state;

void web_platform_set_memory(uint8_t *memory, size_t size)
{
	program_memory = memory;
	program_memory_size = size;
}

void web_platform_set_time(uint32_t value)
{
	now_ms = value;
	systick_cnt = value / 5;
}

bool web_platform_led(void)
{
	return led_state;
}

static bool memory_range(uint32_t address, size_t size, size_t *offset)
{
	if (!program_memory || address < PROGRAM_BASE) return false;
	size_t candidate = address - PROGRAM_BASE;
	if (candidate > program_memory_size || size > program_memory_size - candidate)
		return false;
	*offset = candidate;
	return true;
}

bool trainer_memory_read16(uint32_t address, uint16_t *value)
{
	size_t offset;
	if (!memory_range(address, 2, &offset)) return false;
	*value = (uint16_t)(program_memory[offset] |
		((uint16_t)program_memory[offset + 1] << 8));
	return true;
}

bool trainer_memory_write16(uint32_t address, uint16_t value)
{
	size_t offset;
	if (!memory_range(address, 2, &offset)) return false;
	program_memory[offset] = (uint8_t)value;
	program_memory[offset + 1] = (uint8_t)(value >> 8);
	return true;
}

bool trainer_memory_read(uint32_t address, void *destination, size_t size)
{
	size_t offset;
	if (!memory_range(address, size, &offset)) return false;
	memcpy(destination, program_memory + offset, size);
	return true;
}

bool trainer_memory_write(uint32_t address, const void *source, size_t size)
{
	size_t offset;
	if (!memory_range(address, size, &offset)) return false;
	memcpy(program_memory + offset, source, size);
	return true;
}

void trainer_platform_led_toggle(void) { led_state = !led_state; }
void trainer_platform_led_set(bool on) { led_state = on; }
void trainer_platform_delay_ms(uint32_t milliseconds) { (void)milliseconds; }
uint64_t trainer_platform_random_next(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}
void trainer_platform_random_blend(uint32_t value) { random_state ^= value; }
uint32_t trainer_platform_ticks(void) { return systick_cnt; }
uint32_t trainer_platform_millis(void) { return now_ms; }
uint32_t trainer_platform_read_number(uint8_t base) { (void)base; return 0; }
bool trainer_platform_save_write(int slot)
{
	if (slot < 0 || slot >= 3 || program_memory_size < SAVE_SIZE) return false;
	memcpy(save_slots[slot], program_memory, SAVE_SIZE);
	return true;
}
bool trainer_platform_save_read(int slot)
{
	if (slot < 0 || slot >= 3 || program_memory_size < SAVE_SIZE) return false;
	memcpy(program_memory, save_slots[slot], SAVE_SIZE);
	return true;
}
void trainer_platform_single_step(bool enabled) { (void)enabled; }
int trainer_platform_snake(void) { return 0; }

matrix_cb_t matrix_set_callback(matrix_cb_t callback)
{
	matrix_cb_t previous = matrix_callback;
	matrix_callback = callback;
	return previous;
}

void matrix_scanstep(void)
{
}

void matrix_scanall(void)
{
}

void matrix_init(void)
{
}

void matrix_key_changed(enum matrix_keys key, bool pressed)
{
	if (matrix_callback) matrix_callback(key, pressed);
}
