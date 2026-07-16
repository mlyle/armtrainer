#include <m4f/m4f.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROGRAM_BASE UINT32_C(0x20000000)
#define PROGRAM_MEMORY_SIZE UINT32_C(0x10000)
#define PROGRAM_STACK (PROGRAM_BASE + UINT32_C(0xe000))
#define PROGRAM_OUTPUT (PROGRAM_BASE + UINT32_C(0xf000))
#define PROGRAM_STOP (PROGRAM_BASE + UINT32_C(0xfff0))
#define PROGRAM_STEP_LIMIT UINT32_C(100000)

struct program_memory {
	uint8_t bytes[PROGRAM_MEMORY_SIZE];
	unsigned writes;
};

static enum m4f_mem_result program_read(void *context, uint32_t address,
		unsigned size, bool execute, uint32_t *value)
{
	struct program_memory *memory = context;
	(void)execute;
	if (address < PROGRAM_BASE || size > 4 ||
			address - PROGRAM_BASE > PROGRAM_MEMORY_SIZE - size)
		return M4F_MEM_FAULT;
	*value = 0;
	for (unsigned i = 0; i < size; ++i)
		*value |= (uint32_t)memory->bytes[address - PROGRAM_BASE + i] << (i * 8);
	return M4F_MEM_OK;
}

static enum m4f_mem_result program_write(void *context, uint32_t address,
		unsigned size, uint32_t value)
{
	struct program_memory *memory = context;
	if (address < PROGRAM_BASE || size > 4 ||
			address - PROGRAM_BASE > PROGRAM_MEMORY_SIZE - size)
		return M4F_MEM_FAULT;
	for (unsigned i = 0; i < size; ++i)
		memory->bytes[address - PROGRAM_BASE + i] = (uint8_t)(value >> (i * 8));
	memory->writes++;
	return M4F_MEM_OK;
}

static uint64_t memory_hash(const struct program_memory *memory)
{
	uint64_t hash = UINT64_C(14695981039346656037);
	for (size_t i = 0; i < sizeof(memory->bytes); ++i) {
		hash ^= memory->bytes[i];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static uint32_t architectural_xpsr(const struct m4f_cpu *cpu)
{
	uint32_t value = cpu->xpsr & ~((UINT32_C(3) << 25) |
		(UINT32_C(0x3f) << 10));
	value |= ((uint32_t)cpu->itstate & 3) << 25;
	value |= ((uint32_t)cpu->itstate >> 2) << 10;
	return value;
}

static void print_state(const char *tag, uint32_t step,
		const struct m4f_step_result *result, const struct m4f_cpu *cpu,
		const struct program_memory *memory, unsigned writes)
{
	printf("%s %" PRIu32 " %08" PRIx32 " %08" PRIx32 " %u %u",
		tag, step, result ? result->instruction_address : cpu->r[15],
		result ? result->instruction : 0, result ? result->instruction_size : 0,
		writes);
	for (unsigned i = 0; i < 16; ++i) printf(" %08" PRIx32, cpu->r[i]);
	printf(" %08" PRIx32 " %08" PRIx32,
		architectural_xpsr(cpu), cpu->fpscr & M4F_FPSCR_MASK);
	for (unsigned i = 0; i < 32; ++i) printf(" %08" PRIx32, cpu->s[i]);
	printf(" %016" PRIx64 "\n", memory_hash(memory));
}

static int load_binary(struct program_memory *memory, const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		perror(path);
		return 0;
	}
	size_t size = fread(memory->bytes, 1, PROGRAM_STACK - PROGRAM_BASE, file);
	if (ferror(file) || !feof(file)) {
		fprintf(stderr, "%s: program is too large\n", path);
		fclose(file);
		return 0;
	}
	fclose(file);
	if (!size) {
		fprintf(stderr, "%s: empty program\n", path);
		return 0;
	}
	return 1;
}

int main(int argc, char **argv)
{
	bool trace = argc == 3 && !strcmp(argv[2], "--trace");
	if (argc < 2 || argc > 3 || (argc == 3 && !trace)) {
		fprintf(stderr, "usage: %s PROGRAM.bin [--trace]\n", argv[0]);
		return 2;
	}
	struct program_memory memory = { 0 };
	if (!load_binary(&memory, argv[1])) return 2;
	struct m4f_memory interface = {
		.read = program_read,
		.write = program_write,
		.context = &memory,
	};
	struct m4f_cpu cpu;
	m4f_reset(&cpu, &interface);
	cpu.r[0] = UINT32_C(0x13579bdf);
	cpu.r[1] = 17;
	cpu.r[2] = PROGRAM_OUTPUT;
	cpu.r[13] = PROGRAM_STACK;
	cpu.r[14] = PROGRAM_STOP | 1;
	m4f_set_pc(&cpu, PROGRAM_BASE);
	struct m4f_step_result empty = { 0 };
	if (trace) print_state("INIT", 0, &empty, &cpu, &memory, 0);

	uint32_t steps;
	for (steps = 0; steps < PROGRAM_STEP_LIMIT && cpu.r[15] != PROGRAM_STOP;
			++steps) {
		memory.writes = 0;
		struct m4f_step_result result = m4f_step(&cpu);
		if (trace) print_state("TRACE", steps + 1, &result, &cpu, &memory,
				memory.writes);
		if (result.stop != M4F_STEP_OK) {
			fprintf(stderr, "program stopped at %08" PRIx32 ": %s\n",
				result.instruction_address, m4f_stop_name(result.stop));
			return 1;
		}
	}
	if (cpu.r[15] != PROGRAM_STOP) {
		fprintf(stderr, "program did not return within %" PRIu32 " steps\n",
			PROGRAM_STEP_LIMIT);
		return 1;
	}
	printf("RESULT %08" PRIx32, cpu.r[0]);
	for (unsigned i = 0; i < 8; ++i) {
		uint32_t value;
		memcpy(&value, &memory.bytes[PROGRAM_OUTPUT - PROGRAM_BASE + i * 4],
				sizeof(value));
		printf(" %08" PRIx32, value);
	}
	putchar('\n');
	return 0;
}
