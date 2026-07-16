#include <stdint.h>

static __attribute__((noinline)) uint32_t mix(uint32_t value, uint32_t key)
{
	value ^= key + UINT32_C(0x9e3779b9) + (value << 6) + (value >> 2);
	value = (value << 7) | (value >> 25);
	return value * UINT32_C(0x45d9f3b);
}

uint32_t student_main(uint32_t seed, uint32_t count, uint32_t *output)
{
	uint32_t value = seed;
	uint32_t sum = 0;
	for (uint32_t i = 0; i < count; ++i) {
		value = mix(value, i * 17 + 3);
		sum += value ^ (value >> (i & 7));
		if (value & 0x100) sum ^= UINT32_C(0xa5a5a5a5);
		else sum += UINT32_C(0x1020304);
	}
	uint32_t quotient = value / (count | 1);
	uint32_t remainder = value % (count | 1);
	output[0] = value;
	output[1] = sum;
	output[2] = quotient;
	output[3] = remainder;
	output[4] = (value < sum) ? value : sum;
	output[5] = (int32_t)value < (int32_t)sum ? sum : value;
	return value ^ sum ^ quotient ^ remainder;
}
