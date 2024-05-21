#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <led.h>
#include <lcdutil.h>
#include <save.h>
#include <stringutil.h>
#include <thumbdisasm.h>

#include <armdio.h>

#include <stm32f4xx_rcc.h>
#include <systick_handler.h>

#include <delayutil.h>
#include <randomutil.h>
#include <matrix.h>
#include <console.h>

#include <ctype.h>

#ifndef MIN
#define MIN(a,b) \
	({ __typeof__ (a) _a = (a); \
	 __typeof__ (b) _b = (b); \
	 _a < _b ? _a : _b; })
#endif

#define NELEMENTS(x) (sizeof(x) / sizeof(*(x)))

struct __attribute__((packed)) ContextStateFrame_s {
	uint32_t r[4];
	uint32_t r12;
	uint32_t lr;
	uint32_t return_address;
	uint32_t xpsr;
};

struct __attribute__((packed)) EnhancedContextStateFrame_s {
	struct ContextStateFrame_s csf;

	uint32_t rh[4];
};

/* Single step state machine */
enum progrun_state {
	STATE_STOPPED,
	STATE_STEP,
	STATE_RUN
} prog_state;

/* Flag if something went wrong with oscillator startup.
 * Right now this causes us to panic, but there's no firm reason for
 * this -- we could work fine from RC oscillator.
 */
static bool osc_err = false;

/* State machine for keypad editing addresses/values */
static bool editing_addr = true;
static uint8_t edit_pos = 0;
static uint32_t edit_addr = 0;
static uint32_t loaded_addr = 0;
static uint32_t edit_val = 0;

/* The current stack frame.  This is a global so that we can redraw
 * the screen without passing this everywhere.
 */
static struct EnhancedContextStateFrame_s *global_frame;

/* Which screen of registers should be shown right now */
static int register_screen = 0;

/* Flag: Show registers in decimal instead of hex */
static bool show_decimal = false;

/* Flag: Don't wait for screen redraws to proceed with next instruction
 * in run mode.  Still a tiny fraction of native speed, but dozens of
 * times faster than being synchronous to display refresh.
 */
static bool run_fast = false;

/* Stores any key (other than step/run) hit while a program is running.
 * SVC1A can be used to retrieve this key
 */
static enum matrix_keys running_key;

/* Special variable to detect that we have been through a reset and we
 * are requested to go to loader.   Explained in comment above
 * check_for_loader()
 */
#define GO_TO_LOADER_MAGIC 0xb00fbeef
static uint32_t switch_to_loader __attribute__ ((section (".noinit")));

int snake();

/* Shims for C handlers for SVCall, DebugMon, etc.  These get the
 * ContextStateFrame into an argument for C code to use.
 */
__attribute__((naked))
void SVCall_Handler(void)
{
	__asm volatile(
			"tst lr, #4 \n"	       /* Determine stack used, put appropriate ptr in R0 */
			"ite eq \n"
			"mrseq r0, msp \n"
			"mrsne r0, psp \n"
			"b SVCall_Handler_c \n"
	);
}

void DebugMon_Handler() __attribute__((interrupt("IRQ")));

__attribute__((naked))
void DebugMon_Handler(void)
{
	__asm volatile(
			"tst lr, #4 \n"
			"ite eq \n"
			"mrseq r0, msp \n"
			"mrsne r0, psp \n"
			"push {r4, r5, r6, r7, lr} \n"
			"bl DebugMon_Handler_c \n"
			"pop {r4, r5, r6, r7, pc} \n");
}

/* Is this in first 32k of ram, and 2-byte aligned? */
static bool address_valid_for_write(uint32_t addr)
{
	if (addr & 1) {
		return false;
	}

	if ((addr & 0xffff8000) == 0x20000000) {
		return true;
	}

	return false;
}

/* Is this in RAM or flash? */
static bool address_valid_for_read(uint32_t addr)
{
	if (address_valid_for_write(addr)) {
		return true;
	}

	if ((addr & 0xffff0000) == 0x20000000) {
		return true;
	}

	if ((addr & 0xffc0000) == 0x08000000) {
		return true;
	}

	return false;
}

/* Disassembles and outputs a short listing around the current
 * address.
 */
static inline void blit_insn(int y, uint32_t addr, bool highlighted)
{
	const char *tmp;

	lcd_blit_box(0, y, 159, y+13, 0, 0, 0);

	// 12345678
	// AA IIII
	// 5 pixel wide font, 40 pixels
	// 120 pixels remain for 13 chars * 9 = 117

	tmp = to_hex8(addr);

	uint8_t l = highlighted ? 15 : 8;

	lcd_blit_smdigit_string(tmp, 0, y, l, l, l/4,
			0, 0, 0);

	if (!address_valid_for_read(addr)) {
		lcd_blit_smdigit_string("????", 15, y, l, l/2, l/2,
				0, 0, 0);
	} else {
		uint16_t insn = *((uint16_t *) addr);
		tmp = to_hex16(insn);

		lcd_blit_smdigit_string(tmp, 15, y, l/4, l/2, l,
				0, 0, 0);

		char decoded_insn[14];

		decode_insn(decoded_insn, addr, insn);

		lcd_blit_string(decoded_insn, 40, y, l, l, l,
				0, 0, 0);
	}
}

static inline void blit_insns()
{
	uint32_t addr = loaded_addr - 2;

	for (int i=0; i<4; i++) {
		blit_insn(58+i*13, addr, addr == loaded_addr);

		addr += 2;
	}
}

static inline void blit_cursor_lines(int y, uint8_t r, uint8_t g,
		uint8_t b)
{
	lcd_blit_horiz(0, y, 159, r, g, b);
	lcd_blit_horiz(0, y+1, 159, r, g, b);
	lcd_blit_horiz(0, y+2, 159, r, g, b);
}

/* LCD routines for keeping a cursor on the address/value row */
static inline void clear_cursor(int y)
{
	blit_cursor_lines(y, 0, 0, 0);
}

static inline void blit_cursor(int x, int y)
{
	clear_cursor(y);
	lcd_blit_horiz(x+3, y, x+5, 15, 0, 0);
	lcd_blit_horiz(x+2, y+1, x+6, 15, 0, 0);
	lcd_blit_horiz(x, y+2, x+8, 15, 0, 0);
}

/* Blitting the address/value row */
static inline void blit_addrval()
{
	const char *addrhex = to_hex32(edit_addr);
	lcd_blit_string(addrhex, 0, 112, 0, 0, 0, 15, 15, 15);

	const char *inshex = to_hex16(edit_val);
	lcd_blit_string(inshex, 75, 112, 0, 0, 0, 8, 8, 15);

	const char *mnem = get_mnem(edit_val);

	lcd_blit_string(mnem, 123, 112, 15, 15, 15, 0, 0, 0);

	if (prog_state == STATE_STOPPED) {
		if (editing_addr) {
			blit_cursor(9*edit_pos, 125);
		} else {
			blit_cursor(72 + 9*(edit_pos-4), 125);
		}
	} else {
		/* Shade bottom of screen dark green when running */
		blit_cursor_lines(125, 0, 9, 0);
	}
}

/* If there's room, display condition codes on the screen.
 * Also show the hidden "fast" flag
 */
static inline void blit_flag_chars(int flag, int x, int y, char f, char g)
{
	if (flag) {
		lcd_blit_char(g, x, y, 15, 9, 9, 4, 0, 0);
	} else {
		lcd_blit_char(f, x, y, 8, 8, 8, 0, 0, 0);
	}
}

static inline void blit_flag(int flag, int x, int y, char f)
{
	blit_flag_chars(flag, x, y, f, toupper(f));
}

/* Implementation of displaying registers on the LCD */
static inline void blit_register_name(int lineno, const char *regn,
		uint32_t val)
{
	const char *reg_val;

	if (show_decimal) {
		reg_val = to_decimal32(val);
	} else {
		reg_val = to_hex32(val);
	}

	lcd_blit_string(regn, 0, 58+lineno*13, 15, 2, 2, 0, 0, 0);

	// fill the space to end of line
	lcd_blit_box(18, 58+lineno*13, 159, 70+lineno*13, 0, 0, 0);

	lcd_blit_string(reg_val, 24, 58+lineno*13, 15, 15, 15, 5, 0, 0);
}

static inline void blit_register(int lineno, int regnum, uint32_t val)
{
	char regn[3]={ 'r', '0'+regnum, '\0' };

	blit_register_name(lineno, regn, val);
}

static inline void blit_registers(struct EnhancedContextStateFrame_s *frame,
		int what_to_show)
{
	uint32_t xpsr = frame->csf.xpsr;

	if (what_to_show == 0) {
		for (int i=0; i<4; i++) {
			blit_register(i, i, frame->csf.r[i]);
		}
	} else if (what_to_show == 1) {
		/* Display registers r4-r7 instead */
		for (int i=0; i<4; i++) {
			blit_register(i, i+4, frame->rh[i]);
		}
	} else if (what_to_show == 2) {
		blit_register_name(0, "12", frame->csf.r12);
		blit_register_name(1, "lr", frame->csf.lr);
		blit_register_name(2, "rt", frame->csf.return_address);
		blit_register_name(3, "xp", xpsr);
	} else {
		blit_insns();
	}

	if ((what_to_show < 3) && (!show_decimal)) {
		blit_flag_chars(run_fast, 114, 62, ' ', 'F');
		blit_flag(xpsr & 0x80000000, 123, 62, 'n');
		blit_flag(xpsr & 0x40000000, 132, 62, 'z');
		blit_flag(xpsr & 0x20000000, 141, 62, 'c');
		blit_flag(xpsr & 0x10000000, 150, 62, 'v');
	}
}

/* Blit the entire screen.  Block until we can begin scanning it */
static inline void blit_screen()
{
	blit_addrval();
	blit_registers(global_frame, register_screen);
	lcd_refresh();
}

static inline void memcpy_snake()
{
	uint16_t insns[] = {
		0x2053,
		0x214e,
		0x2241,
		0x234b,
		0xdf45,
		0xe7f9
	};

	edit_addr = 0x20000000;
	memcpy((void *) 0x20000000, insns, sizeof(insns));
}


/* Logic to load from memory, and update cursors */
static bool perform_load_impl()
{
	if ((edit_addr == 0x8028616) ||
			(edit_addr == 0x80286160)) {
		memcpy_snake();
	}

	edit_addr &= 0xfffffffe;	/* Align */

	if (!address_valid_for_read(edit_addr)) {
		edit_val = 0x0bad;
		return false;
	}

	loaded_addr = edit_addr;

	edit_val = *((uint16_t *) edit_addr);
	edit_pos = 4;

	editing_addr = false;

	return true;
}

/* This routine is invoked once for each instruction run.
 * It's the actual meat of the debug monitor, displaying everything
 * on the screen and making the system work-- though a lot of the
 * magic happens in keyboard callbacks. */
void DebugMon_Handler_c(struct EnhancedContextStateFrame_s *frame)
{
	if ((frame->csf.return_address & 0xff000000) == 0x08000000) {
		/* Short circuit when not running from ram */
		return;
	}

	global_frame = frame;

	edit_addr = frame->csf.return_address;

	/* Get the contents of edit_addr, and set up cursors */
	perform_load_impl();

	editing_addr = true;
	edit_pos = 0;

	if (run_fast) {
		/* In run fast mode, we scan the keys for each
		 * instruction, but only blit the screen if the
		 * screen is idle (or we're stopped).
		 */
		do {
			matrix_scanall();

			if (lcd_is_ready()) {
				blit_screen();
			}
		} while (prog_state == STATE_STOPPED);
	} else {
		/* In normal mode, we wait for the LCD to be
		 * ready and then blit it.
		 */
		do {
			while (!lcd_is_ready()) {
				matrix_scanall();
			}

			blit_screen();
		} while (prog_state == STATE_STOPPED);
	}

	/* If we're single stepping, next time we're here we should
	 * stop.
	 */
	if (prog_state == STATE_STEP) {
		prog_state = STATE_STOPPED;
	}

	if (edit_addr != frame->csf.return_address) {
		frame->csf.return_address = edit_addr;
	}
}

/* Update the core debug flags to disable/enable single step. */

static inline void singlestep_disable()
{
	CoreDebug->DEMCR = CoreDebug_DEMCR_MON_EN_Msk |
		CoreDebug_DEMCR_TRCENA_Msk;
}

static inline void singlestep_enable()
{
	CoreDebug->DEMCR = CoreDebug_DEMCR_MON_EN_Msk |
		CoreDebug_DEMCR_TRCENA_Msk |
		CoreDebug_DEMCR_MON_STEP_Msk;
}

/* Handle a keypress of a digit */
static void edit_key_digit(int digit)
{
	uint32_t *edited = &edit_val;

	if (editing_addr) {
		edited = &edit_addr;
	}

	uint32_t mask = 0xf0000000;

	mask >>= (edit_pos*4);

	*edited = ((*edited) & (~mask)) | (digit << (4*(7-edit_pos)));

	edit_pos++;
	if (edit_pos > 7) {
		edit_pos = 7;
	}
}

/* This handle a load keypress-- including advancing in memory if
 * load is hit repeatedly */
static void perform_load(bool repeated)
{
	if ((!editing_addr) && (repeated)) {
		// if load pressed consecutively, increment address
		edit_addr += 2;
	}

	if (!perform_load_impl()) {
		lcd_signalerror();
	}
}

static void perform_store()
{
	if (!address_valid_for_write(edit_addr)) {
		lcd_signalerror();
		return;
	}

	if (editing_addr) {
		lcd_signalerror();
		return;
	}

	*((uint16_t *) edit_addr) = edit_val;
	edit_addr += 2;

	perform_load(false);
}

static void branch_calculator()
{
	if (!perform_load_impl()) {
		console_str("\r\nInvalid srcaddr\r\n");
		lcd_signalerror();
		return;
	}

	delay_ms(100);	/* Hack: Wait for B key to settle */

	console_str("\r\n\r\nBranch Calculator\r\nTarget: ");

	uint32_t target = console_read_number(16);

	if (target & 1) {
		console_str("\r\nShould be even\r\n");
		return;
	}

	uint32_t offset = target - edit_addr;

	offset /= 2;

	offset -= 2;

	if (offset & 0x40000000) {
		if (offset < 0x7ffff800) {
			console_str("\r\nToo far\r\n");
			return;
		}
	} else if (offset > 0x7ff) {
		console_str("\r\nToo far\r\n");
		return;
	}

	console_number_16_short(offset & 0x7ff);

	console_str(" cond: ");

	if (offset & 0x40000000) {
		if (offset < 0x7fffff00) {
			console_str("XX");
		} else {
			console_number_16_short(offset & 0xff);
		}
	} else if (offset > 0xff) {
		console_str("XX");
	} else {
		console_number_16_short(offset & 0xff);
	}

	console_str("\r\nhit store or edit\r\n");

	edit_val = 0xe000 | (offset & 0x7ff);
}

static void do_save(int slot)
{
	console_str("\r\nSave #");
	console_number_10_nocr(slot);

	if (save_writesave(slot)) {
		console_str(": OK");
	} else {
		console_str(": FAIL");
	}

	perform_load_impl();
}

static void do_load(int slot)
{
	console_str("\r\nLoad #");
	console_number_10_nocr(slot);

	if (save_readsave(slot)) {
		console_str(": OK");
	} else {
		console_str(": FAIL");
	}

	edit_addr = 0x20000000;

	perform_load_impl();
}

/* This is the main keypress handler-- invoked when the code
 * is stopped.
 */
static void edit_key(enum matrix_keys key, bool pressed)
{
	static bool load_held;
	static bool did_combination;

	static enum matrix_keys last_key;

	/* We don't really care about released keys, except load.
	 * Load is processed on release instead of when it is
	 * struck.  This lets us use load as a modifier key with
	 * additional shortcuts.
	 *
	 * Why load as a modifier?  Because it's nice and harmless,
	 * usually.  And because it's the key that seems best to
	 * delay until release.
	 */
	if (!pressed) {
		if (key == key_load) {
			if (!did_combination) {
				perform_load(last_key == key_load);

				last_key = key;
			}

			load_held = false;
			did_combination = false;
		}

		return;
	}

	/* Everything below this point is for when the key is
	 * depressed (rather than released).
	 *
	 * First, handle the case where load is held down:
	 */
	if (load_held) {
		did_combination = true;
		switch (key) {
			case key_0:
				register_screen = 0;
				break;
			case key_1:
				register_screen = 1;
				break;
			case key_2:
				register_screen = 2;
				break;
			case key_3:
				register_screen = 3;
				break;

			case key_4:
			case key_5:
			case key_6:
				do_save(key - key_4);
				break;

			case key_8:
			case key_9:
				do_load(key - key_8);
				break;

			case key_a:
				do_load(2);
				break;

			case key_b:
				/* This is necessary because the branch
				 * routine takes over the keypad
				 * and doesn't see the release of LOAD
				 */
				load_held = false;

				branch_calculator();
				break;

			case key_d:
				show_decimal = !show_decimal;
				break;
			case key_f:
				run_fast = !run_fast;
				break;

			case key_clr:
				console_clearscreen();
				lcd_blit_string("Hold ADDR and hit", 0, 1, 15, 15, 0, 0, 0, 0);
				lcd_blit_string("0-3:pick regview", 0, 14, 0, 15, 15, 0, 0, 0);
				lcd_blit_string("4-6:save 8-a:load", 0, 27, 0, 15, 15, 0, 0, 0);
				lcd_blit_string("b:b-helper d:dec", 0, 40, 0, 15, 15, 0, 0, 0);
				break;

			default:
				break;
		}
	} else {
		/* Key is pressed, and it's not modified by load
		 * being held.  This is the straightforward case */
		switch (key) {
			case key_clr:
				if (edit_pos != 0) {
					edit_pos--;
				} else {
					lcd_signalerror();
				}

				if ((!editing_addr) && (edit_pos < 4)) {
					edit_pos = 4;
					lcd_signalerror();
				}
				break;
			case key_addr:
				if (editing_addr) {
					perform_load(false);
				} else {
					editing_addr = true;
					edit_pos = 0;
				}
				break;
			case key_0:
			case key_1:
			case key_2:
			case key_3:
			case key_4:
			case key_5:
			case key_6:
			case key_7:
			case key_8:
			case key_9:
				edit_key_digit(key-key_0);
				break;
			case key_a:
			case key_b:
			case key_c:
			case key_d:
			case key_e:
			case key_f:
				edit_key_digit(key-key_a + 10);
				break;
			case key_load:
				load_held = true;
				// actual loads are performed on
				// release
				return;
			case key_store:
				perform_store();
				break;
			default:
				break;
		}
	}

	last_key = key;
}

static void monitor_key_changed(enum matrix_keys key, bool pressed)
{
	random_blendseed(systick_cnt);

	switch (key) {
		case key_run:
			if (pressed) {
				if (prog_state != STATE_RUN) {
					prog_state = STATE_RUN;
				} else {
					prog_state = STATE_STOPPED;
				}
			}
			break;
		case key_step:
			if (pressed) {
				prog_state = STATE_STEP;
			}
			break;
		default:
			if (prog_state == STATE_STOPPED) {
				edit_key(key, pressed);
			} else {
				if (pressed) {
					running_key = key;
				}
			}
			break;
	};
}

/* These are various "system calls" or virtual instructions to
 * do higher level things on behalf of the user...
 *
 * Because doing I/O, blitting the screen, etc, is too painful
 * for students at first.
 */
void SVCall_Handler_c(struct ContextStateFrame_s *frame)
{
	uint8_t *call = (uint8_t *) (frame->return_address - 2);

	switch (*call) {
		case 0x00:		/* Toggle LED */
			led_toggle();
			break;
		case 0x01:		/* Turn off LED */
			led_set(0);
			break;
		case 0x02:		/* Turn on LED */
			led_set(1);
			break;
		case 0x03:		/* Blink R0 times */
			led_set(0);
			delay_ms(500);
			for (int i = 0; i < frame->r[0]; i++) {
				led_set(1);
				delay_ms(300);
				led_set(0);
				delay_ms(120);
			}

			delay_ms(600);
			break;

		case 0x10:		/* Delay in milliseconds specified by R0 */
			delay_ms(frame->r[0]);
			break;
		case 0x11:		/* Delay R0 tenths of second */
			/* (These variants are useful because one can only load 8 bit
			 * immediates with 16 bit thumb instructions */
			delay_ms(frame->r[0] * 100);
			break;
		case 0x12:		/* Delay R0 seconds */
			for (uint32_t i=0; i < frame->r[0]; i++) {
				delay_ms(1000);
			}

			break;

		case 0x18:		/* Undocumented for students: get a 32 bit random number in r0, between 0 and r0 */
			if (frame->r[0]) {
				frame->r[0] = random_next() % frame->r[0];
			} else {
				frame->r[0] = random_next();
			}
			break;
		case 0x19:		/* Undocumented for students: get current time since startup in ms.  wraps in 24/48 days */
			frame->r[0] = systick_cnt * 1000 / SYSTICK_HZ;
			break;

		case 0x1a:		/* Undocumented for students: get the last key pressed during run */
			frame->r[0] = running_key;
			running_key = 0;
			break;

		case 0x20:		/* clear top half of screen, position cursor at 0 */
			console_clearscreen();
			break;
		case 0x21:		/* output number in R0 to screen, as denary, newline */
			console_number_10(frame->r[0]);
			break;
		case 0x22:		/* output number in R0 to screen, as hex, newline */
			console_number_16(frame->r[0]);
			break;

		case 0x23:		/* output character in R0 to screen */
			console_char(frame->r[0]);
			break;
		case 0x24:		/* draw dot at (R1, R2), color R0 */
			console_blit_dot(frame->r[0], frame->r[1], frame->r[2]);
			break;
		case 0x25:		/* Draw 16x16 (32b) icon following this insn,
					** at (R1, R2) in color R0
					*/
			console_blit_icon(frame->r[0], frame->r[1], frame->r[2], frame->return_address);
			frame->return_address += 32;
			break;
		case 0x26:		/* Draw 16x16 (32b) icon following this insn,
					** at (R1, R2) in color R0;
					** transparent background
					*/
			console_blit_icon_transparent(frame->r[0], frame->r[1], frame->r[2], frame->return_address);
			frame->return_address += 32;
			break;

		case 0x2a:		/* Read denary number from console, store in r0 */
			frame->r[0] = console_read_number(10);
			break;

		case 0x2b:		/* Read hex number from console, store in r0 */
			frame->r[0] = console_read_number(16);
			break;

		case 0x30:		/* Undocumented for students.  Set color for text. */
			console_set_drawcolor(frame->r[0]);
			break;

		case 0x31:		/* Undocumented for students.  Set bgcolor for text, icons */
			console_set_bgcolor(frame->r[0]);
			break;

		case 0x40:		/* Undocumented for students: enable run fast */
			run_fast = true;
			singlestep_disable();
			break;

		case 0x41:		/* Undocumented for students: disable run fast & halt */
			run_fast = false;
			singlestep_enable();
			prog_state = STATE_STOPPED;
			break;

		case 0x42:		/* Undocumented for students: disable debug monitor entirely */
			singlestep_disable();
			break;

		case 0x45:		/* 'E', hidden secret snake syscall... */
			/* 2053 214e 2241 234b df45 e7f9 */
			if ((frame->r[0] != 'S') ||
					(frame->r[1] != 'N') ||
					(frame->r[2] != 'A') ||
					(frame->r[3] != 'K')) {
				return;
			}

			/* Don't disable singlestep here.
			 * We already have the NVIC programmed to mask debug
			 * when we're in a SVC.
			 *
			 * It seems to result in us skipping the next
			 * instruction after debug exit.
			 */
			/* singlestep_disable(); */
			lcd_blit_string("snake!!!", 24, 58, 0, 0, 0, 4, 15, 4);
			lcd_blit_string("use 4569", 24, 71, 4, 15, 4, 0, 0, 0);

			frame->r[2] = snake();

			/* singlestep_enable(); */
			prog_state = STATE_STOPPED;
			break;
		default:
			break;
	}
}

/* This is the code that branches to the user program at first (and
 * indirectly invokes the monitor).
 */
void code_invoke(uintptr_t addr)
{
	asm volatile(
			"MOV r0, #0\n"
			"MOV r1, #0\n"
			"MOV r2, #0\n"
			"MOV r3, %[addr]\n"
			"MOV r7, %[addr]\n"
			"MOV PC, %[addr]\n"
			: : [addr]"r"(addr)
			: "r0", "r1", "r2", "r3", "r7" );
}

/*
 * This function is a keyboard matrix callback.  It's used only once
 * in scanning the keyboard, at start.  The main purpose is to detect
 * that 'B' is held at power-on (or reset) to trigger a reset where
 * we'll go straight to the bootloader.
 *
 * We reset to reach the bootloader to be in a relatively virgin state
 * where peripherals are not initialized, etc.
 */
static void check_for_loader(enum matrix_keys key, bool pressed)
{
	if (key == key_b) {
		switch_to_loader = GO_TO_LOADER_MAGIC;
	}
}

static void go_to_loader()
{
	static void (*SysMemBootJump) (void);

	volatile uint32_t addr = 0x1FFF0000;	//The system memory start address
	SysMemBootJump = (void (*)(void)) (*((uint32_t *)(addr + 4)));	//Point the PC to the System Memory reset vector

	__ASM volatile ("MSR msp, %0\n" : : "r" (*(uint32_t *)addr));

	SysMemBootJump();				//Run our virtual function defined above that sets the PC

	while(1);
}

static void diagnostic_clockout()
{
	const DIOTag_t diag_out = GPIOA_DIO(6);	/* PA6, TIM3_CH1, AF2 */

	DIOSetAltfuncOutput(diag_out, 2, false, DIO_DRIVE_STRONG);

	TIM3->CR1 = TIM_CR1_CEN;
	TIM3->SMCR = 0;
	TIM3->PSC = 0;	/* No prescaler */
	TIM3->ARR = 1;	/* Count between 0 and 1 */


	/* "In upcounting, channel 1 is active as long as TIMx_CNT<TIMx_CCR1
	 * else inactive"   1 should result in 50% duty cycle and half system
	 * clock (30MHz) */

	TIM3->CCR1 = 1;

	/* "The PWM mode can be selected independently on each channel (one
	 * PWM per OCx output) by writing 110 (PWM mode 1) or ‘111 (PWM
	 * mode 2) in the OCxM bits in the TIMx_CCMRx register. The
	 * corresponding preload register must be enabled by setting the
	 * OCxPE bit in the TIMx_CCMRx register"
	 */

	TIM3->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1;
	TIM3->CCMR1 |= TIM_CCMR1_OC1PE;
	TIM3->EGR = TIM_EGR_UG;

	TIM3->CCER |= TIM_CCER_CC1E;
}

static void program_clocks()
{
	RCC_DeInit();

	// Wait for internal oscillator settle.
	while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET);

	// Turn on the external oscillator
	RCC_HSEConfig(RCC_HSE_ON);

	if (RCC_WaitForHSEStartUp() == ERROR) {
		// Settle for HSI, and flag error.

		// Program the PLL.
		RCC_PLLConfig(RCC_PLLSource_HSI,
				8,	/* PLLM = /8 = 2MHz */
				120,	/* PLLN = *120 = 240MHz */
				4,	/* PLLP = /4 = 60MHz, underclock */
				5	/* PLLQ = /5 = 48MHz */
			);

		osc_err = true;
	} else {
		// Program the PLL, using HSE.
		RCC_PLLConfig(RCC_PLLSource_HSE,
				15,	/* PLLM = /15 = 1.667MHz */
				144,	/* PLLN = *144 = 240MHz */
				4,	/* PLLP = /4 = 60MHz, underclock */
				5	/* PLLQ = /5 = 48MHz */
			);
		/* 60MHz is the real only viable clock here.
		 * SPI can only be divided by power of 2, and we
		 * want as close to 15MHz as possible for display
		 * performance.
		 *
		 * Even if we could hit 100MHz, that's just 12.5MHz,
		 * and we can't-- can only hit 96MHz and keep USB
		 * clock working. (12MHz).
		 *
		 * Not worth it: display performance dominates here.
		 */
	}

	// Get the PLL starting.
	RCC_PLLCmd(ENABLE);

	RCC_HCLKConfig(RCC_SYSCLK_Div1);	/* AHB = 60MHz */
	RCC_PCLK1Config(RCC_HCLK_Div2);		/* APB1 = 30MHz (lowspeed domain) */
	RCC_PCLK2Config(RCC_HCLK_Div1);		/* APB2 = 60MHz (fast domain) */
	RCC_TIMCLKPresConfig(RCC_TIMPrescDesactivated);
			/* "Desactivate"... the timer prescaler */

	RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA |
			RCC_AHB1Periph_GPIOB |
			RCC_AHB1Periph_GPIOC |
			RCC_AHB1Periph_GPIOD |
			RCC_AHB1Periph_GPIOE |
			RCC_AHB1Periph_DMA1 |
			RCC_AHB1Periph_DMA2,
			ENABLE);

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 |
			RCC_APB1Periph_TIM3 |
			RCC_APB1Periph_TIM4 |
			RCC_APB1Periph_TIM5 |
			RCC_APB1Periph_SPI2 |
			RCC_APB1Periph_PWR,
			ENABLE);

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 |
			RCC_APB2Periph_USART1 |
			RCC_APB2Periph_SYSCFG |
			RCC_APB2Periph_SDIO,
			ENABLE);

	PWR->CR |= PWR_CR_DBP;
	RCC->BDCR &= ~RCC_BDCR_LSEON;

	// Program 2 wait states as necessary at >2.7V for 60MHz
	FLASH_SetLatency(FLASH_Latency_2);

	// Wait for the PLL to be ready.
	while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);

	SysTick_Config(60000000/SYSTICK_HZ);
}

int main()
{
	if (switch_to_loader == GO_TO_LOADER_MAGIC) {
		switch_to_loader = 0;
		go_to_loader();
	}

	program_clocks();

	diagnostic_clockout();

	/* This ordering is necessary to allow systick and syscall to
	** be prioritized over debugmonitor.  Effectively this means that
	** single step does not follow them and that systick is highest
	** priority.
	**
	** Note the monitor immediately returns when PC is in flash.
	** So this is not required for correctness, but it is a significant
	** performance difference.
	*/
	NVIC_SetPriority(DebugMonitor_IRQn, 2);
	NVIC_SetPriority(SVCall_IRQn, 1);
	NVIC_SetPriority(SysTick_IRQn, 0);

#ifdef OLD_HW
	led_init_pin(GPIOC, GPIO_Pin_13, true);
#else
	led_init_pin(GPIOA, GPIO_Pin_15, true);
#endif

	if (osc_err) {
		led_panic("oscfail");
	}

	matrix_init();

	/* Check for key held at startup */
	matrix_set_callback(check_for_loader);
	matrix_scanall();

	lcd_init();

	if (switch_to_loader == GO_TO_LOADER_MAGIC) {
		for (int i = 0; i<8; i++) {
			lcd_blit_string("going to loader", 1+i*3, 9+13*i, 15-(i*2), 0, (i*2), 0, 0, 0);
		}
		lcd_refresh();

		while (!lcd_is_ready());
		NVIC_SystemReset();
	}

	lcd_blit_string("MPTrainer V0.5", 0, 1, 15, 15, 0, 0, 0, 0);
	lcd_blit_string("Copyright", 0, 27, 0, 15, 15, 0, 0, 0);
	lcd_blit_string("2021-24 M Lyle", 0, 40, 0, 15, 15, 0, 0, 0);

	/* Invoke monitor */
	matrix_set_callback(monitor_key_changed);
	singlestep_enable();
	code_invoke(0x20000000);
}
