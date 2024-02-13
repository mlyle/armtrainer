#include <armdio.h>
#include <delayutil.h>
#include <matrix.h>
#include <systick_handler.h>

#define NELEMENTS(x) (sizeof(x) / sizeof(*(x)))

#ifdef OLD_HW
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

#ifdef MATRIX_LED
static DIOTag_t matrix_led = GPIOC_DIO(13);
static uint32_t matrix_scan_count;
#endif

#else /* OLD_HW */

static DIOTag_t matrix_outps[] = {
	GPIOA_DIO(5),
	GPIOA_DIO(4),
	GPIOA_DIO(3),
	GPIOA_DIO(2),
	GPIOA_DIO(1),
	GPIOA_DIO(0),
};

static DIOTag_t matrix_inps[] = {
	GPIOC_DIO(9),
	GPIOC_DIO(8),
	GPIOC_DIO(7),
	GPIOC_DIO(6),
};

#ifdef MATRIX_LED
static DIOTag_t matrix_led = GPIOA_DIO(15);
static uint32_t matrix_scan_count;
#endif

#endif

static uint8_t outp_statuses[NELEMENTS(matrix_outps)];

static matrix_cb_t matrix_callback;

static enum matrix_keys debounce_last;
static uint32_t debounce_time;

/* This is the keymap rotated 90 degrees CCW */
static const enum matrix_keys keymap[NELEMENTS(matrix_inps) * NELEMENTS(matrix_outps)] = {
	key_0, key_4, key_8, key_c,
	key_1, key_5, key_9, key_d,
	key_2, key_6, key_a, key_e,
	key_3, key_7, key_b, key_f,
	key_load, key_store, key_addr, key_clr,
	key_run, key_step, key_invalid, key_invalid
};

static inline bool debounce_expired()
{
	int32_t difference = systick_cnt - debounce_time;

	// 20 ms
	if (difference >= 4) {
		return true;
	} else {
		return false;
	}
}

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

			if ((key != debounce_last) || debounce_expired()) {
				debounce_last = key;
				debounce_time = systick_cnt;

				if (matrix_callback) {
					matrix_callback(key, pin);
				}

				if (pin) {
					outp_stat |= inp_mask;
				} else {
					outp_stat &= ~inp_mask;
				}
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
#ifdef MATRIX_LED
		matrix_scan_count++;
		DIOWrite(matrix_led, (matrix_scan_count & 0x400));
#endif
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
#ifdef MATRIX_LED
	DIOSetOutput(matrix_led, false, DIO_DRIVE_LIGHT, true);
#endif

	/* Cols are OUTPUT HIGH or input pulldown; initially input */
	for (int i=0; i<NELEMENTS(matrix_outps); i++) {
		DIOSetInput(matrix_outps[i], DIO_PULL_DOWN);
	}

	/* Rows are input pulldown */
	for (int i=0; i<NELEMENTS(matrix_inps); i++) {
		DIOSetInput(matrix_inps[i], DIO_PULL_DOWN);
	}
}

