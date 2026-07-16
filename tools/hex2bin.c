#include <stdio.h>
#include <stdint.h>

int main(void) {
	int z;

	while (scanf("%x", &z) == 1) {
		uint16_t val = z;

		fwrite(&val, 2, 1, stdout);

		fwrite("OK\n", 3, 1, stderr);
	}

	return 0;
}
