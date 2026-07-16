#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

#include <matrix.h>

void console_bs();
void console_cr();
void console_nl();
void console_char_norefresh(uint8_t c);
void console_char(uint8_t c);
void console_str(char *str);
void console_clearscreen();
void console_number_10_nocr(uint32_t n);
void console_number_10(uint32_t n);
void console_number_16(uint32_t n);
void console_number_16_short(uint32_t n);
void console_blit_dot(uint32_t color, uint8_t x, uint8_t y);
void console_blit_icon(uint32_t color, uint8_t x, uint8_t y, uint32_t ret_addr);
void console_blit_icon_transparent(uint32_t color, uint8_t x, uint8_t y, uint32_t ret_addr);
void console_blit_icon_data(uint32_t color, uint8_t x, uint8_t y,
		const uint16_t lines[16], bool transparent);
void console_set_drawcolor(uint8_t color);
void console_set_bgcolor(uint8_t color);
uint32_t console_read_number(uint8_t base);
void console_read_number_begin(uint8_t base);
bool console_read_number_key(enum matrix_keys key, bool pressed);
bool console_read_number_finished(void);
uint32_t console_read_number_value(void);

#endif /* _CONSOLE_H */
