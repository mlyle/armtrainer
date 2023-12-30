#ifndef _STRINGUTIL_H
#define _STRINGUTIL_H

static inline char to_hexdigit(uint8_t d)
{
	if (d < 10) {
		return '0' + d;
	}

	if (d < 16) {
		return d-10 + 'A';
	}

	return 0;
}

static inline char *to_hex32(uint32_t ptr)
{
	static char buffer[9];
	buffer[8] = 0;

	for (int i=7; i>=0; i--) {
		buffer[i] = to_hexdigit(ptr & 0xf);
		ptr >>= 4;
	}

	return buffer;
}

static inline char *to_hex16(uint16_t val)
{
	static char buffer[5];
	buffer[4] = 0;

	for (int i=3; i>=0; i--) {
		buffer[i] = to_hexdigit(val & 0xf);
		val >>= 4;
	}

	return buffer;
}

static struct instructions {
	uint16_t mask;
	uint16_t val;
	char mnem[5];	/* with null */
	uint8_t rm : 4, rn : 4, rd : 4, immed:4, immed_len:4,
		immed_br:1;
} insn_list[] = {
	{ 0xf800, 0x0000, "LSLi", 10, 0, 13, 5, 5 },    // 0000 0b
	{ 0xf800, 0x2000, "MOVi", 0, 0, 5, 8, 8 },      // 0010 0b
	{ 0xf800, 0x3000, "ADDi", 0, 0, 5, 8, 8 },      // 0011 0b
	{ 0xf800, 0x3800, "SUBi", 0, 0, 5, 8, 8 },      // 0011 1b
	{ 0xf800, 0xe000, "B   ", 0, 0, 0, 5, 11, 1 },  // 1110 0b

	{ 0xfe00, 0x1800, "ADDr", 7, 10, 13 },          // 0001 100b
	{ 0xfe00, 0x1A00, "SUBr", 7, 10, 13 },          // 0001 101b
	{ 0xfe00, 0x5800, "LDR ", 7, 10, 13 },          // 0101 100b
	{ 0xfe00, 0x5000, "STR ", 7, 10, 13 },          // 0101 000b

	{ 0xff00, 0x4600, "MOVr", 10, 0, 13 },          // 0100 0110b

	{ 0xff00, 0xD000, "BZ  ", 0, 0, 0, 8, 8, 1 },   // 1101 0000b
	{ 0xff00, 0xD100, "BNZ ", 0, 0, 0, 8, 8, 1 },   // 1101 0001b
	{ 0xff00, 0xD400, "BMI ", 0, 0, 0, 8, 8, 1 },   // 1101 0100b

	{ 0xff00, 0xdf00, "SYSc", 0, 0, 0, 8, 8, 0 },   // 1101 1111b

	{ 0xffc0, 0x4000, "ANDr", 10, 13, 13 },         // 0100 0000 00b
	{ 0xffc0, 0x4280, "CMPr", 10, 13, 0 },          // 0100 0010 10b
	{ 0xffc0, 0x4340, "MULr", 10, 13, 13 },	        // 0100 0011 01b
	{ 0xffc0, 0x4300, "ORRr", 10, 13, 13 },         // 0100 0011 00b

	{ 0, 0, "????" }
};

static inline char *get_mnem(uint16_t insn)
{ 
	for (int i = 0; ; i++) {
		if ((insn & insn_list[i].mask) == insn_list[i].val) {
			return insn_list[i].mnem;
		}
	}

    return "????";
}

#endif /*STRINGUTIL_H */
