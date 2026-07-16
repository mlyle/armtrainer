#include <stdint.h>
#include <stdio.h>

uint32_t student_main(uint32_t seed, uint32_t count, uint32_t *output);

int main(void)
{
	uint32_t output[8] = { 0 };
	uint32_t result = student_main(UINT32_C(0x13579bdf), 17, output);
	printf("RESULT %08x", result);
	for (unsigned i = 0; i < 8; ++i) printf(" %08x", output[i]);
	putchar('\n');
	return 0;
}
