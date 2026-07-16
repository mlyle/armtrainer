#ifndef ARMTRAINER_M4F_INTERNAL_H
#define ARMTRAINER_M4F_INTERNAL_H

#include <m4f/m4f.h>

struct m4f_exec {
	struct m4f_cpu *cpu;
	struct m4f_step_result result;
	uint32_t address;
	uint32_t next_address;
};

enum m4f_shift_type {
	M4F_SHIFT_LSL,
	M4F_SHIFT_LSR,
	M4F_SHIFT_ASR,
	M4F_SHIFT_ROR,
	M4F_SHIFT_RRX,
};

struct m4f_shift_result {
	uint32_t result;
	bool carry;
};

struct m4f_add_result {
	uint32_t result;
	bool carry;
	bool overflow;
};

#define M4F_BIT(value, bit) (((value) >> (bit)) & UINT32_C(1))
#define M4F_BITS(value, high, low) \
	(((uint32_t)(value) >> (low)) & \
	 ((UINT32_C(1) << ((high) - (low) + 1)) - UINT32_C(1)))

static inline uint32_t m4f_ror32(uint32_t value, unsigned shift)
{
	shift &= 31;
	return shift ? (value >> shift) | (value << (32 - shift)) : value;
}

static inline int32_t m4f_sign_extend(uint32_t value, unsigned bits)
{
	uint32_t sign = UINT32_C(1) << (bits - 1);
	return (int32_t)((value ^ sign) - sign);
}

bool m4f_condition_passed(const struct m4f_cpu *cpu, unsigned condition);
void m4f_advance_it(struct m4f_cpu *cpu);

struct m4f_shift_result m4f_shift(uint32_t value, enum m4f_shift_type type,
		unsigned amount, bool carry_in);
struct m4f_add_result m4f_add_with_carry(uint32_t x, uint32_t y,
		bool carry_in);
void m4f_set_nz(struct m4f_cpu *cpu, uint32_t value);
void m4f_set_nzc(struct m4f_cpu *cpu, uint32_t value, bool carry);
void m4f_set_nzcv(struct m4f_cpu *cpu, struct m4f_add_result result);

uint32_t m4f_read_reg(const struct m4f_exec *exec, unsigned reg);
void m4f_write_reg(struct m4f_exec *exec, unsigned reg, uint32_t value);
bool m4f_branch_exchange(struct m4f_exec *exec, uint32_t target);
void m4f_branch_write_pc(struct m4f_exec *exec, uint32_t target);

bool m4f_read_mem(struct m4f_exec *exec, uint32_t address, unsigned size,
		bool aligned, uint32_t *value);
bool m4f_write_mem(struct m4f_exec *exec, uint32_t address, unsigned size,
		bool aligned, uint32_t value);

enum m4f_stop m4f_execute_16(struct m4f_exec *exec, uint16_t instruction);
enum m4f_stop m4f_execute_32(struct m4f_exec *exec, uint16_t first,
		uint16_t second);

#endif
