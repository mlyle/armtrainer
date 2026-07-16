#include "trainer_web.h"
#include "lcd_web.h"
#include "platform_web.h"

#include <m4f/m4f.h>

#include <console.h>
#include <lcdutil.h>
#include <trainer_core.h>
#include <trainer_platform.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WEB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WEB_EXPORT
#endif

#define PROGRAM_BASE UINT32_C(0x20000000)
#define PROGRAM_STACK (PROGRAM_BASE + UINT32_C(0xf000))

struct trainer_runtime {
	struct m4f_cpu cpu;
	struct EnhancedContextStateFrame_s frame;
	uint8_t memory[TRAINER_WEB_PROGRAM_CAPACITY];
	uint8_t upload[TRAINER_WEB_PROGRAM_CAPACITY];
	uint32_t instruction_count;
	uint32_t next_instruction_at;
	enum m4f_stop last_stop;
	enum trainer_web_state state;
	bool resume_after_input;
};

static struct trainer_runtime trainer;

static enum m4f_mem_result memory_read(void *context, uint32_t address,
		unsigned size, bool execute, uint32_t *value)
{
	uint8_t bytes[4];
	(void)context;
	(void)execute;
	if (size > sizeof(bytes) || !trainer_memory_read(address, bytes, size))
		return M4F_MEM_FAULT;
	*value = 0;
	for (unsigned i = 0; i < size; ++i)
		*value |= (uint32_t)bytes[i] << (i * 8);
	return M4F_MEM_OK;
}

static enum m4f_mem_result memory_write(void *context, uint32_t address,
		unsigned size, uint32_t value)
{
	uint8_t bytes[4];
	(void)context;
	if (size > sizeof(bytes)) return M4F_MEM_FAULT;
	for (unsigned i = 0; i < size; ++i)
		bytes[i] = (uint8_t)(value >> (i * 8));
	return trainer_memory_write(address, bytes, size) ?
		M4F_MEM_OK : M4F_MEM_FAULT;
}

static void cpu_to_frame(void)
{
	for (unsigned i = 0; i < 4; ++i) {
		trainer.frame.csf.r[i] = trainer.cpu.r[i];
		trainer.frame.rh[i] = trainer.cpu.r[i + 4];
	}
	trainer.frame.csf.r12 = trainer.cpu.r[12];
	trainer.frame.csf.lr = trainer.cpu.r[14];
	trainer.frame.csf.return_address = trainer.cpu.r[15];
	trainer.frame.csf.xpsr = trainer.cpu.xpsr;
}

static void frame_to_cpu(void)
{
	for (unsigned i = 0; i < 4; ++i) {
		trainer.cpu.r[i] = trainer.frame.csf.r[i];
		trainer.cpu.r[i + 4] = trainer.frame.rh[i];
	}
	trainer.cpu.r[12] = trainer.frame.csf.r12;
	trainer.cpu.r[14] = trainer.frame.csf.lr;
	trainer.cpu.r[15] = trainer.frame.csf.return_address;
	trainer.cpu.xpsr = trainer.frame.csf.xpsr;
}

static void update_state(void)
{
	if (trainer.state == TRAINER_WEB_INPUT || trainer.state == TRAINER_WEB_FAULT)
		return;
	trainer.state = trainer_core_run_state() == STATE_STOPPED ?
		TRAINER_WEB_STOPPED : TRAINER_WEB_RUNNING;
}

static void arrive_and_render(void)
{
	cpu_to_frame();
	trainer_core_arrive(&trainer.frame);
	trainer_core_render();
	trainer_core_instruction_complete(&trainer.frame);
	frame_to_cpu();
	update_state();
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
	trainer.instruction_count = 0;
	trainer.next_instruction_at = 0;
	trainer.last_stop = M4F_STEP_OK;
	trainer.resume_after_input = false;
	trainer.state = TRAINER_WEB_STOPPED;
	trainer_core_reset();
	cpu_to_frame();
	trainer_core_arrive(&trainer.frame);
}

WEB_EXPORT void trainer_init(void)
{
	memset(&trainer, 0, sizeof(trainer));
	web_platform_set_memory(trainer.memory, sizeof(trainer.memory));
	web_platform_set_time(0);
	lcd_web_set_time(0);
	lcd_init();
	reset_cpu();
	trainer_core_splash();
	trainer_core_render();
}

WEB_EXPORT void trainer_reset(void)
{
	reset_cpu();
	trainer_core_render();
}

WEB_EXPORT uint8_t *trainer_program_buffer(void) { return trainer.upload; }
WEB_EXPORT uint32_t trainer_program_capacity(void) {
	return TRAINER_WEB_PROGRAM_CAPACITY;
}

WEB_EXPORT int trainer_load(uint32_t size)
{
	if (!size || size > TRAINER_WEB_PROGRAM_CAPACITY) return 0;
	memset(trainer.memory, 0, sizeof(trainer.memory));
	memcpy(trainer.memory, trainer.upload, size);
	reset_cpu();
	console_clearscreen();
	trainer_core_splash();
	trainer_core_render();
	return 1;
}

static void finish_input(void)
{
	trainer.frame.csf.r[0] = console_read_number_value();
	frame_to_cpu();
	if (trainer.resume_after_input) {
		trainer.state = TRAINER_WEB_RUNNING;
	} else {
		trainer_core_instruction_complete(&trainer.frame);
		frame_to_cpu();
		trainer.state = TRAINER_WEB_STOPPED;
		arrive_and_render();
	}
	trainer.resume_after_input = false;
}

static enum matrix_keys matrix_key(uint32_t key)
{
	if (key >= '0' && key <= '9') return (enum matrix_keys)key;
	if (key >= 'a' && key <= 'f') return (enum matrix_keys)key;
	if (key >= 'A' && key <= 'F') return (enum matrix_keys)(key - 'A' + 'a');
	if (key == 13) return key_addr;
	if (key == 8) return key_clr;
	if (key == 'l' || key == 'L') return key_load;
	if (key == 's' || key == 'S') return key_store;
	if (key == 'g' || key == 'G') return key_run;
	if (key == '.' || key == ' ') return key_step;
	return key_invalid;
}

WEB_EXPORT void trainer_key(uint32_t key, int pressed)
{
	if (trainer.state == TRAINER_WEB_INPUT) {
		enum matrix_keys input = key == 13 ? key_store : matrix_key(key);
		if (input != key_invalid && console_read_number_key(input, pressed != 0))
			finish_input();
		return;
	}
	enum matrix_keys mapped = matrix_key(key);
	if (mapped == key_invalid) return;
	if (pressed && mapped == key_run) {
		trainer_command_run();
		return;
	}
	if (pressed && mapped == key_step) {
		trainer_command_step();
		return;
	}
	trainer_core_key(mapped, pressed != 0);
	update_state();
	if (trainer.state == TRAINER_WEB_STOPPED) trainer_core_render();
}

WEB_EXPORT void trainer_command_run(void)
{
	if (trainer.state == TRAINER_WEB_INPUT) {
		if (console_read_number_key(key_run, true)) finish_input();
		return;
	}
	trainer_core_key(key_run, true);
	update_state();
	trainer_core_render();
}

static bool execute_one(void)
{
	struct m4f_step_result result = m4f_step(&trainer.cpu);
	trainer.last_stop = result.stop;
	++trainer.instruction_count;
	if (result.stop == M4F_STEP_OK) return true;
	if (result.stop != M4F_STEP_SVC) {
		trainer.state = TRAINER_WEB_FAULT;
		return false;
	}

	cpu_to_frame();
	enum trainer_svc_result svc = trainer_core_svc(&trainer.frame.csf,
		(uint8_t)result.immediate);
	frame_to_cpu();
	if (svc == TRAINER_SVC_COMPLETE) {
		update_state();
		return trainer.state == TRAINER_WEB_RUNNING;
	}

	console_read_number_begin(svc == TRAINER_SVC_INPUT_DECIMAL ? 10 : 16);
	trainer.resume_after_input = trainer_core_run_state() == STATE_RUN;
	trainer.state = TRAINER_WEB_INPUT;
	return false;
}

WEB_EXPORT void trainer_command_step(void)
{
	if (trainer.state == TRAINER_WEB_INPUT) {
		if (console_read_number_key(key_step, true)) finish_input();
		return;
	}
	trainer_core_key(key_step, true);
	trainer.state = TRAINER_WEB_RUNNING;
	execute_one();
	if (trainer.state != TRAINER_WEB_INPUT && trainer.state != TRAINER_WEB_FAULT)
		arrive_and_render();
}

WEB_EXPORT uint32_t trainer_run_slice(uint32_t budget, uint32_t now_ms)
{
	web_platform_set_time(now_ms);
	lcd_web_set_time(now_ms);
	if (trainer.state != TRAINER_WEB_RUNNING) return 0;
	if (!trainer_core_fast_mode()) {
		if (!lcd_is_ready() ||
				(int32_t)(now_ms - trainer.next_instruction_at) < 0)
			return 0;
		budget = 1;
		trainer.next_instruction_at = now_ms + 100;
	}
	uint32_t executed = 0;
	while (executed < budget && trainer.state == TRAINER_WEB_RUNNING) {
		bool keep_running = execute_one();
		++executed;
		if (!keep_running) break;
	}
	if (trainer.state != TRAINER_WEB_INPUT && trainer.state != TRAINER_WEB_FAULT)
		arrive_and_render();
	return executed;
}

WEB_EXPORT const uint8_t *trainer_framebuffer(void) { return lcd_framebuffer(); }
WEB_EXPORT uint32_t trainer_frame_revision(void) { return lcd_frame_revision(); }
WEB_EXPORT uint32_t trainer_state(void) { return trainer.state; }
WEB_EXPORT uint32_t trainer_pc(void) { return trainer.cpu.r[15]; }
WEB_EXPORT uint32_t trainer_register(uint32_t reg) {
	return reg < 16 ? trainer.cpu.r[reg] : 0;
}
WEB_EXPORT uint32_t trainer_last_stop(void) { return trainer.last_stop; }
WEB_EXPORT uint32_t trainer_led(void) { return web_platform_led(); }
WEB_EXPORT uint32_t trainer_fast_mode(void) { return trainer_core_fast_mode(); }
WEB_EXPORT uint32_t trainer_instruction_count(void) {
	return trainer.instruction_count;
}
