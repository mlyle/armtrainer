#include <armdio.h>
#include <delayutil.h>
#include <matrix.h>

#define NELEMENTS(x) (sizeof(x) / sizeof(*(x)))

static DIOTag_t matrix_outps[] = {
	GPIOA_DIO(15),
	GPIOA_DIO(12),
	GPIOA_DIO(11),
	GPIOA_DIO(10),
	GPIOA_DIO(9),
	GPIOA_DIO(8),
};

static DIOTag_t matrix_inps[] = {
	GPIOB_DIO(6),
	GPIOB_DIO(5),
	GPIOB_DIO(4),
	GPIOB_DIO(3),
};

static uint8_t outp_statuses[NELEMENTS(matrix_outps)];

static matrix_cb_t matrix_callback;


/* This is the keymap rotated 90 degrees CCW */
static const enum matrix_keys keymap[NELEMENTS(matrix_inps) * NELEMENTS(matrix_outps)] = {
	key_0, key_4, key_8, key_c,
	key_1, key_5, key_9, key_d,
	key_2, key_6, key_a, key_e,
	key_3, key_7, key_b, key_f,
	key_load, key_store, key_addr, key_clr,
	key_run, key_step, key_invalid, key_invalid
};

void matrix_scanstep()
{
	static int cur_outp = 0;

	uint8_t inp_mask = 1;

	uint8_t outp_stat = outp_statuses[cur_outp];

	for (int i=0; i<NELEMENTS(matrix_inps); i++) {
		bool pin = DIORead(matrix_inps[i]);

		bool old_status = !!(outp_stat & inp_mask);

		if (old_status != pin) {
			enum matrix_keys key = keymap[cur_outp * NELEMENTS(matrix_inps) + i];

			if (matrix_callback) {
				matrix_callback(key, pin);
			}

			if (pin) {
				outp_stat |= inp_mask;
			} else {
				outp_stat &= ~inp_mask;
			}
		}
		inp_mask <<= 1;
	}

	outp_statuses[cur_outp] = outp_stat;

	// disable current output
	DIOSetInput(matrix_outps[cur_outp], DIO_PULL_DOWN);

	// increment curCol, wrapping around
	cur_outp++;

	if ((cur_outp) >= NELEMENTS(matrix_outps)) {
		cur_outp = 0;
	}

	// set new current col as output high
	DIOSetOutput(matrix_outps[cur_outp], false, DIO_DRIVE_LIGHT, true);
	delay_loop(100);
}

matrix_cb_t matrix_set_callback(matrix_cb_t cb)
{
	matrix_cb_t prev_value = matrix_callback;

	matrix_callback = cb;

	return prev_value;
}

void matrix_scanall()
{
	for (int i=0; i < NELEMENTS(matrix_outps); i++) {
		matrix_scanstep();
	}
}

void matrix_init()
{
	/* Cols are OUTPUT HIGH or input pulldown; initially input */
	for (int i=0; i<NELEMENTS(matrix_outps); i++) {
		DIOSetInput(matrix_outps[i], DIO_PULL_DOWN);
	}

	/* Rows are input pulldown */
	for (int i=0; i<NELEMENTS(matrix_inps); i++) {
		DIOSetInput(matrix_inps[i], DIO_PULL_DOWN);
	}
}

