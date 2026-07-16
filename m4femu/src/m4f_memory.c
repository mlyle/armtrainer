#include "m4f_internal.h"

static unsigned popcount16(uint32_t value)
{
	unsigned count = 0;
	for (unsigned i = 0; i < 16; ++i) count += (value >> i) & 1;
	return count;
}

static enum m4f_stop load_value(struct m4f_exec *exec, unsigned rt,
		uint32_t address, unsigned size, bool sign_extend)
{
	uint32_t value;
	if (!m4f_read_mem(exec, address, size, false, &value))
		return exec->result.stop;
	if (sign_extend) value = (uint32_t)m4f_sign_extend(value, size * 8);
	if (rt == 15) {
		if (size != 4 || sign_extend) return M4F_STEP_UNPREDICTABLE;
		if (!m4f_branch_exchange(exec, value)) return exec->result.stop;
	} else {
		exec->cpu->r[rt] = value;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop store_value(struct m4f_exec *exec, unsigned rt,
		uint32_t address, unsigned size)
{
	if (!m4f_write_mem(exec, address, size, false,
			m4f_read_reg(exec, rt))) return exec->result.stop;
	return M4F_STEP_OK;
}

static enum m4f_stop execute_single_imm12(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned operation = M4F_BITS(first, 7, 4);
	unsigned rn = first & 0xf;
	unsigned rt = M4F_BITS(second, 15, 12);
	uint32_t immediate = second & 0xfff;
	bool load = operation & 1;
	unsigned size = operation < 10 ? 1 : operation < 12 ? 2 : 4;
	bool sign_extend = M4F_BIT(first, 8) && load && size < 4;
	uint32_t base = rn == 15 ? ((exec->address + 4) & ~UINT32_C(3)) :
		exec->cpu->r[rn];
	if (rn == 15 && !load) return M4F_STEP_UNPREDICTABLE;
	uint32_t address = base + immediate;
	if (rt == 15 && size < 4) return M4F_STEP_OK; /* PLD/PLI */
	return load ? load_value(exec, rt, address, size, sign_extend) :
		store_value(exec, rt, address, size);
}

static enum m4f_stop execute_single_flexible(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned operation = M4F_BITS(first, 7, 4);
	unsigned rn = first & 0xf;
	unsigned rt = M4F_BITS(second, 15, 12);
	bool load = operation & 1;
	unsigned size = operation < 2 ? 1 : operation < 4 ? 2 : 4;
	bool sign_extend = M4F_BIT(first, 8) && load && size < 4;
	if (rt == 15 && size < 4) return M4F_STEP_OK; /* PLD/PLI */
	if (rn == 15) {
		/* The subtracting literal form uses this encoding with imm12. */
		if (!load || size != 4)
			return M4F_STEP_UNPREDICTABLE;
		uint32_t base = (exec->address + 4) & ~UINT32_C(3);
		return load_value(exec, rt, base - (second & 0xfff), 4, false);
	}
	uint32_t base = exec->cpu->r[rn];
	uint32_t address;
	bool writeback = false;
	uint32_t updated = base;
	if (!M4F_BIT(second, 11)) { /* register offset */
		if (M4F_BITS(second, 11, 6) != 0 || (second & 0xf) == 15)
			return M4F_STEP_UNDEFINED;
		unsigned shift = M4F_BITS(second, 5, 4);
		address = base + (exec->cpu->r[second & 0xf] << shift);
	} else {
		bool index = M4F_BIT(second, 10);
		bool add = M4F_BIT(second, 9);
		writeback = M4F_BIT(second, 8);
		uint32_t immediate = second & 0xff;
		if (!index && !writeback) return M4F_STEP_UNDEFINED;
		updated = add ? base + immediate : base - immediate;
		address = index ? updated : base;
		if (writeback && rn == rt) return M4F_STEP_UNPREDICTABLE;
	}
	enum m4f_stop stop = load ? load_value(exec, rt, address, size, sign_extend) :
		store_value(exec, rt, address, size);
	if (stop == M4F_STEP_OK && writeback) exec->cpu->r[rn] = updated;
	return stop;
}

static enum m4f_stop execute_multiple(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	bool load = M4F_BIT(first, 4);
	bool writeback = M4F_BIT(first, 5);
	bool decrement_before = M4F_BIT(first, 8);
	unsigned rn = first & 0xf;
	uint32_t registers = second;
	unsigned count = popcount16(registers);
	if (rn == 15 || count < 2 || (registers & (1u << 13)))
		return M4F_STEP_UNPREDICTABLE;
	if (load && (registers & (1u << 14))) return M4F_STEP_UNPREDICTABLE;
	if (!load && (registers & (1u << 15))) return M4F_STEP_UNPREDICTABLE;
	if (writeback && load && (registers & (1u << rn)))
		return M4F_STEP_UNPREDICTABLE;
	uint32_t base = exec->cpu->r[rn];
	uint32_t address = decrement_before ? base - count * 4 : base;
	uint32_t value;
	uint32_t pc_value = 0;
	for (unsigned reg = 0; reg < 16; ++reg) {
		if (!(registers & (1u << reg))) continue;
		if (load) {
			if (!m4f_read_mem(exec, address, 4, true, &value))
				return exec->result.stop;
			if (reg == 15) pc_value = value;
			else exec->cpu->r[reg] = value;
		} else {
			if (!m4f_write_mem(exec, address, 4, true,
					m4f_read_reg(exec, reg))) return exec->result.stop;
		}
		address += 4;
	}
	if (writeback) exec->cpu->r[rn] = decrement_before ?
		base - count * 4 : base + count * 4;
	if (load && (registers & (1u << 15)) &&
			!m4f_branch_exchange(exec, pc_value)) return exec->result.stop;
	return M4F_STEP_OK;
}

static enum m4f_stop execute_exclusive_word(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	bool load = M4F_BIT(first, 4);
	unsigned rn = first & 0xf;
	unsigned rt = M4F_BITS(second, 15, 12);
	unsigned rd = M4F_BITS(second, 11, 8);
	uint32_t address = exec->cpu->r[rn] + (second & 0xff) * 4;
	if (rn == 15 || rt == 15 || (!load && rd == 15))
		return M4F_STEP_UNPREDICTABLE;
	if (load) {
		if (rd != 15) return M4F_STEP_UNDEFINED;
		uint32_t value;
		if (!m4f_read_mem(exec, address, 4, true, &value))
			return exec->result.stop;
		exec->cpu->r[rt] = value;
		exec->cpu->exclusive_address = address;
		exec->cpu->exclusive_value = value;
		exec->cpu->exclusive_size = 4;
		exec->cpu->exclusive_valid = true;
	} else {
		bool succeeds = exec->cpu->exclusive_valid &&
			exec->cpu->exclusive_address == address &&
			exec->cpu->exclusive_size == 4;
		exec->cpu->exclusive_valid = false;
		if (succeeds && !m4f_write_mem(exec, address, 4, true,
				exec->cpu->r[rt])) return exec->result.stop;
		exec->cpu->r[rd] = succeeds ? 0 : 1;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_exclusive_small_or_table(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned rn = first & 0xf;
	if ((first & 0xfff0) == 0xe8d0 && (second & 0xffe0) == 0xf000) {
		unsigned rm = second & 0xf;
		unsigned size = M4F_BIT(second, 4) ? 2 : 1;
		if (rn == 15 || rm == 15 || exec->cpu->itstate)
			return M4F_STEP_UNPREDICTABLE;
		uint32_t value;
		uint32_t address = exec->cpu->r[rn] +
			(size == 2 ? exec->cpu->r[rm] * 2 : exec->cpu->r[rm]);
		if (!m4f_read_mem(exec, address, size, false, &value))
			return exec->result.stop;
		exec->cpu->r[15] = exec->address + 4 + value * 2;
		return M4F_STEP_OK;
	}
	bool load = M4F_BIT(first, 4);
	unsigned operation = M4F_BITS(second, 7, 4);
	unsigned size = operation == 4 ? 1 : operation == 5 ? 2 : 0;
	if (!size) return M4F_STEP_UNDEFINED;
	unsigned rt = M4F_BITS(second, 15, 12);
	unsigned rd = second & 0xf;
	if (rn == 15 || rt == 15 || (load ? rd != 15 : rd == 15))
		return M4F_STEP_UNPREDICTABLE;
	uint32_t address = exec->cpu->r[rn];
	if (load) {
		uint32_t value;
		if (!m4f_read_mem(exec, address, size, true, &value))
			return exec->result.stop;
		exec->cpu->r[rt] = value;
		exec->cpu->exclusive_address = address;
		exec->cpu->exclusive_value = value;
		exec->cpu->exclusive_size = (uint8_t)size;
		exec->cpu->exclusive_valid = true;
	} else {
		bool succeeds = exec->cpu->exclusive_valid &&
			exec->cpu->exclusive_address == address &&
			exec->cpu->exclusive_size == size;
		exec->cpu->exclusive_valid = false;
		if (succeeds && !m4f_write_mem(exec, address, size, true,
				exec->cpu->r[rt])) return exec->result.stop;
		exec->cpu->r[rd] = succeeds ? 0 : 1;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_dual(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	bool index = M4F_BIT(first, 8);
	bool add = M4F_BIT(first, 7);
	bool writeback = M4F_BIT(first, 5);
	bool load = M4F_BIT(first, 4);
	unsigned rn = first & 0xf;
	unsigned rt = M4F_BITS(second, 15, 12);
	unsigned rt2 = M4F_BITS(second, 11, 8);
	uint32_t immediate = (second & 0xff) * 4;
	bool literal = rn == 15 && load && index && !writeback;
	if ((!literal && rn == 15) || rt == 13 || rt == 15 ||
			rt2 == 13 || rt2 == 15 || rt == rt2 ||
			(writeback && (rn == rt || rn == rt2)))
		return M4F_STEP_UNPREDICTABLE;
	uint32_t base = literal ? ((exec->address + 4) & ~UINT32_C(3)) :
		exec->cpu->r[rn];
	uint32_t updated = add ? base + immediate : base - immediate;
	uint32_t address = index ? updated : base;
	if (load) {
		uint32_t first_value, second_value;
		if (!m4f_read_mem(exec, address, 4, true, &first_value) ||
				!m4f_read_mem(exec, address + 4, 4, true, &second_value))
			return exec->result.stop;
		exec->cpu->r[rt] = first_value;
		exec->cpu->r[rt2] = second_value;
	} else {
		if (!m4f_write_mem(exec, address, 4, true, exec->cpu->r[rt]) ||
				!m4f_write_mem(exec, address + 4, 4, true,
					exec->cpu->r[rt2])) return exec->result.stop;
	}
	if (writeback) exec->cpu->r[rn] = updated;
	return M4F_STEP_OK;
}

enum m4f_stop m4f_execute_32_memory(struct m4f_exec *exec, uint16_t first,
		uint16_t second)
{
	if ((first & 0xfe40) == 0xe800)
		return execute_multiple(exec, first, second);
	if ((first & 0xfff0) == 0xe850 || (first & 0xfff0) == 0xe840)
		return execute_exclusive_word(exec, first, second);
	if (((first & 0xfff0) == 0xe8d0 || (first & 0xfff0) == 0xe8c0) &&
			((second & 0xffe0) == 0xf000 ||
			 M4F_BITS(second, 7, 4) == 4 || M4F_BITS(second, 7, 4) == 5))
		return execute_exclusive_small_or_table(exec, first, second);
	if ((first & 0xfe40) == 0xe840)
		return execute_dual(exec, first, second);
	if ((first & 0xfe00) == 0xf800) {
		unsigned operation = M4F_BITS(first, 7, 4);
		if (operation >= 8) return execute_single_imm12(exec, first, second);
		if (operation <= 5 || operation == 9 || operation == 11)
			return execute_single_flexible(exec, first, second);
	}
	return M4F_STEP_UNDEFINED;
}
