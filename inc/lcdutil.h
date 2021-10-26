#ifndef _LCDUTIL_H
#define _LCDUTIL_H

void lcd_blit_char(uint8_t c, int x, int y, uint8_t r, uint8_t g, uint8_t b,
	uint8_t bgr, uint8_t bgg, uint8_t bgb);
int lcd_blit_string(char *str, int x, int y, uint8_t r, uint8_t g, uint8_t b,
		uint8_t bgr, uint8_t bgg, uint8_t bgb);
void lcd_refresh();
void lcd_blit_horiz(int x, int y, int x2, uint8_t r, uint8_t g, uint8_t b);
void lcd_blit_rows(int y, int y2, uint8_t fr, uint8_t fg, uint8_t fb);
void lcd_move_up(int y, int y2, uint8_t fr, uint8_t fg, uint8_t fb);
void lcd_signalerror();
void lcd_blit(int x, int y, uint8_t r, uint8_t g, uint8_t b);

void lcd_init();

#endif
