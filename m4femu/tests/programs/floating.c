#include <stdint.h>

static uint32_t float_bits(float value)
{
	union {
		float value;
		uint32_t bits;
	} converted = { .value = value };
	return converted.bits;
}

uint32_t student_main(uint32_t seed, uint32_t count, uint32_t *output)
{
	float x = (float)(int32_t)(seed & 0x7fff) / 32.0f;
	float y = 1.25f;
	for (uint32_t i = 0; i < (count & 7); ++i) {
		x = (x + y) * 0.5f;
		y = y + 0.75f;
	}
	float product = x * y;
	float quotient = product / 3.0f;
	int32_t truncated = (int32_t)quotient;
	output[0] = float_bits(x);
	output[1] = float_bits(y);
	output[2] = float_bits(product);
	output[3] = float_bits(quotient);
	output[4] = (uint32_t)truncated;
	output[5] = float_bits((float)truncated);
	return float_bits(quotient) ^ (uint32_t)truncated;
}
