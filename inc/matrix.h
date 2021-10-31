#ifndef _MATRIX_H
#define _MATRIX_H

#include <stdbool.h>

enum matrix_keys {
	key_0 = '0',
	key_1, key_2, key_3, key_4, key_5, key_6, key_7, key_8, key_9,
	key_a = 'a',
	key_b, key_c, key_d, key_e, key_f,
	key_load = 'L', key_store = 'S', key_addr = 'A',
	key_clr = '<', key_step = '.', key_run = 'G',
	key_invalid = 'x'
};

// void matrix_key_changed(enum matrix_keys key, bool pressed)
typedef void (*matrix_cb_t)(enum matrix_keys, bool);

/* Returns previous value */
matrix_cb_t matrix_set_callback(matrix_cb_t cb);
void matrix_key_changed(enum matrix_keys key, bool pressed);
void matrix_scanstep();
void matrix_scanall();
void matrix_init();

#endif
