#ifndef _THUMBDISASM_H

#include <stdint.h>
#include <stringutil.h>

static const struct instructions {
	uint16_t mask;
	uint16_t val;
	char mnem[5];	/* with null */
	uint8_t rm : 4, rn : 4, rd : 4, immed:4, immed_len:4,
		immed_br:1;
} insn_list[] = {
	{ 0xf800, 0x0000, "LSLi", 10, 0, 13, 5, 5 },    // 0000 0b
	{ 0xf800, 0x2000, "MOVi", 0, 0, 5, 8, 8 },      // 0010 0b
	{ 0xf800, 0x3000, "ADDi", 5, 0, 5, 8, 8 },      // 0011 0b
	{ 0xf800, 0x3800, "SUBi", 5, 0, 5, 8, 8 },      // 0011 1b
	{ 0xf800, 0xe000, "B   ", 0, 0, 0, 5, 11, 1 },  // 1110 0b

	{ 0xfe00, 0x1800, "ADDr", 7, 10, 13 },          // 0001 100b
	{ 0xfe00, 0x1A00, "SUBr", 7, 10, 13 },          // 0001 101b
	{ 0xfe00, 0x5800, "LDR ", 7, 10, 13 },          // 0101 100b
	{ 0xfe00, 0x5000, "STR ", 7, 10, 13 },          // 0101 000b

	{ 0xff00, 0x4600, "MOVr", 10, 0, 13 },          // 0100 0110b

	{ 0xff00, 0xD000, "BZ  ", 0, 0, 0, 8, 8, 1 },   // 1101 xxxxb
	{ 0xff00, 0xD100, "BNZ ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xD200, "BCS ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xD300, "BCC ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xD400, "BMI ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xD500, "BPL ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xD600, "BVS ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xD700, "BVC ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xD800, "BHI ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xD900, "BLS ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xDA00, "BGE ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xDB00, "BLT ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xDC00, "BGT ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xDD00, "BLE ", 0, 0, 0, 8, 8, 1 },
	{ 0xff00, 0xDE00, "BAL ", 0, 0, 0, 8, 8, 1 },

	{ 0xff00, 0xdf00, "SYSc", 0, 0, 0, 8, 8, 0 },   // 1101 1111b

	{ 0xffc0, 0x4000, "ANDr", 10, 13, 13 },         // 0100 0000 00b
	{ 0xffc0, 0x4280, "CMPr", 10, 13, 0 },          // 0100 0010 10b
	{ 0xffc0, 0x4340, "MULr", 10, 13, 13 },	        // 0100 0011 01b
	{ 0xffc0, 0x4300, "ORr ", 10, 13, 13 },         // 0100 0011 00b

	{ 0, 0, "????" }
};

static inline const struct instructions *find_insn(uint16_t insn)
{
	for (int i = 0; ; i++) {
		if ((insn & insn_list[i].mask) == insn_list[i].val) {
			return &insn_list[i];
		}
	}
}

static inline const char *get_mnem(uint16_t insn)
{
	const struct instructions *insn_ent = find_insn(insn);

	return insn_ent->mnem;
}

static inline int16_t decode_peel_field_signext(uint16_t insn,
		int offs, int len)
{
	int16_t field = insn << offs;

	field = field >> (16-len);

	return field;
}

static inline uint16_t decode_peel_field(uint16_t insn, int offs, int len)
{
	uint16_t mask = (1 << len)-1;

	return (insn >> (16 - offs - len)) & mask;
}

static inline char decode_peel_reg(uint16_t insn, int offs)
{
	return decode_peel_field(insn, offs, 3) + '0';
}

static inline void decode_insn(char *t, uint32_t addr, uint16_t insn)
{
	const struct instructions *insn_ent = find_insn(insn);

	// 13 characters + terminating null
        // 0000000001111
        // 1234567890123
	//
	// register instructions:
        // OOOO RM RN RD
        //
	// immediate instructions:
	// OOOO II RD
        // OOOO RM II RD
        //
	// branches -- calculates real address
        // OOOO xxxxxxxx

	for (int i=0; i<4; i++) {
		*(t++) = insn_ent->mnem[i];
	}

	if (insn_ent->rm) {
		*(t++) = ' ';
		*(t++) = 'r';
		*(t++) = decode_peel_reg(insn, insn_ent->rm);
	}

	if (insn_ent->rn) {
		*(t++) = ' ';
		*(t++) = 'r';
		*(t++) = decode_peel_reg(insn, insn_ent->rn);
	}

	if (insn_ent->immed_br) {
		*(t++) = ' ';
		int16_t branch_offs = decode_peel_field_signext(insn,
				insn_ent->immed, insn_ent->immed_len);

		uint32_t branch_dest = addr + 4 + (branch_offs*2);

		to_hex_buf(t, branch_dest, 8);

		t += 8;
	} else {
		if (insn_ent->immed) {
			*(t++) = ' ';
			int octlen = (insn_ent->immed_len+3) / 4;

			uint16_t immed = decode_peel_field(insn,
					insn_ent->immed,
					insn_ent->immed_len);

			to_hex_buf(t, immed, octlen);

			t += octlen;
		}
	}

	if (insn_ent->rd) {
		*(t++) = ' ';
		*(t++) = 'r';
		*(t++) = decode_peel_reg(insn, insn_ent->rd);
	}

	*t = 0;
}

#endif /* _THUMBDISASM_H */
