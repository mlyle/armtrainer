// Startup code and interrupt vector table.
//
// Copyright (c) 2016, 2021 Michael Lyle
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <string.h>
#include <strings.h>
#include <stm32f4xx.h>

extern char _sbss, _ebss, _sidata, _sdata, _edata, _stack_top,
		_start_vectors, _suserram, _euserram;

typedef const void (vector)(void);

extern int main(void);

void _start(void) __attribute__((noreturn, naked, no_instrument_function));

void _start(void)
{
	SCB->VTOR = (uintptr_t) &_start_vectors;

	/* Copy data segment */
	memcpy(&_sdata, &_sidata, &_edata - &_sdata);

	/* Zero BSS segment */
	bzero(&_sbss, &_ebss - &_sbss);

	/* Zero "user ram" */
	bzero(&_suserram, &_euserram - &_suserram);

	/* We don't have CCM-SRAM, but why limit ourselves */
	/* bzero(&_sfast, &_efast - &_sfast); */

	// XXX GNU-ARM-Eclipse-QEMU can't handle this
#if 0
	/* FPU interrupt handling: lazy save of FP state */
	FPU->FPCCR |= FPU_FPCCR_ASPEN_Msk | FPU_FPCCR_LSPEN_Msk;

	/* Default NaN mode (don't propagate; just make NaN) and
	 * flush-to-zero handling for subnormals
	 */
	FPU->FPDSCR |= FPU_FPDSCR_DN_Msk | FPU_FPDSCR_FZ_Msk;

	/* Enable the FPU in the coprocessor state register; CP10/11 */
	SCB->CPACR |= 0x00f00000;
#endif

	/* Invoke main. */
	asm volatile ("bl	main");
	__builtin_unreachable();
}

void SVCall_Handler() __attribute__((weak));
void DebugMon_Handler() __attribute__((weak));

/* This ends one early, so other code can provide systick handler */
void * const _vectors [] __attribute((section(".vectors"))) =
{
	&_stack_top,
	_start,
	DebugMon_Handler,      /* XXX: NMI_Handler */
	DebugMon_Handler,      /* XXX: HardFault_Handler */
	DebugMon_Handler,      /* XXX: MemManage_Handler */
	DebugMon_Handler,      /* XXX: BusFault_Handler */
	DebugMon_Handler,      /* XXX: UsageFault_Handler */
	0,      /* 4 reserved */
	0,
	0,
	0,
	SVCall_Handler,      /* SVCall */
	DebugMon_Handler,    /* DebugMon */
	0,      /* Reserved */
	0,      /* PendSV */
};
