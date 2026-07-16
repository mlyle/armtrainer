#ifndef ARMTRAINER_CORE_H
#define ARMTRAINER_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include <matrix.h>

struct __attribute__((packed)) ContextStateFrame_s {
	uint32_t r[4];
	uint32_t r12;
	uint32_t lr;
	uint32_t return_address;
	uint32_t xpsr;
};

struct __attribute__((packed)) EnhancedContextStateFrame_s {
	struct ContextStateFrame_s csf;
	uint32_t rh[4];
};

enum progrun_state {
	STATE_STOPPED,
	STATE_STEP,
	STATE_RUN,
};

enum trainer_svc_result {
	TRAINER_SVC_COMPLETE,
	TRAINER_SVC_INPUT_DECIMAL,
	TRAINER_SVC_INPUT_HEX,
};

void trainer_core_reset(void);
void trainer_core_splash(void);
void trainer_core_arrive(struct EnhancedContextStateFrame_s *frame);
void trainer_core_render(void);
void trainer_core_instruction_complete(struct EnhancedContextStateFrame_s *frame);
void trainer_core_key(enum matrix_keys key, bool pressed);
enum trainer_svc_result trainer_core_svc(
		struct ContextStateFrame_s *frame, uint8_t call);

enum progrun_state trainer_core_run_state(void);
bool trainer_core_fast_mode(void);
uint32_t trainer_core_edit_address(void);

#endif
