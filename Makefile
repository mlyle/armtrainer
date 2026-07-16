ARM_SDK_PREFIX := arm-none-eabi-
HOST_CC ?= cc
HOST_AR ?= ar
HOST_CFLAGS ?= -Wall -Wextra -Werror -Wno-missing-field-initializers -O2

M4F_SRC := m4femu/src/m4f.c m4femu/src/m4f_thumb16.c \
	m4femu/src/m4f_thumb32.c m4femu/src/m4f_memory.c m4femu/src/m4f_fp.c
M4F_OBJ := $(patsubst m4femu/src/%.c,build/m4femu/%.o,$(M4F_SRC))
M4F_WASM_OBJ := $(patsubst m4femu/src/%.c,build/m4femu/wasm/%.o,$(M4F_SRC))
M4F_PROGRAM_NAMES := integer memory floating
M4F_PROGRAM_ELF := $(patsubst %,build/m4femu/programs/%.elf,$(M4F_PROGRAM_NAMES))
M4F_PROGRAM_BIN := $(patsubst %,build/m4femu/programs/%.bin,$(M4F_PROGRAM_NAMES))
M4F_PROGRAM_REF := $(patsubst %,build/m4femu/programs/%-reference,$(M4F_PROGRAM_NAMES))

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
OTHERLIB_SRC := $(wildcard libs/src/*.c)
SHARED_SRC := $(OTHERLIB_SRC) $(STDPERIPH_SRC)
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


LDFLAGS := -nostartfiles -Wl,-static -lc -lgcc -Wl,--warn-common
LDFLAGS += -Wl,--fatal-warnings -Wl,--gc-sections

# Old hardware, rev A
#CFLAGS += -DOLD_HW
#LDFLAGS += -Tsrc/memory-f411.ld
# New hardware, rev B or later
LDFLAGS += -Tsrc/memory-f412.ld


CFLAGS += -DHSE_VALUE=25000000


LDFLAGS += -Tsrc/stm32f411.ld

.PHONY: all clean flash manual tools m4f m4f-test m4f-sanitize \
	m4f-wasm-check m4f-wasm-test m4f-program-test m4f-qemu-test \
	web web-test web-serve FORCE

all: build/ef_monitor.bin

tools: build/tools/disasmthumb build/tools/hex2bin

m4f: build/m4femu/libm4f.a

m4f-test: build/m4femu/test_m4f build/m4femu/encoding-corpus.o \
		build/m4femu/encoding-thumb16.o
	$<
	./m4femu/tests/check-corpus.sh build/m4femu/test_m4f \
		build/m4femu/encoding-corpus.o
	./m4femu/tests/check-corpus.sh build/m4femu/test_m4f \
		build/m4femu/encoding-thumb16.o

m4f-sanitize: build/m4femu/test_m4f_san
	env ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $<
	env ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $< --sweep

m4f-wasm-check: build/m4femu/wasm/libm4f.a

m4f-wasm-test: build/m4femu/wasm/test_m4f.js
	node $<

m4f-program-test: build/m4femu/program_runner $(M4F_PROGRAM_ELF) \
		$(M4F_PROGRAM_BIN) \
		$(M4F_PROGRAM_REF)
	./m4femu/tests/run-programs.sh build/m4femu/program_runner \
		build/m4femu/programs

m4f-qemu-test: build/m4femu/program_runner $(M4F_PROGRAM_ELF) \
		$(M4F_PROGRAM_BIN) \
		build/m4femu/qemu_bootstrap.bin
	python3 m4femu/tests/qemu_diff.py --runner build/m4femu/program_runner \
		--bootstrap build/m4femu/qemu_bootstrap.bin \
		$(M4F_PROGRAM_BIN)

web: build/web/trainer.mjs

web-test: build/web/test_trainer_web build/web/test_trainer_web.js
	build/web/test_trainer_web
	node build/web/test_trainer_web.js
	node web/tests/test_browser_module.mjs
	node web/tests/test_timing.mjs
	node --check web/app.js
	node --check web/worker.js

web-serve: web
	python3 -m http.server 8000

build/web/test_trainer_web: $(M4F_SRC) web/c/trainer_web.c \
		web/c/trainer_web.h web/tests/test_trainer_web.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -std=c11 -Im4femu/include -Iinc -Iweb/c \
		$(M4F_SRC) web/c/trainer_web.c web/tests/test_trainer_web.c \
		-lm -o $@

build/web/test_trainer_web.js: $(M4F_SRC) web/c/trainer_web.c \
		web/c/trainer_web.h web/tests/test_trainer_web.c
	@mkdir -p $(dir $@)
	env EM_CACHE=$(abspath build/m4femu/emscripten-cache) emcc \
		-Wall -Wextra -Werror -Wno-missing-field-initializers -O2 \
		-std=c11 -Im4femu/include -Iinc -Iweb/c \
		$(M4F_SRC) web/c/trainer_web.c web/tests/test_trainer_web.c -lm \
		-sENVIRONMENT=node -sEXIT_RUNTIME=1 -o $@

build/web/trainer.mjs: $(M4F_SRC) web/c/trainer_web.c web/c/trainer_web.h
	@mkdir -p $(dir $@)
	env EM_CACHE=$(abspath build/m4femu/emscripten-cache) emcc \
		-Wall -Wextra -Werror -Wno-missing-field-initializers -O2 \
		-std=c11 -Im4femu/include -Iinc -Iweb/c \
		$(M4F_SRC) web/c/trainer_web.c -lm --no-entry \
		-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createTrainerModule \
		-sENVIRONMENT=worker -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=0 \
		-sEXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
		-sEXPORTED_FUNCTIONS='["_trainer_init","_trainer_reset",\
"_trainer_program_buffer","_trainer_program_capacity","_trainer_load",\
"_trainer_key","_trainer_command_run","_trainer_command_step",\
"_trainer_run_slice","_trainer_framebuffer","_trainer_frame_revision",\
"_trainer_state","_trainer_pc","_trainer_register","_trainer_last_stop",\
"_trainer_led","_trainer_fast_mode","_trainer_instruction_count"]' \
		-o $@

build/m4femu/libm4f.a: $(M4F_OBJ)
	$(HOST_AR) rcs $@ $^

build/m4femu/wasm/libm4f.a: $(M4F_WASM_OBJ)
	emar rcs $@ $^

build/m4femu/%.o: m4femu/src/%.c m4femu/include/m4f/m4f.h \
		m4femu/src/m4f_internal.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -std=c11 -Im4femu/include -c $< -o $@

build/m4femu/wasm/%.o: m4femu/src/%.c m4femu/include/m4f/m4f.h \
		m4femu/src/m4f_internal.h
	@mkdir -p $(dir $@)
	emcc -Wall -Wextra -Werror -O2 -std=c11 -Im4femu/include -c $< -o $@

build/m4femu/encoding-corpus.o: m4femu/tests/encoding-corpus.S
	@mkdir -p $(dir $@)
	$(ARM_SDK_PREFIX)as -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 $< -o $@

build/m4femu/encoding-thumb16.o: m4femu/tests/encoding-thumb16.S
	@mkdir -p $(dir $@)
	$(ARM_SDK_PREFIX)as -mcpu=cortex-m4 -mthumb $< -o $@

build/m4femu/test_m4f: $(M4F_SRC) m4femu/tests/test_m4f.c \
		m4femu/include/m4f/m4f.h m4femu/src/m4f_internal.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -std=c11 -Im4femu/include \
		$(M4F_SRC) m4femu/tests/test_m4f.c -lm -o $@

build/m4femu/test_m4f_san: $(M4F_SRC) m4femu/tests/test_m4f.c \
		m4femu/include/m4f/m4f.h m4femu/src/m4f_internal.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -Wall -Wextra -Werror -Wno-missing-field-initializers \
		-O1 -g -fsanitize=address,undefined -std=c11 -Im4femu/include \
		$(M4F_SRC) m4femu/tests/test_m4f.c -lm -o $@

build/m4femu/wasm/test_m4f.js: m4femu/tests/test_m4f.c \
		build/m4femu/wasm/libm4f.a m4femu/include/m4f/m4f.h
	@mkdir -p $(dir $@)
	env EM_CACHE=$(abspath build/m4femu/emscripten-cache) emcc \
		-Wall -Wextra -Werror -O2 -std=c11 -Im4femu/include \
		m4femu/tests/test_m4f.c build/m4femu/wasm/libm4f.a -lm \
		-sENVIRONMENT=node -sEXIT_RUNTIME=1 -o $@

build/m4femu/program_runner: $(M4F_SRC) m4femu/tests/program_runner.c \
		m4femu/include/m4f/m4f.h m4femu/src/m4f_internal.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -std=c11 -Im4femu/include \
		$(M4F_SRC) m4femu/tests/program_runner.c -lm -o $@

build/m4femu/programs/%.elf: m4femu/tests/programs/%.c \
		m4femu/tests/program.ld
	@mkdir -p $(dir $@)
	$(ARM_SDK_PREFIX)gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 \
		-mfloat-abi=softfp -O1 -g -ffreestanding -fno-builtin \
		-fno-stack-protector -ffunction-sections -fdata-sections \
		-ffp-contract=off -nostdlib -nostartfiles \
		-Wl,--gc-sections -Wl,-T,m4femu/tests/program.ld $< -o $@

build/m4femu/programs/%.bin: build/m4femu/programs/%.elf
	$(ARM_SDK_PREFIX)objcopy -O binary $< $@

build/m4femu/programs/%-reference: m4femu/tests/programs/%.c \
		m4femu/tests/program_reference.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -std=c11 -ffp-contract=off $^ -o $@

build/m4femu/qemu_bootstrap.o: m4femu/tests/qemu_bootstrap.S
	@mkdir -p $(dir $@)
	$(ARM_SDK_PREFIX)as -mcpu=cortex-m4 -mthumb $< -o $@

build/m4femu/qemu_bootstrap.bin: build/m4femu/qemu_bootstrap.o
	$(ARM_SDK_PREFIX)objcopy -O binary $< $@

build/tools/disasmthumb: tools/disasmthumb.c inc/thumbdisasm.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -Iinc $< -o $@

build/tools/hex2bin: tools/hex2bin.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@

build/ef_monitor.bin: build/monitor.bin
	cat build/monitor.bin > $@

flash: build/ef_monitor.bin
	dfu-util -a 0 -i 0 -s 0x08000000 -D  build/monitor.bin
#	openocd -f /opt/homebrew/share/openocd/scripts/interface/stlink.cfg -f /opt/homebrew/share/openocd/scripts/target/stm32f4x.cfg -c "program build/ef_monitor.bin verify reset exit 0x08000000"

%.bin: %
	$(ARM_SDK_PREFIX)objcopy -O binary $< $@

build/monitor.stack: $(OBJ) build/monitor
	printf 'Memory used by data+BSS: %d\n\n' `/bin/echo -n '0x';$(ARM_SDK_PREFIX)nm build/monitor | grep _ebss | cut -c 2-8` > $@
	./misc/avstack.pl $(OBJ) >> $@

build/monitor: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

build/%.o: %.c $(OBJ_FORCE)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

inc/%cfg.h: %.cfg FORCE
	echo "const " > $@
	xxd -i $< >> $@

manual: $(BUILD_DIR)/ARM_Thumb_Manual_Mlyle.pdf

$(BUILD_DIR)/ARM_Thumb_Manual_Mlyle.pdf: FORCE
	cd arm-manual && pdflatex -output-directory ../$(BUILD_DIR) ARM_Thumb_Manual_Mlyle.tex

FORCE:
