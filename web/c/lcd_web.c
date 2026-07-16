#include <lcdutil.h>

#include <stdbool.h>
#include <stdint.h>

#define LCD_REDRAW_MS UINT32_C(50)

static uint32_t lcd_now_ms;
static uint32_t lcd_ready_at;

void lcd_web_set_time(uint32_t now_ms)
{
	lcd_now_ms = now_ms;
}

void lcd_init(void)
{
	lcd_ready_at = lcd_now_ms;
}

void lcd_refresh(void)
{
	lcd_frame_published();
	lcd_ready_at = lcd_now_ms + LCD_REDRAW_MS;
}

bool lcd_is_ready(void)
{
	return (int32_t)(lcd_now_ms - lcd_ready_at) >= 0;
}

void lcd_release(void)
{
}

void lcd_signalerror(void)
{
	lcd_refresh();
}
