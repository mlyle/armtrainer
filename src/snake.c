#include <stdint.h>
#include <stdbool.h>

#include <lcdutil.h>
#include <matrix.h>
#include <delayutil.h>

#define COLOR_SNAKE         0x4f4 // Light green
#define COLOR_BACKGROUND    0x000 // Black
#define COLOR_FOOD          0xf71 // Orange-ish

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

static uint16_t snake_curcolor(uint8_t x, uint8_t y)
{
    uint8_t r, g, b;

    lcd_getcolor(x*SNAKE_SCALEFACTOR + SNAKE_SCALEFACTOR/2,
            y*SNAKE_SCALEFACTOR + SNAKE_SCALEFACTOR/2,
            &r, &g, &b);

    uint16_t packed_color = (r << 8) | (g << 4) | (b);

    return packed_color;
}

static void snake_draw(uint8_t x, uint8_t y, uint16_t color)
{
    (void) x; (void) y; (void) color;

    uint8_t r = (color >> 8) & 0xff;
    uint8_t g = (color >> 4) & 0xff;
    uint8_t b = (color) & 0xff;

    for (int i = 0; i < SNAKE_SCALEFACTOR; i++)
    {
        lcd_blit_horiz(x*SNAKE_SCALEFACTOR, y+i, (x+1)*SNAKE_SCALEFACTOR, r, g, b);
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

    while (idx_in >= NUMPOSITIONS){
        idx_in -= NUMPOSITIONS;
    }

    return idx_in;
}

static void snake_key_changed(enum matrix_keys key, bool pressed)
{
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

void snake(void)
{
    struct position {
        int8_t x;
        int8_t y;
    } positions[NUMPOSITIONS] = {};

    uint16_t position_idx = 0;

    int8_t curX = MAXX/2, curY = MAXY/2;

    /* Food starts to our left, a couple rows down */
    int8_t foodX = MAXX/3, foodY = curY + 2;

    uint16_t length = 5;

    /* Clear top of screen */
    snake_clearscreen(COLOR_BACKGROUND);

    /* Draw initial food */
    snake_draw(foodX, foodY, COLOR_FOOD);

    matrix_cb_t prev_keycallback = matrix_set_callback(snake_key_changed);

    while (true) {
        if ((curX < 0) || (curX > MAXX) || (curY < 0) || (curX > MAXY))
        {
            /* Death: hitting the wall */
            break;
        }

        if (snake_curcolor(curX, curY) == COLOR_SNAKE) {
            /* Death: hitting the snake */
            break;
        }

        if ((curX == foodX) && (curY == foodY)) {
            length++;

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
        while ((systick_cnt - orig) < 4) {
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
    // XXX halt / thunk back to monitor
}