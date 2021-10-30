#include <stdint.h>
#include <stdbool.h>

#include <lcdutil.h>
#include <matrix.h>
#include <delayutil.h>
#include <stringutil.h>

#define COLOR_SNAKE         0x4f4 // Light green
#define COLOR_BACKGROUND    0x000 // Black
#define COLOR_FOOD          0xf71 // Orange-ish
#define COLOR_BORDER        0xf58 // Red-ish

#define SNAKE_SCALEFACTOR 5

#define MAXX 31
#define MAXY 9
#define NUMPOSITIONS ((MAXX+1) * (MAXY+1))

static enum snake_direction_e {
    DIR_LEFT,
    DIR_RIGHT,
    DIR_UP,
    DIR_DOWN
} snake_direction = DIR_RIGHT;

static struct position {
    int8_t x;
    int8_t y;
} positions[NUMPOSITIONS] = {};


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

    lcd_blit_horiz(0, 0, 159, r, g, b);
    lcd_blit_horiz(0, 127, 159, r, g, b);
    
    for (int i = 1; i < 127; i++) {
        lcd_blit(0, i, r, g, b);
        lcd_blit(159, i, r, g, b);
    }
}

static void snake_draw(uint8_t x, uint8_t y, uint16_t color)
{
    (void) x; (void) y; (void) color;

    uint8_t r = (color >> 8) & 0xff;
    uint8_t g = (color >> 4) & 0xff;
    uint8_t b = (color) & 0xff;
    uint8_t startX = x*SNAKE_SCALEFACTOR;
    uint8_t endX = (x+1)*SNAKE_SCALEFACTOR;

    /* These 4 conditionals protect the border of the field */
    if (startX == 0) {
        startX = 1;
    }

    if (endX > 159) {
        endX = 159;
    }

    for (int i = 0; i < SNAKE_SCALEFACTOR; i++)
    {
        if ((y+i) == 0) {
            continue;
        }

        if ((y+i) == 127) {
            continue;
        }

        lcd_blit_horiz(startX, y*SNAKE_SCALEFACTOR+i, endX, r, g, b);
    }
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
    /* XXX prevent doubling back?? */
	if (pressed) {
		switch (key) {
			case key_1:
                snake_direction = DIR_DOWN;
                break;
            case key_4:
                snake_direction = DIR_LEFT;
                break;
            case key_6:
                snake_direction = DIR_RIGHT;
                break;
            case key_9:
                snake_direction = DIR_UP;
                break;
            default:
                lcd_signalerror();
                break;
        }
    }
}

uint16_t myrand(void) {
    static unsigned long next = 1;
    next = next * 1103515245 + 12345;
    return ((unsigned)(next/65536) % 32768);
}

int snake(void)
{
    int position_idx = 0;

    snake_direction = DIR_RIGHT;

    int8_t curX = MAXX/2, curY = MAXY/2;

    /* Food starts to our left, a couple rows down */
    int8_t foodX = MAXX/3, foodY = curY + 2;

    int16_t length = 5;

    /* Clear top of screen */
    snake_clearscreen(COLOR_BACKGROUND);

    /* Draw a crude border */
    snake_drawborder(COLOR_BORDER);

    /* Draw initial food */
    snake_draw(foodX, foodY, COLOR_FOOD);

    matrix_cb_t prev_keycallback = matrix_set_callback(snake_key_changed);

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

            char *score = to_hex32(length);
            lcd_blit_string(score, 24, 84, 15, 15, 0, 0, 0, 9);

            // Generate food positions until we find an empty one.
            do {
                uint16_t rand_num = myrand();
                foodX = rand_num % 31;
                foodY = rand_num % 9;
            } while (snake_curcolor(foodX, foodY) != COLOR_BACKGROUND);

            snake_draw(foodX, foodY, COLOR_FOOD);
        } else {
            // erase old position
            int to_erase = calc_index(position_idx - length);

            snake_draw(positions[to_erase].x, positions[to_erase].y, COLOR_BACKGROUND);
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
        while ((systick_cnt - orig) < 26) {
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

    }

    matrix_set_callback(prev_keycallback);

    return length;
}