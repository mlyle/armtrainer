#ifndef _LCDUTIL_H
#define _LCDUTIL_H

void lcd_blit_char(uint8_t c, int x, int y, uint8_t r, uint8_t g, uint8_t b,
	uint8_t bgr, uint8_t bgg, uint8_t bgb);
void lcd_init();

#endif
