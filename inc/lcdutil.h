#ifndef _LCDUTIL_H
#define _LCDUTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void lcd_blit_char(uint8_t c, int x, int y, uint8_t r, uint8_t g, uint8_t b,
	uint8_t bgr, uint8_t bgg, uint8_t bgb);
int lcd_blit_string(const char *str, int x, int y, uint8_t r, uint8_t g, uint8_t b,
		uint8_t bgr, uint8_t bgg, uint8_t bgb);
int lcd_blit_smdigit_string(const char *str, int x, int y,
		uint8_t r, uint8_t g, uint8_t b,
		uint8_t bgr, uint8_t bgg, uint8_t bgb);
void lcd_refresh();
void lcd_blit_horiz(int x, int y, int x2, uint8_t r, uint8_t g, uint8_t b);
void lcd_blit_rows(int y, int y2, uint8_t fr, uint8_t fg, uint8_t fb);
void lcd_blit_box(int x, int y, int x2, int y2, uint8_t r,
		uint8_t g, uint8_t b);
void lcd_move_up(int y, int y2, uint8_t fr, uint8_t fg, uint8_t fb);
void lcd_signalerror();
void lcd_blit(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void lcd_getcolor(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b);
bool lcd_is_ready();

/* Waits for display operations to complete, and releases the LCD CS,
 * in preparation for SD io.
 */
void lcd_release();

void lcd_init();

const uint8_t *lcd_framebuffer(void);
size_t lcd_framebuffer_size(void);
uint32_t lcd_frame_revision(void);
void lcd_frame_published(void);

#endif
