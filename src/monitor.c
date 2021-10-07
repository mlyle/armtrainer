#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <led.h>
#include <lcdutil.h>

#include <armdio.h>

#include <stm32f4xx_rcc.h>
#include <systick_handler.h>

#include <delayutil.h>
#include <matrix.h>

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
#if 0
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
#endif
	uint32_t r12;
	uint32_t lr;
	uint32_t return_address;
	uint32_t xpsr;
};


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

void matrix_key_changed(enum matrix_keys key, bool pressed)
{
	static uint8_t horiz_pos = 0;

	if (pressed) {
		lcd_blit_char(key, horiz_pos, 113, 15, 0, 0, 0, 0, 0);
	} else {
		lcd_blit_char(key, horiz_pos, 113, 0, 0, 15, 0, 0, 0);
	}

	horiz_pos += 9;
	if (horiz_pos > 144) {
		horiz_pos = 0;
	}
}

static inline char to_hexdigit(uint8_t d)
{
	if (d < 10) {
		return '0' + d;
	}

	if (d < 16) {
		return d-10 + 'A';
	}

	return 0;
}

static char *to_hex32(uint32_t ptr)
{
	static char buffer[9];
	buffer[8] = 0;

	for (int i=7; i>=0; i--) {
		buffer[i] = to_hexdigit(ptr & 0xf);
		ptr >>= 4;
	}

	return buffer;
}

static char *to_hex16(uint16_t val)
{
	static char buffer[5];
	buffer[4] = 0;

	for (int i=3; i>=0; i--) {
		buffer[i] = to_hexdigit(val & 0xf);
		val >>= 4;
	}

	return buffer;
}

struct instructions {
	uint16_t mask;
	uint16_t val;
	char mnem[5];	/* with null */
} insn_list[] = {
	{ 0xf800, 0x3000, "ADDi" }, // 00110b
	{ 0xfe00, 0x1800, "ADDr" }, // 0001100
	{ 0xf800, 0xe000, "B   " },    // 11100b
	{ 0xff00, 0xdf00, "SYSc" },	// 11011111b
	{ 0xf800, 0x2000, "MOVi" }, 	// 00100b
	{ 0xff00, 0x4600, "MOVr" },	// 0100 0110b
	{ 0, 0, "" }
};


static inline void blit_addrval(uint32_t addr)
{
	char *addrhex = to_hex32(addr);

	lcd_blit_string(addrhex, 0, 114, 0, 0, 0, 15, 15, 15);

	uint16_t *insn = (uint16_t *) addr;
	char *inshex = to_hex16(*insn);
	lcd_blit_string(inshex, 72, 114, 0, 0, 0, 8, 8, 15);

	char *mnem="????";

	for (int i = 0; insn_list[i].mask; i++) {
		if ((*insn & insn_list[i].mask) == insn_list[i].val) {
			mnem = insn_list[i].mnem;
			break;
		}
	}

	lcd_blit_string(mnem, 123, 114, 15, 15, 15, 0, 0, 0);
}

static inline void blit_registers(struct ContextStateFrame_s *frame)
{
	for (int i=0; i<4; i++) {
		char regn[3]={ 'r', '0'+i, '\0' };
		char *reg_val = to_hex32(frame->r[i]);

		lcd_blit_string(regn, 0, 62+i*13, 15, 2, 2, 0, 0, 0);
		lcd_blit_string(reg_val, 24, 62+i*13, 15, 15, 15, 5, 0 ,0);
	}
}

void DebugMon_Handler_c(struct ContextStateFrame_s *frame)
{
	if ((frame->return_address & 0x2FFF0000) != 0x20000000) {
		return;
	}

	static bool skip_next = false;

	if (skip_next == true) {
		skip_next = false;
		return;
	}

	uint8_t *firstbyte = (uint8_t *) (frame->return_address + 1);
	if (*firstbyte == 0xdf) {
		skip_next = true;
		/* Skip return from syscall */
	}

	blit_addrval(frame->return_address);
	blit_registers(frame);

	led_set(1);
	delay_loop( 100000);
	led_set(0);
	delay_loop(7000000);	/* Delay half a second per insn or so */
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

		case 0x20:
			break;		/* XXX: clear top half of screen, position
					   cursor at 0,0 */
		case 0x21:		/* XXX: output number in R0 to screen, as denary, newline */
			break;
		case 0x22:		/* XXX: output character in R0 to screen */
			break;
		case 0x23:		/* XXX: draw white dot at (R0, R1) */
			break;
		case 0x24:		/* XXX: draw black dot at (R0, R1) */
			break;

		default:
			break;
	}
}

#if 0
uint16_t myprog[] =
	{
		0x467f,			/* Load current PC value(+4) to R7 */
		0xe003,			/* Branch forward 4 halfwords of instructions */
		0xffff,			/* Literal pool -- R7 + 0 */
		0x0040,			// byte order is confusing for students, praps avoid literals
		0xffff,			/* Literal pool -- R7 + 2*/
		0x0008,
		0x6838,			/* BEGIN: load R0 from [R7 + 0] */
		0x3801,			/* LOOP: Subtract 1 from R0 */
		0xd1fd,			/* Branch if not zero to previous instruction (LOOP) */
		0xdf01,			/* syscall 1: turn off LED */
		0x6878,			/* load R0 from [R7 + 2] */
		0x3801,			/* LOOP2: Subtract 1 from R0 */
		0xd1fd,			/* Branch if not zero to previous instruction (LOOP2) */
		0xdf02,			/* syscall 0: turn on LED */
		0xe7f6,			/* Branch back to BEGIN */
	};
#endif

#if 0
uint16_t myprog[] =
	{
		0xdf00,			/* syscall 0x00: toggle LED */
		0x2004,			/* load 0x4 (4) into register R0 */
		0xdf11,			/* syscall 0x11: Delay r0 (0.4s) */
		0xe7fb,			/* go back to the first syscall */
	};
#endif

uint16_t myprog[] =
	{
		0x2001,			/* R0 = 1 */
		0x2101,			/* R1 = 1 */
		0xdf03,			/* LOOPBEGIN: Syscall: Blink R0 times */
		0x180A,			/* R2 = R0 + R1 */
		0x4608,			/* R0=R1 */
		0x4611,			/* R1=R2 */
		0xe7fa,			/* Goto 4 instructions before this one */
	};



#if 0
void SVCall_Handler_c() {
	led_toggle();
}
#endif

void EnableSingleStep()
{
	CoreDebug->DEMCR = CoreDebug_DEMCR_MON_EN_Msk |
		CoreDebug_DEMCR_TRCENA_Msk |
		CoreDebug_DEMCR_MON_STEP_Msk;
}

void GoTo(uintptr_t addr)
{
	asm volatile("MOV PC, %[addr]\n" : : [addr]"r"(addr) );
}


int main() {
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

	NVIC_SetPriority(SVCall_IRQn, 3);
	NVIC_SetPriority(SysTick_IRQn, 0);

	led_init_pin(GPIOC, GPIO_Pin_13, true);

	if (osc_err) {
		led_panic("oscfail");
	}

#if 0
	led_panic("ohno");
#endif

	lcd_init();
	matrix_init();

#if 0
	while(1) {
		matrix_scanstep();
	}
#endif

	EnableSingleStep();
	GoTo((uintptr_t)myprog);
}
