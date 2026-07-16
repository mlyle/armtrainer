#include <stdbool.h>
#include <stdint.h>

#include <armdio.h>
#include <console.h>
#include <delayutil.h>
#include <lcdutil.h>
#include <led.h>
#include <matrix.h>
#include <trainer_core.h>
#include <trainer_platform.h>

#include <stm32f4xx_rcc.h>
#include <systick_handler.h>

/*
 * STM32 adapter for the shared trainer core. Exception entry, debug control,
 * clocks, and bootloader transfer remain deliberately embedded-only.
 */

static bool osc_err;

#define GO_TO_LOADER_MAGIC UINT32_C(0xb00fbeef)
static uint32_t switch_to_loader __attribute__((section(".noinit")));

__attribute__((naked))
void SVCall_Handler(void)
{
	__asm volatile(
		"tst lr, #4 \n"
		"ite eq \n"
		"mrseq r0, msp \n"
		"mrsne r0, psp \n"
		"b SVCall_Handler_c \n");
}

void DebugMon_Handler(void) __attribute__((interrupt("IRQ")));

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

void DebugMon_Handler_c(struct EnhancedContextStateFrame_s *frame)
{
	if ((frame->csf.return_address & UINT32_C(0xff000000)) ==
			UINT32_C(0x08000000))
		return;

	trainer_core_arrive(frame);
	if (trainer_core_fast_mode()) {
		do {
			matrix_scanall();
			if (lcd_is_ready()) trainer_core_render();
		} while (trainer_core_run_state() == STATE_STOPPED);
	} else {
		do {
			while (!lcd_is_ready()) matrix_scanall();
			trainer_core_render();
		} while (trainer_core_run_state() == STATE_STOPPED);
	}
	trainer_core_instruction_complete(frame);
}

void trainer_platform_single_step(bool enabled)
{
	CoreDebug->DEMCR = CoreDebug_DEMCR_MON_EN_Msk |
		CoreDebug_DEMCR_TRCENA_Msk |
		(enabled ? CoreDebug_DEMCR_MON_STEP_Msk : 0);
}

void SVCall_Handler_c(struct ContextStateFrame_s *frame)
{
	uint8_t call = *(const uint8_t *)(uintptr_t)(frame->return_address - 2);
	enum trainer_svc_result result = trainer_core_svc(frame, call);
	if (result == TRAINER_SVC_INPUT_DECIMAL)
		frame->r[0] = console_read_number(10);
	else if (result == TRAINER_SVC_INPUT_HEX)
		frame->r[0] = console_read_number(16);
}

void code_invoke(uintptr_t address)
{
	asm volatile(
		"MOV r0, #0\n"
		"MOV r1, #0\n"
		"MOV r2, #0\n"
		"MOV r3, %[addr]\n"
		"MOV r7, %[addr]\n"
		"MOV PC, %[addr]\n"
		: : [addr]"r"(address)
		: "r0", "r1", "r2", "r3", "r7");
}

static void check_for_loader(enum matrix_keys key, bool pressed)
{
	(void)pressed;
	if (key == key_b) switch_to_loader = GO_TO_LOADER_MAGIC;
}

static void go_to_loader(void)
{
	static void (*system_memory_boot)(void);
	volatile uint32_t address = UINT32_C(0x1fff0000);
	system_memory_boot = (void (*)(void))(*(uint32_t *)(address + 4));
	__ASM volatile("MSR msp, %0\n" : : "r"(*(uint32_t *)address));
	system_memory_boot();
	while (1);
}

static void diagnostic_clockout(void)
{
	const DIOTag_t diagnostic_output = GPIOA_DIO(6);
	DIOSetAltfuncOutput(diagnostic_output, 2, false, DIO_DRIVE_STRONG);

	TIM3->CR1 = TIM_CR1_CEN;
	TIM3->SMCR = 0;
	TIM3->PSC = 0;
	TIM3->ARR = 1;
	TIM3->CCR1 = 1;
	TIM3->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1;
	TIM3->CCMR1 |= TIM_CCMR1_OC1PE;
	TIM3->EGR = TIM_EGR_UG;
	TIM3->CCER |= TIM_CCER_CC1E;
}

static void program_clocks(void)
{
	RCC_DeInit();
	while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET);
	RCC_HSEConfig(RCC_HSE_ON);

	if (RCC_WaitForHSEStartUp() == ERROR) {
		RCC_PLLConfig(RCC_PLLSource_HSI, 8, 120, 4, 5);
		osc_err = true;
	} else {
		RCC_PLLConfig(RCC_PLLSource_HSE, 15, 144, 4, 5);
	}

	RCC_PLLCmd(ENABLE);
	RCC_HCLKConfig(RCC_SYSCLK_Div1);
	RCC_PCLK1Config(RCC_HCLK_Div2);
	RCC_PCLK2Config(RCC_HCLK_Div1);
	RCC_TIMCLKPresConfig(RCC_TIMPrescDesactivated);
	RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA |
		RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC |
		RCC_AHB1Periph_GPIOD | RCC_AHB1Periph_GPIOE |
		RCC_AHB1Periph_DMA1 | RCC_AHB1Periph_DMA2, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3 |
		RCC_APB1Periph_TIM4 | RCC_APB1Periph_TIM5 | RCC_APB1Periph_SPI2 |
		RCC_APB1Periph_PWR, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 | RCC_APB2Periph_USART1 |
		RCC_APB2Periph_SYSCFG | RCC_APB2Periph_SDIO, ENABLE);

	PWR->CR |= PWR_CR_DBP;
	RCC->BDCR &= ~RCC_BDCR_LSEON;
	FLASH_SetLatency(FLASH_Latency_2);
	while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
	SysTick_Config(60000000 / SYSTICK_HZ);
}

int main(void)
{
	if (switch_to_loader == GO_TO_LOADER_MAGIC) {
		switch_to_loader = 0;
		go_to_loader();
	}

	program_clocks();
	diagnostic_clockout();
	NVIC_SetPriority(DebugMonitor_IRQn, 2);
	NVIC_SetPriority(SVCall_IRQn, 1);
	NVIC_SetPriority(SysTick_IRQn, 0);

#ifdef OLD_HW
	led_init_pin(GPIOC, GPIO_Pin_13, true);
#else
	led_init_pin(GPIOA, GPIO_Pin_15, true);
#endif
	if (osc_err) led_panic("oscfail");

	matrix_init();
	matrix_set_callback(check_for_loader);
	matrix_scanall();
	lcd_init();

	if (switch_to_loader == GO_TO_LOADER_MAGIC) {
		for (int i = 0; i < 8; ++i)
			lcd_blit_string("going to loader", 1 + i * 3, 9 + 13 * i,
				15 - i * 2, 0, i * 2, 0, 0, 0);
		lcd_refresh();
		while (!lcd_is_ready());
		NVIC_SystemReset();
	}

	trainer_core_reset();
	trainer_core_splash();
	matrix_set_callback(trainer_core_key);
	trainer_platform_single_step(true);
	code_invoke(UINT32_C(0x20000000));
}
