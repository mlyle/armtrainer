#ifndef ARMTRAINER_M4F_H
#define ARMTRAINER_M4F_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* APSR fields kept in m4f_cpu.xpsr. */
#define M4F_XPSR_N (UINT32_C(1) << 31)
#define M4F_XPSR_Z (UINT32_C(1) << 30)
#define M4F_XPSR_C (UINT32_C(1) << 29)
#define M4F_XPSR_V (UINT32_C(1) << 28)
#define M4F_XPSR_Q (UINT32_C(1) << 27)
#define M4F_XPSR_T (UINT32_C(1) << 24)
#define M4F_XPSR_GE_MASK (UINT32_C(0xf) << 16)

/* FPSCR fields used by FPv4-SP-D16. */
#define M4F_FPSCR_N (UINT32_C(1) << 31)
#define M4F_FPSCR_Z (UINT32_C(1) << 30)
#define M4F_FPSCR_C (UINT32_C(1) << 29)
#define M4F_FPSCR_V (UINT32_C(1) << 28)
#define M4F_FPSCR_AHP (UINT32_C(1) << 26)
#define M4F_FPSCR_DN (UINT32_C(1) << 25)
#define M4F_FPSCR_FZ (UINT32_C(1) << 24)
#define M4F_FPSCR_RMODE_MASK (UINT32_C(3) << 22)
#define M4F_FPSCR_IDC (UINT32_C(1) << 7)
#define M4F_FPSCR_IXC (UINT32_C(1) << 4)
#define M4F_FPSCR_UFC (UINT32_C(1) << 3)
#define M4F_FPSCR_OFC (UINT32_C(1) << 2)
#define M4F_FPSCR_DZC (UINT32_C(1) << 1)
#define M4F_FPSCR_IOC (UINT32_C(1) << 0)
#define M4F_FPSCR_MASK UINT32_C(0xf7c0009f)

enum m4f_mem_result {
	M4F_MEM_OK = 0,
	M4F_MEM_FAULT,
	M4F_MEM_ALIGN,
};

/*
 * Memory callbacks exchange little-endian scalar values.  size is always
 * 1, 2, or 4.  Instruction fetches use read with execute=true.
 */
typedef enum m4f_mem_result (*m4f_read_fn)(void *context, uint32_t address,
		unsigned size, bool execute, uint32_t *value);
typedef enum m4f_mem_result (*m4f_write_fn)(void *context, uint32_t address,
		unsigned size, uint32_t value);

struct m4f_memory {
	m4f_read_fn read;
	m4f_write_fn write;
	void *context;
};

struct m4f_cpu {
	/* r[15] is the address of the instruction to execute next. */
	uint32_t r[16];
	uint32_t xpsr;

	uint32_t msp;
	uint32_t psp;
	uint32_t control;
	uint32_t primask;
	uint32_t basepri;
	uint32_t faultmask;

	/* FPv4-SP-D16: S0-S31 and FPSCR. */
	uint32_t s[32];
	uint32_t fpscr;

	/* Architectural ITSTATE, stored in its compact eight-bit form. */
	uint8_t itstate;

	/* Local exclusive monitor. */
	uint32_t exclusive_address;
	uint32_t exclusive_value;
	uint8_t exclusive_size;
	bool exclusive_valid;

	/* Event register used by SEV/WFE. */
	bool event_registered;
	bool big_endian;

	struct m4f_memory memory;
};

enum m4f_stop {
	M4F_STEP_OK = 0,
	M4F_STEP_SVC,
	M4F_STEP_BKPT,
	M4F_STEP_WFI,
	M4F_STEP_WFE,
	M4F_STEP_UDF,
	M4F_STEP_UNDEFINED,
	M4F_STEP_UNPREDICTABLE,
	M4F_STEP_FETCH_FAULT,
	M4F_STEP_DATA_FAULT,
	M4F_STEP_ALIGNMENT_FAULT,
};

struct m4f_step_result {
	enum m4f_stop stop;
	uint32_t instruction_address;
	uint32_t instruction;
	uint32_t fault_address;
	uint8_t instruction_size;
	uint32_t immediate;
};

void m4f_reset(struct m4f_cpu *cpu, const struct m4f_memory *memory);
void m4f_set_pc(struct m4f_cpu *cpu, uint32_t address);
void m4f_clear_exclusive(struct m4f_cpu *cpu);

/* Execute at most one instruction. */
struct m4f_step_result m4f_step(struct m4f_cpu *cpu);

const char *m4f_stop_name(enum m4f_stop stop);

#ifdef __cplusplus
}
#endif

#endif
