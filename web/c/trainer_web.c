#include "trainer_web.h"

#include <m4f/m4f.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <font8x13.h>
#include <stringutil.h>
#include <thumbdisasm.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WEB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WEB_EXPORT
#endif

#define PROGRAM_BASE UINT32_C(0x20000000)
#define PROGRAM_STACK (PROGRAM_BASE + UINT32_C(0xf000))
#define FRAME_BYTES (TRAINER_WEB_WIDTH * TRAINER_WEB_HEIGHT * 4)
#define CONSOLE_BOTTOM 53

struct trainer_runtime {
	struct m4f_cpu cpu;
	uint8_t memory[TRAINER_WEB_PROGRAM_CAPACITY];
	uint8_t upload[TRAINER_WEB_PROGRAM_CAPACITY];
	uint8_t framebuffer[FRAME_BYTES];
	uint32_t frame_revision;
	uint32_t now_ms;
	uint32_t instruction_count;
	uint32_t random_state;
	uint32_t edit_address;
	uint16_t edit_value;
	uint8_t edit_position;
	uint8_t console_column;
	uint8_t draw_r;
	uint8_t draw_g;
	uint8_t draw_b;
	uint8_t background_r;
	uint8_t background_g;
	uint8_t background_b;
	uint8_t input_base;
	uint8_t input_position;
	uint32_t input_value;
	uint32_t running_key;
	enum m4f_stop last_stop;
	enum trainer_web_state state;
	bool editing_address;
	bool resume_after_input;
	bool stepping;
	bool led;
	bool fast_mode;
};

static struct trainer_runtime trainer;

static bool memory_range(uint32_t address, unsigned size, uint32_t *offset)
{
	if (address < PROGRAM_BASE || size > 4)
		return false;
	uint32_t candidate = address - PROGRAM_BASE;
	if (candidate > TRAINER_WEB_PROGRAM_CAPACITY - size)
		return false;
	*offset = candidate;
	return true;
}

static enum m4f_mem_result memory_read(void *context, uint32_t address,
		unsigned size, bool execute, uint32_t *value)
{
	struct trainer_runtime *runtime = context;
	uint32_t offset;
	(void)execute;
	if (!memory_range(address, size, &offset))
		return M4F_MEM_FAULT;
	*value = 0;
	for (unsigned i = 0; i < size; ++i)
		*value |= (uint32_t)runtime->memory[offset + i] << (i * 8);
	return M4F_MEM_OK;
}

static enum m4f_mem_result memory_write(void *context, uint32_t address,
		unsigned size, uint32_t value)
{
	struct trainer_runtime *runtime = context;
	uint32_t offset;
	if (!memory_range(address, size, &offset))
		return M4F_MEM_FAULT;
	for (unsigned i = 0; i < size; ++i)
		runtime->memory[offset + i] = (uint8_t)(value >> (i * 8));
	return M4F_MEM_OK;
}

static void pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
	if (x < 0 || x >= TRAINER_WEB_WIDTH || y < 0 || y >= TRAINER_WEB_HEIGHT)
		return;
	uint8_t *destination = &trainer.framebuffer[(y * TRAINER_WEB_WIDTH + x) * 4];
	destination[0] = (uint8_t)((r & 15) * 17);
	destination[1] = (uint8_t)((g & 15) * 17);
	destination[2] = (uint8_t)((b & 15) * 17);
	destination[3] = 255;
}

static void fill_box(int x, int y, int x2, int y2,
		uint8_t r, uint8_t g, uint8_t b)
{
	for (int row = y; row <= y2; ++row)
		for (int column = x; column <= x2; ++column)
			pixel(column, row, r, g, b);
}

static void draw_char(uint8_t character, int x, int y,
		uint8_t r, uint8_t g, uint8_t b,
		uint8_t br, uint8_t bg, uint8_t bb)
{
	if (character < 0x20 || character > 0x7e)
		character = ' ';
	const uint8_t *raster = font_8x13_rasters[character - 0x20];
	for (int row = 12; row >= 0; --row) {
		uint8_t bits = raster[row];
		for (int column = 0; column < 9; ++column) {
			if (bits & 0x80)
				pixel(x + column, y, r, g, b);
			else
				pixel(x + column, y, br, bg, bb);
			bits <<= 1;
		}
		++y;
	}
}

static int draw_string(const char *text, int x, int y,
		uint8_t r, uint8_t g, uint8_t b,
		uint8_t br, uint8_t bg, uint8_t bb)
{
	while (*text && x <= 151) {
		draw_char((uint8_t)*text++, x, y, r, g, b, br, bg, bb);
		x += 9;
	}
	return x;
}

static void console_scroll(void)
{
	size_t row_bytes = TRAINER_WEB_WIDTH * 4;
	memmove(trainer.framebuffer, trainer.framebuffer + 13 * row_bytes,
		40 * row_bytes);
	fill_box(0, 40, 159, CONSOLE_BOTTOM, 0, 0, 0);
}

static void console_character(uint8_t character)
{
	switch (character) {
	case '\n':
		console_scroll();
		break;
	case '\r':
		trainer.console_column = 0;
		break;
	case 8:
	case 127:
		if (trainer.console_column) --trainer.console_column;
		break;
	default:
		if (trainer.console_column >= 17) {
			trainer.console_column = 0;
			console_scroll();
		}
		draw_char(character, trainer.console_column * 9, 40,
			trainer.draw_r, trainer.draw_g, trainer.draw_b,
			trainer.background_r, trainer.background_g, trainer.background_b);
		++trainer.console_column;
		break;
	}
}

static void console_text(const char *text)
{
	while (*text) console_character((uint8_t)*text++);
}

static void console_clear(void)
{
	fill_box(0, 0, 159, CONSOLE_BOTTOM, 0, 0, 0);
	trainer.console_column = 0;
}

static void console_hex(uint32_t value)
{
	char text[9];
	to_hex_buf(text, value, 8);
	console_character('\r');
	console_character('\n');
	console_text(text);
}

static void console_decimal(uint32_t value)
{
	char text[11];
	unsigned length = 0;
	do {
		text[length++] = (char)('0' + value % 10);
		value /= 10;
	} while (value);
	console_character('\r');
	console_character('\n');
	while (length) console_character((uint8_t)text[--length]);
}

static void console_color(uint8_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
	*r = (color & 0xc0) >> 4;
	*g = (color & 0x38) >> 2;
	*b = (color & 0x07) << 1;
}

static bool read_u16(uint32_t address, uint16_t *value)
{
	uint32_t offset;
	if (!memory_range(address, 2, &offset)) return false;
	*value = (uint16_t)(trainer.memory[offset] |
		((uint16_t)trainer.memory[offset + 1] << 8));
	return true;
}

static bool write_u16(uint32_t address, uint16_t value)
{
	uint32_t offset;
	if (!memory_range(address, 2, &offset) || address & 1) return false;
	trainer.memory[offset] = (uint8_t)value;
	trainer.memory[offset + 1] = (uint8_t)(value >> 8);
	return true;
}

static void render_register(unsigned row, unsigned reg)
{
	char name[3] = { 'r', (char)('0' + reg), 0 };
	char value[9];
	to_hex_buf(value, trainer.cpu.r[reg], 8);
	int y = 58 + (int)row * 13;
	draw_string(name, 0, y, 15, 2, 2, 0, 0, 0);
	fill_box(18, y, 159, y + 12, 0, 0, 0);
	draw_string(value, 24, y, 15, 15, 15, 5, 0, 0);
}

static void render_flag(uint32_t mask, int x, char lower)
{
	char character = (trainer.cpu.xpsr & mask) ? (char)(lower - 'a' + 'A') : lower;
	uint8_t level = (trainer.cpu.xpsr & mask) ? 15 : 8;
	draw_char((uint8_t)character, x, 62, level, level, level, 0, 0, 0);
}

static void render_cursor(void)
{
	fill_box(0, 125, 159, 127, 0, 0, 0);
	if (trainer.state == TRAINER_WEB_RUNNING) {
		fill_box(0, 125, 159, 127, 0, 9, 0);
		return;
	}
	int position = trainer.editing_address ? trainer.edit_position :
		72 + 9 * (trainer.edit_position - 4);
	if (trainer.editing_address) position *= 9;
	fill_box(position + 3, 125, position + 5, 125, 15, 0, 0);
	fill_box(position + 2, 126, position + 6, 126, 15, 0, 0);
	fill_box(position, 127, position + 8, 127, 15, 0, 0);
}

static void render_monitor(void)
{
	fill_box(0, 58, 159, 127, 0, 0, 0);
	for (unsigned i = 0; i < 4; ++i) render_register(i, i);
	render_flag(M4F_XPSR_N, 123, 'n');
	render_flag(M4F_XPSR_Z, 132, 'z');
	render_flag(M4F_XPSR_C, 141, 'c');
	render_flag(M4F_XPSR_V, 150, 'v');

	char address[9];
	char value[5];
	to_hex_buf(address, trainer.edit_address, 8);
	to_hex_buf(value, trainer.edit_value, 4);
	draw_string(address, 0, 112, 0, 0, 0, 15, 15, 15);
	draw_string(value, 75, 112, 0, 0, 0, 8, 8, 15);
	draw_string(get_mnem(trainer.edit_value), 123, 112,
		15, 15, 15, 0, 0, 0);
	render_cursor();
	++trainer.frame_revision;
}

static void refresh_edit_value(void)
{
	if (!read_u16(trainer.edit_address & ~UINT32_C(1), &trainer.edit_value))
		trainer.edit_value = UINT16_C(0x0bad);
}

static void reset_cpu(void)
{
	struct m4f_memory memory = {
		.read = memory_read,
		.write = memory_write,
		.context = &trainer,
	};
	m4f_reset(&trainer.cpu, &memory);
	trainer.cpu.r[3] = PROGRAM_BASE;
	trainer.cpu.r[7] = PROGRAM_BASE;
	trainer.cpu.r[13] = PROGRAM_STACK;
	trainer.cpu.msp = PROGRAM_STACK;
	m4f_set_pc(&trainer.cpu, PROGRAM_BASE);
	trainer.edit_address = PROGRAM_BASE;
	trainer.editing_address = true;
	trainer.edit_position = 0;
	trainer.state = TRAINER_WEB_STOPPED;
	trainer.last_stop = M4F_STEP_OK;
	trainer.input_base = 0;
	trainer.resume_after_input = false;
	trainer.stepping = false;
	trainer.running_key = 0;
	trainer.led = false;
	trainer.fast_mode = false;
	trainer.instruction_count = 0;
	refresh_edit_value();
}

WEB_EXPORT void trainer_init(void)
{
	memset(&trainer, 0, sizeof(trainer));
	trainer.draw_r = trainer.draw_g = trainer.draw_b = 15;
	trainer.random_state = UINT32_C(0x6d2b79f5);
	for (size_t i = 3; i < FRAME_BYTES; i += 4)
		trainer.framebuffer[i] = 255;
	console_clear();
	draw_string("MPTrainer Web", 0, 1, 15, 15, 0, 0, 0, 0);
	draw_string("Load a program", 0, 27, 0, 15, 15, 0, 0, 0);
	reset_cpu();
	render_monitor();
}

WEB_EXPORT void trainer_reset(void)
{
	reset_cpu();
	render_monitor();
}

WEB_EXPORT uint8_t *trainer_program_buffer(void)
{
	return trainer.upload;
}

WEB_EXPORT uint32_t trainer_program_capacity(void)
{
	return TRAINER_WEB_PROGRAM_CAPACITY;
}

WEB_EXPORT int trainer_load(uint32_t size)
{
	if (!size || size > TRAINER_WEB_PROGRAM_CAPACITY) return 0;
	memset(trainer.memory, 0, sizeof(trainer.memory));
	memcpy(trainer.memory, trainer.upload, size);
	console_clear();
	draw_string("MPTrainer Web", 0, 1, 15, 15, 0, 0, 0, 0);
	reset_cpu();
	render_monitor();
	return 1;
}

static void finish_input(void)
{
	trainer.cpu.r[0] = trainer.input_value;
	trainer.input_base = 0;
	trainer.state = trainer.resume_after_input ? TRAINER_WEB_RUNNING : TRAINER_WEB_STOPPED;
	trainer.resume_after_input = false;
	console_character(' ');
	console_character(8);
}

static int key_digit(uint32_t key)
{
	if (key >= '0' && key <= '9') return (int)(key - '0');
	if (key >= 'a' && key <= 'f') return (int)(key - 'a' + 10);
	if (key >= 'A' && key <= 'F') return (int)(key - 'A' + 10);
	return -1;
}

static void input_key(uint32_t key)
{
	int digit = key_digit(key);
	if (digit >= 0 && digit < trainer.input_base) {
		uint64_t next = (uint64_t)trainer.input_value * trainer.input_base +
			(unsigned)digit;
		if (next <= UINT32_MAX) {
			trainer.input_value = (uint32_t)next;
			++trainer.input_position;
			console_character((uint8_t)key);
		}
		return;
	}
	if (key == 8 && trainer.input_position) {
		trainer.input_value /= trainer.input_base;
		--trainer.input_position;
		console_character(8);
		console_character(' ');
		console_character(8);
		return;
	}
	if (key == 13 || key == '.' || key == 'g' || key == 'G')
		finish_input();
}

static void edit_digit(unsigned digit)
{
	uint32_t mask = UINT32_C(0xf0000000) >> (trainer.edit_position * 4);
	if (trainer.editing_address) {
		trainer.edit_address = (trainer.edit_address & ~mask) |
			((uint32_t)digit << (4 * (7 - trainer.edit_position)));
	} else {
		uint32_t value = trainer.edit_value;
		value = (value & ~mask) |
			((uint32_t)digit << (4 * (7 - trainer.edit_position)));
		trainer.edit_value = (uint16_t)value;
	}
	if (trainer.edit_position < 7) ++trainer.edit_position;
}

static void perform_load(void)
{
	trainer.edit_address &= ~UINT32_C(1);
	refresh_edit_value();
	trainer.editing_address = false;
	trainer.edit_position = 4;
}

WEB_EXPORT void trainer_command_run(void)
{
	if (trainer.state == TRAINER_WEB_INPUT) {
		finish_input();
		return;
	}
	if (trainer.state == TRAINER_WEB_RUNNING) {
		trainer.state = TRAINER_WEB_STOPPED;
		trainer.edit_address = trainer.cpu.r[15];
		refresh_edit_value();
		render_monitor();
		return;
	}
	trainer.cpu.r[15] = trainer.edit_address & ~UINT32_C(1);
	trainer.stepping = false;
	trainer.state = TRAINER_WEB_RUNNING;
	trainer.last_stop = M4F_STEP_OK;
	render_monitor();
}

static bool handle_svc(uint32_t immediate)
{
	switch (immediate) {
	case 0x00: trainer.led = !trainer.led; break;
	case 0x01: trainer.led = false; break;
	case 0x02: trainer.led = true; break;
	case 0x03: trainer.led = false; break;
	case 0x10:
	case 0x11:
	case 0x12:
		break;
	case 0x18:
		trainer.random_state ^= trainer.random_state << 13;
		trainer.random_state ^= trainer.random_state >> 17;
		trainer.random_state ^= trainer.random_state << 5;
		trainer.cpu.r[0] = trainer.cpu.r[0] ?
			trainer.random_state % trainer.cpu.r[0] : trainer.random_state;
		break;
	case 0x19: trainer.cpu.r[0] = trainer.now_ms; break;
	case 0x1a:
		trainer.cpu.r[0] = trainer.running_key;
		trainer.running_key = 0;
		break;
	case 0x20: console_clear(); break;
	case 0x21: console_decimal(trainer.cpu.r[0]); break;
	case 0x22: console_hex(trainer.cpu.r[0]); break;
	case 0x23: console_character((uint8_t)trainer.cpu.r[0]); break;
	case 0x24: {
		uint8_t r, g, b;
		console_color((uint8_t)trainer.cpu.r[0], &r, &g, &b);
		pixel((int)trainer.cpu.r[1], (int)trainer.cpu.r[2], r, g, b);
		break;
	}
	case 0x25:
	case 0x26: {
		uint8_t r, g, b;
		console_color((uint8_t)trainer.cpu.r[0], &r, &g, &b);
		uint32_t source = trainer.cpu.r[15];
		for (unsigned y = 0; y < 16; ++y) {
			uint16_t row;
			if (!read_u16(source + y * 2, &row)) return false;
			for (unsigned x = 0; x < 16; ++x) {
				if (row & (UINT16_C(0x8000) >> x))
					pixel((int)trainer.cpu.r[1] + (int)x,
						(int)trainer.cpu.r[2] + (int)y, r, g, b);
				else if (immediate == 0x25)
					pixel((int)trainer.cpu.r[1] + (int)x,
						(int)trainer.cpu.r[2] + (int)y,
						trainer.background_r, trainer.background_g,
						trainer.background_b);
			}
		}
		trainer.cpu.r[15] += 32;
		break;
	}
	case 0x2a:
	case 0x2b:
		trainer.input_base = immediate == 0x2a ? 10 : 16;
		trainer.input_position = 0;
		trainer.input_value = 0;
		trainer.resume_after_input = trainer.state == TRAINER_WEB_RUNNING &&
			!trainer.stepping;
		trainer.state = TRAINER_WEB_INPUT;
		return false;
	case 0x30:
		console_color((uint8_t)trainer.cpu.r[0], &trainer.draw_r,
			&trainer.draw_g, &trainer.draw_b);
		break;
	case 0x31:
		console_color((uint8_t)trainer.cpu.r[0], &trainer.background_r,
			&trainer.background_g, &trainer.background_b);
		break;
	case 0x40: trainer.fast_mode = true; break;
	case 0x41:
		trainer.fast_mode = false;
		trainer.state = TRAINER_WEB_STOPPED;
		return false;
	case 0x42: break;
	default: break;
	}
	return true;
}

static bool execute_one(void)
{
	struct m4f_step_result result = m4f_step(&trainer.cpu);
	trainer.last_stop = result.stop;
	++trainer.instruction_count;
	if (result.stop == M4F_STEP_OK) return true;
	if (result.stop == M4F_STEP_SVC) return handle_svc(result.immediate);
	trainer.state = TRAINER_WEB_FAULT;
	return false;
}

WEB_EXPORT void trainer_command_step(void)
{
	if (trainer.state == TRAINER_WEB_INPUT) {
		finish_input();
		return;
	}
	if (trainer.state == TRAINER_WEB_RUNNING) trainer.state = TRAINER_WEB_STOPPED;
	trainer.cpu.r[15] = trainer.edit_address & ~UINT32_C(1);
	trainer.stepping = true;
	trainer.state = TRAINER_WEB_RUNNING;
	execute_one();
	trainer.stepping = false;
	if (trainer.state == TRAINER_WEB_RUNNING) trainer.state = TRAINER_WEB_STOPPED;
	trainer.edit_address = trainer.cpu.r[15];
	refresh_edit_value();
	trainer.editing_address = true;
	trainer.edit_position = 0;
	render_monitor();
}

WEB_EXPORT void trainer_key(uint32_t key, int pressed)
{
	if (!pressed) return;
	if (trainer.state == TRAINER_WEB_INPUT) {
		input_key(key);
		render_monitor();
		return;
	}
	if (key == 'g' || key == 'G') {
		trainer_command_run();
		return;
	}
	if (key == '.' || key == ' ') {
		trainer_command_step();
		return;
	}
	if (trainer.state == TRAINER_WEB_RUNNING) {
		trainer.running_key = key;
		return;
	}
	int digit = key_digit(key);
	if (digit >= 0) {
		edit_digit((unsigned)digit);
	} else if (key == 13) {
		if (trainer.editing_address) perform_load();
		else {
			trainer.editing_address = true;
			trainer.edit_position = 0;
		}
	} else if (key == 'l' || key == 'L') {
		perform_load();
	} else if (key == 's' || key == 'S') {
		if (!trainer.editing_address && write_u16(trainer.edit_address,
				trainer.edit_value)) {
			trainer.edit_address += 2;
			perform_load();
		}
	} else if (key == 8 && trainer.edit_position) {
		--trainer.edit_position;
		if (!trainer.editing_address && trainer.edit_position < 4)
			trainer.edit_position = 4;
	}
	render_monitor();
}

WEB_EXPORT uint32_t trainer_run_slice(uint32_t instruction_budget, uint32_t now_ms)
{
	trainer.now_ms = now_ms;
	if (trainer.state != TRAINER_WEB_RUNNING) return 0;
	uint32_t executed = 0;
	while (executed < instruction_budget && trainer.state == TRAINER_WEB_RUNNING) {
		if (!execute_one()) break;
		++executed;
	}
	trainer.edit_address = trainer.cpu.r[15];
	refresh_edit_value();
	trainer.editing_address = true;
	trainer.edit_position = 0;
	render_monitor();
	return executed;
}

WEB_EXPORT const uint8_t *trainer_framebuffer(void) { return trainer.framebuffer; }
WEB_EXPORT uint32_t trainer_frame_revision(void) { return trainer.frame_revision; }
WEB_EXPORT uint32_t trainer_state(void) { return trainer.state; }
WEB_EXPORT uint32_t trainer_pc(void) { return trainer.cpu.r[15]; }
WEB_EXPORT uint32_t trainer_register(uint32_t reg) {
	return reg < 16 ? trainer.cpu.r[reg] : 0;
}
WEB_EXPORT uint32_t trainer_last_stop(void) { return trainer.last_stop; }
WEB_EXPORT uint32_t trainer_led(void) { return trainer.led; }
WEB_EXPORT uint32_t trainer_fast_mode(void) { return trainer.fast_mode; }
WEB_EXPORT uint32_t trainer_instruction_count(void) {
	return trainer.instruction_count;
}
