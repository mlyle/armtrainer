#ifndef _DELAYUTIL_H
#define _DELAYUTIL_H

#include <systick_handler.h>

#define SYSTICK_HZ 200

static inline void delay_ms(uint32_t ms) {
	/* XXX could true this up with a calibrated delay loop */
	/* XXX or looking at the systick underlying counter.. */
	uint32_t next = systick_cnt + (ms * SYSTICK_HZ / 1000) + 1;

	while (systick_cnt < next);
}

static inline void delay_loop(uint32_t len)
{
	while (len--) {
		asm volatile("NOP\n");
	}
}

#endif
