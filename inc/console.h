#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <stdint.h>

void console_bs();
void console_cr();
void console_nl();
void console_char_norefresh(uint8_t c);
void console_char(uint8_t c);
void console_clearscreen();
void console_number_10(uint32_t n);
void console_number_16(uint32_t n);
void console_blit_dot(uint32_t color, uint8_t x, uint8_t y);
void console_blit_icon(uint32_t color, uint8_t x, uint8_t y, uint32_t ret_addr);
void console_set_drawcolor(uint8_t color);
void console_set_bgcolor(uint8_t color);
uint32_t console_read_number(uint8_t base);

#endif /* _CONSOLE_H */