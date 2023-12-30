/* 4x12 font, hex digits, by MPL; CC0 */
/* Bottom row is not used, so really 4x11. */

/* 0 .... 1 ...# 2 ..#. 3 ..##
 * 4 .#.. 5 .#.# 6 .##. 7 .###
 * 8 #... 9 #..# A #.#. B #.##
 * C ##.. D ##.# E ###. F ####
 */
uint8_t font4x12_rasters[][6] = {
	/* 0 */
	{ 0x69, 0x99, 0x99, 0x99, 0x99, 0x60 },
	{ 0x6a, 0x22, 0x22, 0x22, 0x22, 0xf0 },
	{ 0x69, 0x11, 0x12, 0x48, 0x88, 0xf0 },
	{ 0x69, 0x11, 0x11, 0x61, 0x19, 0x60 },
	/* 4 */
	{ 0x99, 0x99, 0x97, 0x11, 0x11, 0x10 },
	{ 0xf8, 0x88, 0x8e, 0x11, 0x11, 0xe0 },
	{ 0x69, 0x88, 0x8e, 0x99, 0x99, 0x60 },
	{ 0xf1, 0x11, 0x12, 0x22, 0x11, 0x10 },
	/* 8 */
	{ 0x69, 0x99, 0x69, 0x99, 0x99, 0x60 },
	{ 0x69, 0x99, 0x97, 0x11, 0x11, 0x96 },
	{ 0x69, 0x99, 0x9f, 0x99, 0x99, 0x90 },
	{ 0xe9, 0x99, 0x9e, 0x99, 0x99, 0xe0 },
	/* C */
	{ 0x78, 0x88, 0x88, 0x88, 0x88, 0x70 },
	{ 0xe9, 0x99, 0x99, 0x99, 0x99, 0xe0 },
	{ 0xf8, 0x88, 0x8e, 0x88, 0x88, 0xf0 },
	{ 0xf8, 0x88, 0x8e, 0x88, 0x88, 0x80 },
};
