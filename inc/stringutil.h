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

static inline void to_hex_buf(char *b, uint32_t val, int len)
{
	b[len] = 0;

	for (int i=(len-1); i>=0; i--) {
		b[i] = to_hexdigit(val & 0xf);
		val >>= 4;
	}
}

static inline const char *to_decimal32(int32_t val)
{
	/* 12345678901
	 * -2147483648
	 * 12 characters with term null
	 */
	static char buffer[12];

	uint32_t uval;

	buffer[11]=0;

	if (val < 0) {
		buffer[0] = '-';
		uval = (~((uint32_t) val)) + 1;
	} else {
		buffer[0] = '+';
		uval = val;
	}

	for (int pos=10; pos>0; pos--) {
		buffer[pos] = (uval % 10) + '0';
		uval /= 10;
	}

	return buffer;
}

static inline const char *to_hex(uint32_t val, int len)
{
	static char buffer[9];

	to_hex_buf(buffer, val, len);

	return buffer;
}

static inline const char *to_hex32(uint32_t ptr)
{
	return to_hex(ptr, 8);
}

static inline const char *to_hex16(uint16_t val)
{
	return to_hex(val, 4);
}

static inline const char *to_hex8(uint16_t val)
{
	return to_hex(val, 2);
}


#endif /*STRINGUTIL_H */
