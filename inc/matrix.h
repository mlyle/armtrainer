#ifndef _MATRIX_H
#define _MATRIX_H

enum matrix_keys {
	key_0 = '0',
	key_1, key_2, key_3, key_4, key_5, key_6, key_7, key_8, key_9,
	key_a = 'a',
	key_b, key_c, key_d, key_e, key_f,
	key_load = 'L', key_store = 'S', key_addr = 'A',
	key_clr = '<', key_step = '.', key_run = 'G',
	key_invalid = 'x'
};

void matrix_key_changed(enum matrix_keys key, bool pressed);
void matrix_scanstep();
void matrix_init();

#endif
