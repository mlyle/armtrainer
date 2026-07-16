#include "m4f_internal.h"

#include <limits.h>

static bool is_sp_or_pc(unsigned reg)
{
	return reg == 13 || reg == 15;
}

static struct m4f_shift_result decode_imm_shift(uint32_t value,
		unsigned type_bits, unsigned amount, bool carry)
{
	enum m4f_shift_type type;
	if (type_bits == 0) {
		type = M4F_SHIFT_LSL;
	} else if (type_bits == 1) {
		type = M4F_SHIFT_LSR;
		if (!amount) amount = 32;
	} else if (type_bits == 2) {
		type = M4F_SHIFT_ASR;
		if (!amount) amount = 32;
	} else if (!amount) {
		type = M4F_SHIFT_RRX;
	} else {
		type = M4F_SHIFT_ROR;
	}
	return m4f_shift(value, type, amount, carry);
}

static enum m4f_stop data_processing_common(struct m4f_exec *exec,
		unsigned opcode, bool setflags, unsigned rn, unsigned rd,
		uint32_t operand2, bool shifter_carry)
{
	uint32_t operand1 = m4f_read_reg(exec, rn);
	uint32_t result = 0;
	bool logical = false;
	struct m4f_add_result added;

	switch (opcode) {
	case 0: result = operand1 & operand2; logical = true; break; /* AND */
	case 1: result = operand1 & ~operand2; logical = true; break; /* BIC */
	case 2: result = operand1 | operand2; logical = true; break; /* ORR/MOV */
	case 3: result = operand1 | ~operand2; logical = true; break; /* ORN/MVN */
	case 4: result = operand1 ^ operand2; logical = true; break; /* EOR */
	case 8:
		added = m4f_add_with_carry(operand1, operand2, false);
		result = added.result;
		break;
	case 10:
		added = m4f_add_with_carry(operand1, operand2,
				(exec->cpu->xpsr & M4F_XPSR_C) != 0);
		result = added.result;
		break;
	case 11:
		added = m4f_add_with_carry(operand1, ~operand2,
				(exec->cpu->xpsr & M4F_XPSR_C) != 0);
		result = added.result;
		break;
	case 13:
		added = m4f_add_with_carry(operand1, ~operand2, true);
		result = added.result;
		break;
	case 14:
		added = m4f_add_with_carry(~operand1, operand2, true);
		result = added.result;
		break;
	default:
		return M4F_STEP_UNDEFINED;
	}

	if (rd == 15) {
		if (!setflags) return M4F_STEP_UNPREDICTABLE;
		if (logical) {
			m4f_set_nzc(exec->cpu, result, shifter_carry);
		} else {
			m4f_set_nzcv(exec->cpu, added);
		}
		return M4F_STEP_OK;
	}

	exec->cpu->r[rd] = result;
	if (setflags) {
		if (logical) m4f_set_nzc(exec->cpu, result, shifter_carry);
		else m4f_set_nzcv(exec->cpu, added);
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_shifted_register(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned opcode = M4F_BITS(first, 8, 5);
	bool setflags = M4F_BIT(first, 4);
	unsigned rn = first & 0xf;
	unsigned rd = M4F_BITS(second, 11, 8);
	unsigned rm = second & 0xf;
	unsigned shift_type = M4F_BITS(second, 5, 4);
	unsigned amount = (M4F_BITS(second, 14, 12) << 2) |
		M4F_BITS(second, 7, 6);
	bool carry = (exec->cpu->xpsr & M4F_XPSR_C) != 0;
	struct m4f_shift_result shifted = decode_imm_shift(
			exec->cpu->r[rm], shift_type, amount, carry);

	if (opcode == 6) { /* PKHBT / PKHTB */
		if (setflags || rd == 15 || rn == 15 || rm == 15)
			return M4F_STEP_UNPREDICTABLE;
		if (M4F_BIT(second, 5)) {
			exec->cpu->r[rd] = (exec->cpu->r[rn] & UINT32_C(0xffff0000)) |
				(shifted.result & UINT32_C(0x0000ffff));
		} else {
			exec->cpu->r[rd] = (exec->cpu->r[rn] & UINT32_C(0x0000ffff)) |
				(shifted.result & UINT32_C(0xffff0000));
		}
		return M4F_STEP_OK;
	}

	/* ORR/ORN with Rn == PC are MOV/MVN aliases. */
	if (rn == 15 && opcode != 2 && opcode != 3)
		return M4F_STEP_UNPREDICTABLE;
	uint32_t operand2 = shifted.result;
	if (rn == 15 && opcode == 2) {
		if (rd == 15) return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rd] = operand2;
		if (setflags) m4f_set_nzc(exec->cpu, operand2, shifted.carry);
		return M4F_STEP_OK;
	}
	if (rn == 15 && opcode == 3) {
		if (rd == 15) return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rd] = ~operand2;
		if (setflags) m4f_set_nzc(exec->cpu, ~operand2, shifted.carry);
		return M4F_STEP_OK;
	}
	return data_processing_common(exec, opcode, setflags, rn, rd,
			operand2, shifted.carry);
}

static struct m4f_shift_result thumb_expand_immediate(uint32_t imm12,
		bool carry_in, bool *valid)
{
	struct m4f_shift_result result = { 0, carry_in };
	*valid = true;
	uint32_t imm8 = imm12 & 0xff;
	if ((imm12 & 0xc00) == 0) {
		switch ((imm12 >> 8) & 3) {
		case 0: result.result = imm8; break;
		case 1:
			if (!imm8) *valid = false;
			result.result = (imm8 << 16) | imm8;
			break;
		case 2:
			if (!imm8) *valid = false;
			result.result = (imm8 << 24) | (imm8 << 8);
			break;
		default:
			if (!imm8) *valid = false;
			result.result = imm8 * UINT32_C(0x01010101);
			break;
		}
	} else {
		unsigned rotation = M4F_BITS(imm12, 11, 7);
		result.result = m4f_ror32(UINT32_C(0x80) | (imm12 & 0x7f), rotation);
		result.carry = M4F_BIT(result.result, 31);
	}
	return result;
}

static enum m4f_stop execute_modified_immediate(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned opcode = M4F_BITS(first, 8, 5);
	bool setflags = M4F_BIT(first, 4);
	unsigned rn = first & 0xf;
	unsigned rd = M4F_BITS(second, 11, 8);
	uint32_t imm12 = (M4F_BIT(first, 10) << 11) |
		(M4F_BITS(second, 14, 12) << 8) | (second & 0xff);
	bool valid;
	struct m4f_shift_result immediate = thumb_expand_immediate(imm12,
			(exec->cpu->xpsr & M4F_XPSR_C) != 0, &valid);
	if (!valid) return M4F_STEP_UNPREDICTABLE;

	if (rn == 15 && opcode == 2) { /* MOV immediate */
		if (rd == 15) return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rd] = immediate.result;
		if (setflags) m4f_set_nzc(exec->cpu, immediate.result, immediate.carry);
		return M4F_STEP_OK;
	}
	if (rn == 15 && opcode == 3) { /* MVN immediate */
		if (rd == 15) return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rd] = ~immediate.result;
		if (setflags) m4f_set_nzc(exec->cpu, ~immediate.result, immediate.carry);
		return M4F_STEP_OK;
	}
	if (rn == 15) return M4F_STEP_UNPREDICTABLE;
	return data_processing_common(exec, opcode, setflags, rn, rd,
			immediate.result, immediate.carry);
}

static int32_t signed_saturate(struct m4f_cpu *cpu, int64_t value,
		unsigned bits)
{
	int64_t low = bits == 32 ? INT32_MIN : -(INT64_C(1) << (bits - 1));
	int64_t high = bits == 32 ? INT32_MAX : (INT64_C(1) << (bits - 1)) - 1;
	if (value < low) {
		cpu->xpsr |= M4F_XPSR_Q;
		return (int32_t)low;
	}
	if (value > high) {
		cpu->xpsr |= M4F_XPSR_Q;
		return (int32_t)high;
	}
	return (int32_t)value;
}

static uint32_t unsigned_saturate(struct m4f_cpu *cpu, int64_t value,
		unsigned bits)
{
	uint64_t high = bits == 32 ? UINT32_MAX :
		bits == 0 ? 0 : (UINT64_C(1) << bits) - 1;
	if (value < 0) {
		cpu->xpsr |= M4F_XPSR_Q;
		return 0;
	}
	if ((uint64_t)value > high) {
		cpu->xpsr |= M4F_XPSR_Q;
		return (uint32_t)high;
	}
	return (uint32_t)value;
}

static enum m4f_stop execute_saturate(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	bool is_unsigned = (first & 0x0080) != 0;
	bool halfwords = !is_unsigned && M4F_BIT(first, 5) &&
		M4F_BITS(second, 14, 12) == 0 && M4F_BITS(second, 7, 6) == 0 &&
		(second & 0x1f) <= 15;
	if ((first & 0xfbe0) == 0xf3a0) halfwords = true;
	unsigned rn = first & 0xf;
	unsigned rd = M4F_BITS(second, 11, 8);
	if (is_sp_or_pc(rn) || is_sp_or_pc(rd)) return M4F_STEP_UNPREDICTABLE;
	uint32_t source = exec->cpu->r[rn];
	if (halfwords) {
		unsigned sat = (second & 0xf) + (is_unsigned ? 0 : 1);
		uint32_t low = is_unsigned ?
			unsigned_saturate(exec->cpu, (int16_t)source, sat) :
			(uint16_t)signed_saturate(exec->cpu, (int16_t)source, sat);
		uint32_t high = is_unsigned ?
			unsigned_saturate(exec->cpu, (int16_t)(source >> 16), sat) :
			(uint16_t)signed_saturate(exec->cpu,
					(int16_t)(source >> 16), sat);
		exec->cpu->r[rd] = (low & 0xffff) | ((high & 0xffff) << 16);
		return M4F_STEP_OK;
	}
	unsigned amount = (M4F_BITS(second, 14, 12) << 2) |
		M4F_BITS(second, 7, 6);
	enum m4f_shift_type type = M4F_BIT(first, 5) ? M4F_SHIFT_ASR :
		M4F_SHIFT_LSL;
	if (type == M4F_SHIFT_ASR && amount == 0) amount = 32;
	int32_t shifted = (int32_t)m4f_shift(source, type, amount, false).result;
	unsigned sat = second & 0x1f;
	if (!is_unsigned) sat++;
	exec->cpu->r[rd] = is_unsigned ?
		unsigned_saturate(exec->cpu, shifted, sat) :
		(uint32_t)signed_saturate(exec->cpu, shifted, sat);
	return M4F_STEP_OK;
}

static enum m4f_stop execute_plain_immediate(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned rd = M4F_BITS(second, 11, 8);
	uint32_t imm12 = (M4F_BIT(first, 10) << 11) |
		(M4F_BITS(second, 14, 12) << 8) | (second & 0xff);
	if ((first & 0xfbf0) == 0xf240) { /* MOVW */
		uint32_t imm16 = ((uint32_t)(first & 0xf) << 12) | imm12;
		if (rd == 15) return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rd] = imm16;
		return M4F_STEP_OK;
	}
	if ((first & 0xfbf0) == 0xf2c0) { /* MOVT */
		uint32_t imm16 = ((uint32_t)(first & 0xf) << 12) | imm12;
		if (rd == 15) return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rd] = (exec->cpu->r[rd] & 0xffff) | (imm16 << 16);
		return M4F_STEP_OK;
	}
	if ((first & 0xfbe0) == 0xf200 ||
			(first & 0xfbe0) == 0xf2a0) { /* ADDW/SUBW/ADR */
		bool subtract = (first & 0x00a0) == 0x00a0;
		unsigned rn = first & 0xf;
		uint32_t base = rn == 15 ? ((exec->address + 4) & ~UINT32_C(3)) :
			exec->cpu->r[rn];
		if (rd == 15) return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rd] = subtract ? base - imm12 : base + imm12;
		return M4F_STEP_OK;
	}
	if ((first & 0xfbc0) == 0xf300 ||
			(first & 0xfbc0) == 0xf380)
		return execute_saturate(exec, first, second);

	unsigned rn = first & 0xf;
	unsigned lsb = (M4F_BITS(second, 14, 12) << 2) |
		M4F_BITS(second, 7, 6);
	if ((first & 0xfbe0) == 0xf340) { /* SBFX */
		unsigned width = (second & 0x1f) + 1;
		if (rd == 15 || rn == 15 || lsb + width > 32)
			return M4F_STEP_UNPREDICTABLE;
		uint32_t field = exec->cpu->r[rn] >> lsb;
		exec->cpu->r[rd] = (uint32_t)m4f_sign_extend(field, width);
		return M4F_STEP_OK;
	}
	if ((first & 0xfbe0) == 0xf3c0) { /* UBFX */
		unsigned width = (second & 0x1f) + 1;
		if (rd == 15 || rn == 15 || lsb + width > 32)
			return M4F_STEP_UNPREDICTABLE;
		uint32_t mask = width == 32 ? UINT32_MAX :
			(UINT32_C(1) << width) - 1;
		exec->cpu->r[rd] = (exec->cpu->r[rn] >> lsb) & mask;
		return M4F_STEP_OK;
	}
	if ((first & 0xfbe0) == 0xf360) { /* BFI/BFC */
		unsigned msb = second & 0x1f;
		if (rd == 15 || msb < lsb) return M4F_STEP_UNPREDICTABLE;
		unsigned width = msb - lsb + 1;
		uint32_t mask = width == 32 ? UINT32_MAX :
			((UINT32_C(1) << width) - 1) << lsb;
		uint32_t source = rn == 15 ? 0 : exec->cpu->r[rn] << lsb;
		exec->cpu->r[rd] = (exec->cpu->r[rd] & ~mask) | (source & mask);
		return M4F_STEP_OK;
	}
	return M4F_STEP_UNDEFINED;
}

static uint32_t reverse_bits(uint32_t value)
{
	value = ((value & UINT32_C(0x55555555)) << 1) |
		((value >> 1) & UINT32_C(0x55555555));
	value = ((value & UINT32_C(0x33333333)) << 2) |
		((value >> 2) & UINT32_C(0x33333333));
	value = ((value & UINT32_C(0x0f0f0f0f)) << 4) |
		((value >> 4) & UINT32_C(0x0f0f0f0f));
	value = ((value & UINT32_C(0x00ff00ff)) << 8) |
		((value >> 8) & UINT32_C(0x00ff00ff));
	return (value << 16) | (value >> 16);
}

static enum m4f_stop execute_extend_register(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned family = M4F_BITS(first, 6, 4);
	unsigned rn = first & 0xf;
	unsigned rd = M4F_BITS(second, 11, 8);
	unsigned rm = second & 0xf;
	unsigned rotation = M4F_BITS(second, 5, 4) * 8;
	if (is_sp_or_pc(rd) || is_sp_or_pc(rm) || rn == 13)
		return M4F_STEP_UNPREDICTABLE;
	uint32_t rotated = m4f_ror32(exec->cpu->r[rm], rotation);
	if (family == 2 || family == 3) { /* SXTAB16/UXTAB16 and aliases */
		bool is_unsigned = family == 3;
		int32_t low = is_unsigned ? (int32_t)(rotated & 0xff) :
			m4f_sign_extend(rotated & 0xff, 8);
		int32_t high = is_unsigned ? (int32_t)((rotated >> 16) & 0xff) :
			m4f_sign_extend((rotated >> 16) & 0xff, 8);
		uint32_t value;
		if (rn == 15) {
			value = ((uint32_t)low & 0xffff) | (((uint32_t)high & 0xffff) << 16);
		} else {
			value = ((exec->cpu->r[rn] + (uint32_t)low) & 0xffff) |
				((exec->cpu->r[rn] + ((uint32_t)high << 16)) &
				 UINT32_C(0xffff0000));
		}
		exec->cpu->r[rd] = value;
		return M4F_STEP_OK;
	}
	bool byte = family == 4 || family == 5;
	bool is_unsigned = family == 1 || family == 5;
	unsigned bits = byte ? 8 : 16;
	uint32_t extended = is_unsigned ?
		(rotated & ((UINT32_C(1) << bits) - 1)) :
		(uint32_t)m4f_sign_extend(rotated, bits);
	exec->cpu->r[rd] = rn == 15 ? extended : exec->cpu->r[rn] + extended;
	return M4F_STEP_OK;
}

static int32_t saturating_add(struct m4f_cpu *cpu, int64_t value)
{
	return signed_saturate(cpu, value, 32);
}

static enum m4f_stop execute_register_data(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned rn = first & 0xf;
	unsigned rd = M4F_BITS(second, 11, 8);
	unsigned rm = second & 0xf;
	if ((second & 0xf0f0) == 0xf000 &&
			(first & 0xff80) == 0xfa00) { /* register-controlled shifts */
		unsigned operation = M4F_BITS(first, 6, 5);
		struct m4f_shift_result shifted = m4f_shift(exec->cpu->r[rn],
				(enum m4f_shift_type)operation, exec->cpu->r[rm] & 0xff,
				(exec->cpu->xpsr & M4F_XPSR_C) != 0);
		if (is_sp_or_pc(rd) || is_sp_or_pc(rn) || is_sp_or_pc(rm))
			return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rd] = shifted.result;
		if (M4F_BIT(first, 4))
			m4f_set_nzc(exec->cpu, shifted.result, shifted.carry);
		return M4F_STEP_OK;
	}
	unsigned family = M4F_BITS(first, 6, 4);
	if (!M4F_BIT(first, 7) && (second & 0xf080) == 0xf080 &&
			(family <= 5))
		return execute_extend_register(exec, first, second);

	if ((first & 0xfff0) == 0xfab0 && (second & 0xf0f0) == 0xf080) {
		if (is_sp_or_pc(rd) || is_sp_or_pc(rn) || (second & 0xf) != rn)
			return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rd] = exec->cpu->r[rn] ?
			(uint32_t)__builtin_clz(exec->cpu->r[rn]) : 32;
		return M4F_STEP_OK;
	}
	if ((first & 0xfff0) == 0xfa90 && (second & 0xf080) == 0xf080) {
		if (is_sp_or_pc(rd) || is_sp_or_pc(rn) || (second & 0xf) != rn)
			return M4F_STEP_UNPREDICTABLE;
		uint32_t value = exec->cpu->r[rn];
		switch (M4F_BITS(second, 7, 4)) {
		case 8:
			value = ((value & UINT32_C(0x000000ff)) << 24) |
				((value & UINT32_C(0x0000ff00)) << 8) |
				((value & UINT32_C(0x00ff0000)) >> 8) |
				((value & UINT32_C(0xff000000)) >> 24);
			break;
		case 9:
			value = ((value & UINT32_C(0x00ff00ff)) << 8) |
				((value & UINT32_C(0xff00ff00)) >> 8);
			break;
		case 10: value = reverse_bits(value); break;
		case 11:
			value = (uint32_t)m4f_sign_extend(
					((value & 0xff) << 8) | ((value >> 8) & 0xff), 16);
			break;
		default: return M4F_STEP_UNDEFINED;
		}
		exec->cpu->r[rd] = value;
		return M4F_STEP_OK;
	}
	if ((first & 0xfff0) == 0xfa80 && (second & 0xf0c0) == 0xf080) {
		if (is_sp_or_pc(rd) || is_sp_or_pc(rn) || is_sp_or_pc(rm))
			return M4F_STEP_UNPREDICTABLE;
		/* This encoding places Rm in the first halfword and Rn second. */
		int64_t x = (int32_t)exec->cpu->r[rm];
		int64_t y = (int32_t)exec->cpu->r[rn];
		int32_t result;
		switch (M4F_BITS(second, 5, 4)) {
		case 0: result = saturating_add(exec->cpu, x + y); break;
		case 1:
			y = saturating_add(exec->cpu, y * 2);
			result = saturating_add(exec->cpu, x + y);
			break;
		case 2: result = saturating_add(exec->cpu, x - y); break;
		default:
			y = saturating_add(exec->cpu, y * 2);
			result = saturating_add(exec->cpu, x - y);
			break;
		}
		exec->cpu->r[rd] = (uint32_t)result;
		return M4F_STEP_OK;
	}
	if ((first & 0xfff0) == 0xfaa0 && (second & 0xf0f0) == 0xf080) {
		if (is_sp_or_pc(rd) || is_sp_or_pc(rn) || is_sp_or_pc(rm))
			return M4F_STEP_UNPREDICTABLE;
		uint32_t ge = (exec->cpu->xpsr >> 16) & 0xf;
		uint32_t a = exec->cpu->r[rn], b = exec->cpu->r[rm], value = 0;
		for (unsigned i = 0; i < 4; ++i) {
			uint32_t mask = UINT32_C(0xff) << (i * 8);
			value |= (ge & (1u << i)) ? (a & mask) : (b & mask);
		}
		exec->cpu->r[rd] = value;
		return M4F_STEP_OK;
	}
	return M4F_STEP_UNDEFINED;
}

static int32_t lane_signed(uint32_t value, unsigned lane, unsigned width)
{
	return m4f_sign_extend(value >> (lane * width), width);
}

static uint32_t lane_unsigned(uint32_t value, unsigned lane, unsigned width)
{
	uint32_t mask = (UINT32_C(1) << width) - 1;
	return (value >> (lane * width)) & mask;
}

/* The parallel Q/UQ instructions saturate each lane but do not update APSR.Q. */
static uint32_t lane_signed_saturate(int64_t value, unsigned width)
{
	int64_t low = -(INT64_C(1) << (width - 1));
	int64_t high = (INT64_C(1) << (width - 1)) - 1;
	if (value < low) value = low;
	if (value > high) value = high;
	return (uint32_t)value;
}

static uint32_t lane_unsigned_saturate(int64_t value, unsigned width)
{
	uint64_t high = (UINT64_C(1) << width) - 1;
	if (value < 0) return 0;
	if ((uint64_t)value > high) return (uint32_t)high;
	return (uint32_t)value;
}

static enum m4f_stop execute_parallel(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned operation = M4F_BITS(first, 7, 4);
	unsigned variant = M4F_BITS(second, 6, 4);
	bool is_unsigned = variant >= 4;
	unsigned mode = variant & 3; /* 0 normal, 1 saturating, 2 halving */
	unsigned rn = first & 0xf, rd = M4F_BITS(second, 11, 8), rm = second & 0xf;
	if (mode == 3 || is_sp_or_pc(rn) || is_sp_or_pc(rd) || is_sp_or_pc(rm))
		return M4F_STEP_UNPREDICTABLE;
	uint32_t a = exec->cpu->r[rn], b = exec->cpu->r[rm], result = 0;
	uint32_t ge = 0;
	if (operation != 8 && operation != 9 && operation != 10 &&
			operation != 12 && operation != 13 && operation != 14)
		return M4F_STEP_UNDEFINED;
	bool bytes = operation == 8 || operation == 12;
	unsigned lanes = bytes ? 4 : 2;
	unsigned width = bytes ? 8 : 16;
	bool subtract_all = operation == 12 || operation == 13;
	bool exchange = operation == 10 || operation == 14;
	bool subtract_top = operation == 14;

	for (unsigned lane = 0; lane < lanes; ++lane) {
		unsigned other_lane = lane;
		bool subtract = subtract_all;
		if (exchange) {
			other_lane = lane ^ 1;
			subtract = subtract_top ? (lane == 1) : (lane == 0);
		}
		int64_t signed_value;
		uint64_t unsigned_value;
		uint32_t lane_result;
		if (is_unsigned) {
			uint32_t x = lane_unsigned(a, lane, width);
			uint32_t y = lane_unsigned(b, other_lane, width);
			if (subtract) {
				int64_t difference = (int64_t)x - y;
				if (mode == 1) lane_result = lane_unsigned_saturate(
						difference, width);
				else if (mode == 2) lane_result = (uint32_t)difference >> 1;
				else lane_result = (uint32_t)difference;
				if (difference >= 0) ge |= bytes ? 1u << lane : 3u << (lane * 2);
			} else {
				unsigned_value = (uint64_t)x + y;
				if (mode == 1) lane_result = lane_unsigned_saturate(
						(int64_t)unsigned_value, width);
				else if (mode == 2) lane_result = (uint32_t)(unsigned_value >> 1);
				else lane_result = (uint32_t)unsigned_value;
				if (unsigned_value >> width)
					ge |= bytes ? 1u << lane : 3u << (lane * 2);
			}
		} else {
			int32_t x = lane_signed(a, lane, width);
			int32_t y = lane_signed(b, other_lane, width);
			signed_value = subtract ? (int64_t)x - y : (int64_t)x + y;
			if (mode == 1) lane_result = lane_signed_saturate(
					signed_value, width);
			else if (mode == 2) lane_result = (uint32_t)(signed_value >> 1);
			else lane_result = (uint32_t)signed_value;
			if (signed_value >= 0)
				ge |= bytes ? 1u << lane : 3u << (lane * 2);
		}
		uint32_t mask = (UINT32_C(1) << width) - 1;
		result |= (lane_result & mask) << (lane * width);
	}
	if (mode == 0) {
		exec->cpu->xpsr = (exec->cpu->xpsr & ~M4F_XPSR_GE_MASK) |
			((ge & 0xf) << 16);
	}
	exec->cpu->r[rd] = result;
	return M4F_STEP_OK;
}

static int32_t selected_half(uint32_t value, bool top)
{
	return (int16_t)(top ? value >> 16 : value);
}

static enum m4f_stop execute_multiply(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned operation = M4F_BITS(first, 7, 4);
	unsigned rn = first & 0xf;
	unsigned ra = M4F_BITS(second, 15, 12);
	unsigned rd = M4F_BITS(second, 11, 8);
	unsigned op2 = M4F_BITS(second, 7, 4);
	unsigned rm = second & 0xf;
	if (is_sp_or_pc(rn) || is_sp_or_pc(rm) || is_sp_or_pc(rd) || ra == 13)
		return M4F_STEP_UNPREDICTABLE;
	uint32_t n = exec->cpu->r[rn], m = exec->cpu->r[rm];
	if (operation == 0) {
		if (op2 > 1 || (ra == 15 && op2 == 1)) return M4F_STEP_UNDEFINED;
		uint32_t product = n * m;
		if (ra == 15) exec->cpu->r[rd] = product;
		else if (op2 == 0) exec->cpu->r[rd] = product + exec->cpu->r[ra];
		else exec->cpu->r[rd] = exec->cpu->r[ra] - product;
		return M4F_STEP_OK;
	}
	if (operation == 1) {
		int32_t product = selected_half(n, M4F_BIT(op2, 1)) *
			selected_half(m, M4F_BIT(op2, 0));
		if (ra == 15) {
			exec->cpu->r[rd] = (uint32_t)product;
		} else {
			int64_t value = (int32_t)exec->cpu->r[ra] + (int64_t)product;
			if (value > INT32_MAX || value < INT32_MIN)
				exec->cpu->xpsr |= M4F_XPSR_Q;
			exec->cpu->r[rd] = (uint32_t)value;
		}
		return M4F_STEP_OK;
	}
	if (operation == 2 || operation == 4) {
		if (op2 > 1) return M4F_STEP_UNDEFINED;
		int32_t n0 = (int16_t)n, n1 = (int16_t)(n >> 16);
		int32_t m0 = (int16_t)m, m1 = (int16_t)(m >> 16);
		if (op2) { int32_t temporary = m0; m0 = m1; m1 = temporary; }
		int64_t products = operation == 2 ?
			(int64_t)n0 * m0 + (int64_t)n1 * m1 :
			(int64_t)n0 * m0 - (int64_t)n1 * m1;
		if (ra == 15) {
			if (products > INT32_MAX || products < INT32_MIN)
				exec->cpu->xpsr |= M4F_XPSR_Q;
			exec->cpu->r[rd] = (uint32_t)products;
		} else {
			int64_t value = (int32_t)exec->cpu->r[ra] + products;
			if (value > INT32_MAX || value < INT32_MIN)
				exec->cpu->xpsr |= M4F_XPSR_Q;
			exec->cpu->r[rd] = (uint32_t)value;
		}
		return M4F_STEP_OK;
	}
	if (operation == 3) { /* SMLAWB/T and SMULWB/T */
		if (op2 > 1) return M4F_STEP_UNDEFINED;
		int64_t product = (int64_t)(int32_t)n * selected_half(m, op2 != 0);
		int64_t accumulated = product;
		if (ra != 15) accumulated +=
			(int64_t)(int32_t)exec->cpu->r[ra] * INT64_C(65536);
		int64_t shifted = accumulated >> 16;
		if (ra != 15 && (shifted > INT32_MAX || shifted < INT32_MIN))
			exec->cpu->xpsr |= M4F_XPSR_Q;
		exec->cpu->r[rd] = (uint32_t)shifted;
		return M4F_STEP_OK;
	}
	if (operation == 5 || operation == 6) {
		if (op2 > 1) return M4F_STEP_UNDEFINED;
		int64_t product = (int64_t)(int32_t)n * (int32_t)m;
		if (op2) product += INT64_C(0x80000000);
		int32_t high = (int32_t)(product >> 32);
		if (ra == 15) {
			if (operation != 5) return M4F_STEP_UNDEFINED;
			exec->cpu->r[rd] = (uint32_t)high;
		} else if (operation == 5) {
			exec->cpu->r[rd] = (uint32_t)high + exec->cpu->r[ra];
		} else {
			exec->cpu->r[rd] = exec->cpu->r[ra] - (uint32_t)high;
		}
		return M4F_STEP_OK;
	}
	if (operation == 7 && op2 == 0) {
		uint32_t sum = 0;
		for (unsigned i = 0; i < 4; ++i) {
			int difference = (int)((n >> (i * 8)) & 0xff) -
				(int)((m >> (i * 8)) & 0xff);
			sum += (uint32_t)(difference < 0 ? -difference : difference);
		}
		if (ra != 15) sum += exec->cpu->r[ra];
		exec->cpu->r[rd] = sum;
		return M4F_STEP_OK;
	}
	return M4F_STEP_UNDEFINED;
}

static enum m4f_stop execute_long_multiply_divide(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned operation = M4F_BITS(first, 7, 4);
	unsigned rn = first & 0xf;
	unsigned rdlo = M4F_BITS(second, 15, 12);
	unsigned rdhi = M4F_BITS(second, 11, 8);
	unsigned op2 = M4F_BITS(second, 7, 4);
	unsigned rm = second & 0xf;
	if (is_sp_or_pc(rn) || is_sp_or_pc(rm)) return M4F_STEP_UNPREDICTABLE;
	uint32_t n = exec->cpu->r[rn], m = exec->cpu->r[rm];
	if ((operation == 9 || operation == 11) && rdlo == 15 && op2 == 15) {
		if (is_sp_or_pc(rdhi)) return M4F_STEP_UNPREDICTABLE;
		if (m == 0) {
			exec->cpu->r[rdhi] = 0; /* DIV_0_TRP is outside this core. */
		} else if (operation == 9) {
			int32_t dividend = (int32_t)n, divisor = (int32_t)m;
			exec->cpu->r[rdhi] = dividend == INT32_MIN && divisor == -1 ?
				(uint32_t)INT32_MIN : (uint32_t)(dividend / divisor);
		} else {
			exec->cpu->r[rdhi] = n / m;
		}
		return M4F_STEP_OK;
	}
	if (is_sp_or_pc(rdlo)) return M4F_STEP_UNPREDICTABLE;
	if (is_sp_or_pc(rdhi) || rdhi == rdlo) return M4F_STEP_UNPREDICTABLE;
	uint64_t result;
	if (op2 == 0 && (operation == 8 || operation == 10 ||
			operation == 12 || operation == 14)) {
		if (operation == 8) result = (uint64_t)(int64_t)
			((int64_t)(int32_t)n * (int32_t)m);
		else if (operation == 10) result = (uint64_t)n * m;
		else {
			uint64_t accumulator = ((uint64_t)exec->cpu->r[rdhi] << 32) |
				exec->cpu->r[rdlo];
			result = operation == 12 ? accumulator +
				(uint64_t)(int64_t)((int64_t)(int32_t)n * (int32_t)m) :
				accumulator + (uint64_t)n * m;
		}
	} else if (operation == 12 && op2 >= 8 && op2 <= 11) {
		int64_t product = (int64_t)selected_half(n, M4F_BIT(op2, 1)) *
			selected_half(m, M4F_BIT(op2, 0));
		uint64_t accumulator = ((uint64_t)exec->cpu->r[rdhi] << 32) |
			exec->cpu->r[rdlo];
		result = accumulator + (uint64_t)product;
	} else if ((operation == 12 || operation == 13) &&
			(op2 == 12 || op2 == 13)) {
		int32_t n0 = (int16_t)n, n1 = (int16_t)(n >> 16);
		int32_t m0 = (int16_t)m, m1 = (int16_t)(m >> 16);
		if (op2 & 1) { int32_t temporary = m0; m0 = m1; m1 = temporary; }
		int64_t products = (int64_t)n0 * m0 +
			(operation == 12 ? (int64_t)n1 * m1 : -(int64_t)n1 * m1);
		uint64_t accumulator = ((uint64_t)exec->cpu->r[rdhi] << 32) |
			exec->cpu->r[rdlo];
		result = accumulator + (uint64_t)products;
	} else if (operation == 14 && op2 == 6) { /* UMAAL */
		result = (uint64_t)n * m + exec->cpu->r[rdlo] + exec->cpu->r[rdhi];
	} else {
		return M4F_STEP_UNDEFINED;
	}
	exec->cpu->r[rdlo] = (uint32_t)result;
	exec->cpu->r[rdhi] = (uint32_t)(result >> 32);
	return M4F_STEP_OK;
}

static enum m4f_stop execute_multiply_group(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned operation = M4F_BITS(first, 7, 4);
	if (operation >= 8) return execute_long_multiply_divide(exec, first, second);
	return execute_multiply(exec, first, second);
}

static enum m4f_stop execute_branch(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	bool s = M4F_BIT(first, 10);
	if (!M4F_BIT(second, 12)) { /* B<c>.W */
		unsigned condition = M4F_BITS(first, 9, 6);
		if (condition >= 14 || exec->cpu->itstate)
			return M4F_STEP_UNPREDICTABLE;
		uint32_t encoded = ((uint32_t)s << 20) |
			(M4F_BIT(second, 11) << 19) | (M4F_BIT(second, 13) << 18) |
			(M4F_BITS(first, 5, 0) << 12) | ((second & 0x7ff) << 1);
		int32_t offset = m4f_sign_extend(encoded, 21);
		if (m4f_condition_passed(exec->cpu, condition))
			exec->cpu->r[15] = exec->address + 4 + (uint32_t)offset;
		return M4F_STEP_OK;
	}
	if (exec->cpu->itstate) return M4F_STEP_UNPREDICTABLE;
	bool j1 = M4F_BIT(second, 13), j2 = M4F_BIT(second, 11);
	bool i1 = !(j1 ^ s), i2 = !(j2 ^ s);
	uint32_t encoded = ((uint32_t)s << 24) | ((uint32_t)i1 << 23) |
		((uint32_t)i2 << 22) | (M4F_BITS(first, 9, 0) << 12) |
		((second & 0x7ff) << 1);
	int32_t offset = m4f_sign_extend(encoded, 25);
	bool link = M4F_BIT(second, 14);
	if (!M4F_BIT(second, 14) && !M4F_BIT(second, 12))
		return M4F_STEP_UNDEFINED;
	if (link) exec->cpu->r[14] = exec->next_address | 1;
	exec->cpu->r[15] = exec->address + 4 + (uint32_t)offset;
	return M4F_STEP_OK;
}

static bool special_register_valid(unsigned sysm)
{
	return sysm <= 3 || (sysm >= 5 && sysm <= 9) ||
		(sysm >= 16 && sysm <= 20);
}

static uint32_t read_special_register(const struct m4f_cpu *cpu, unsigned sysm)
{
	uint32_t apsr = cpu->xpsr & UINT32_C(0xf80f0000);
	bool privileged = !(cpu->control & 1);
	switch (sysm) {
	case 0: /* APSR */
	case 1: /* IAPSR: IPSR is zero outside exceptions. */
	case 2: /* EAPSR: EPSR reads as zero through MRS. */
	case 3: return apsr; /* XPSR */
	case 5: return 0; /* IPSR: exceptions are not entered. */
	case 6: /* EPSR */
	case 7: return 0; /* IEPSR */
	case 8: return privileged ? cpu->msp : 0;
	case 9: return privileged ? cpu->psp : 0;
	case 16: return privileged ? cpu->primask & 1 : 0;
	case 17: return privileged ? cpu->basepri & 0xff : 0;
	case 18: return privileged ? cpu->basepri & 0xff : 0;
	case 19: return privileged ? cpu->faultmask & 1 : 0;
	case 20: return cpu->control & 7;
	default: return 0;
	}
}

static enum m4f_stop write_special_register(struct m4f_cpu *cpu,
		unsigned sysm, unsigned mask, uint32_t value)
{
	switch (sysm) {
	case 0:
	case 1:
	case 2:
	case 3:
		if (mask & 2) {
			cpu->xpsr = (cpu->xpsr & ~UINT32_C(0xf8000000)) |
				(value & UINT32_C(0xf8000000));
		}
		if (mask & 1) {
			cpu->xpsr = (cpu->xpsr & ~M4F_XPSR_GE_MASK) |
				(value & M4F_XPSR_GE_MASK);
		}
		break;
	case 5:
	case 6:
	case 7:
		if (mask != 2) return M4F_STEP_UNDEFINED;
		break; /* No writable fields when exceptions are not modeled. */
	case 8:
		if (mask != 2) return M4F_STEP_UNDEFINED;
		if (cpu->control & 1) break;
		cpu->msp = value & ~UINT32_C(3);
		if (!(cpu->control & 2)) cpu->r[13] = cpu->msp;
		break;
	case 9:
		if (mask != 2) return M4F_STEP_UNDEFINED;
		if (cpu->control & 1) break;
		cpu->psp = value & ~UINT32_C(3);
		if (cpu->control & 2) cpu->r[13] = cpu->psp;
		break;
	case 16:
		if (mask != 2) return M4F_STEP_UNDEFINED;
		if (cpu->control & 1) break;
		cpu->primask = value & 1;
		break;
	case 17:
		if (mask != 2) return M4F_STEP_UNDEFINED;
		if (cpu->control & 1) break;
		cpu->basepri = value & 0xff;
		break;
	case 18:
		if (mask != 2) return M4F_STEP_UNDEFINED;
		if (cpu->control & 1) break;
		value &= 0xff;
		if (value && (!cpu->basepri || value < cpu->basepri)) cpu->basepri = value;
		break;
	case 19:
		if (mask != 2) return M4F_STEP_UNDEFINED;
		if (cpu->control & 1) break;
		cpu->faultmask = value & 1;
		break;
	case 20: {
		if (mask != 2) return M4F_STEP_UNDEFINED;
		if (cpu->control & 1) break;
		bool old_psp = (cpu->control & 2) != 0;
		if (old_psp) cpu->psp = cpu->r[13];
		else cpu->msp = cpu->r[13];
		cpu->control = value & 7;
		cpu->r[13] = (cpu->control & 2) ? cpu->psp : cpu->msp;
		break;
	}
	default: return M4F_STEP_UNDEFINED;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_system(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	if (first == 0xf3ef && (second & 0xf000) == 0x8000) { /* MRS */
		unsigned rd = M4F_BITS(second, 11, 8);
		unsigned sysm = second & 0xff;
		if (rd == 13 || rd == 15 || !special_register_valid(sysm))
			return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rd] = read_special_register(exec->cpu, sysm);
		return M4F_STEP_OK;
	}
	if ((first & 0xfff0) == 0xf380 && (second & 0xf000) == 0x8000 &&
			M4F_BITS(second, 11, 10) != 0) {
		unsigned rn = first & 0xf;
		unsigned sysm = second & 0xff;
		if (rn == 13 || rn == 15 || !special_register_valid(sysm))
			return M4F_STEP_UNPREDICTABLE;
		if (M4F_BITS(second, 11, 10) != 2 && sysm > 3)
			return M4F_STEP_UNPREDICTABLE;
		return write_special_register(exec->cpu, sysm,
				M4F_BITS(second, 11, 10),
				exec->cpu->r[rn]);
	}
	if (first == 0xf3bf && (second & 0xff00) == 0x8f00) {
		switch (M4F_BITS(second, 7, 4)) {
		case 2:
			if ((second & 0xf) != 0xf) return M4F_STEP_UNDEFINED;
			exec->cpu->exclusive_valid = false;
			return M4F_STEP_OK;
		case 4: /* DSB */
		case 5: return M4F_STEP_OK; /* DMB */
		case 6:
			return (second & 0xf) == 0xf ? M4F_STEP_OK :
				M4F_STEP_UNDEFINED; /* ISB */
		default: return M4F_STEP_UNDEFINED;
		}
	}
	if (first == 0xf3af && (second & 0xfff0) == 0x8000) {
		switch (second & 0xf) {
		case 2:
			if (exec->cpu->event_registered) {
				exec->cpu->event_registered = false;
				return M4F_STEP_OK;
			}
			return M4F_STEP_WFE;
		case 3: return M4F_STEP_WFI;
		case 4: exec->cpu->event_registered = true; return M4F_STEP_OK;
		default: return M4F_STEP_OK;
		}
	}
	if (first == 0xf3af && (second & 0xfff0) == 0x80f0)
		return M4F_STEP_OK; /* DBG is an implementation hint here. */
	return M4F_STEP_UNDEFINED;
}

/* Memory, exclusive, and floating-point groups are in separate translation units. */
enum m4f_stop m4f_execute_32_memory(struct m4f_exec *exec, uint16_t first,
		uint16_t second);
enum m4f_stop m4f_execute_32_fp(struct m4f_exec *exec, uint16_t first,
		uint16_t second);

enum m4f_stop m4f_execute_32(struct m4f_exec *exec, uint16_t first,
		uint16_t second)
{
	if ((first & 0xfe00) == 0xea00)
		return execute_shifted_register(exec, first, second);
	if ((first & 0xfa00) == 0xf000 && !M4F_BIT(second, 15))
		return execute_modified_immediate(exec, first, second);
	if ((first & 0xfa00) == 0xf200 && !M4F_BIT(second, 15))
		return execute_plain_immediate(exec, first, second);
	if ((first & 0xff00) == 0xfa00) {
		if ((first & 0xff80) >= 0xfa80 && !(second & 0x0080) &&
				(second & 0xf000) == 0xf000) {
			enum m4f_stop parallel = execute_parallel(exec, first, second);
			if (parallel != M4F_STEP_UNDEFINED) return parallel;
		}
		return execute_register_data(exec, first, second);
	}
	if ((first & 0xff00) == 0xfb00)
		return execute_multiply_group(exec, first, second);
	if ((first & 0xff00) == 0xf300) {
		enum m4f_stop system = execute_system(exec, first, second);
		if (system != M4F_STEP_UNDEFINED) return system;
	}
	if ((first & 0xfff0) == 0xf7f0) {
		exec->result.immediate = ((uint32_t)(first & 0xf) << 12) |
			(second & 0xfff);
		return M4F_STEP_UDF;
	}
	if ((first & 0xf800) == 0xf000 && M4F_BIT(second, 15))
		return execute_branch(exec, first, second);
	if ((first & 0xec00) == 0xec00) {
		enum m4f_stop fp = m4f_execute_32_fp(exec, first, second);
		if (fp != M4F_STEP_UNDEFINED) return fp;
	}
	return m4f_execute_32_memory(exec, first, second);
}
