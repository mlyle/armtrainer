#ifndef ARMTRAINER_WEB_TRAINER_H
#define ARMTRAINER_WEB_TRAINER_H

#include <stddef.h>
#include <stdint.h>

#define TRAINER_WEB_WIDTH 160
#define TRAINER_WEB_HEIGHT 128
#define TRAINER_WEB_PROGRAM_CAPACITY 0x10000u

enum trainer_web_state {
	TRAINER_WEB_STOPPED = 0,
	TRAINER_WEB_RUNNING,
	TRAINER_WEB_INPUT,
	TRAINER_WEB_FAULT,
};

void trainer_init(void);
void trainer_reset(void);
uint8_t *trainer_program_buffer(void);
uint32_t trainer_program_capacity(void);
int trainer_load(uint32_t size);

void trainer_key(uint32_t key, int pressed);
void trainer_command_run(void);
void trainer_command_step(void);
uint32_t trainer_run_slice(uint32_t instruction_budget, uint32_t now_ms);

const uint8_t *trainer_framebuffer(void);
uint32_t trainer_frame_revision(void);
uint32_t trainer_state(void);
uint32_t trainer_pc(void);
uint32_t trainer_register(uint32_t reg);
uint32_t trainer_last_stop(void);
uint32_t trainer_led(void);
uint32_t trainer_fast_mode(void);
uint32_t trainer_instruction_count(void);

#endif
