#ifndef __ARM_DIO_H
#define __ARM_DIO_H

#include <stdint.h>
#include <stdbool.h>

#if defined(STM32F4XX)
enum DIODriveStr {
	DIO_DRIVE_WEAK = 0,
	DIO_DRIVE_LIGHT,
	DIO_DRIVE_MEDIUM,
	DIO_DRIVE_STRONG
};
#elif defined(STM32F30X) || defined (STM32F0XX)
enum DIODriveStr {
	DIO_DRIVE_WEAK = 0,
	DIO_DRIVE_LIGHT = 0,
	DIO_DRIVE_MEDIUM = 1,
	DIO_DRIVE_STRONG = 3
};
#elif defined(STM32F10X_MD)
enum DIODriveStr {
	DIO_DRIVE_WEAK = 2,	// "2MHz"
	DIO_DRIVE_LIGHT = 1,	// "10MHz"
	DIO_DRIVE_MEDIUM = 1,
	DIO_DRIVE_STRONG = 3	// "50MHz"
};
#else
#error Unknown target
#endif

enum DIOPinFunc {
	DIO_PIN_INPUT = 0,
	DIO_PIN_OUTPUT = 1,
	DIO_PIN_ALTFUNC_IN = 2,
	DIO_PIN_ALTFUNC_OUT = 3,
	DIO_PIN_ANALOG = 4
};

enum DIOPull {
	DIO_PULL_NONE = 0,
	DIO_PULL_UP,
	DIO_PULL_DOWN
};

typedef uint32_t DIOTag_t;
typedef uint64_t DIOInitTag_t;

#define DIO_NULL ((uintptr_t) 0)

/**
 * @brief Initializes a GPIO per an inittag.
 *
 * @param[in] t The DIO init tag.  Created by ORing together a GPIO
 * specifier and one of INIT_DIO_INPUT, INIT_DIO_OUTPUT, and
 * INIT_DIO_ALTFUNC_OUT.
 */
void DIOInit(DIOInitTag_t t);

/**
 * @brief Configures a GPIO as alternate function output.
 *
 * @param[in] t The GPIO specifier tag (created with e.g. GPIOA_DIO(3) )
 * @param[in] alt_func The alternate function specifier.
 * @param[in] openDrain true for open-collector + pull-up semantics
 * @param[in] DIODriveStrength The drive strength to use for the GPIO
 */
static inline void DIOSetAltfuncOutput(DIOTag_t t, int alt_func,
		bool openDrain, enum DIODriveStr strength);

/**
 * @brief Configures a GPIO as alternate function input.
 *
 * @param[in] t The GPIO specifier tag (created with e.g. GPIOA_DIO(3) )
 * @param[in] alt_func The alternate function specifier.
 * @param[in] pull Pullup/Pulldown configuration.
 */
static inline void DIOSetAltfuncInput(DIOTag_t t, int alt_func,
		enum DIOPull pull);

/**
 * @brief Configures a GPIO as analog.  Disables pullups/pulldowns, etc.
 *
 * @param[in] t The GPIO specifier tag (created with e.g. GPIOA_DIO(3) )
 */
static inline void DIOSetAnalog(DIOTag_t t);

/**
 * @brief Configures a GPIO as an output.
 * @param[in] t The GPIO specifier tag (created with e.g. GPIOA_DIO(3) )
 * @param[in] openDrain true for open-collector + pull-up semantics
 * @param[in] DIODriveStrength The drive strength to use for the GPIO
 * @param[in] firstValue Whether it should be initially driven high or low.
 */
static inline void DIOSetOutput(DIOTag_t t, bool openDrain,
		enum DIODriveStr strength, bool firstValue);

/**
 * @brief Toggles an output GPIO to the opposite level.
 * @param[in] t The GPIO specifier tag
 */
static inline void DIOToggle(DIOTag_t t);

/**
 * @brief Sets an output GPIO high.
 * @param[in] t The GPIO specifier tag
 */
static inline void DIOHigh(DIOTag_t t);

/**
 * @brief Sets an output GPIO low.
 * @param[in] t The GPIO specifier tag
 */
static inline void DIOLow(DIOTag_t t);

/**
 * @brief Sets an output GPIO to a chosen logical level.
 * @param[in] t The GPIO specifier tag
 * @param[in] high Whether the GPIO should be high.
 */
static inline void DIOWrite(DIOTag_t t, bool high);

/**
 * @brief Configures a GPIO as an input.
 * @param[in] t The GPIO specifier tag (created with e.g. GPIOA_DIO(3) )
 * @param[in] pull Pullup/Pulldown configuration.
 */
static inline void DIOSetInput(DIOTag_t t, enum DIOPull pull);

/**
 * @brief Reads a GPIO logical value.
 * @param[in] t The GPIO specifier tag
 * @retval True if the GPIO is high; otherwise low.
 */
static inline bool DIORead(DIOTag_t t);

/* Implementation crud below here */

#define DIO_BASE ( (uintptr_t) GPIOA )

#define DIO_PORT_OFFSET(port) (((uintptr_t) (port)) - DIO_BASE)

#define DIO_MAKE_TAG(port, pin) ((uintptr_t) ((DIO_PORT_OFFSET(port) << 16) | ((uint16_t) (pin))))

#define DIOSHL(a, b) (((uint64_t) (a)) << (b))

#define DIO_MAKE_INITTAG(func, pull, drive, initial, od, altFunc) ( \
		DIOSHL(func, 32) | \
		DIOSHL(pull, 36) | \
		DIOSHL(drive, 40) | \
		DIOSHL(initial, 44) | \
		DIOSHL(od, 45) | \
		DIOSHL(altFunc, 46))

#define GPIOA_DIO(pin_num) DIO_MAKE_TAG(GPIOA, 1<<(pin_num))
#define GPIOB_DIO(pin_num) DIO_MAKE_TAG(GPIOB, 1<<(pin_num))
#define GPIOC_DIO(pin_num) DIO_MAKE_TAG(GPIOC, 1<<(pin_num))
#define GPIOD_DIO(pin_num) DIO_MAKE_TAG(GPIOD, 1<<(pin_num))
#define GPIOE_DIO(pin_num) DIO_MAKE_TAG(GPIOE, 1<<(pin_num))
#define GPIOF_DIO(pin_num) DIO_MAKE_TAG(GPIOF, 1<<(pin_num))

#define INIT_DIO_INPUT(pull) \
	DIO_MAKE_INITTAG(DIO_PIN_INPUT, pull, DIO_DRIVE_WEAK, false, false, 0)
#define INIT_DIO_OUTPUT(drive, initial, od) \
	DIO_MAKE_INITTAG(DIO_PIN_OUTPUT, DIO_PULL_NONE, drive, initial, od, 0)
#define INIT_DIO_ALTFUNC_OUT(pull, drive, od, altFunc) \
	DIO_MAKE_INITTAG(DIO_PIN_ALTFUNC_OUT, pull, drive, false, od, altFunc)
#define INIT_DIO_ALTFUNC_IN(pull, altFunc) \
	DIO_MAKE_INITTAG(DIO_PIN_ALTFUNC_OUT, pull, DIO_DRIVE_WEAK, false, false, altFunc)

#define GET_DIO_PORT(dio) ( (GPIO_TypeDef *) (((dio) >> 16) + DIO_BASE) )

#define GET_DIO_PIN(dio)  ((dio) & 0xffff)
#define GET_DIO_FUNC(dio) (((dio) >> 32) & 0x0f)
#define GET_DIO_PULL(dio) (((dio) >> 36) & 0x0f)
#define GET_DIO_DRIVE(dio) (((dio) >> 40) & 0x0f)
#define GET_DIO_INITIAL(dio) (((dio) >> 44) & 1)
#define GET_DIO_OD(dio) (((dio) >> 45) & 1)
#define GET_DIO_ALTFUNC(dio) (((dio) >> 46) & 0x0f)

#define _DIO_PRELUDE_RET(s) \
	if (t == DIO_NULL) { return (s); } \
	GPIO_TypeDef * gp = GET_DIO_PORT(t); \
	uint16_t pin = GET_DIO_PIN(t);

#define _DIO_PRELUDE \
	if (t == DIO_NULL) { return; } \
	GPIO_TypeDef * gp = GET_DIO_PORT(t); \
	uint16_t pin = GET_DIO_PIN(t);

static inline void DIOHigh(DIOTag_t t)
{
	_DIO_PRELUDE;

	gp->BSRRL = pin;
}

static inline void DIOLow(DIOTag_t t)
{
	_DIO_PRELUDE;

	gp->BSRRH = pin;
}

static inline void DIOWrite(DIOTag_t t, bool high)
{
	if (high) {
		DIOHigh(t);
	} else {
		DIOLow(t);
	}
}

static inline void DIOToggle(DIOTag_t t)
{
	_DIO_PRELUDE;

	DIOWrite(t, ! (gp->ODR & pin) );
}

/* Some of the bitfields here are two bits wide; some are 1 bit wide */

// Uses regs twice, naughty.
#define DIO_SETREGS_FOURWIDE(reglow, reghigh, idx, val) \
	do { \
		int __pos = (idx); \
		if (__pos >= 8) { \
			__pos -= 8; \
			(reghigh) = ((reghigh) & ~(15 << (__pos * 4))) | ( (val) << (__pos * 4)); \
		} else { \
			(reglow) = ((reglow) & ~(15 << (__pos * 4))) | ( (val) << (__pos * 4)); \
		} \
	} while (0)

#define DIO_SETREG_TWOWIDE(reg, idx, val) \
	do { \
		int __pos = (idx); \
		(reg) = ((reg) & ~(3 << (__pos * 2))) | ( (val) << (__pos * 2)); \
	} while (0)

#define DIO_SETREG_ONEWIDE(reg, idx, val) \
	do { \
		int __pos = (idx); \
		(reg) = ((reg) & ~(1 << (__pos))) | ( (val) << __pos); \
	} while (0)

/* Note all of these configuration things have atomicity problems---
 * like underlying stdperiph.  Really all of these read-modify-write things
 * would need to disable interrupts to be safe.
 */
#if defined(STM32F10X_MD)
static inline void DIORemap(int alt_func)
{
	if (alt_func) {
		 GPIO_PinRemapConfig(alt_func, ENABLE);
	}
}
#endif

enum DIOSTMPinFunc {
	DIO_STMPIN_INPUT = 0,
	DIO_STMPIN_OUTPUT = 1,
	DIO_STMPIN_ALTFUNC = 2,
	DIO_STMPIN_ANALOG = 3
};

static inline void DIOSetAltfuncOutput(DIOTag_t t, int alt_func,
		bool openDrain, enum DIODriveStr strength)
{
	_DIO_PRELUDE;

	uint8_t pos = __builtin_ctz(pin);

#if defined(STM32F10X_MD)
	_dio_remap(alt_func);

	/* MODE .. CNF = 1cSS
	 * where S is strength and c is true for open collector
	 */
	uint8_t val = 8 | strength;

	if (openDrain) {
		val |= 4;
	}

	DIO_SETREGS_FOURWIDE(gp->CRL, gp->CRH, pos, val);
#else
	DIOSetOutput(t, openDrain, strength, false);

	DIO_SETREGS_FOURWIDE(gp->AFR[0], gp->AFR[1], pos, alt_func);
	DIO_SETREG_TWOWIDE(gp->MODER, pos, DIO_STMPIN_ALTFUNC);
#endif
}

static inline void DIOSetAltfuncInput(DIOTag_t t, int alt_func,
		enum DIOPull pull)
{
#if defined(STM32F10X_MD)
	DIORemap(alt_func);
#endif

	DIOSetInput(t, pull);

	/* F1 altfunc inputs are just normal inputs */

#if !defined(STM32F10X_MD)
	_DIO_PRELUDE;

	uint8_t pos = __builtin_ctz(pin);

	DIO_SETREGS_FOURWIDE(gp->AFR[0], gp->AFR[1], pos, alt_func);
	DIO_SETREG_TWOWIDE(gp->MODER, pos, DIO_STMPIN_ALTFUNC);
#endif
}

static inline void DIOSetAnalog(DIOTag_t t)
{
	_DIO_PRELUDE;
	uint8_t pos = __builtin_ctz(pin);
#if defined(STM32F10X_MD)
	/* 0000 Analog input */
	DIO_SETREGS_FOURWIDE(gp->CRL, gp->CRH, pos, 0);
#else
	DIO_SETREG_TWOWIDE(gp->PUPDR, pos, DIO_PULL_NONE);

	DIO_SETREG_TWOWIDE(gp->MODER, pos, DIO_STMPIN_ANALOG);
#endif
}

static inline void DIOSetOutput(DIOTag_t t, bool openDrain,
		enum DIODriveStr strength, bool firstValue)
{
	_DIO_PRELUDE;

	uint8_t pos = __builtin_ctz(pin);

	/* Set output data register to first alue */
	DIOWrite(t, firstValue);
#if defined(STM32F10X_MD)
	/* MODE .. CNF = 0cSS
	 * where S is strength and c is true for open collector
	 */
	uint8_t val = strength;

	if (openDrain) {
		val |= 4;
	}

	DIO_SETREGS_FOURWIDE(gp->CRL, gp->CRH, pos, val);
#else

	if (openDrain) {
		/* Request pullup */
		DIO_SETREG_TWOWIDE(gp->PUPDR, pos, DIO_PULL_UP);
		/* Set as open drain output type */
		DIO_SETREG_ONEWIDE(gp->OTYPER, pos, 1);
	} else {
		/* Request no pullup / pulldown */
		DIO_SETREG_TWOWIDE(gp->PUPDR, pos, DIO_PULL_NONE);

		/* Set as normal output */
		DIO_SETREG_ONEWIDE(gp->OTYPER, pos, 0);
	}

	DIO_SETREG_TWOWIDE(gp->OSPEEDR, pos, strength);

	/* Set appropriate position to output */
	DIO_SETREG_TWOWIDE(gp->MODER, pos, DIO_STMPIN_OUTPUT);
#endif
}

static inline void DIOSetInput(DIOTag_t t, enum DIOPull pull)
{
	_DIO_PRELUDE;

	uint8_t pos = __builtin_ctz(pin);

#if defined(STM32F10X_MD)
	uint8_t val;

	if (pull == DIO_PULL_NONE) {
		val = 4;	/* 0100 input floating */
	} else {
		val = 8;	/* 1000 input pulled */
	}

	DIO_SETREGS_FOURWIDE(gp->CRL, gp->CRH, pos, val);

	if (pull == DIO_PULL_UP) {
		dio_high(t);
	} else if (pull == DIO_PULL_DOWN) {
		dio_low(t);
	}
#else
	/* Set caller-specified pullup/pulldown */
	DIO_SETREG_TWOWIDE(gp->PUPDR, pos, pull);

	/* Set appropriate position to input */
	DIO_SETREG_TWOWIDE(gp->MODER, pos, DIO_STMPIN_INPUT);
#endif
}

static inline bool DIORead(DIOTag_t t)
{
	_DIO_PRELUDE_RET(false);

	return !! (gp->IDR & pin);
}

#endif /* __ARM_DIO_H */
