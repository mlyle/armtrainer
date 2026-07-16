#include <m4f/m4f.h>

#include <string.h>

#include "m4f_internal.h"

void m4f_reset(struct m4f_cpu *cpu, const struct m4f_memory *memory)
{
	memset(cpu, 0, sizeof(*cpu));
	if (memory) {
		cpu->memory = *memory;
	}
	cpu->xpsr = M4F_XPSR_T;
}

void m4f_set_pc(struct m4f_cpu *cpu, uint32_t address)
{
	cpu->r[15] = address & ~UINT32_C(1);
	cpu->xpsr |= M4F_XPSR_T;
}

void m4f_clear_exclusive(struct m4f_cpu *cpu)
{
	cpu->exclusive_valid = false;
}

const char *m4f_stop_name(enum m4f_stop stop)
{
	switch (stop) {
	case M4F_STEP_OK: return "ok";
	case M4F_STEP_SVC: return "svc";
	case M4F_STEP_BKPT: return "bkpt";
	case M4F_STEP_WFI: return "wfi";
	case M4F_STEP_WFE: return "wfe";
	case M4F_STEP_UDF: return "udf";
	case M4F_STEP_UNDEFINED: return "undefined";
	case M4F_STEP_UNPREDICTABLE: return "unpredictable";
	case M4F_STEP_FETCH_FAULT: return "fetch fault";
	case M4F_STEP_DATA_FAULT: return "data fault";
	case M4F_STEP_ALIGNMENT_FAULT: return "alignment fault";
	}
	return "unknown";
}

static struct m4f_step_result result_at(struct m4f_cpu *cpu,
		enum m4f_stop stop)
{
	struct m4f_step_result result = {
		.stop = stop,
		.instruction_address = cpu->r[15],
	};
	return result;
}

bool m4f_condition_passed(const struct m4f_cpu *cpu, unsigned condition)
{
	bool n = (cpu->xpsr & M4F_XPSR_N) != 0;
	bool z = (cpu->xpsr & M4F_XPSR_Z) != 0;
	bool c = (cpu->xpsr & M4F_XPSR_C) != 0;
	bool v = (cpu->xpsr & M4F_XPSR_V) != 0;
	bool result;

	switch (condition >> 1) {
	case 0: result = z; break;
	case 1: result = c; break;
	case 2: result = n; break;
	case 3: result = v; break;
	case 4: result = c && !z; break;
	case 5: result = n == v; break;
	case 6: result = (n == v) && !z; break;
	default: result = true; break;
	}
	if ((condition & 1) && condition != 15) {
		result = !result;
	}
	return result;
}

void m4f_advance_it(struct m4f_cpu *cpu)
{
	if ((cpu->itstate & 7) == 0) {
		cpu->itstate = 0;
	} else {
		cpu->itstate = (uint8_t)((cpu->itstate & 0xe0) |
				((cpu->itstate << 1) & 0x1f));
	}
}

struct m4f_shift_result m4f_shift(uint32_t value, enum m4f_shift_type type,
		unsigned amount, bool carry_in)
{
	struct m4f_shift_result out = { value, carry_in };
	switch (type) {
	case M4F_SHIFT_LSL:
		if (amount == 0) return out;
		if (amount < 32) {
			out.result = value << amount;
			out.carry = M4F_BIT(value, 32 - amount);
		} else if (amount == 32) {
			out.result = 0;
			out.carry = M4F_BIT(value, 0);
		} else {
			out.result = 0;
			out.carry = false;
		}
		break;
	case M4F_SHIFT_LSR:
		if (amount == 0) return out;
		if (amount < 32) {
			out.result = value >> amount;
			out.carry = M4F_BIT(value, amount - 1);
		} else if (amount == 32) {
			out.result = 0;
			out.carry = M4F_BIT(value, 31);
		} else {
			out.result = 0;
			out.carry = false;
		}
		break;
	case M4F_SHIFT_ASR:
		if (amount == 0) return out;
		if (amount < 32) {
			out.result = (uint32_t)((int32_t)value >> amount);
			out.carry = M4F_BIT(value, amount - 1);
		} else {
			out.carry = M4F_BIT(value, 31);
			out.result = out.carry ? UINT32_MAX : 0;
		}
		break;
	case M4F_SHIFT_ROR:
		if (amount == 0) return out;
		amount &= 31;
		if (amount == 0) {
			out.result = value;
			out.carry = M4F_BIT(value, 31);
		} else {
			out.result = m4f_ror32(value, amount);
			out.carry = M4F_BIT(out.result, 31);
		}
		break;
	case M4F_SHIFT_RRX:
		out.result = ((uint32_t)carry_in << 31) | (value >> 1);
		out.carry = M4F_BIT(value, 0);
		break;
	}
	return out;
}

struct m4f_add_result m4f_add_with_carry(uint32_t x, uint32_t y,
		bool carry_in)
{
	uint64_t unsigned_sum = (uint64_t)x + y + (carry_in ? 1 : 0);
	uint32_t result = (uint32_t)unsigned_sum;
	struct m4f_add_result out = {
		.result = result,
		.carry = (unsigned_sum >> 32) != 0,
		.overflow = ((~(x ^ y) & (x ^ result)) >> 31) != 0,
	};
	return out;
}

void m4f_set_nz(struct m4f_cpu *cpu, uint32_t value)
{
	cpu->xpsr &= ~(M4F_XPSR_N | M4F_XPSR_Z);
	if (value & UINT32_C(0x80000000)) cpu->xpsr |= M4F_XPSR_N;
	if (value == 0) cpu->xpsr |= M4F_XPSR_Z;
}

void m4f_set_nzc(struct m4f_cpu *cpu, uint32_t value, bool carry)
{
	m4f_set_nz(cpu, value);
	cpu->xpsr &= ~M4F_XPSR_C;
	if (carry) cpu->xpsr |= M4F_XPSR_C;
}

void m4f_set_nzcv(struct m4f_cpu *cpu, struct m4f_add_result value)
{
	m4f_set_nzc(cpu, value.result, value.carry);
	cpu->xpsr &= ~M4F_XPSR_V;
	if (value.overflow) cpu->xpsr |= M4F_XPSR_V;
}

uint32_t m4f_read_reg(const struct m4f_exec *exec, unsigned reg)
{
	if (reg == 15) {
		return exec->address + 4;
	}
	return exec->cpu->r[reg];
}

void m4f_write_reg(struct m4f_exec *exec, unsigned reg, uint32_t value)
{
	if (reg == 15) {
		exec->cpu->r[15] = value & ~UINT32_C(1);
	} else {
		exec->cpu->r[reg] = value;
	}
}

bool m4f_branch_exchange(struct m4f_exec *exec, uint32_t target)
{
	if (!(target & 1)) {
		exec->result.stop = M4F_STEP_UNDEFINED;
		return false;
	}
	exec->cpu->r[15] = target & ~UINT32_C(1);
	return true;
}

void m4f_branch_write_pc(struct m4f_exec *exec, uint32_t target)
{
	exec->cpu->r[15] = target & ~UINT32_C(1);
}

static uint32_t byte_swap(uint32_t value, unsigned size)
{
	if (size == 2) {
		return ((value & 0xff) << 8) | ((value >> 8) & 0xff);
	}
	if (size == 4) {
		return ((value & UINT32_C(0x000000ff)) << 24) |
			((value & UINT32_C(0x0000ff00)) << 8) |
			((value & UINT32_C(0x00ff0000)) >> 8) |
			((value & UINT32_C(0xff000000)) >> 24);
	}
	return value;
}

bool m4f_read_mem(struct m4f_exec *exec, uint32_t address, unsigned size,
		bool aligned, uint32_t *value)
{
	if (aligned && (address & (size - 1))) {
		exec->result.stop = M4F_STEP_ALIGNMENT_FAULT;
		exec->result.fault_address = address;
		return false;
	}
	if (!exec->cpu->memory.read) {
		exec->result.stop = M4F_STEP_DATA_FAULT;
		exec->result.fault_address = address;
		return false;
	}
	enum m4f_mem_result status = exec->cpu->memory.read(
			exec->cpu->memory.context, address, size, false, value);
	if (status != M4F_MEM_OK) {
		exec->result.stop = status == M4F_MEM_ALIGN ?
			M4F_STEP_ALIGNMENT_FAULT : M4F_STEP_DATA_FAULT;
		exec->result.fault_address = address;
		return false;
	}
	if (exec->cpu->big_endian) *value = byte_swap(*value, size);
	return true;
}

bool m4f_write_mem(struct m4f_exec *exec, uint32_t address, unsigned size,
		bool aligned, uint32_t value)
{
	if (aligned && (address & (size - 1))) {
		exec->result.stop = M4F_STEP_ALIGNMENT_FAULT;
		exec->result.fault_address = address;
		return false;
	}
	if (!exec->cpu->memory.write) {
		exec->result.stop = M4F_STEP_DATA_FAULT;
		exec->result.fault_address = address;
		return false;
	}
	if (exec->cpu->big_endian) value = byte_swap(value, size);
	enum m4f_mem_result status = exec->cpu->memory.write(
			exec->cpu->memory.context, address, size, value);
	if (status != M4F_MEM_OK) {
		exec->result.stop = status == M4F_MEM_ALIGN ?
			M4F_STEP_ALIGNMENT_FAULT : M4F_STEP_DATA_FAULT;
		exec->result.fault_address = address;
		return false;
	}
	/* A normal store by this core clears a matching local monitor. */
	if (exec->cpu->exclusive_valid &&
			address < exec->cpu->exclusive_address + exec->cpu->exclusive_size &&
			exec->cpu->exclusive_address < address + size) {
		exec->cpu->exclusive_valid = false;
	}
	return true;
}

struct m4f_step_result m4f_step(struct m4f_cpu *cpu)
{
	struct m4f_step_result result = result_at(cpu, M4F_STEP_UNDEFINED);
	uint32_t value = 0;

	if (!cpu->memory.read) {
		result.stop = M4F_STEP_FETCH_FAULT;
		result.fault_address = cpu->r[15];
		return result;
	}

	enum m4f_mem_result memory_result = cpu->memory.read(
			cpu->memory.context, cpu->r[15], 2, true, &value);
	if (memory_result != M4F_MEM_OK) {
		result.stop = memory_result == M4F_MEM_ALIGN ?
			M4F_STEP_ALIGNMENT_FAULT : M4F_STEP_FETCH_FAULT;
		result.fault_address = cpu->r[15];
		return result;
	}

	uint16_t first = (uint16_t)value;
	bool wide = (first & UINT16_C(0xf800)) >= UINT16_C(0xe800);
	uint16_t second = 0;
	if (wide) {
		memory_result = cpu->memory.read(cpu->memory.context,
				cpu->r[15] + 2, 2, true, &value);
		if (memory_result != M4F_MEM_OK) {
			result.stop = memory_result == M4F_MEM_ALIGN ?
				M4F_STEP_ALIGNMENT_FAULT : M4F_STEP_FETCH_FAULT;
			result.fault_address = cpu->r[15] + 2;
			return result;
		}
		second = (uint16_t)value;
		result.instruction = ((uint32_t)first << 16) | second;
		result.instruction_size = 4;
	} else {
		result.instruction = first;
		result.instruction_size = 2;
	}

	struct m4f_exec exec = {
		.cpu = cpu,
		.result = result,
		.address = cpu->r[15],
		.next_address = cpu->r[15] + result.instruction_size,
	};
	/* r13 is the externally visible alias of the selected stack bank. */
	if (cpu->control & 2) cpu->psp = cpu->r[13];
	else cpu->msp = cpu->r[13];
	bool in_it = cpu->itstate != 0;
	bool execute = !in_it || m4f_condition_passed(cpu, cpu->itstate >> 4);
	cpu->r[15] = exec.next_address;

	if (execute) {
		exec.result.stop = wide ? m4f_execute_32(&exec, first, second) :
			m4f_execute_16(&exec, first);
	} else {
		exec.result.stop = M4F_STEP_OK;
	}

	if (in_it) m4f_advance_it(cpu);
	if (exec.result.stop == M4F_STEP_UDF ||
			exec.result.stop == M4F_STEP_UNDEFINED ||
			exec.result.stop == M4F_STEP_UNPREDICTABLE ||
			exec.result.stop == M4F_STEP_FETCH_FAULT ||
			exec.result.stop == M4F_STEP_DATA_FAULT ||
			exec.result.stop == M4F_STEP_ALIGNMENT_FAULT) {
		cpu->r[15] = exec.address;
	}
	if (cpu->control & 2) cpu->psp = cpu->r[13];
	else cpu->msp = cpu->r[13];
	return exec.result;
}
