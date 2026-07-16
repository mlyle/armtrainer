#include "trainer_web.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	/* movs r0,#42; svc #22 (hex output); adds r0,#1; b . */
	static const uint8_t program[] = {
		0x2a, 0x20, 0x22, 0xdf, 0x01, 0x30, 0xfe, 0xe7,
	};

	trainer_init();
	assert(trainer_program_capacity() >= sizeof(program));
	memcpy(trainer_program_buffer(), program, sizeof(program));
	assert(trainer_load(sizeof(program)));
	uint32_t initial_revision = trainer_frame_revision();

	trainer_key(' ', 1);
	assert(trainer_register(0) == 42);
	assert(trainer_pc() == UINT32_C(0x20000002));
	assert(trainer_frame_revision() > initial_revision);

	trainer_key('g', 1);
	assert(trainer_state() == TRAINER_WEB_RUNNING);
	assert(trainer_run_slice(3, 49) == 0);
	assert(trainer_run_slice(3, 50) == 1);
	assert(trainer_run_slice(3, 149) == 0);
	assert(trainer_run_slice(3, 150) == 1);
	assert(trainer_register(0) == 43);
	assert(trainer_run_slice(3, 250) == 1);
	assert(trainer_instruction_count() == 4);

	const uint8_t *frame = trainer_framebuffer();
	unsigned lit = 0;
	for (unsigned i = 0; i < TRAINER_WEB_WIDTH * TRAINER_WEB_HEIGHT * 3 / 2; ++i)
		lit += frame[i] != 0;
	assert(lit > 100);

	trainer_key('g', 1);
	assert(trainer_state() == TRAINER_WEB_STOPPED);

	/* svc #40 enables fast mode; svc #41 disables it and stops. */
	static const uint8_t fast_program[] = {
		0x40, 0xdf, 0x01, 0x20, 0x41, 0xdf,
	};
	memcpy(trainer_program_buffer(), fast_program, sizeof(fast_program));
	assert(trainer_load(sizeof(fast_program)));
	trainer_command_run();
	assert(trainer_run_slice(1, 2000) == 1);
	assert(trainer_fast_mode());
	assert(trainer_run_slice(4, 2001) == 2);
	assert(!trainer_fast_mode());
	assert(trainer_state() == TRAINER_WEB_STOPPED);
	puts("m4f: browser trainer runtime tests passed");
	return 0;
}
