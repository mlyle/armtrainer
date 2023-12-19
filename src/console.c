#include <console.h>
#include <lcdutil.h>
#include <matrix.h>
#include <stringutil.h>
#include <systick_handler.h>
#include <stdbool.h>

#define NUMCOL 17

static uint8_t draw_column;
static uint8_t draw_r = 0x0f, draw_g = 0x0f, draw_b = 0x0f;
static uint8_t draw_bg_r, draw_bg_g, draw_bg_b;

void console_bs()
{
	if (draw_column >= 1)
	{
		draw_column--;
	}
}

void console_cr()
{
	draw_column = 0;
}

void console_nl()
{
	lcd_move_up(13, 53, 3, 3, 3);
}

void console_char_norefresh(uint8_t c)
{
	switch (c)
	{
		case 10:
			console_nl();
			break;
		case 13:
			console_cr();
			break;
		case 127:
		case 8:
			console_bs();
			break;
		case 7: /* bell */
			lcd_signalerror();
			break;
		default:
			if (draw_column >= NUMCOL) {
				console_cr();
				console_nl();
			}

			lcd_blit_char(c, draw_column*9, 40, draw_r, draw_g, draw_b,
					draw_bg_r, draw_bg_g, draw_bg_b);
			draw_column++;

			break;
	}
}

void console_char(uint8_t c)
{
	console_char_norefresh(c);
	lcd_refresh();
}

void console_clearscreen()
{
	lcd_blit_rows(0, 53, 0, 0, 0);
	draw_column = 0;
}

void console_number_10(uint32_t n)
{
	console_cr();
	console_nl();

	if (!n) {
		console_char('0');
		return;
	}

	char buf[11];

	char *c = buf+10;
	*c = 0;

	while (n) {
		c--;
		*c = (n % 10) + '0';
		n /= 10;
	}

	while (*c) {
		console_char_norefresh(*c);
		c++;
	}

	lcd_refresh();
}

void console_number_16(uint32_t n)
{
	console_cr();
	console_nl();

	char *str = to_hex32(n);
	while (*str) {
		console_char_norefresh(*str);
		str++;
	}

	lcd_refresh();
}

static inline void color_8bit_to_12bit(uint32_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
	*r = (color & 0xc0) >> 4;   /* RR00 0000 -> RR00 */
	*g = (color & 0x31) >> 2;   /* 00GG G000 -> GGG0 */
	*b = (color & 0x7) << 1;	/* 0000 0BBB -> BBB0 */
}

void console_blit_dot(uint32_t color, uint8_t x, uint8_t y)
{
	uint8_t r, g, b;

	color_8bit_to_12bit(color, &r, &g, &b);

	lcd_blit(x, y, r, g, b);
}

void console_blit_icon(uint32_t color, uint8_t x, uint8_t y, uint32_t ret_addr)
{
	uint16_t *lines = (uint16_t *)ret_addr;

	uint8_t r, g, b;

	color_8bit_to_12bit(color, &r, &g, &b);
	for (int i = 0; i < 16; i++) {
		uint16_t tmp = lines[i];

		for (int j = 0; j < 16; j++) {
			if (tmp & 0x8000) {
				lcd_blit(x+j, y+i, r, g, b);
			} else {
				lcd_blit(x+j, y+i, draw_bg_r, draw_bg_g, draw_bg_b);
			}

			tmp <<= 1;
		}
	}
}

void console_set_drawcolor(uint8_t color)
{
	color_8bit_to_12bit(color, &draw_r, &draw_g, &draw_b);
}

void console_set_bgcolor(uint8_t color)
{
	color_8bit_to_12bit(color, &draw_bg_r, &draw_bg_g, &draw_bg_b);
}

/* Necessary to be visible to matrix keyboard callback */
struct read_number_state_s {
	uint32_t accum;
	uint8_t pos;
	uint8_t base;
	bool finished;
} read_number_state;

static bool handle_digit(int digit)
{
	if (digit >= read_number_state.base) {
		return false;
	}

	uint64_t new_accum;

	new_accum = ((uint64_t) read_number_state.accum) * read_number_state.base + digit;

	if (new_accum > UINT32_MAX) {
		/* Overflow! */
		return false;
	}

	read_number_state.accum = new_accum;
	read_number_state.pos++;

	return true;
}

static void read_number_key_changed(enum matrix_keys key, bool pressed)
{
	if (pressed) {
		/* Keys only valid after first position */
		if (read_number_state.pos) {
			switch (key) {
				case key_0:
					if (handle_digit(0)) {
						console_char(key);
						return;
					}

					break;
				case key_clr:
					read_number_state.pos--;
					read_number_state.accum /= read_number_state.base;
					console_char(' '); /* Erase cursor if necessary */
					console_bs();
					console_bs();
					return;

				case key_store:
				case key_run:
					console_char(' '); /* Erase cursor if necessary */
					console_bs();
					console_cr(); console_nl();
					read_number_state.finished = true;
					return;

				default:
					break;
			}
		}

		if ((key >= key_1) && (key <= key_9)) {
			if (handle_digit(key - key_0)) {
				console_char(key);
				return;
			}
		}

		if ((key >= key_a) && (key <= key_f)) {
			if (handle_digit(key - key_a + 10)) {
				console_char(key);
				return;
			}
		}

		lcd_signalerror();
	}
}

uint32_t console_read_number(uint8_t base)
{
	matrix_cb_t prev_keycallback = matrix_set_callback(read_number_key_changed);

	read_number_state = (struct read_number_state_s) { .base = base };

	int iter = 0;

	do {
		/* Print cursor and move position left */
		if (iter % 2) { 
			console_char('_');
		} else {
			console_char(' ');
		}

		iter++;

		console_bs();

		uint32_t orig = systick_cnt;
		while ((systick_cnt - orig) < 10) {
			matrix_scanall();
		}

	} while (!read_number_state.finished);

	matrix_set_callback(prev_keycallback);

	return read_number_state.accum;
}
