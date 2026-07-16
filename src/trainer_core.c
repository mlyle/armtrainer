#include <trainer_core.h>

#include <console.h>
#include <lcdutil.h>
#include <stringutil.h>
#include <thumbdisasm.h>
#include <trainer_platform.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static enum progrun_state prog_state;
static bool editing_addr = true;
static uint8_t edit_pos;
static uint32_t edit_addr;
static uint32_t loaded_addr;
static uint32_t edit_val;
static int8_t save_slot = -1;
static bool unsafe_to_load;
static struct EnhancedContextStateFrame_s *global_frame;
static int register_screen;
static bool show_decimal;
static bool run_fast;
static enum matrix_keys running_key;
static bool load_held;
static bool did_combination;
static enum matrix_keys last_key;

static bool address_valid_for_write(uint32_t address)
{
	if (address & 1) return false;
	return (address & UINT32_C(0xffff8000)) == UINT32_C(0x20000000);
}

static bool address_valid_for_read(uint32_t address)
{
	if (address_valid_for_write(address)) return true;
	if ((address & UINT32_C(0xffff0000)) == UINT32_C(0x20000000))
		return true;
	return (address & UINT32_C(0x0ffc0000)) == UINT32_C(0x08000000);
}

static void blit_insn(int y, uint32_t address, bool highlighted)
{
	lcd_blit_box(0, y, 159, y + 13, 0, 0, 0);
	const char *text = to_hex8(address);
	uint8_t level = highlighted ? 15 : 8;
	lcd_blit_smdigit_string(text, 0, y, level, level, level / 4, 0, 0, 0);

	uint16_t instruction;
	if (!address_valid_for_read(address) ||
			!trainer_memory_read16(address, &instruction)) {
		lcd_blit_smdigit_string("????", 15, y, level, level / 2,
			level / 2, 0, 0, 0);
		return;
	}

	text = to_hex16(instruction);
	lcd_blit_smdigit_string(text, 15, y, level / 4, level / 2, level,
		0, 0, 0);
	char decoded[14];
	decode_insn(decoded, address, instruction);
	lcd_blit_string(decoded, 40, y, level, level, level, 0, 0, 0);
}

static void blit_insns(void)
{
	uint32_t address = loaded_addr - 2;
	for (int i = 0; i < 4; ++i) {
		blit_insn(58 + i * 13, address, address == loaded_addr);
		address += 2;
	}
}

static void blit_cursor_lines(int y, uint8_t r, uint8_t g, uint8_t b)
{
	lcd_blit_horiz(0, y, 159, r, g, b);
	lcd_blit_horiz(0, y + 1, 159, r, g, b);
	lcd_blit_horiz(0, y + 2, 159, r, g, b);
}

static void clear_cursor(int y)
{
	blit_cursor_lines(y, 0, 0, 0);
}

static void blit_cursor(int x, int y)
{
	clear_cursor(y);
	lcd_blit_horiz(x + 3, y, x + 5, 15, 0, 0);
	lcd_blit_horiz(x + 2, y + 1, x + 6, 15, 0, 0);
	lcd_blit_horiz(x, y + 2, x + 8, 15, 0, 0);
}

static void blit_addrval(void)
{
	const char *address = to_hex32(edit_addr);
	lcd_blit_string(address, 0, 112, 0, 0, 0, 15, 15, 15);
	const char *instruction = to_hex16(edit_val);
	lcd_blit_string(instruction, 75, 112, 0, 0, 0, 8, 8, 15);
	lcd_blit_string(get_mnem(edit_val), 123, 112, 15, 15, 15, 0, 0, 0);

	if (prog_state == STATE_STOPPED) {
		if (editing_addr) blit_cursor(9 * edit_pos, 125);
		else blit_cursor(72 + 9 * (edit_pos - 4), 125);
	} else {
		blit_cursor_lines(125, 0, 9, 0);
	}
}

static void blit_flag_chars(int flag, int x, int y, char lower, char upper)
{
	if (flag)
		lcd_blit_char(upper, x, y, 15, 9, 9, 4, 0, 0);
	else
		lcd_blit_char(lower, x, y, 8, 8, 8, 0, 0, 0);
}

static void blit_flag(int flag, int x, int y, char lower)
{
	blit_flag_chars(flag, x, y, lower, toupper(lower));
}

static void blit_register_name(int line, const char *name, uint32_t value)
{
	const char *text = show_decimal ? to_decimal32(value) : to_hex32(value);
	lcd_blit_string(name, 0, 58 + line * 13, 15, 2, 2, 0, 0, 0);
	lcd_blit_box(18, 58 + line * 13, 159, 70 + line * 13, 0, 0, 0);
	lcd_blit_string(text, 24, 58 + line * 13, 15, 15, 15, 5, 0, 0);
}

static void blit_register(int line, int reg, uint32_t value)
{
	char name[3] = { 'r', (char)('0' + reg), '\0' };
	blit_register_name(line, name, value);
}

static void blit_registers(struct EnhancedContextStateFrame_s *frame,
		int what_to_show)
{
	uint32_t xpsr = frame->csf.xpsr;
	if (what_to_show == 0) {
		for (int i = 0; i < 4; ++i) blit_register(i, i, frame->csf.r[i]);
	} else if (what_to_show == 1) {
		for (int i = 0; i < 4; ++i) blit_register(i, i + 4, frame->rh[i]);
	} else if (what_to_show == 2) {
		blit_register_name(0, "12", frame->csf.r12);
		blit_register_name(1, "lr", frame->csf.lr);
		blit_register_name(2, "rt", frame->csf.return_address);
		blit_register_name(3, "xp", xpsr);
	} else {
		blit_insns();
	}

	if (what_to_show < 3 && !show_decimal) {
		blit_flag_chars(run_fast, 114, 62, ' ', 'F');
		blit_flag(xpsr & UINT32_C(0x80000000), 123, 62, 'n');
		blit_flag(xpsr & UINT32_C(0x40000000), 132, 62, 'z');
		blit_flag(xpsr & UINT32_C(0x20000000), 141, 62, 'c');
		blit_flag(xpsr & UINT32_C(0x10000000), 150, 62, 'v');
	}
}

void trainer_core_render(void)
{
	if (!global_frame) return;
	blit_addrval();
	blit_registers(global_frame, register_screen);
	lcd_refresh();
}

void trainer_core_splash(void)
{
	lcd_blit_string("MPTrainer V0.5", 0, 1, 15, 15, 0, 0, 0, 0);
	lcd_blit_string("Copyright", 0, 27, 0, 15, 15, 0, 0, 0);
	lcd_blit_string("2021-26 M Lyle", 0, 40, 0, 15, 15, 0, 0, 0);
}

static void memcpy_snake(void)
{
	static const uint16_t instructions[] = {
		0x2053, 0x214e, 0x2241, 0x234b, 0xdf45, 0xe7f9,
	};
	edit_addr = UINT32_C(0x20000000);
	trainer_memory_write(edit_addr, instructions, sizeof(instructions));
}

static bool perform_load_impl(void)
{
	if (edit_addr == UINT32_C(0x08028616) ||
			edit_addr == UINT32_C(0x80286160))
		memcpy_snake();
	edit_addr &= UINT32_C(0xfffffffe);
	uint16_t instruction;
	if (!address_valid_for_read(edit_addr) ||
			!trainer_memory_read16(edit_addr, &instruction)) {
		edit_val = UINT32_C(0x0bad);
		return false;
	}
	edit_val = instruction;
	loaded_addr = edit_addr;
	edit_pos = 4;
	editing_addr = false;
	return true;
}

void trainer_core_arrive(struct EnhancedContextStateFrame_s *frame)
{
	global_frame = frame;
	edit_addr = frame->csf.return_address;
	perform_load_impl();
	editing_addr = true;
	edit_pos = 0;
}

void trainer_core_instruction_complete(struct EnhancedContextStateFrame_s *frame)
{
	if (prog_state == STATE_STEP) prog_state = STATE_STOPPED;
	if (edit_addr != frame->csf.return_address)
		frame->csf.return_address = edit_addr;
}

static void edit_key_digit(int digit)
{
	uint32_t *edited = editing_addr ? &edit_addr : &edit_val;
	uint32_t mask = UINT32_C(0xf0000000) >> (edit_pos * 4);
	*edited = (*edited & ~mask) | ((uint32_t)digit << (4 * (7 - edit_pos)));
	if (++edit_pos > 7) edit_pos = 7;
}

static void perform_load(bool repeated)
{
	if (!editing_addr && repeated) edit_addr += 2;
	if (!perform_load_impl()) lcd_signalerror();
}

static void perform_store(void)
{
	if (!address_valid_for_write(edit_addr) || editing_addr) {
		lcd_signalerror();
		return;
	}
	unsafe_to_load = true;
	if (!trainer_memory_write16(edit_addr, (uint16_t)edit_val)) {
		lcd_signalerror();
		return;
	}
	edit_addr += 2;
	perform_load(false);
}

static void branch_calculator(void)
{
	if (!perform_load_impl()) {
		console_str("\r\nInvalid srcaddr\r\n");
		lcd_signalerror();
		return;
	}
	trainer_platform_delay_ms(100);
	console_str("\r\n\r\nBranch Calculator\r\nTarget: ");
	uint32_t target = trainer_platform_read_number(16);
	if (target & 1) {
		console_str("\r\nShould be even\r\n");
		return;
	}
	uint32_t offset = (target - edit_addr) / 2 - 2;
	if (offset & UINT32_C(0x40000000)) {
		if (offset < UINT32_C(0x7ffff800)) {
			console_str("\r\nToo far\r\n");
			return;
		}
	} else if (offset > UINT32_C(0x7ff)) {
		console_str("\r\nToo far\r\n");
		return;
	}
	console_number_16_short(offset & UINT32_C(0x7ff));
	console_str(" cond: ");
	if (offset & UINT32_C(0x40000000)) {
		if (offset < UINT32_C(0x7fffff00)) console_str("XX");
		else console_number_16_short(offset & UINT32_C(0xff));
	} else if (offset > UINT32_C(0xff)) {
		console_str("XX");
	} else {
		console_number_16_short(offset & UINT32_C(0xff));
	}
	console_str("\r\nhit store or edit\r\n");
	edit_val = UINT32_C(0xe000) | (offset & UINT32_C(0x7ff));
}

static bool do_confirmation(void)
{
	console_str("\r\nType 1+ST if sure\r\n");
	if (trainer_platform_read_number(10) != 1) {
		console_str("CANCELED");
		return false;
	}
	return true;
}

static void do_save(int slot)
{
	console_str("\r\nSave #");
	console_number_10_nocr(slot);
	console_str(": ");
	if (save_slot != slot && !do_confirmation()) return;
	if (trainer_platform_save_write(slot)) {
		console_str("SAVED");
		save_slot = slot;
		unsafe_to_load = false;
	} else {
		console_str("FAIL");
	}
	perform_load_impl();
}

static void do_load(int slot)
{
	console_str("\r\nLoad #");
	console_number_10_nocr(slot);
	console_str(": ");
	if (unsafe_to_load && !do_confirmation()) return;
	if (trainer_platform_save_read(slot)) {
		console_str("LOADED");
		save_slot = slot;
		unsafe_to_load = false;
	} else {
		console_str("FAIL");
	}
	edit_addr = UINT32_C(0x20000000);
	perform_load_impl();
}

static void edit_key(enum matrix_keys key, bool pressed)
{
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

	if (load_held) {
		did_combination = true;
		switch (key) {
		case key_0: register_screen = 0; break;
		case key_1: register_screen = 1; break;
		case key_2: register_screen = 2; break;
		case key_3: register_screen = 3; break;
		case key_4:
		case key_5:
		case key_6:
			load_held = false;
			do_save(key - key_4);
			break;
		case key_8:
		case key_9:
			load_held = false;
			do_load(key - key_8);
			break;
		case key_a:
			load_held = false;
			do_load(2);
			break;
		case key_b:
			load_held = false;
			branch_calculator();
			break;
		case key_d: show_decimal = !show_decimal; break;
		case key_f: run_fast = !run_fast; break;
		case key_clr:
			console_clearscreen();
			lcd_blit_string("Hold LOAD and hit", 0, 1, 15, 15, 0, 0, 0, 0);
			lcd_blit_string("0-3:pick regview", 0, 14, 0, 15, 15, 0, 0, 0);
			lcd_blit_string("4-6:save 8-a:load", 0, 27, 0, 15, 15, 0, 0, 0);
			lcd_blit_string("b:b-helper d:dec", 0, 40, 0, 15, 15, 0, 0, 0);
			break;
		default: break;
		}
	} else {
		switch (key) {
		case key_clr:
			if (edit_pos) --edit_pos;
			else lcd_signalerror();
			if (!editing_addr && edit_pos < 4) {
				edit_pos = 4;
				lcd_signalerror();
			}
			break;
		case key_addr:
			if (editing_addr) perform_load(false);
			else {
				editing_addr = true;
				edit_pos = 0;
			}
			break;
		case key_0: case key_1: case key_2: case key_3: case key_4:
		case key_5: case key_6: case key_7: case key_8: case key_9:
			edit_key_digit(key - key_0);
			break;
		case key_a: case key_b: case key_c:
		case key_d: case key_e: case key_f:
			edit_key_digit(key - key_a + 10);
			break;
		case key_load:
			load_held = true;
			return;
		case key_store: perform_store(); break;
		default: break;
		}
	}
	last_key = key;
}

void trainer_core_key(enum matrix_keys key, bool pressed)
{
	trainer_platform_random_blend(trainer_platform_ticks());
	switch (key) {
	case key_run:
		if (pressed)
			prog_state = prog_state != STATE_RUN ? STATE_RUN : STATE_STOPPED;
		break;
	case key_step:
		if (pressed) prog_state = STATE_STEP;
		break;
	default:
		if (prog_state == STATE_STOPPED) edit_key(key, pressed);
		else if (pressed) running_key = key;
		break;
	}
}

enum trainer_svc_result trainer_core_svc(
		struct ContextStateFrame_s *frame, uint8_t call)
{
	switch (call) {
	case 0x00: trainer_platform_led_toggle(); break;
	case 0x01: trainer_platform_led_set(false); break;
	case 0x02: trainer_platform_led_set(true); break;
	case 0x03:
		trainer_platform_led_set(false);
		trainer_platform_delay_ms(500);
		for (uint32_t i = 0; i < frame->r[0]; ++i) {
			trainer_platform_led_set(true);
			trainer_platform_delay_ms(300);
			trainer_platform_led_set(false);
			trainer_platform_delay_ms(120);
		}
		trainer_platform_delay_ms(600);
		break;
	case 0x10: trainer_platform_delay_ms(frame->r[0]); break;
	case 0x11: trainer_platform_delay_ms(frame->r[0] * 100); break;
	case 0x12:
		for (uint32_t i = 0; i < frame->r[0]; ++i)
			trainer_platform_delay_ms(1000);
		break;
	case 0x18:
		if (frame->r[0])
			frame->r[0] = trainer_platform_random_next() % frame->r[0];
		else
			frame->r[0] = trainer_platform_random_next();
		break;
	case 0x19: frame->r[0] = trainer_platform_millis(); break;
	case 0x1a:
		frame->r[0] = running_key;
		running_key = 0;
		break;
	case 0x20: console_clearscreen(); break;
	case 0x21: console_number_10(frame->r[0]); break;
	case 0x22: console_number_16(frame->r[0]); break;
	case 0x23: console_char(frame->r[0]); break;
	case 0x24: console_blit_dot(frame->r[0], frame->r[1], frame->r[2]); break;
	case 0x25:
	case 0x26: {
		uint16_t lines[16];
		if (trainer_memory_read(frame->return_address, lines, sizeof(lines))) {
			console_blit_icon_data(frame->r[0], frame->r[1], frame->r[2], lines,
				call == 0x26);
			frame->return_address += sizeof(lines);
		}
		break;
	}
	case 0x2a: return TRAINER_SVC_INPUT_DECIMAL;
	case 0x2b: return TRAINER_SVC_INPUT_HEX;
	case 0x30: console_set_drawcolor(frame->r[0]); break;
	case 0x31: console_set_bgcolor(frame->r[0]); break;
	case 0x40:
		run_fast = true;
		trainer_platform_single_step(false);
		break;
	case 0x41:
		run_fast = false;
		trainer_platform_single_step(true);
		prog_state = STATE_STOPPED;
		break;
	case 0x42: trainer_platform_single_step(false); break;
	case 0x45:
		if (frame->r[0] != 'S' || frame->r[1] != 'N' ||
				frame->r[2] != 'A' || frame->r[3] != 'K')
			break;
		lcd_blit_string("snake!!!", 24, 58, 0, 0, 0, 4, 15, 4);
		lcd_blit_string("use 4569", 24, 71, 4, 15, 4, 0, 0, 0);
		frame->r[2] = trainer_platform_snake();
		prog_state = STATE_STOPPED;
		break;
	default: break;
	}
	return TRAINER_SVC_COMPLETE;
}

void trainer_core_reset(void)
{
	prog_state = STATE_STOPPED;
	editing_addr = true;
	edit_pos = 0;
	edit_addr = UINT32_C(0x20000000);
	loaded_addr = edit_addr;
	edit_val = 0;
	save_slot = -1;
	unsafe_to_load = false;
	global_frame = NULL;
	register_screen = 0;
	show_decimal = false;
	run_fast = false;
	running_key = (enum matrix_keys)0;
	load_held = false;
	did_combination = false;
	last_key = key_invalid;
}

enum progrun_state trainer_core_run_state(void) { return prog_state; }
bool trainer_core_fast_mode(void) { return run_fast; }
uint32_t trainer_core_edit_address(void) { return edit_addr; }
