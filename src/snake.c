#include <stdint.h>
#include <stdbool.h>

#include <lcdutil.h>
#include <matrix.h>
#include <delayutil.h>
#include <stringutil.h>
#include <randomutil.h>

#define COLOR_SNAKE         0x4f4 // Light green
#define COLOR_BACKGROUND    0x000 // Black
#define COLOR_FOOD          0xf71 // Orange-ish
#define COLOR_BORDER        0xf58 // Purple-ish

#define SNAKE_SCALEFACTOR 5

#define MAXX 31
#define MAXY 9
#define NUMPOSITIONS ((MAXX+1) * (MAXY+1))

static const uint8_t right_border = (MAXX+1) * SNAKE_SCALEFACTOR - 1;
static const uint8_t bottom_border = (MAXY+1) * SNAKE_SCALEFACTOR - 1;

enum snake_direction_e {
	DIR_LEFT,
	DIR_RIGHT,
	DIR_UP,
	DIR_DOWN
};

static enum snake_direction_e snake_direction, snake_last_direction;

static uint16_t snake_curcolor(uint8_t x, uint8_t y)
{
	uint8_t r, g, b;

	lcd_getcolor(x*SNAKE_SCALEFACTOR + SNAKE_SCALEFACTOR/2,
			y*SNAKE_SCALEFACTOR + SNAKE_SCALEFACTOR/2,
			&r, &g, &b);

	uint16_t packed_color = (r << 8) | (g << 4) | (b);

	return packed_color;
}

static void snake_drawborder(uint16_t color)
{
	uint8_t r = (color >> 8) & 0xff;
	uint8_t g = (color >> 4) & 0xff;
	uint8_t b = (color) & 0xff;

	lcd_blit_horiz(0, 0, right_border, r, g, b);
	lcd_blit_horiz(0, bottom_border, right_border, r, g, b);

	for (int i = 1; i < bottom_border; i++) {
		lcd_blit(0, i, r, g, b);
		lcd_blit(right_border, i, r, g, b);
	}
}

static void snake_draw(uint8_t x, uint8_t y, uint16_t color)
{
	(void) x; (void) y; (void) color;

	uint8_t r = (color >> 8) & 0xff;
	uint8_t g = (color >> 4) & 0xff;
	uint8_t b = (color) & 0xff;

	uint8_t startX = x * SNAKE_SCALEFACTOR + 1;
	uint8_t endX = startX + SNAKE_SCALEFACTOR - 2;
	/* e.g. 1 to 4 (4 pixels) or 156 to 159 */

	if (endX >= right_border) {
		endX = right_border-1;	/* protect border */
	}

	uint8_t startY = y * SNAKE_SCALEFACTOR + 1;
	uint8_t endY = startY + SNAKE_SCALEFACTOR - 2;
	/* 1 to 4 or 46 to 49 */

	if (endY >= bottom_border) {
		endY = bottom_border-1;
	}

	lcd_blit_box(startX, startY, endX, endY, r, g, b);
}

static void snake_clearscreen(uint16_t color)
{
	uint8_t r = (color >> 8) & 0xff;
	uint8_t g = (color >> 4) & 0xff;
	uint8_t b = (color) & 0xff;

	lcd_blit_rows(0, 53, r, g, b);
}

static inline int calc_index(int idx_in)
{
	while (idx_in < 0) {
		idx_in += NUMPOSITIONS;
	}

	idx_in %= NUMPOSITIONS;

	return idx_in;
}

static void snake_key_changed(enum matrix_keys key, bool pressed)
{
	if (pressed) {
		switch (key) {
			case key_1:
			case key_5: /* accept "5" for WASD pattern */
				if (snake_last_direction != DIR_UP)
					snake_direction = DIR_DOWN;
				break;
			case key_4:
				if (snake_last_direction != DIR_RIGHT)
					snake_direction = DIR_LEFT;
				break;
			case key_6:
				if (snake_last_direction != DIR_LEFT)
					snake_direction = DIR_RIGHT;
				break;
			case key_9:
				if (snake_last_direction != DIR_DOWN)
					snake_direction = DIR_UP;
				break;
			default:
				lcd_signalerror();
				break;
		}
	}
}

int snake(void)
{
	int position_idx = 0;
	struct position {
		int8_t x;
		int8_t y;
	} positions[NUMPOSITIONS] = {};

	snake_direction = DIR_RIGHT;
	snake_last_direction = DIR_RIGHT;

	int8_t curX = MAXX/2, curY = MAXY/2;

	/* Food starts to our left, a couple rows down */
	int8_t foodX = MAXX/3, foodY = curY + 2;

	int16_t length = 5;
	int16_t donterase = 0;

	/* Clear top of screen */
	snake_clearscreen(COLOR_BACKGROUND);

	/* Draw a crude border */
	snake_drawborder(COLOR_BORDER);

	/* Draw initial food */
	snake_draw(foodX, foodY, COLOR_FOOD);

	lcd_refresh();

	matrix_cb_t prev_keycallback = matrix_set_callback(snake_key_changed);

	/* Delay half a beat at the beginning */
	{
		uint32_t orig = systick_cnt;
		while ((systick_cnt - orig) < 125);
	}

	while (true) {
		if ((curX < 0) || (curX > MAXX) || (curY < 0) || (curY > MAXY))
		{
			/* Death: hitting the wall */
			break;
		}

		if (snake_curcolor(curX, curY) == COLOR_SNAKE) {
			/* Death: hitting the snake */
			break;
		}

		if ((curX == foodX) && (curY == foodY)) {
			length += 3;

			// Keeping track of a "do not erase" field lets us fix a bug where
			// erasing past the end of a growing snake could conceal food or
			// bits of crossing snake.
			donterase += 2;

			const char *score = to_hex32(length);
			lcd_blit_string(score, 24, 84, 15, 15, 0, 0, 0, 9);

			// Generate food positions until we find an empty one.
			do {
				uint32_t rand_num = random_next();  // Truncates 64 bit
				rand_num %= NUMPOSITIONS;

				foodX = rand_num % (MAXX+1);
				foodY = rand_num / (MAXX+1);
			} while (snake_curcolor(foodX, foodY) != COLOR_BACKGROUND);

			snake_draw(foodX, foodY, COLOR_FOOD);
		} else {
			if (!donterase) {
				// erase old position
				int to_erase = calc_index(position_idx - length);

				snake_draw(positions[to_erase].x, positions[to_erase].y, COLOR_BACKGROUND);
			} else {
				donterase--;
			}
		}

		// append to snake queue/list
		position_idx = calc_index(position_idx + 1);

		positions[position_idx].x = curX;
		positions[position_idx].y = curY;

		// Extend snake by drawing current pixel green
		snake_draw(curX, curY, COLOR_SNAKE);

		// repaint, delay & check for input
		lcd_refresh();

		uint32_t orig = systick_cnt;
		while ((systick_cnt - orig) < 30) {
			matrix_scanall();
		}

		// Update position based on current direction
		switch (snake_direction) {
			case DIR_LEFT:
				curX--;
				break;
			case DIR_RIGHT:
				curX++;
				break;
			case DIR_UP:
				curY--;
				break;
			case DIR_DOWN:
				curY++;
				break;
		}

		snake_last_direction = snake_direction;
	}

	matrix_set_callback(prev_keycallback);

	return length;
}
