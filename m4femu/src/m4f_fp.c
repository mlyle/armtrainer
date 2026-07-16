#include "m4f_internal.h"

#include <float.h>
#include <math.h>
#include <string.h>

static float bits_to_float(uint32_t bits)
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static uint32_t float_to_bits(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static bool fp_is_nan(uint32_t value)
{
	return (value & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
		(value & UINT32_C(0x007fffff));
}

static bool fp_is_signaling_nan(uint32_t value)
{
	return fp_is_nan(value) && !(value & UINT32_C(0x00400000));
}

static bool fp_is_infinity(uint32_t value)
{
	return (value & UINT32_C(0x7fffffff)) == UINT32_C(0x7f800000);
}

static bool fp_is_zero(uint32_t value)
{
	return (value & UINT32_C(0x7fffffff)) == 0;
}

static bool fp_is_subnormal(uint32_t value)
{
	return !(value & UINT32_C(0x7f800000)) &&
		(value & UINT32_C(0x007fffff));
}

static uint32_t fp_default_nan(void)
{
	return UINT32_C(0x7fc00000);
}

static uint32_t fp_process_nan(struct m4f_cpu *cpu, uint32_t value)
{
	if (fp_is_signaling_nan(value)) cpu->fpscr |= M4F_FPSCR_IOC;
	if (cpu->fpscr & M4F_FPSCR_DN) return fp_default_nan();
	return value | UINT32_C(0x00400000);
}

static uint32_t fp_prepare_input(struct m4f_cpu *cpu, uint32_t value)
{
	if (fp_is_subnormal(value) && (cpu->fpscr & M4F_FPSCR_FZ)) {
		cpu->fpscr |= M4F_FPSCR_IDC;
		return value & UINT32_C(0x80000000);
	}
	return value;
}

static uint32_t fp_propagate_nan2(struct m4f_cpu *cpu, uint32_t a, uint32_t b)
{
	if (fp_is_signaling_nan(a)) return fp_process_nan(cpu, a);
	if (fp_is_signaling_nan(b)) return fp_process_nan(cpu, b);
	if (fp_is_nan(a)) return fp_process_nan(cpu, a);
	return fp_process_nan(cpu, b);
}

static uint32_t fp_propagate_nan3(struct m4f_cpu *cpu, uint32_t a,
		uint32_t b, uint32_t c)
{
	if (fp_is_signaling_nan(a)) return fp_process_nan(cpu, a);
	if (fp_is_signaling_nan(b)) return fp_process_nan(cpu, b);
	if (fp_is_signaling_nan(c)) return fp_process_nan(cpu, c);
	if (fp_is_nan(a)) return fp_process_nan(cpu, a);
	if (fp_is_nan(b)) return fp_process_nan(cpu, b);
	return fp_process_nan(cpu, c);
}

static uint32_t fp_round_with_mode(struct m4f_cpu *cpu, double exact,
		unsigned mode)
{
	float rounded = (float)exact;
	bool overflow = isfinite(exact) && isinf(rounded);
	if (overflow) {
		bool negative = signbit(exact);
		if ((mode == 1 && negative) || (mode == 2 && !negative) || mode == 3)
			rounded = negative ? -FLT_MAX : FLT_MAX;
	}
	if (isfinite(exact) && isfinite(rounded) && (double)rounded != exact) {
		if (mode == 1 && (double)rounded < exact)
			rounded = nextafterf(rounded, INFINITY);
		else if (mode == 2 && (double)rounded > exact)
			rounded = nextafterf(rounded, -INFINITY);
		else if (mode == 3 &&
				((exact > 0 && (double)rounded > exact) ||
				 (exact < 0 && (double)rounded < exact)))
			rounded = nextafterf(rounded, 0.0f);
	}
	uint32_t result = float_to_bits(rounded);
	bool inexact = isfinite(exact) && (double)rounded != exact;
	if (overflow)
		cpu->fpscr |= M4F_FPSCR_OFC | M4F_FPSCR_IXC;
	else if (inexact)
		cpu->fpscr |= M4F_FPSCR_IXC;
	if (inexact && exact != 0.0 && fabs(exact) < FLT_MIN)
		cpu->fpscr |= M4F_FPSCR_UFC;
	if (fp_is_subnormal(result)) {
		if (cpu->fpscr & M4F_FPSCR_FZ) {
			cpu->fpscr |= M4F_FPSCR_UFC | M4F_FPSCR_IXC;
			result &= UINT32_C(0x80000000);
		}
	}
	return result;
}

static uint32_t fp_round(struct m4f_cpu *cpu, double exact)
{
	return fp_round_with_mode(cpu, exact,
			(cpu->fpscr & M4F_FPSCR_RMODE_MASK) >> 22);
}

enum fp_binary_operation {
	FP_ADD,
	FP_SUB,
	FP_MUL,
	FP_DIV,
};

static uint32_t fp_binary(struct m4f_cpu *cpu, uint32_t a_bits,
		uint32_t b_bits, enum fp_binary_operation operation)
{
	a_bits = fp_prepare_input(cpu, a_bits);
	b_bits = fp_prepare_input(cpu, b_bits);
	if (fp_is_nan(a_bits) || fp_is_nan(b_bits))
		return fp_propagate_nan2(cpu, a_bits, b_bits);
	bool invalid = false;
	if (operation == FP_ADD)
		invalid = fp_is_infinity(a_bits) && fp_is_infinity(b_bits) &&
			M4F_BIT(a_bits ^ b_bits, 31);
	if (operation == FP_SUB)
		invalid = fp_is_infinity(a_bits) && fp_is_infinity(b_bits) &&
			!M4F_BIT(a_bits ^ b_bits, 31);
	if (operation == FP_MUL)
		invalid = (fp_is_infinity(a_bits) && fp_is_zero(b_bits)) ||
			(fp_is_zero(a_bits) && fp_is_infinity(b_bits));
	if (operation == FP_DIV)
		invalid = (fp_is_zero(a_bits) && fp_is_zero(b_bits)) ||
			(fp_is_infinity(a_bits) && fp_is_infinity(b_bits));
	if (invalid) {
		cpu->fpscr |= M4F_FPSCR_IOC;
		return fp_default_nan();
	}
	if (operation == FP_DIV && fp_is_zero(b_bits) && !fp_is_zero(a_bits) &&
			!fp_is_infinity(a_bits)) {
		cpu->fpscr |= M4F_FPSCR_DZC;
		return ((a_bits ^ b_bits) & UINT32_C(0x80000000)) |
			UINT32_C(0x7f800000);
	}
	double a = bits_to_float(a_bits);
	double b = bits_to_float(b_bits);
	double exact;
	switch (operation) {
	case FP_ADD: exact = a + b; break;
	case FP_SUB: exact = a - b; break;
	case FP_MUL: exact = a * b; break;
	default: exact = a / b; break;
	}
	if ((operation == FP_ADD || operation == FP_SUB) && exact == 0.0) {
		bool sign_a = M4F_BIT(a_bits, 31);
		bool sign_b = M4F_BIT(b_bits, 31) ^ (operation == FP_SUB);
		if (fp_is_zero(a_bits) && fp_is_zero(b_bits) && sign_a == sign_b)
			return sign_a ? UINT32_C(0x80000000) : 0;
		return (cpu->fpscr & M4F_FPSCR_RMODE_MASK) ==
			(UINT32_C(2) << 22) ? UINT32_C(0x80000000) : 0;
	}
	return fp_round(cpu, exact);
}

static unsigned fp_sd(uint16_t first, uint16_t second)
{
	return (M4F_BITS(second, 15, 12) << 1) | M4F_BIT(first, 6);
}

static unsigned fp_sn(uint16_t first, uint16_t second)
{
	return ((first & 0xf) << 1) | M4F_BIT(second, 7);
}

static unsigned fp_sm(uint16_t second)
{
	return ((second & 0xf) << 1) | M4F_BIT(second, 5);
}

static enum m4f_stop execute_fp_three_register(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned vd = fp_sd(first, second), vn = fp_sn(first, second),
		vm = fp_sm(second);
	uint16_t operation = first & 0xffb0;
	bool subcode = M4F_BIT(second, 6);
	uint32_t result;
	if (vd >= 32 || vn >= 32 || vm >= 32) return M4F_STEP_UNDEFINED;
	if (operation == 0xee30) {
		result = fp_binary(exec->cpu, exec->cpu->s[vn], exec->cpu->s[vm],
				subcode ? FP_SUB : FP_ADD);
	} else if (operation == 0xee20) {
		result = fp_binary(exec->cpu, exec->cpu->s[vn], exec->cpu->s[vm],
				FP_MUL);
		if (subcode) result ^= UINT32_C(0x80000000);
	} else if (operation == 0xee80) {
		if (subcode) return M4F_STEP_UNDEFINED;
		result = fp_binary(exec->cpu, exec->cpu->s[vn], exec->cpu->s[vm],
				FP_DIV);
	} else if (operation == 0xee00 || operation == 0xee10) {
		uint32_t product = fp_binary(exec->cpu, exec->cpu->s[vn],
				exec->cpu->s[vm], FP_MUL);
		if (subcode) product ^= UINT32_C(0x80000000);
		if (operation == 0xee00) {
			result = fp_binary(exec->cpu, exec->cpu->s[vd], product, FP_ADD);
		} else {
			result = fp_binary(exec->cpu,
					exec->cpu->s[vd] ^ UINT32_C(0x80000000), product,
					FP_ADD);
		}
	} else if (operation == 0xeea0 || operation == 0xee90) {
		uint32_t nbits = fp_prepare_input(exec->cpu, exec->cpu->s[vn]);
		uint32_t mbits = fp_prepare_input(exec->cpu, exec->cpu->s[vm]);
		uint32_t dbits = fp_prepare_input(exec->cpu, exec->cpu->s[vd]);
		if (subcode) nbits ^= UINT32_C(0x80000000);
		if (operation == 0xee90) dbits ^= UINT32_C(0x80000000);
		bool invalid_product =
			(fp_is_infinity(nbits) && fp_is_zero(mbits)) ||
			(fp_is_zero(nbits) && fp_is_infinity(mbits));
		if (fp_is_nan(nbits) || fp_is_nan(mbits) || fp_is_nan(dbits)) {
			if (invalid_product && fp_is_nan(dbits) &&
					!fp_is_signaling_nan(dbits)) {
				exec->cpu->fpscr |= M4F_FPSCR_IOC;
				result = fp_default_nan();
			} else {
				result = fp_propagate_nan3(exec->cpu, dbits, nbits, mbits);
			}
		} else {
			bool product_negative = M4F_BIT(nbits ^ mbits, 31);
			bool accumulator_negative = M4F_BIT(dbits, 31);
			bool invalid_sum = (fp_is_infinity(nbits) ||
				fp_is_infinity(mbits)) && fp_is_infinity(dbits) &&
				(product_negative != accumulator_negative);
			if (invalid_product || invalid_sum) {
				exec->cpu->fpscr |= M4F_FPSCR_IOC;
				exec->cpu->s[vd] = fp_default_nan();
				return M4F_STEP_OK;
			}
			double exact = (double)bits_to_float(nbits) * bits_to_float(mbits);
			double accumulator = bits_to_float(dbits);
			exact += accumulator;
			if (exact == 0.0) {
				bool product_zero = fp_is_zero(nbits) || fp_is_zero(mbits);
				bool accumulator_zero = fp_is_zero(dbits);
				if (product_zero && accumulator_zero &&
						product_negative == accumulator_negative) {
					result = product_negative ? UINT32_C(0x80000000) : 0;
				} else {
					result = (exec->cpu->fpscr & M4F_FPSCR_RMODE_MASK) ==
						(UINT32_C(2) << 22) ? UINT32_C(0x80000000) : 0;
				}
			} else {
				result = fp_round(exec->cpu, exact);
			}
		}
	} else {
		return M4F_STEP_UNDEFINED;
	}
	exec->cpu->s[vd] = result;
	return M4F_STEP_OK;
}

static uint32_t vfp_expand_immediate(uint8_t immediate)
{
	uint32_t sign = (uint32_t)(immediate & 0x80) << 24;
	uint32_t bit6 = M4F_BIT(immediate, 6);
	uint32_t exponent = ((!bit6) << 7) | (bit6 ? 0x7c : 0) |
		M4F_BITS(immediate, 5, 4);
	uint32_t fraction = (uint32_t)(immediate & 0xf) << 19;
	return sign | (exponent << 23) | fraction;
}

static void fp_compare(struct m4f_cpu *cpu, uint32_t a_bits,
		uint32_t b_bits, bool quiet_nan_invalid)
{
	a_bits = fp_prepare_input(cpu, a_bits);
	b_bits = fp_prepare_input(cpu, b_bits);
	cpu->fpscr &= ~(M4F_FPSCR_N | M4F_FPSCR_Z | M4F_FPSCR_C | M4F_FPSCR_V);
	if (fp_is_nan(a_bits) || fp_is_nan(b_bits)) {
		cpu->fpscr |= M4F_FPSCR_C | M4F_FPSCR_V;
		if (quiet_nan_invalid || fp_is_signaling_nan(a_bits) ||
				fp_is_signaling_nan(b_bits)) cpu->fpscr |= M4F_FPSCR_IOC;
		return;
	}
	float a = bits_to_float(a_bits), b = bits_to_float(b_bits);
	if (a == b) cpu->fpscr |= M4F_FPSCR_Z | M4F_FPSCR_C;
	else if (a < b) cpu->fpscr |= M4F_FPSCR_N;
	else cpu->fpscr |= M4F_FPSCR_C;
}

static uint32_t fp_to_integer(struct m4f_cpu *cpu, uint32_t source,
		bool is_unsigned, bool round_toward_zero, unsigned fraction_bits,
		unsigned integer_bits)
{
	source = fp_prepare_input(cpu, source);
	if (fp_is_nan(source)) {
		cpu->fpscr |= M4F_FPSCR_IOC;
		return 0;
	}
	double value = bits_to_float(source) * ldexp(1.0, (int)fraction_bits);
	double rounded;
	unsigned mode = round_toward_zero ? 3 :
		(cpu->fpscr & M4F_FPSCR_RMODE_MASK) >> 22;
	if (mode == 0) rounded = nearbyint(value);
	else if (mode == 1) rounded = ceil(value);
	else if (mode == 2) rounded = floor(value);
	else rounded = trunc(value);
	if (rounded != value) cpu->fpscr |= M4F_FPSCR_IXC;
	if (is_unsigned) {
		double high = integer_bits == 32 ? UINT32_MAX :
			(double)((UINT32_C(1) << integer_bits) - 1);
		if (!(rounded >= 0.0) || rounded > high) {
			cpu->fpscr |= M4F_FPSCR_IOC;
			return rounded < 0 ? 0 : (uint32_t)high;
		}
		return (uint32_t)rounded;
	}
	double low = integer_bits == 32 ? INT32_MIN :
		-(double)(UINT32_C(1) << (integer_bits - 1));
	double high = integer_bits == 32 ? INT32_MAX :
		(double)((UINT32_C(1) << (integer_bits - 1)) - 1);
	if (rounded < low || rounded > high) {
		cpu->fpscr |= M4F_FPSCR_IOC;
		return rounded < 0 ? (uint32_t)(int32_t)low : (uint32_t)high;
	}
	return (uint32_t)(int32_t)rounded;
}

static uint32_t half_to_single(struct m4f_cpu *cpu, uint16_t half)
{
	uint32_t sign = (uint32_t)(half & 0x8000) << 16;
	unsigned exponent = (half >> 10) & 0x1f;
	uint32_t fraction = half & 0x3ff;
	if (exponent == 0) {
		if (!fraction) return sign;
		if (cpu->fpscr & M4F_FPSCR_FZ) {
			cpu->fpscr |= M4F_FPSCR_IDC;
			return sign;
		}
		int shift = 0;
		while (!(fraction & 0x400)) {
			fraction <<= 1;
			shift++;
		}
		fraction &= 0x3ff;
		unsigned single_exp = (unsigned)(127 - 14 - shift);
		return sign | (single_exp << 23) | (fraction << 13);
	}
	if (exponent == 31 && !(cpu->fpscr & M4F_FPSCR_AHP)) {
		if (!fraction) return sign | UINT32_C(0x7f800000);
		uint32_t nan = sign | UINT32_C(0x7f800000) | (fraction << 13);
		return fp_process_nan(cpu, nan);
	}
	unsigned single_exp = exponent + (127 - 15);
	return sign | (single_exp << 23) | (fraction << 13);
}

static uint64_t rounded_shift_right(uint64_t value, unsigned shift,
		bool negative, unsigned mode, bool *inexact)
{
	uint64_t quotient = shift >= 64 ? 0 : value >> shift;
	uint64_t remainder;
	if (shift == 0) remainder = 0;
	else if (shift >= 64) remainder = value;
	else remainder = value & ((UINT64_C(1) << shift) - 1);
	*inexact = remainder != 0;
	if (!remainder) return quotient;
	bool increment = false;
	if (mode == 0 && shift < 64) {
		uint64_t halfway = UINT64_C(1) << (shift - 1);
		increment = remainder > halfway ||
			(remainder == halfway && (quotient & 1));
	} else if (mode == 1) increment = !negative;
	else if (mode == 2) increment = negative;
	return quotient + (increment ? 1 : 0);
}

static uint16_t single_to_half(struct m4f_cpu *cpu, uint32_t value)
{
	value = fp_prepare_input(cpu, value);
	bool negative = M4F_BIT(value, 31);
	uint16_t sign = (uint16_t)(value >> 16) & 0x8000;
	unsigned exponent = M4F_BITS(value, 30, 23);
	uint32_t fraction = value & UINT32_C(0x7fffff);
	if (exponent == 255) {
		if (cpu->fpscr & M4F_FPSCR_AHP) {
			cpu->fpscr |= M4F_FPSCR_IOC;
			return fraction ? sign : (uint16_t)(sign | 0x7fff);
		}
		if (!fraction) return sign | 0x7c00;
		if (fp_is_signaling_nan(value)) cpu->fpscr |= M4F_FPSCR_IOC;
		if (cpu->fpscr & M4F_FPSCR_DN) return 0x7e00;
		uint16_t payload = (uint16_t)(fraction >> 13) | 0x0200;
		return sign | 0x7c00 | payload;
	}
	if (exponent == 0 && !fraction) return sign;
	int unbiased = (int)exponent - 127;
	uint64_t significand = UINT64_C(0x800000) | fraction;
	unsigned mode = (cpu->fpscr & M4F_FPSCR_RMODE_MASK) >> 22;
	int maximum_exponent = (cpu->fpscr & M4F_FPSCR_AHP) ? 16 : 15;
	if (unbiased > maximum_exponent) {
		if (cpu->fpscr & M4F_FPSCR_AHP) {
			cpu->fpscr |= M4F_FPSCR_IOC;
			return sign | 0x7fff;
		}
		cpu->fpscr |= M4F_FPSCR_OFC | M4F_FPSCR_IXC;
		bool infinity = !(cpu->fpscr & M4F_FPSCR_AHP) &&
			(mode == 0 || (mode == 1 && !negative) ||
			 (mode == 2 && negative));
		return sign | (infinity ? 0x7c00 : 0x7bff);
	}
	bool inexact;
	uint64_t rounded;
	uint16_t result;
	if (unbiased >= -14) {
		rounded = rounded_shift_right(significand, 13, negative, mode, &inexact);
		unsigned half_exp = (unsigned)(unbiased + 15);
		if (rounded == 0x800) {
			rounded = 0x400;
			half_exp++;
		}
		if (half_exp > (unsigned)(maximum_exponent + 15)) {
			if (cpu->fpscr & M4F_FPSCR_AHP) {
				cpu->fpscr |= M4F_FPSCR_IOC;
				return sign | 0x7fff;
			}
			cpu->fpscr |= M4F_FPSCR_OFC | M4F_FPSCR_IXC;
			bool infinity = !(cpu->fpscr & M4F_FPSCR_AHP) &&
				(mode == 0 || (mode == 1 && !negative) ||
				 (mode == 2 && negative));
			return sign | (infinity ? 0x7c00 : 0x7bff);
		}
		result = sign | (uint16_t)(half_exp << 10) |
			(uint16_t)(rounded & 0x3ff);
	} else {
		unsigned shift = (unsigned)(-unbiased - 1);
		rounded = rounded_shift_right(significand, shift, negative, mode, &inexact);
		result = sign | (uint16_t)rounded;
		if (inexact) cpu->fpscr |= M4F_FPSCR_UFC;
		if ((cpu->fpscr & M4F_FPSCR_FZ) && (result & 0x03ff)) {
			result = sign;
			cpu->fpscr |= M4F_FPSCR_UFC | M4F_FPSCR_IXC;
		}
	}
	if (inexact) cpu->fpscr |= M4F_FPSCR_IXC;
	return result;
}

static enum m4f_stop execute_fp_one_register(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned vd = fp_sd(first, second), vm = fp_sm(second);
	if (vd >= 32 || vm >= 32) return M4F_STEP_UNDEFINED;
	unsigned low = first & 0xf;
	bool bit7 = M4F_BIT(second, 7);
	if (!(second & 0x00e0)) { /* VMOV immediate */
		uint8_t immediate = (uint8_t)((first & 0xf) << 4) | (second & 0xf);
		exec->cpu->s[vd] = vfp_expand_immediate(immediate);
		return M4F_STEP_OK;
	}
	if (low == 2) { /* VCVTB/VCVTT F32.F16 */
		uint32_t source = exec->cpu->s[vm];
		uint16_t half = bit7 ? (uint16_t)(source >> 16) : (uint16_t)source;
		exec->cpu->s[vd] = half_to_single(exec->cpu, half);
		return M4F_STEP_OK;
	}
	if (low == 3) { /* VCVTB/VCVTT F16.F32 */
		uint16_t half = single_to_half(exec->cpu, exec->cpu->s[vm]);
		if (bit7) exec->cpu->s[vd] = (exec->cpu->s[vd] & 0xffff) |
			((uint32_t)half << 16);
		else exec->cpu->s[vd] = (exec->cpu->s[vd] & UINT32_C(0xffff0000)) |
			half;
		return M4F_STEP_OK;
	}
	if (low == 0 || low == 1) {
		uint32_t value = exec->cpu->s[vm];
		if (low == 0 && !bit7) { /* VMOV register */
			exec->cpu->s[vd] = value;
		} else if (low == 0) { /* VABS */
			exec->cpu->s[vd] = value & UINT32_C(0x7fffffff);
		} else if (!bit7) { /* VNEG */
			exec->cpu->s[vd] = value ^ UINT32_C(0x80000000);
		} else { /* VSQRT */
			value = fp_prepare_input(exec->cpu, value);
			if (fp_is_nan(value)) exec->cpu->s[vd] = fp_process_nan(exec->cpu, value);
			else if (M4F_BIT(value, 31) && !fp_is_zero(value)) {
				exec->cpu->fpscr |= M4F_FPSCR_IOC;
				exec->cpu->s[vd] = fp_default_nan();
			} else exec->cpu->s[vd] = fp_round(exec->cpu,
				sqrt((double)bits_to_float(value)));
		}
		return M4F_STEP_OK;
	}
	if (low == 4 || low == 5) { /* VCMP/VCMPE, register or zero */
		bool with_zero = low == 5;
		fp_compare(exec->cpu, exec->cpu->s[vd],
				with_zero ? 0 : exec->cpu->s[vm], bit7);
		return M4F_STEP_OK;
	}
	if (low == 12 || low == 13) { /* float to integer */
		bool is_unsigned = low == 12;
		bool round_zero = bit7;
		exec->cpu->s[vd] = fp_to_integer(exec->cpu, exec->cpu->s[vm],
				is_unsigned, round_zero, 0, 32);
		return M4F_STEP_OK;
	}
	if (low == 8) { /* integer to float */
		bool is_unsigned = !bit7;
		uint32_t input = exec->cpu->s[vm];
		double exact = is_unsigned ? (double)input : (double)(int32_t)input;
		exec->cpu->s[vd] = fp_round(exec->cpu, exact);
		return M4F_STEP_OK;
	}
	return M4F_STEP_UNDEFINED;
}

static enum m4f_stop execute_fp_fixed(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned vd = fp_sd(first, second);
	unsigned encoded = ((second & 0xf) << 1) | M4F_BIT(second, 5);
	unsigned size = M4F_BIT(second, 7) ? 32 : 16;
	unsigned fraction_bits = size - encoded;
	bool to_integer = M4F_BIT(first, 2);
	bool is_unsigned = M4F_BIT(first, 0);
	if (vd >= 32 || encoded > size)
		return M4F_STEP_UNPREDICTABLE;
	if (to_integer) {
		uint32_t converted = fp_to_integer(exec->cpu, exec->cpu->s[vd],
				is_unsigned, true, fraction_bits, size);
		if (size == 16) converted = is_unsigned ? (uint16_t)converted :
			(uint32_t)(int32_t)(int16_t)converted;
		exec->cpu->s[vd] = converted;
	} else {
		uint32_t input = size == 16 ? (uint16_t)exec->cpu->s[vd] :
			exec->cpu->s[vd];
		if (!is_unsigned && size == 16) input =
			(uint32_t)(int32_t)(int16_t)input;
		double value = is_unsigned ? (double)input : (double)(int32_t)input;
		exec->cpu->s[vd] = fp_round_with_mode(exec->cpu,
				ldexp(value, -(int)fraction_bits), 0);
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_core_transfer(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned rt = M4F_BITS(second, 15, 12);
	unsigned sn = ((first & 0xf) << 1) | M4F_BIT(second, 7);
	if ((second & 0x0f7f) != 0x0a10 || sn >= 32)
		return M4F_STEP_UNDEFINED;
	if (M4F_BIT(first, 4)) { /* VMOV core <- single */
		if (rt == 13 || rt == 15) return M4F_STEP_UNPREDICTABLE;
		exec->cpu->r[rt] = exec->cpu->s[sn];
	} else { /* VMOV single <- core */
		if (rt == 13 || rt == 15) return M4F_STEP_UNPREDICTABLE;
		exec->cpu->s[sn] = exec->cpu->r[rt];
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_status_transfer(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	if ((second & 0x0fff) != 0x0a10) return M4F_STEP_UNDEFINED;
	unsigned rt = M4F_BITS(second, 15, 12);
	if (M4F_BIT(first, 4)) { /* VMRS */
		if (rt == 15) {
			exec->cpu->xpsr = (exec->cpu->xpsr & ~UINT32_C(0xf0000000)) |
				(exec->cpu->fpscr & UINT32_C(0xf0000000));
		} else if (rt == 13) {
			return M4F_STEP_UNPREDICTABLE;
		} else {
			exec->cpu->r[rt] = exec->cpu->fpscr & M4F_FPSCR_MASK;
		}
	} else { /* VMSR */
		if (rt == 13 || rt == 15) return M4F_STEP_UNPREDICTABLE;
		exec->cpu->fpscr = exec->cpu->r[rt] & M4F_FPSCR_MASK;
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_double_core_transfer(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	unsigned coprocessor = M4F_BITS(second, 11, 8);
	if ((coprocessor != 10 && coprocessor != 11) || !M4F_BIT(second, 4))
		return M4F_STEP_UNDEFINED;
	unsigned rt = M4F_BITS(second, 15, 12);
	unsigned rt2 = first & 0xf;
	unsigned first_s = coprocessor == 10 ?
		((second & 0xf) << 1) | M4F_BIT(second, 5) :
		(((M4F_BIT(second, 5) << 4) | (second & 0xf)) * 2);
	if (rt == 13 || rt == 15 || rt2 == 13 || rt2 == 15 || first_s + 1 >= 32 ||
			(M4F_BIT(first, 4) && rt == rt2))
		return M4F_STEP_UNPREDICTABLE;
	if (M4F_BIT(first, 4)) {
		exec->cpu->r[rt] = exec->cpu->s[first_s];
		exec->cpu->r[rt2] = exec->cpu->s[first_s + 1];
	} else {
		exec->cpu->s[first_s] = exec->cpu->r[rt];
		exec->cpu->s[first_s + 1] = exec->cpu->r[rt2];
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_fp_load_store(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	bool add = M4F_BIT(first, 7), load = M4F_BIT(first, 4);
	bool double_register = M4F_BITS(second, 11, 8) == 11;
	unsigned rn = first & 0xf;
	unsigned vd = double_register ?
		(M4F_BITS(second, 15, 12) | (M4F_BIT(first, 6) << 4)) * 2 :
		fp_sd(first, second);
	uint32_t base = rn == 15 ? ((exec->address + 4) & ~UINT32_C(3)) :
		exec->cpu->r[rn];
	uint32_t offset = (second & 0xff) * 4;
	uint32_t address = add ? base + offset : base - offset;
	unsigned words = double_register ? 2 : 1;
	if (vd + words > 32 || (!load && rn == 15)) return M4F_STEP_UNPREDICTABLE;
	if (load) {
		uint32_t values[2];
		for (unsigned i = 0; i < words; ++i)
			if (!m4f_read_mem(exec, address + i * 4, 4, true, &values[i]))
				return exec->result.stop;
		if (double_register && exec->cpu->big_endian) {
			exec->cpu->s[vd] = values[1];
			exec->cpu->s[vd + 1] = values[0];
		} else {
			for (unsigned i = 0; i < words; ++i)
				exec->cpu->s[vd + i] = values[i];
		}
	} else {
		for (unsigned i = 0; i < words; ++i) {
			unsigned source = double_register && exec->cpu->big_endian ?
				vd + (1 - i) : vd + i;
			if (!m4f_write_mem(exec, address + i * 4, 4, true,
					exec->cpu->s[source])) return exec->result.stop;
		}
	}
	return M4F_STEP_OK;
}

static enum m4f_stop execute_fp_multiple(struct m4f_exec *exec,
		uint16_t first, uint16_t second)
{
	bool add = M4F_BIT(first, 7), writeback = M4F_BIT(first, 5),
		load = M4F_BIT(first, 4);
	bool double_register = M4F_BITS(second, 11, 8) == 11;
	unsigned rn = first & 0xf;
	unsigned vd = double_register ?
		(M4F_BITS(second, 15, 12) | (M4F_BIT(first, 6) << 4)) * 2 :
		fp_sd(first, second);
	unsigned count = second & 0xff;
	if (!count || vd + count > 32 || rn == 15 ||
			(writeback && rn == 15) || (double_register && (count & 1)))
		return M4F_STEP_UNPREDICTABLE;
	uint32_t base = exec->cpu->r[rn];
	uint32_t address = add ? base : base - count * 4;
	for (unsigned i = 0; i < count; i += double_register ? 2 : 1) {
		unsigned words = double_register ? 2 : 1;
		if (load) {
			uint32_t values[2];
			for (unsigned word = 0; word < words; ++word) {
				if (!m4f_read_mem(exec, address, 4, true, &values[word]))
					return exec->result.stop;
				address += 4;
			}
			for (unsigned word = 0; word < words; ++word) {
				unsigned destination = double_register && exec->cpu->big_endian ?
					vd + i + (1 - word) : vd + i + word;
				exec->cpu->s[destination] = values[word];
			}
		} else {
			for (unsigned word = 0; word < words; ++word) {
				unsigned source = double_register && exec->cpu->big_endian ?
					vd + i + (1 - word) : vd + i + word;
				if (!m4f_write_mem(exec, address, 4, true,
						exec->cpu->s[source])) return exec->result.stop;
				address += 4;
			}
		}
	}
	if (writeback) exec->cpu->r[rn] = add ? base + count * 4 :
		base - count * 4;
	return M4F_STEP_OK;
}

enum m4f_stop m4f_execute_32_fp(struct m4f_exec *exec, uint16_t first,
		uint16_t second)
{
	if ((first & 0xff00) == 0xee00 && (second & 0x0f00) == 0x0a00) {
		if (((first & 0xfff0) == 0xee00 || (first & 0xfff0) == 0xee10) &&
				(second & 0x0f7f) == 0x0a10)
			return execute_core_transfer(exec, first, second);
		if ((first & 0xffef) == 0xeee1 && (second & 0x0fff) == 0x0a10)
			return execute_status_transfer(exec, first, second);
		if ((first & 0xff00) == 0xee00 &&
				((first & 0x00b0) != 0x00b0))
			return execute_fp_three_register(exec, first, second);
		if ((first & 0xffb0) == 0xeeb0) {
			unsigned low = first & 0xf;
			if (low == 10 || low == 11 || low == 14 || low == 15)
				return execute_fp_fixed(exec, first, second);
			return execute_fp_one_register(exec, first, second);
		}
	}
	if ((first & 0xff00) == 0xec00 && !M4F_BIT(first, 7) &&
			M4F_BIT(first, 6) &&
			((second & 0x0f10) == 0x0a10 ||
			 (second & 0x0f10) == 0x0b10))
		return execute_double_core_transfer(exec, first, second);
	if ((first & 0xfe00) == 0xec00 &&
			((second & 0x0f00) == 0x0a00 ||
			 (second & 0x0f00) == 0x0b00)) {
		if (M4F_BIT(first, 8) && !M4F_BIT(first, 5))
			return execute_fp_load_store(exec, first, second);
		return execute_fp_multiple(exec, first, second);
	}
	return M4F_STEP_UNDEFINED;
}
