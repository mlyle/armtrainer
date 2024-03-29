/* This 4x12 typeface by Michael Lyle is marked with CC0 1.0 Universal.
 * To view a copy of this license, visit
 * http://creativecommons.org/publicdomain/zero/1.0
 */

/* Bottom row is not used, so really 4x11. 
 * Each nibble represents one row.
 *
 * Characters: 01234567890?·
 */

const uint8_t font_4x12_rasters[][6] = {
	/* 0 */
	{ 0x69, 0x99, 0x99, 0x99, 0x99, 0x60 },
	{ 0x6a, 0x22, 0x22, 0x22, 0x22, 0xf0 },
	{ 0x69, 0x11, 0x12, 0x48, 0x88, 0xf0 },
	{ 0x69, 0x11, 0x11, 0x61, 0x19, 0x60 },
	/* 4 */
	{ 0x99, 0x99, 0x97, 0x11, 0x11, 0x10 },
	{ 0xf8, 0x88, 0x8e, 0x11, 0x11, 0xe0 },
	{ 0x69, 0x88, 0x8e, 0x99, 0x99, 0x60 },
	{ 0xf1, 0x11, 0x12, 0x22, 0x44, 0x40 },
	/* 8 */
	{ 0x69, 0x99, 0x69, 0x99, 0x99, 0x60 },
	{ 0x69, 0x99, 0x97, 0x11, 0x19, 0x60 },
	{ 0x69, 0x99, 0x9f, 0x99, 0x99, 0x90 },
	{ 0xe9, 0x99, 0x9e, 0x99, 0x99, 0xe0 },
	/* C */
	{ 0x78, 0x88, 0x88, 0x88, 0x88, 0x70 },
	{ 0xe9, 0x99, 0x99, 0x99, 0x99, 0xe0 },
	{ 0xf8, 0x88, 0x8e, 0x88, 0x88, 0xf0 },
	{ 0xf8, 0x88, 0x8e, 0x88, 0x88, 0x80 },
	/* question mark */
	{ 0x69, 0x99, 0x12, 0x22, 0x20, 0x20 },
	/* dot in middle, invalid/space */
	{ 0x00, 0x00, 0x06, 0x60, 0x00, 0x00 },
};
