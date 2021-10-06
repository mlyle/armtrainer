ARM_SDK_PREFIX := arm-none-eabi-

INC :=
INC += inc
INC += libs/STM32F4xx_StdPeriph_Driver/inc
INC += libs/fatfs
INC += libs/inc

BUILD_DIR := build

STDPERIPH_SRC :=
STDPERIPH_SRC += stm32f4xx_adc.c
STDPERIPH_SRC += stm32f4xx_dma.c
STDPERIPH_SRC += stm32f4xx_flash.c
STDPERIPH_SRC += stm32f4xx_gpio.c
STDPERIPH_SRC += stm32f4xx_iwdg.c
STDPERIPH_SRC += stm32f4xx_pwr.c
STDPERIPH_SRC += stm32f4xx_rcc.c
STDPERIPH_SRC += stm32f4xx_sdio.c
STDPERIPH_SRC += stm32f4xx_spi.c
STDPERIPH_SRC += stm32f4xx_tim.c
STDPERIPH_SRC += stm32f4xx_usart.c
STDPERIPH_SRC += misc.c

STDPERIPH_SRC := $(patsubst %,libs/STM32F4xx_StdPeriph_Driver/src/%,$(STDPERIPH_SRC))
FATFS_SRC := $(wildcard libs/fatfs/*.c)
OTHERLIB_SRC := $(wildcard libs/src/*.c)
SHARED_SRC := $(wildcard shared/*.c) $(OTHERLIB_SRC) $(FATFS_SRC) $(STDPERIPH_SRC)
MONITOR_SRC := $(wildcard src/*.c)

SRC := $(SHARED_SRC) $(MONITOR_SRC)
OBJ := $(patsubst %.c,build/%.o,$(SRC))

OBJ_FORCE :=

ifeq ("$(STACK_USAGE)","")
    CCACHE_BIN := $(shell which ccache 2>/dev/null)
endif

CC := $(CCACHE_BIN) $(ARM_SDK_PREFIX)gcc

CPPFLAGS += $(patsubst %,-I%,$(INC))
CPPFLAGS += -DSTM32F401xx -DSTM32F4XX -DUSE_STDPERIPH_DRIVER

CFLAGS :=
CFLAGS += -mcpu=cortex-m4 -mthumb -fdata-sections -ffunction-sections
CFLAGS += -fomit-frame-pointer -Wall -Os -g3

ifneq ("$(STACK_USAGE)","")
    OBJ_FORCE := FORCE
    CFLAGS += -fstack-usage

ALL: build/monitor.stack build/bootmonitor.stack

else
    CFLAGS += -Werror
endif

CFLAGS += -DHSE_VALUE=8000000

LDFLAGS := -nostartfiles -Wl,-static -lc -lgcc -Wl,--warn-common
LDFLAGS += -Wl,--fatal-warnings -Wl,--gc-sections
LDFLAGS += -Tshared/stm32f411.ld

all: build/ef_monitor.bin

flash: build/ef_monitor.bin
	openocd -f /usr/local/share/openocd/scripts/interface/stlink-v2.cfg -f /usr/local/share/openocd/scripts/target/stm32f4x.cfg -f flash.cfg

build/ef_monitor.bin: build/monitor.bin
	cat build/monitor.bin > $@

%.bin: %
	$(ARM_SDK_PREFIX)objcopy -O binary $< $@

build/monitor.stack: $(OBJ) build/monitor
	printf 'Memory used by data+BSS: %d\n\n' `/bin/echo -n '0x';$(ARM_SDK_PREFIX)nm build/monitor | grep _ebss | cut -c 2-8` > $@
	./misc/avstack.pl $(OBJ) >> $@

build/monitor: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ -Tsrc/memory.ld $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

build/%.o: %.c $(OBJ_FORCE)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

inc/%cfg.h: %.cfg FORCE
	echo "const " > $@
	xxd -i $< >> $@

FORCE:
