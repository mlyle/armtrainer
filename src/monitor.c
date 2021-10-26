#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <led.h>
#include <lcdutil.h>
#include <stringutil.h>

#include <armdio.h>

#include <stm32f4xx_rcc.h>
#include <systick_handler.h>

#include <delayutil.h>
#include <matrix.h>

#include <ctype.h>

#ifndef MIN
#define MIN(a,b) \
	({ __typeof__ (a) _a = (a); \
	 __typeof__ (b) _b = (b); \
	 _a < _b ? _a : _b; })
#endif

static bool osc_err = false;

#define NELEMENTS(x) (sizeof(x) / sizeof(*(x)))

struct __attribute__((packed)) ContextStateFrame_s {
	uint32_t r[4];
	uint32_t r12;
	uint32_t lr;
	uint32_t return_address;
	uint32_t xpsr;
};

/* Single step state machine */
enum progrun_state {
	STATE_STOPPED,
	STATE_STEP,
	STATE_RUN
} prog_state;

/* State machine for keypad editing addresses/values */
bool editing_addr = true;
uint8_t edit_pos = 0;
uint32_t edit_addr = 0;
uint32_t edit_val = 0;

uint8_t draw_column;
uint8_t draw_r = 0x0f, draw_g = 0x0f, draw_b = 0x0f;
uint8_t draw_bg_r, draw_bg_g, draw_bg_b;

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
			"b DebugMon_Handler_c \n");
}

static inline void blit_cursor(int x, int y)
{
	lcd_blit_horiz(0, y, 159, 0, 0, 0);
	lcd_blit_horiz(0, y+1, 159, 0, 0, 0);
	lcd_blit_horiz(0, y+2, 159, 0, 0, 0);
	lcd_blit_horiz(x+3, y, x+5, 15, 0, 0);
	lcd_blit_horiz(x+2, y+1, x+6, 15, 0, 0);
	lcd_blit_horiz(x, y+2, x+8, 15, 0, 0);
}

static inline void blit_addrval()
{
	char *addrhex = to_hex32(edit_addr);

	lcd_blit_string(addrhex, 0, 112, 0, 0, 0, 15, 15, 15);
	char *inshex = to_hex16(edit_val);
	lcd_blit_string(inshex, 75, 112, 0, 0, 0, 8, 8, 15);

	char *mnem = get_mnem(edit_val);

	lcd_blit_string(mnem, 123, 112, 15, 15, 15, 0, 0, 0);

	if (editing_addr) {
		blit_cursor(9*edit_pos, 125);
	} else {
		blit_cursor(72 + 9*(edit_pos-4), 125);
	}
}

static inline void blit_screen()
{
	blit_addrval();
	lcd_refresh();
}

static inline void blit_flag(int flag, int x, int y, char f)
{
	if (flag) {
		lcd_blit_char(toupper(f), x, y, 15, 9, 9, 4, 0, 0);
	} else {
		lcd_blit_char(tolower(f), x, y, 8, 8, 8, 0, 0, 0);
	}
}

static inline void blit_registers(struct ContextStateFrame_s *frame)
{
	for (int i=0; i<4; i++) {
		char regn[3]={ 'r', '0'+i, '\0' };
		char *reg_val = to_hex32(frame->r[i]);

		lcd_blit_string(regn, 0, 58+i*13, 15, 2, 2, 0, 0, 0);
		lcd_blit_string(reg_val, 24, 58+i*13, 15, 15, 15, 5, 0 ,0);
	}

	blit_flag(frame->xpsr & 0x80000000, 123, 62, 'n');
	blit_flag(frame->xpsr & 0x40000000, 132, 62, 'z');
	blit_flag(frame->xpsr & 0x20000000, 141, 62, 'c');
	blit_flag(frame->xpsr & 0x10000000, 150, 62, 'v');
}

void DebugMon_Handler_c(struct ContextStateFrame_s *frame)
{
	if ((frame->return_address & 0x2FFF0000) != 0x20000000) {
		return;
	}

	edit_addr = frame->return_address;
	edit_val = *((uint16_t *) edit_addr);

	editing_addr = true;
	edit_pos = 0;

	blit_screen();
	blit_registers(frame);
	lcd_refresh();

	do {
		matrix_scanall();
	} while (prog_state == STATE_STOPPED);

	if (prog_state == STATE_STEP) {
		prog_state = STATE_STOPPED;
	}

	if (edit_addr != frame->return_address) {
		frame->return_address = edit_addr;
	}
}


#define NUMCOL 17

void console_bs()
{
	if (draw_column >= 1)
	{
		draw_column--;
	}
}

void console_cr()
{
	draw_column = 0;
}

void console_nl()
{
	lcd_move_up(13, 53, 3, 3, 3);
}

void console_char(uint8_t c)
{
	switch (c)
	{
		case 10:
			console_nl();
			break;
		case 13:
			console_cr();
			break;
		case 127:
		case 8:
			console_bs();
			break;
		case 7: /* bell */
			lcd_signalerror();
			break;
		default:
			if (draw_column >= NUMCOL) {
				console_cr();
				console_nl();
			}

			lcd_blit_char(c, draw_column*9, 40, draw_r, draw_g, draw_b,
				draw_bg_r, draw_bg_g, draw_bg_b);
			draw_column++;

			break;
	}
}

void console_number_10(uint32_t n)
{
	console_cr();
	console_nl();

	char buf[11];

	char *c = buf+10;
	*c = 0;

	while (n) {
		c--;
		*c = (n % 10) + '0';
		n /= 10;
	}

	while (*c) {
		console_char(*c);
		c++;
	}

	lcd_refresh();
}

void console_number_16(uint32_t n)
{
	console_cr();
	console_nl();

	char *str = to_hex32(n);
	while (*str) {
		console_char(*str);
		str++;
	}

	lcd_refresh();
}

static inline void color_8bit_to_12bit(uint32_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
	*r = (color & 0xc0) >> 4;   /* RR00 0000 -> RR00 */
	*g = (color & 0x31) >> 2;   /* 00GG G000 -> GGG0 */
	*b = (color & 0x7) << 1;	/* 0000 0BBB -> BBB0 */
}

void blit_dot(uint32_t color, uint8_t x, uint8_t y)
{
	uint8_t r, g, b;
	
	color_8bit_to_12bit(color, &r, &g, &b);

	lcd_blit(x, y, r, g, b);
}

void blit_icon(uint32_t color, uint8_t x, uint8_t y, uint32_t ret_addr) {
	uint16_t *lines = (uint16_t *)ret_addr;

	uint8_t r, g, b;
	
	color_8bit_to_12bit(color, &r, &g, &b);
	for (int i = 0; i < 16; i++) {
		uint16_t tmp = lines[i];

		for (int j = 0; j < 16; j++) {
			if (tmp & 0x8000) {
				lcd_blit(x+j, y+i, r, g, b);
			} else {
				lcd_blit(x+j, y+i, draw_bg_r, draw_bg_g, draw_bg_b);
			}

			tmp <<= 1;
		}
	}
}

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

static bool address_valid_for_write()
{
	if (edit_addr & 1) {
		return false;
	}

	if ((edit_addr & 0xffff8000) == 0x20000000) {
		return true;
	}

	return false;
}

static bool address_valid_for_read()
{
	if (address_valid_for_write()) {
		return true;
	}


	if ((edit_addr & 0xffff0000) == 0x20000000) {
		return true;
	}

	if ((edit_addr & 0xffc0000) == 0x08000000) {
		return true;
	}

	return false;
}

static void perform_load(bool repeated)
{
	edit_addr &= 0xfffffffe;	/* Align */

	if ((!editing_addr) && (repeated)) {
		// if load pressed consecutively, increment address
		edit_addr += 2;
	}

	if (!address_valid_for_read()) {
		lcd_signalerror();

		return;
	}

	edit_val = *((uint16_t *) edit_addr);
	edit_pos = 4;

	editing_addr = false;
}

static void perform_store()
{
	if (!address_valid_for_write()) {
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

static void edit_key(enum matrix_keys key)
{
	static enum matrix_keys last_key;

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
			perform_load(last_key == key_load);
			break;
		case key_store:
			perform_store();
			break;
		default:
			break;
	}

	blit_screen();

	last_key = key;
}

void matrix_key_changed(enum matrix_keys key, bool pressed)
{
	if (pressed) {
		switch (key) {
			case key_run:
				if (prog_state != STATE_RUN) {
					prog_state = STATE_RUN;
				} else {
					prog_state = STATE_STOPPED;
				}
				break;
			case key_step:
				prog_state = STATE_STEP;
				break;
			default:
				if (prog_state == STATE_STOPPED) {
					edit_key(key);
				}
				break;
		};
	}
}

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

		case 0x20:		/* clear top half of screen, position cursor at 0 */
			lcd_blit_rows(0, 53, 0, 0, 0);
			draw_column = 0;
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
			blit_dot(frame->r[0], frame->r[1], frame->r[2]);
			break;
		case 0x25:		/* Draw 16x16 (32b) icon following this insn,
						** at (R1, R2) in color R0
						*/
			blit_icon(frame->r[0], frame->r[1], frame->r[2], frame->return_address);
			frame->return_address += 32;
			break;
		
		case 0x30:		/* Undocumented for students.  Set color for text. */
			color_8bit_to_12bit(frame->r[0], &draw_r, &draw_g, &draw_b);
			break;

		case 0x31:		/* Undocumented for students.  Set bgcolor for text, icons */
			color_8bit_to_12bit(frame->r[0], &draw_bg_r, &draw_bg_g, &draw_bg_b);
			break;

		default:
			break;
	}
}

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

int main()
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
				144,	/* PLLN = *96 = 240MHz */
				4,	/* PLLP = /4 = 60MHz, underclock */
				5	/* PLLQ = /5 = 48MHz */
			);
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

	SysTick_Config(60000000/200);	/* 200Hz systick */

	/* This ordering is necessary to allow systick and syscall to
	** be prioritized over debugmonitor.  Effectively this means that
	** single step does not follow them and that systick is highest
	** priority.
	*/
	NVIC_SetPriority(DebugMonitor_IRQn, 2);
	NVIC_SetPriority(SVCall_IRQn, 1);
	NVIC_SetPriority(SysTick_IRQn, 0);

	led_init_pin(GPIOC, GPIO_Pin_13, true);

	if (osc_err) {
		led_panic("oscfail");
	}

	lcd_init();
	lcd_blit_string("MPTrainer V0.1", 0, 1, 15, 15, 0, 0, 0, 0);
	lcd_blit_string("Copyright", 0, 27, 0, 15, 15, 0, 0, 0);
	lcd_blit_string("2021   M. Lyle", 0, 40, 0, 15, 15, 0, 0, 0);
	matrix_init();

	singlestep_enable();
	code_invoke(0x20000000);
}
