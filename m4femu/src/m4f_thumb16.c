#include "m4f_internal.h"

static unsigned popcount8(uint32_t value)
{
	unsigned count = 0;
	for (unsigned i = 0; i < 8; ++i) count += (value >> i) & 1;
	return count;
}

static bool implicit_flags(const struct m4f_cpu *cpu)
{
	return cpu->itstate == 0;
}

static enum m4f_stop execute_shift_immediate(struct m4f_exec *exec,
		uint16_t instruction)
{
	unsigned opcode = instruction >> 11;
	unsigned amount = M4F_BITS(instruction, 10, 6);
	unsigned rm = M4F_BITS(instruction, 5, 3);
	unsigned rd = M4F_BITS(instruction, 2, 0);
	enum m4f_shift_type type = (enum m4f_shift_type)opcode;
	if (type != M4F_SHIFT_LSL && amount == 0) amount = 32;
	struct m4f_shift_result shifted = m4f_shift(exec->cpu->r[rm], type,
			amount, (exec->cpu->xpsr & M4F_XPSR_C) != 0);
	exec->cpu->r[rd] = shifted.result;
	if (implicit_flags(exec->cpu)) {
		m4f_set_nzc(exec->cpu, shifted.result, shifted.carry);
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_add_sub(struct m4f_exec *exec,
		uint16_t instruction)
{
	bool immediate = M4F_BIT(instruction, 10);
	bool subtract = M4F_BIT(instruction, 9);
	unsigned rn_or_imm = M4F_BITS(instruction, 8, 6);
	unsigned rn = M4F_BITS(instruction, 5, 3);
	unsigned rd = M4F_BITS(instruction, 2, 0);
	uint32_t operand = immediate ? rn_or_imm : exec->cpu->r[rn_or_imm];
	struct m4f_add_result result = subtract ?
		m4f_add_with_carry(exec->cpu->r[rn], ~operand, true) :
		m4f_add_with_carry(exec->cpu->r[rn], operand, false);
	exec->cpu->r[rd] = result.result;
	if (implicit_flags(exec->cpu)) m4f_set_nzcv(exec->cpu, result);
	return M4F_STEP_OK;
}

static enum m4f_stop execute_immediate(struct m4f_exec *exec,
		uint16_t instruction)
{
	unsigned opcode = M4F_BITS(instruction, 12, 11);
	unsigned rdn = M4F_BITS(instruction, 10, 8);
	uint32_t immediate = M4F_BITS(instruction, 7, 0);
	struct m4f_add_result result;
	switch (opcode) {
	case 0: /* MOVS */
		exec->cpu->r[rdn] = immediate;
		if (implicit_flags(exec->cpu)) m4f_set_nz(exec->cpu, immediate);
		break;
	case 1: /* CMP */
		result = m4f_add_with_carry(exec->cpu->r[rdn], ~immediate, true);
		m4f_set_nzcv(exec->cpu, result);
		break;
	case 2: /* ADDS */
		result = m4f_add_with_carry(exec->cpu->r[rdn], immediate, false);
		exec->cpu->r[rdn] = result.result;
		if (implicit_flags(exec->cpu)) m4f_set_nzcv(exec->cpu, result);
		break;
	default: /* SUBS */
		result = m4f_add_with_carry(exec->cpu->r[rdn], ~immediate, true);
		exec->cpu->r[rdn] = result.result;
		if (implicit_flags(exec->cpu)) m4f_set_nzcv(exec->cpu, result);
		break;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_data_processing(struct m4f_exec *exec,
		uint16_t instruction)
{
	unsigned opcode = M4F_BITS(instruction, 9, 6);
	unsigned rm = M4F_BITS(instruction, 5, 3);
	unsigned rdn = M4F_BITS(instruction, 2, 0);
	uint32_t x = exec->cpu->r[rdn];
	uint32_t y = exec->cpu->r[rm];
	uint32_t value = 0;
	bool write = true;
	bool setflags = implicit_flags(exec->cpu);
	struct m4f_shift_result shifted;
	struct m4f_add_result added;

	switch (opcode) {
	case 0: value = x & y; break;
	case 1: value = x ^ y; break;
	case 2:
	case 3:
	case 4:
	case 7:
		shifted = m4f_shift(x,
				opcode == 2 ? M4F_SHIFT_LSL :
				opcode == 3 ? M4F_SHIFT_LSR :
				opcode == 4 ? M4F_SHIFT_ASR : M4F_SHIFT_ROR,
				y & 0xff, (exec->cpu->xpsr & M4F_XPSR_C) != 0);
		value = shifted.result;
		if (setflags) m4f_set_nzc(exec->cpu, value, shifted.carry);
		setflags = false;
		break;
	case 5: /* ADC */
		added = m4f_add_with_carry(x, y,
				(exec->cpu->xpsr & M4F_XPSR_C) != 0);
		value = added.result;
		if (setflags) m4f_set_nzcv(exec->cpu, added);
		setflags = false;
		break;
	case 6: /* SBC */
		added = m4f_add_with_carry(x, ~y,
				(exec->cpu->xpsr & M4F_XPSR_C) != 0);
		value = added.result;
		if (setflags) m4f_set_nzcv(exec->cpu, added);
		setflags = false;
		break;
	case 8: /* TST */
		value = x & y;
		write = false;
		setflags = true;
		break;
	case 9: /* RSB #0 */
		added = m4f_add_with_carry(0, ~y, true);
		value = added.result;
		if (setflags) m4f_set_nzcv(exec->cpu, added);
		setflags = false;
		break;
	case 10: /* CMP */
		added = m4f_add_with_carry(x, ~y, true);
		m4f_set_nzcv(exec->cpu, added);
		return M4F_STEP_OK;
	case 11: /* CMN */
		added = m4f_add_with_carry(x, y, false);
		m4f_set_nzcv(exec->cpu, added);
		return M4F_STEP_OK;
	case 12: value = x | y; break;
	case 13: value = x * y; break;
	case 14: value = x & ~y; break;
	default: value = ~y; break;
	}
	if (write) exec->cpu->r[rdn] = value;
	if (setflags) m4f_set_nz(exec->cpu, value);
	return M4F_STEP_OK;
}

static enum m4f_stop execute_special(struct m4f_exec *exec,
		uint16_t instruction)
{
	unsigned opcode = M4F_BITS(instruction, 9, 8);
	unsigned rm = M4F_BITS(instruction, 6, 3);
	unsigned rdn = M4F_BITS(instruction, 2, 0) |
		(M4F_BIT(instruction, 7) << 3);
	uint32_t x = m4f_read_reg(exec, rdn);
	uint32_t y = m4f_read_reg(exec, rm);
	if (opcode == 0) { /* ADD high registers */
		uint32_t value = x + y;
		if (rdn == 15) {
			m4f_branch_write_pc(exec, value);
		} else {
			exec->cpu->r[rdn] = value;
		}
	} else if (opcode == 1) { /* CMP high registers */
		m4f_set_nzcv(exec->cpu, m4f_add_with_carry(x, ~y, true));
	} else if (opcode == 2) { /* MOV high registers */
		if (rdn == 15) {
			m4f_branch_write_pc(exec, y);
		} else {
			exec->cpu->r[rdn] = y;
		}
	} else { /* BX or BLX */
		if (instruction & 7) return M4F_STEP_UNPREDICTABLE;
		if (M4F_BIT(instruction, 7)) exec->cpu->r[14] = exec->next_address | 1;
		if (!m4f_branch_exchange(exec, y)) return exec->result.stop;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_load_store_register(struct m4f_exec *exec,
		uint16_t instruction)
{
	unsigned opcode = M4F_BITS(instruction, 11, 9);
	unsigned rm = M4F_BITS(instruction, 8, 6);
	unsigned rn = M4F_BITS(instruction, 5, 3);
	unsigned rt = M4F_BITS(instruction, 2, 0);
	uint32_t address = exec->cpu->r[rn] + exec->cpu->r[rm];
	uint32_t value;
	unsigned size = (opcode == 1 || opcode == 5 || opcode == 7) ? 2 :
		(opcode == 2 || opcode == 3 || opcode == 6) ? 1 : 4;
	if (opcode == 0 || opcode == 1 || opcode == 2) {
		if (!m4f_write_mem(exec, address, size, false, exec->cpu->r[rt]))
			return exec->result.stop;
	} else {
		if (!m4f_read_mem(exec, address, size, false, &value))
			return exec->result.stop;
		if (opcode == 3) value = (uint32_t)m4f_sign_extend(value, 8);
		if (opcode == 7) value = (uint32_t)m4f_sign_extend(value, 16);
		exec->cpu->r[rt] = value;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_load_store_immediate(struct m4f_exec *exec,
		uint16_t instruction)
{
	unsigned top = M4F_BITS(instruction, 15, 12);
	bool byte = top == 7;
	bool load = M4F_BIT(instruction, 11);
	unsigned immediate = M4F_BITS(instruction, 10, 6);
	unsigned rn = M4F_BITS(instruction, 5, 3);
	unsigned rt = M4F_BITS(instruction, 2, 0);
	unsigned size = byte ? 1 : 4;
	uint32_t address = exec->cpu->r[rn] + (byte ? immediate : immediate * 4);
	uint32_t value;
	if (load) {
		if (!m4f_read_mem(exec, address, size, false, &value))
			return exec->result.stop;
		exec->cpu->r[rt] = value;
	} else if (!m4f_write_mem(exec, address, size, false, exec->cpu->r[rt])) {
		return exec->result.stop;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_halfword_immediate(struct m4f_exec *exec,
		uint16_t instruction)
{
	bool load = M4F_BIT(instruction, 11);
	unsigned immediate = M4F_BITS(instruction, 10, 6) * 2;
	unsigned rn = M4F_BITS(instruction, 5, 3);
	unsigned rt = M4F_BITS(instruction, 2, 0);
	uint32_t address = exec->cpu->r[rn] + immediate;
	uint32_t value;
	if (load) {
		if (!m4f_read_mem(exec, address, 2, false, &value))
			return exec->result.stop;
		exec->cpu->r[rt] = value;
	} else if (!m4f_write_mem(exec, address, 2, false, exec->cpu->r[rt])) {
		return exec->result.stop;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_sp_relative(struct m4f_exec *exec,
		uint16_t instruction)
{
	bool load = M4F_BIT(instruction, 11);
	unsigned rt = M4F_BITS(instruction, 10, 8);
	uint32_t address = exec->cpu->r[13] + M4F_BITS(instruction, 7, 0) * 4;
	uint32_t value;
	if (load) {
		if (!m4f_read_mem(exec, address, 4, false, &value))
			return exec->result.stop;
		exec->cpu->r[rt] = value;
	} else if (!m4f_write_mem(exec, address, 4, false, exec->cpu->r[rt])) {
		return exec->result.stop;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_extend(struct m4f_exec *exec,
		uint16_t instruction)
{
	unsigned opcode = M4F_BITS(instruction, 7, 6);
	unsigned rm = M4F_BITS(instruction, 5, 3);
	unsigned rd = M4F_BITS(instruction, 2, 0);
	uint32_t value = exec->cpu->r[rm];
	if (opcode == 0) value = (uint32_t)m4f_sign_extend(value & 0xffff, 16);
	if (opcode == 1) value = (uint32_t)m4f_sign_extend(value & 0xff, 8);
	if (opcode == 2) value &= 0xffff;
	if (opcode == 3) value &= 0xff;
	exec->cpu->r[rd] = value;
	return M4F_STEP_OK;
}

static enum m4f_stop execute_reverse(struct m4f_exec *exec,
		uint16_t instruction)
{
	unsigned opcode = M4F_BITS(instruction, 7, 6);
	unsigned rm = M4F_BITS(instruction, 5, 3);
	unsigned rd = M4F_BITS(instruction, 2, 0);
	uint32_t value = exec->cpu->r[rm];
	if (opcode == 0) {
		value = ((value & UINT32_C(0x000000ff)) << 24) |
			((value & UINT32_C(0x0000ff00)) << 8) |
			((value & UINT32_C(0x00ff0000)) >> 8) |
			((value & UINT32_C(0xff000000)) >> 24);
	} else if (opcode == 1) {
		value = ((value & UINT32_C(0x00ff00ff)) << 8) |
			((value & UINT32_C(0xff00ff00)) >> 8);
	} else if (opcode == 3) {
		value = (uint32_t)m4f_sign_extend(
				((value & 0xff) << 8) | ((value >> 8) & 0xff), 16);
	} else {
		return M4F_STEP_UNDEFINED;
	}
	exec->cpu->r[rd] = value;
	return M4F_STEP_OK;
}

static enum m4f_stop execute_push_pop(struct m4f_exec *exec,
		uint16_t instruction)
{
	bool pop = M4F_BIT(instruction, 11);
	bool extra = M4F_BIT(instruction, 8);
	uint32_t registers = instruction & 0xff;
	unsigned count = popcount8(registers) + (extra ? 1 : 0);
	if (count == 0) return M4F_STEP_UNPREDICTABLE;
	uint32_t address = pop ? exec->cpu->r[13] : exec->cpu->r[13] - count * 4;
	uint32_t original = address;
	uint32_t value;
	if (!pop) {
		for (unsigned reg = 0; reg < 8; ++reg) {
			if (registers & (1u << reg)) {
				if (!m4f_write_mem(exec, address, 4, true,
						exec->cpu->r[reg])) return exec->result.stop;
				address += 4;
			}
		}
		if (extra && !m4f_write_mem(exec, address, 4, true,
				exec->cpu->r[14])) return exec->result.stop;
		exec->cpu->r[13] = original;
	} else {
		for (unsigned reg = 0; reg < 8; ++reg) {
			if (registers & (1u << reg)) {
				if (!m4f_read_mem(exec, address, 4, true, &value))
					return exec->result.stop;
				exec->cpu->r[reg] = value;
				address += 4;
			}
		}
		if (extra) {
			if (!m4f_read_mem(exec, address, 4, true, &value))
				return exec->result.stop;
			address += 4;
			if (!m4f_branch_exchange(exec, value)) return exec->result.stop;
		}
		exec->cpu->r[13] = address;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_misc(struct m4f_exec *exec,
		uint16_t instruction)
{
	if ((instruction & 0xff00) == 0xb000) { /* ADD/SUB SP */
		uint32_t amount = (instruction & 0x7f) * 4;
		exec->cpu->r[13] = M4F_BIT(instruction, 7) ?
			exec->cpu->r[13] - amount : exec->cpu->r[13] + amount;
		return M4F_STEP_OK;
	}
	if ((instruction & 0xf500) == 0xb100) { /* CBZ/CBNZ */
		if (exec->cpu->itstate) return M4F_STEP_UNPREDICTABLE;
		unsigned rn = instruction & 7;
		uint32_t offset = (M4F_BIT(instruction, 9) << 6) |
			(M4F_BITS(instruction, 7, 3) << 1);
		bool nonzero = M4F_BIT(instruction, 11);
		if ((exec->cpu->r[rn] != 0) == nonzero)
			exec->cpu->r[15] = exec->address + 4 + offset;
		return M4F_STEP_OK;
	}
	if ((instruction & 0xff00) == 0xb200) return execute_extend(exec, instruction);
	if ((instruction & 0xfe00) == 0xb400 ||
			(instruction & 0xfe00) == 0xbc00)
		return execute_push_pop(exec, instruction);
	if ((instruction & 0xff00) == 0xba00) return execute_reverse(exec, instruction);
	if ((instruction & 0xfff7) == 0xb650) { /* SETEND */
		exec->cpu->big_endian = M4F_BIT(instruction, 3);
		return M4F_STEP_OK;
	}
	if ((instruction & 0xffe8) == 0xb660) { /* CPSIE/CPSID */
		if (exec->cpu->itstate) return M4F_STEP_UNPREDICTABLE;
		if (!(instruction & 3)) return M4F_STEP_UNPREDICTABLE;
		if (exec->cpu->control & 1) return M4F_STEP_OK;
		bool disable = M4F_BIT(instruction, 4);
		if (instruction & 2) exec->cpu->primask = disable;
		if (instruction & 1) exec->cpu->faultmask = disable;
		return M4F_STEP_OK;
	}
	if ((instruction & 0xff00) == 0xbe00) {
		exec->result.immediate = instruction & 0xff;
		return M4F_STEP_BKPT;
	}
	if ((instruction & 0xff00) == 0xbf00) {
		unsigned condition = M4F_BITS(instruction, 7, 4);
		unsigned mask = instruction & 0xf;
		if (mask) {
			if (exec->cpu->itstate || condition >= 14)
				return M4F_STEP_UNPREDICTABLE;
			exec->cpu->itstate = (uint8_t)((condition << 4) | mask);
			return M4F_STEP_OK;
		}
		switch (condition) {
		case 2: /* WFE */
			if (exec->cpu->event_registered) {
				exec->cpu->event_registered = false;
				return M4F_STEP_OK;
			}
			return M4F_STEP_WFE;
		case 3: return M4F_STEP_WFI;
		case 4: exec->cpu->event_registered = true; return M4F_STEP_OK;
		default: return M4F_STEP_OK; /* NOP, YIELD, and implementation hints */
		}
	}
	return M4F_STEP_UNDEFINED;
}

static enum m4f_stop execute_multiple(struct m4f_exec *exec,
		uint16_t instruction)
{
	bool load = M4F_BIT(instruction, 11);
	unsigned rn = M4F_BITS(instruction, 10, 8);
	uint32_t registers = instruction & 0xff;
	if (!registers) return M4F_STEP_UNPREDICTABLE;
	uint32_t address = exec->cpu->r[rn];
	uint32_t original = address;
	uint32_t value;
	for (unsigned reg = 0; reg < 8; ++reg) {
		if (!(registers & (1u << reg))) continue;
		if (load) {
			if (!m4f_read_mem(exec, address, 4, true, &value))
				return exec->result.stop;
			exec->cpu->r[reg] = value;
		} else if (!m4f_write_mem(exec, address, 4, true,
				exec->cpu->r[reg])) {
			return exec->result.stop;
		}
		address += 4;
	}
	if (!load || !(registers & (1u << rn))) exec->cpu->r[rn] = address;
	(void)original;
	return M4F_STEP_OK;
}

enum m4f_stop m4f_execute_16(struct m4f_exec *exec, uint16_t instruction)
{
	if ((instruction & 0xe000) == 0x0000) {
		if ((instruction & 0x1800) == 0x1800)
			return execute_add_sub(exec, instruction);
		return execute_shift_immediate(exec, instruction);
	}
	if ((instruction & 0xe000) == 0x2000)
		return execute_immediate(exec, instruction);
	if ((instruction & 0xfc00) == 0x4000)
		return execute_data_processing(exec, instruction);
	if ((instruction & 0xfc00) == 0x4400)
		return execute_special(exec, instruction);
	if ((instruction & 0xf800) == 0x4800) { /* LDR literal */
		unsigned rt = M4F_BITS(instruction, 10, 8);
		uint32_t address = ((exec->address + 4) & ~UINT32_C(3)) +
			M4F_BITS(instruction, 7, 0) * 4;
		uint32_t value;
		if (!m4f_read_mem(exec, address, 4, false, &value))
			return exec->result.stop;
		exec->cpu->r[rt] = value;
		return M4F_STEP_OK;
	}
	if ((instruction & 0xf000) == 0x5000)
		return execute_load_store_register(exec, instruction);
	if ((instruction & 0xe000) == 0x6000)
		return execute_load_store_immediate(exec, instruction);
	if ((instruction & 0xf000) == 0x8000)
		return execute_halfword_immediate(exec, instruction);
	if ((instruction & 0xf000) == 0x9000)
		return execute_sp_relative(exec, instruction);
	if ((instruction & 0xf000) == 0xa000) { /* ADR / ADD Rd,SP */
		unsigned rd = M4F_BITS(instruction, 10, 8);
		uint32_t base = M4F_BIT(instruction, 11) ? exec->cpu->r[13] :
			((exec->address + 4) & ~UINT32_C(3));
		exec->cpu->r[rd] = base + M4F_BITS(instruction, 7, 0) * 4;
		return M4F_STEP_OK;
	}
	if ((instruction & 0xf000) == 0xb000)
		return execute_misc(exec, instruction);
	if ((instruction & 0xf000) == 0xc000)
		return execute_multiple(exec, instruction);
	if ((instruction & 0xf000) == 0xd000) {
		unsigned condition = M4F_BITS(instruction, 11, 8);
		if (condition == 14) {
			exec->result.immediate = instruction & 0xff;
			return M4F_STEP_UDF;
		}
		if (condition == 15) {
			exec->result.immediate = instruction & 0xff;
			return M4F_STEP_SVC;
		}
		if (exec->cpu->itstate) return M4F_STEP_UNPREDICTABLE;
		if (m4f_condition_passed(exec->cpu, condition)) {
			int32_t offset = m4f_sign_extend(instruction & 0xff, 8) * 2;
			exec->cpu->r[15] = exec->address + 4 + (uint32_t)offset;
		}
		return M4F_STEP_OK;
	}
	if ((instruction & 0xf800) == 0xe000) {
		if (exec->cpu->itstate) return M4F_STEP_UNPREDICTABLE;
		int32_t offset = m4f_sign_extend(instruction & 0x7ff, 11) * 2;
		exec->cpu->r[15] = exec->address + 4 + (uint32_t)offset;
		return M4F_STEP_OK;
	}
	return M4F_STEP_UNDEFINED;
}
