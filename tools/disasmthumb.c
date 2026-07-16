#include <stdint.h>
#include <stdio.h>
#include <thumbdisasm.h>

int main(void)
{
	uint16_t insn;

	uint32_t addr = 0x20000000;

	char buf[64];

	while (fread(&insn, 2, 1, stdin) == 1) {
		if (insn != 0x0000) {
			decode_insn(buf, addr, insn);
			printf("%08x: %04x    %s\n", addr, insn, buf);

			if ((insn == 0xdf25) || (insn == 0xdf26)) {
				for (int i = 0; i < 16; i++) {
					uint16_t row;

					if (fread(&row, 2, 1, stdin) == 1) {
						printf("%08x: %04x                    ; ICONDATA   ",
								addr, row);

						for (int j = 0; j < 16; j++) {
							putchar((row & 0x8000) ? '#' : ' ');
							row = row << 1;
						}

						putchar('\n');
						addr += 2;
					}
				}
			}
		}

		addr += 2;
	}

	return 0;
}
