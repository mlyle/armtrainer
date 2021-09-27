#define STM32F4XX

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <led.h>

#include <armdio.h>

#include <stm32f4xx_rcc.h>
#include <systick_handler.h>

#ifndef MIN
#define MIN(a,b) \
	({ __typeof__ (a) _a = (a); \
	 __typeof__ (b) _b = (b); \
	 _a < _b ? _a : _b; })
#endif

static bool osc_err = false;

#define NELEMENTS(x) (sizeof(x) / sizeof(*(x)))

struct __attribute__((packed)) ContextStateFrame_s {
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t return_address;
	uint32_t xpsr;
};

void delay_ms(uint32_t ms) {
	/* XXX could true this up with a calibrated delay loop */
	/* XXX or looking at the systick underlying counter.. */
	uint32_t next = systick_cnt + (ms / 5) + 1;

	while (systick_cnt < next);
}

void SVCall_Handler() __attribute__((interrupt("SWI")));

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



DIOTag_t matrix_outps[] = {
	GPIOA_DIO(15),
	GPIOA_DIO(12),
	GPIOA_DIO(11),
	GPIOA_DIO(10),
	GPIOA_DIO(9),
	GPIOA_DIO(8),
};

DIOTag_t matrix_inps[] = {
	GPIOB_DIO(6),
	GPIOB_DIO(5),
	GPIOB_DIO(4),
	GPIOB_DIO(3),
};

int outp_statuses[NELEMENTS(matrix_outps)];

void matrix_key_changed(int key_num, bool pressed)
{
}

void matrix_scanstep()
{
	static int cur_outp = 0;

	int inp_mask = 1;

	int outp_stat = outp_statuses[cur_outp];

	for (int i=0; i<NELEMENTS(matrix_inps); i++) {
		bool pin = DIORead(matrix_inps[i]);

		bool old_status = !!(outp_stat & inp_mask);

		if (old_status != pin) {
			matrix_key_changed(
					cur_outp * NELEMENTS(matrix_inps) + i,
					pin);

			if (pin) {
				outp_stat |= inp_mask;
			} else {
				outp_stat &= ~inp_mask;
			}
		}
	}

	outp_statuses[cur_outp] = outp_stat;

	// disable current output
	DIOSetInput(matrix_outps[cur_outp], DIO_PULL_DOWN);

	// increment curCol, wrapping around
	if ((++cur_outp) > NELEMENTS(matrix_outps)) {
		cur_outp = 0;
	}

	// set new current col as output high
	DIOSetOutput(matrix_outps[cur_outp], false, DIO_DRIVE_LIGHT, true);
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

void DelayLoop(uint32_t len)
{
	while (len--) {
		asm volatile("NOP\n");
	}
}

void DebugMon_Handler_c(struct ContextStateFrame_s *frame)
{
	if ((frame->return_address & 0x2FFF0000) != 0x20000000) {
		return;
	}

	led_set(1);
	DelayLoop( 100000);
	led_set(0);
	DelayLoop(7000000);	/* Delay half a second per insn or so */
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
			for (int i = 0; i < frame->r0; i++) {
				led_set(1);
				delay_ms(300);
				led_set(0);
				delay_ms(120);
			}

			delay_ms(600);
			break;

		case 0x10:		/* Delay in milliseconds specified by R0 */
			delay_ms(frame->r0);
			break;
		case 0x11:		/* Delay R0 tenths of second */
			/* (These variants are useful because one can only load 8 bit
			 * immediates with 16 bit thumb instructions */
			delay_ms(frame->r0 * 100);
			break;
		case 0x12:		/* Delay R0 seconds */
			for (uint32_t i=0; i < frame->r0; i++) {
				delay_ms(1000);
			}

			break;

		case 0x20:
			break;		/* XXX: clear top half of screen, position
					   cursor at 0,0 */
		case 0x21:		/* XXX: output number in R0 to screen + newline */
			break;
		case 0x22:		/* XXX: output character in R0 to screen */
			break;
		case 0x23:		/* XXX: draw white dot at (R0, R1) */
			break;
		case 0x24:		/* XXX: draw dot of color (R2, R3, R4) at (R0, R1) */
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

void EnableSingleStep() {
	CoreDebug->DEMCR = CoreDebug_DEMCR_MON_EN_Msk |
		CoreDebug_DEMCR_TRCENA_Msk |
		CoreDebug_DEMCR_MON_STEP_Msk;
}

void GoTo(uintptr_t addr) {
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
			RCC_APB1Periph_TIM5,
			ENABLE);

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 |
			RCC_APB2Periph_USART1 |
			RCC_APB2Periph_SPI1 |
			RCC_APB2Periph_SYSCFG |
			RCC_APB2Periph_SDIO,
			ENABLE);

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

	EnableSingleStep();
	GoTo((uintptr_t)myprog);
}
