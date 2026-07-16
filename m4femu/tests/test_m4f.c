#include <m4f/m4f.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct test_memory {
	uint8_t bytes[4096];
	uint32_t base;
};

static enum m4f_mem_result test_read(void *context, uint32_t address,
		unsigned size, bool execute, uint32_t *value)
{
	struct test_memory *memory = context;
	(void)execute;
	if (address < memory->base || size > sizeof(*value) ||
			address - memory->base > sizeof(memory->bytes) - size) {
		return M4F_MEM_FAULT;
	}
	uint32_t offset = address - memory->base;
	*value = 0;
	for (unsigned i = 0; i < size; ++i) {
		*value |= (uint32_t)memory->bytes[offset + i] << (8 * i);
	}
	return M4F_MEM_OK;
}

static enum m4f_mem_result test_write(void *context, uint32_t address,
		unsigned size, uint32_t value)
{
	struct test_memory *memory = context;
	if (address < memory->base || size > sizeof(value) ||
			address - memory->base > sizeof(memory->bytes) - size) {
		return M4F_MEM_FAULT;
	}
	uint32_t offset = address - memory->base;
	for (unsigned i = 0; i < size; ++i) {
		memory->bytes[offset + i] = (uint8_t)(value >> (8 * i));
	}
	return M4F_MEM_OK;
}

static void fail(const char *message)
{
	fprintf(stderr, "FAIL: %s\n", message);
	exit(1);
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #expression); \
		exit(1); \
	} \
} while (0)

static struct m4f_memory memory_interface(struct test_memory *memory)
{
	struct m4f_memory interface = {
		.read = test_read,
		.write = test_write,
		.context = memory,
	};
	return interface;
}

static void prepare(struct m4f_cpu *cpu, struct test_memory *memory)
{
	memset(memory->bytes, 0, sizeof(memory->bytes));
	struct m4f_memory interface = memory_interface(memory);
	m4f_reset(cpu, &interface);
	m4f_set_pc(cpu, memory->base);
}

static void emit16(struct test_memory *memory, uint32_t address,
		uint16_t instruction)
{
	CHECK(test_write(memory, address, 2, instruction) == M4F_MEM_OK);
}

static void emit32(struct test_memory *memory, uint32_t address,
		uint16_t first, uint16_t second)
{
	emit16(memory, address, first);
	emit16(memory, address + 2, second);
}

static struct m4f_step_result step_ok(struct m4f_cpu *cpu)
{
	struct m4f_step_result result = m4f_step(cpu);
	if (result.stop != M4F_STEP_OK) {
		fprintf(stderr, "FAIL: step at %08x stopped: %s (%08x)\n",
				result.instruction_address, m4f_stop_name(result.stop),
				result.instruction);
		exit(1);
	}
	return result;
}

static void test_thumb16(struct m4f_cpu *cpu, struct test_memory *memory)
{
	prepare(cpu, memory);
	emit16(memory, memory->base + 0, 0x20ff); /* movs r0, #255 */
	emit16(memory, memory->base + 2, 0x3001); /* adds r0, #1 */
	emit16(memory, memory->base + 4, 0x0040); /* lsls r0, r0, #1 */
	emit16(memory, memory->base + 6, 0x2800); /* cmp r0, #0 */
	step_ok(cpu);
	CHECK(cpu->r[0] == 255 && !(cpu->xpsr & M4F_XPSR_Z));
	step_ok(cpu);
	CHECK(cpu->r[0] == 256 && !(cpu->xpsr & M4F_XPSR_C));
	step_ok(cpu);
	CHECK(cpu->r[0] == 512);
	step_ok(cpu);
	CHECK((cpu->xpsr & (M4F_XPSR_N | M4F_XPSR_Z | M4F_XPSR_C)) ==
			M4F_XPSR_C);

	prepare(cpu, memory);
	cpu->r[0] = UINT32_C(0x80000001);
	cpu->r[1] = 1;
	emit16(memory, memory->base, 0x40c8); /* lsrs r0, r1 (r0 >>= r1) */
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x40000000));
	CHECK(cpu->xpsr & M4F_XPSR_C);

	prepare(cpu, memory);
	cpu->r[0] = memory->base + 0x100;
	cpu->r[1] = UINT32_C(0x11223344);
	emit16(memory, memory->base + 0, 0x6001); /* str r1, [r0] */
	emit16(memory, memory->base + 2, 0x6802); /* ldr r2, [r0] */
	step_ok(cpu);
	step_ok(cpu);
	CHECK(cpu->r[2] == UINT32_C(0x11223344));

	prepare(cpu, memory);
	cpu->r[0] = 0;
	emit16(memory, memory->base + 0, 0xbf08); /* it eq */
	emit16(memory, memory->base + 2, 0x2007); /* moveq r0, #7 */
	cpu->xpsr |= M4F_XPSR_Z;
	step_ok(cpu);
	CHECK(cpu->itstate == 8);
	step_ok(cpu);
	CHECK(cpu->r[0] == 7 && cpu->itstate == 0);

	prepare(cpu, memory);
	emit16(memory, memory->base, 0xdf25);
	struct m4f_step_result svc = m4f_step(cpu);
	CHECK(svc.stop == M4F_STEP_SVC && svc.immediate == 0x25);
	CHECK(cpu->r[15] == memory->base + 2);

	prepare(cpu, memory);
	emit16(memory, memory->base, 0xe7fe); /* b . */
	step_ok(cpu);
	CHECK(cpu->r[15] == memory->base);
}

static void test_thumb2_data(struct m4f_cpu *cpu, struct test_memory *memory)
{
	prepare(cpu, memory);
	cpu->r[1] = 0xff;
	cpu->r[2] = 3;
	emit32(memory, memory->base, 0xea01, 0x00c2); /* and.w r0,r1,r2,lsl#3 */
	step_ok(cpu);
	CHECK(cpu->r[0] == 24);

	prepare(cpu, memory);
	emit32(memory, memory->base, 0xf04f, 0x30ab); /* mov.w r0,#abababab */
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0xabababab));

	prepare(cpu, memory);
	cpu->r[1] = 1;
	emit32(memory, memory->base, 0xf601, 0x20bc); /* addw r0,r1,#abc */
	step_ok(cpu);
	CHECK(cpu->r[0] == 0xabd);

	prepare(cpu, memory);
	emit32(memory, memory->base + 0, 0xf64a, 0x30cd); /* movw */
	emit32(memory, memory->base + 4, 0xf2c1, 0x2034); /* movt */
	step_ok(cpu);
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x1234abcd));

	prepare(cpu, memory);
	cpu->r[1] = 100000;
	emit32(memory, memory->base, 0xf301, 0x0010); /* ssat r0,#17,r1 */
	step_ok(cpu);
	CHECK(cpu->r[0] == 65535 && (cpu->xpsr & M4F_XPSR_Q));

	prepare(cpu, memory);
	cpu->r[1] = UINT32_MAX;
	emit32(memory, memory->base + 0, 0xf361, 0x10d3); /* bfi r0,r1,#7,#13 */
	emit32(memory, memory->base + 4, 0xf3c0, 0x12cc); /* ubfx r2,r0,#7,#13 */
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x000fff80));
	step_ok(cpu);
	CHECK(cpu->r[2] == 0x1fff);

	prepare(cpu, memory);
	cpu->r[1] = UINT32_C(0x01020304);
	cpu->r[2] = 8;
	emit32(memory, memory->base, 0xfa01, 0xf002); /* lsl.w r0,r1,r2 */
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x02030400));

	prepare(cpu, memory);
	cpu->r[1] = 2;
	cpu->xpsr |= M4F_XPSR_C;
	emit32(memory, memory->base, 0xea5f, 0x0031); /* rrxs r0,r1 */
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x80000001));
	CHECK(!(cpu->xpsr & M4F_XPSR_C));

	prepare(cpu, memory);
	cpu->r[1] = 0x100;
	emit32(memory, memory->base + 0, 0xfab1, 0xf081); /* clz r0,r1 */
	emit32(memory, memory->base + 4, 0xfa91, 0xf0a1); /* rbit r0,r1 */
	step_ok(cpu);
	CHECK(cpu->r[0] == 23);
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x00800000));

	prepare(cpu, memory);
	cpu->r[1] = INT32_MAX;
	cpu->r[2] = 1;
	emit32(memory, memory->base, 0xfa82, 0xf081); /* qadd r0,r1,r2 */
	step_ok(cpu);
	CHECK(cpu->r[0] == (uint32_t)INT32_MAX && (cpu->xpsr & M4F_XPSR_Q));

	prepare(cpu, memory);
	cpu->r[1] = UINT32_C(0x7f80ff01);
	cpu->r[2] = UINT32_C(0x017f0203);
	emit32(memory, memory->base, 0xfa81, 0xf002); /* sadd8 r0,r1,r2 */
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x80ff0104));

	/* Parallel lane saturation deliberately does not update the scalar Q flag. */
	prepare(cpu, memory);
	cpu->r[1] = UINT32_C(0x7f7f7f7f);
	cpu->r[2] = UINT32_C(0x01010101);
	emit32(memory, memory->base, 0xfa81, 0xf012); /* qadd8 r0,r1,r2 */
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x7f7f7f7f));
	CHECK(!(cpu->xpsr & M4F_XPSR_Q));

	prepare(cpu, memory);
	cpu->r[1] = UINT32_C(0x05040302);
	cpu->r[2] = UINT32_C(0x01020304);
	emit32(memory, memory->base, 0xfac1, 0xf002); /* ssub8 r0,r1,r2 */
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x040200fe));
}

static void test_multiply_and_divide(struct m4f_cpu *cpu,
		struct test_memory *memory)
{
	prepare(cpu, memory);
	cpu->r[1] = 7;
	cpu->r[2] = 9;
	emit32(memory, memory->base, 0xfb01, 0xf002); /* mul.w */
	step_ok(cpu);
	CHECK(cpu->r[0] == 63);

	prepare(cpu, memory);
	cpu->r[1] = (uint32_t)-100;
	cpu->r[2] = 7;
	emit32(memory, memory->base, 0xfb91, 0xf0f2); /* sdiv */
	step_ok(cpu);
	CHECK((int32_t)cpu->r[0] == -14);

	prepare(cpu, memory);
	cpu->r[2] = UINT32_C(0xffffffff);
	cpu->r[3] = 2;
	emit32(memory, memory->base, 0xfb82, 0x0103); /* smull r0,r1,r2,r3 */
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0xfffffffe));
	CHECK(cpu->r[1] == UINT32_C(0xffffffff));

	prepare(cpu, memory);
	cpu->r[1] = UINT32_C(0x00020003);
	cpu->r[2] = UINT32_C(0x00040005);
	emit32(memory, memory->base, 0xfb21, 0xf002); /* smuad */
	step_ok(cpu);
	CHECK(cpu->r[0] == 23);

	prepare(cpu, memory);
	cpu->r[0] = 5;
	cpu->r[1] = 0;
	cpu->r[2] = 3;
	cpu->r[3] = 4;
	emit32(memory, memory->base + 0, 0xfbc2, 0x0183); /* smlalbb */
	emit32(memory, memory->base + 4, 0xfbe2, 0x0163); /* umaal */
	step_ok(cpu);
	CHECK(cpu->r[0] == 17 && cpu->r[1] == 0);
	cpu->r[0] = 5;
	cpu->r[1] = 7;
	step_ok(cpu);
	CHECK(cpu->r[0] == 24 && cpu->r[1] == 0);
}

static void test_thumb2_memory(struct m4f_cpu *cpu,
		struct test_memory *memory)
{
	prepare(cpu, memory);
	cpu->r[0] = UINT32_C(0x89abcdef);
	cpu->r[1] = memory->base + 0x100;
	emit32(memory, memory->base + 0, 0xf8c1, 0x0020); /* str.w r0,[r1,#32] */
	emit32(memory, memory->base + 4, 0xf8d1, 0x2020); /* ldr.w r2,[r1,#32] */
	step_ok(cpu);
	step_ok(cpu);
	CHECK(cpu->r[2] == UINT32_C(0x89abcdef));

	prepare(cpu, memory);
	cpu->r[0] = UINT32_C(0x11223344);
	cpu->r[1] = memory->base + 0x100;
	cpu->r[2] = 3;
	emit32(memory, memory->base + 0, 0xf841, 0x0022); /* str.w r0,[r1,r2,lsl#2] */
	emit32(memory, memory->base + 4, 0xf851, 0x3032); /* ldr.w r3,[r1,r2,lsl#3] */
	step_ok(cpu);
	uint32_t stored;
	CHECK(test_read(memory, cpu->r[1] + 12, 4, false, &stored) == M4F_MEM_OK);
	CHECK(stored == UINT32_C(0x11223344));
	/* Put a distinct value at the LSL #3 address. */
	CHECK(test_write(memory, cpu->r[1] + 24, 4, UINT32_C(0xaabbccdd)) == M4F_MEM_OK);
	step_ok(cpu);
	CHECK(cpu->r[3] == UINT32_C(0xaabbccdd));

	prepare(cpu, memory);
	cpu->r[1] = memory->base + 0x100;
	CHECK(test_write(memory, cpu->r[1] + 16, 4, UINT32_C(0xfeedface)) == M4F_MEM_OK);
	emit32(memory, memory->base + 0, 0xe851, 0x0f04); /* ldrex r0,[r1,#16] */
	emit32(memory, memory->base + 4, 0xe841, 0x2004); /* strex r0,r2,[r1,#16] */
	cpu->r[2] = UINT32_C(0x13579bdf);
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0xfeedface) && cpu->exclusive_valid);
	step_ok(cpu);
	CHECK(cpu->r[0] == 0 && !cpu->exclusive_valid);
	CHECK(test_read(memory, cpu->r[1] + 16, 4, false, &stored) == M4F_MEM_OK);
	CHECK(stored == UINT32_C(0x13579bdf));

	prepare(cpu, memory);
	emit32(memory, memory->base, 0xf85f, 0x0004); /* ldr.w r0,[pc,#-4] */
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x0004f85f));

	prepare(cpu, memory);
	emit32(memory, memory->base, 0xe9df, 0x0102); /* ldrd r0,r1,[pc,#8] */
	CHECK(test_write(memory, memory->base + 12, 4, UINT32_C(0x11223344)) ==
			M4F_MEM_OK);
	CHECK(test_write(memory, memory->base + 16, 4, UINT32_C(0xaabbccdd)) ==
			M4F_MEM_OK);
	step_ok(cpu);
	CHECK(cpu->r[0] == UINT32_C(0x11223344));
	CHECK(cpu->r[1] == UINT32_C(0xaabbccdd));
}

static void test_system(struct m4f_cpu *cpu, struct test_memory *memory)
{
	prepare(cpu, memory);
	cpu->xpsr |= M4F_XPSR_Z;
	cpu->r[0] = UINT32_C(0x000a0000);
	emit32(memory, memory->base + 0, 0xf380, 0x8400); /* msr apsr_g,r0 */
	emit32(memory, memory->base + 4, 0xf380, 0x8800); /* msr apsr_nzcvq,r0 */
	emit32(memory, memory->base + 8, 0xf380, 0x8c00); /* msr apsr_nzcvqg,r0 */
	step_ok(cpu);
	CHECK((cpu->xpsr & M4F_XPSR_GE_MASK) == UINT32_C(0x000a0000));
	CHECK(cpu->xpsr & M4F_XPSR_Z);
	cpu->r[0] = M4F_XPSR_N | M4F_XPSR_Q;
	step_ok(cpu);
	CHECK((cpu->xpsr & (M4F_XPSR_N | M4F_XPSR_Q)) ==
			(M4F_XPSR_N | M4F_XPSR_Q));
	CHECK((cpu->xpsr & M4F_XPSR_GE_MASK) == UINT32_C(0x000a0000));
	cpu->r[0] = M4F_XPSR_C | UINT32_C(0x00050000);
	step_ok(cpu);
	CHECK((cpu->xpsr & (M4F_XPSR_C | M4F_XPSR_GE_MASK)) ==
			(M4F_XPSR_C | UINT32_C(0x00050000)));

	prepare(cpu, memory);
	cpu->xpsr |= M4F_XPSR_N;
	cpu->itstate = 0x18; /* NE, one instruction; condition passes with Z clear. */
	emit32(memory, memory->base, 0xf3ef, 0x8003); /* mrs r0,xpsr */
	step_ok(cpu);
	CHECK(cpu->r[0] == M4F_XPSR_N); /* EPSR.T and IT read as zero via MRS. */

	prepare(cpu, memory);
	cpu->control = 1; /* Unprivileged Thread mode. */
	cpu->primask = 1;
	cpu->r[0] = 0;
	emit32(memory, memory->base + 0, 0xf3ef, 0x8110); /* mrs r1,primask */
	emit32(memory, memory->base + 4, 0xf380, 0x8810); /* msr primask,r0 */
	step_ok(cpu);
	CHECK(cpu->r[1] == 0);
	step_ok(cpu);
	CHECK(cpu->primask == 1);
}

static void test_stop_contract(struct m4f_cpu *cpu, struct test_memory *memory)
{
	prepare(cpu, memory);
	emit16(memory, memory->base, 0xb600); /* unallocated 16-bit encoding */
	struct m4f_step_result result = m4f_step(cpu);
	CHECK(result.stop == M4F_STEP_UNDEFINED);
	CHECK(cpu->r[15] == memory->base);

	prepare(cpu, memory);
	emit16(memory, memory->base, 0xde5a); /* udf #0x5a */
	result = m4f_step(cpu);
	CHECK(result.stop == M4F_STEP_UDF && result.immediate == 0x5a);
	CHECK(cpu->r[15] == memory->base);

	prepare(cpu, memory);
	emit16(memory, memory->base, 0xbe7f); /* bkpt #0x7f */
	result = m4f_step(cpu);
	CHECK(result.stop == M4F_STEP_BKPT && result.immediate == 0x7f);
	CHECK(cpu->r[15] == memory->base + 2);

	prepare(cpu, memory);
	cpu->r[1] = memory->base + sizeof(memory->bytes);
	emit16(memory, memory->base, 0x6808); /* ldr r0,[r1] */
	result = m4f_step(cpu);
	CHECK(result.stop == M4F_STEP_DATA_FAULT);
	CHECK(result.fault_address == cpu->r[1] && cpu->r[15] == memory->base);

	prepare(cpu, memory);
	cpu->r[13] = memory->base + 0x103;
	emit16(memory, memory->base, 0xb401); /* push {r0} */
	result = m4f_step(cpu);
	CHECK(result.stop == M4F_STEP_ALIGNMENT_FAULT);
	CHECK(cpu->r[15] == memory->base);

	prepare(cpu, memory);
	m4f_set_pc(cpu, memory->base + sizeof(memory->bytes));
	result = m4f_step(cpu);
	CHECK(result.stop == M4F_STEP_FETCH_FAULT);
	CHECK(result.fault_address == memory->base + sizeof(memory->bytes));

	prepare(cpu, memory);
	emit16(memory, memory->base + 0, 0xbf20); /* wfe */
	emit16(memory, memory->base + 2, 0xbf40); /* sev */
	emit16(memory, memory->base + 4, 0xbf20); /* wfe consumes event */
	result = m4f_step(cpu);
	CHECK(result.stop == M4F_STEP_WFE);
	step_ok(cpu);
	CHECK(cpu->event_registered);
	step_ok(cpu);
	CHECK(!cpu->event_registered);
}

static uint32_t float_bits(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float bits_float(uint32_t value)
{
	float result;
	memcpy(&result, &value, sizeof(result));
	return result;
}

static void test_fp(struct m4f_cpu *cpu, struct test_memory *memory)
{
	prepare(cpu, memory);
	cpu->s[1] = float_bits(1.5f);
	cpu->s[2] = float_bits(2.25f);
	emit32(memory, memory->base, 0xee30, 0x0a81); /* vadd.f32 s0,s1,s2 */
	step_ok(cpu);
	CHECK(bits_float(cpu->s[0]) == 3.75f);

	prepare(cpu, memory);
	cpu->s[7] = float_bits(-3.0f);
	cpu->s[8] = float_bits(4.0f);
	emit32(memory, memory->base, 0xee23, 0x3a84); /* vmul s6,s7,s8 */
	step_ok(cpu);
	CHECK(bits_float(cpu->s[6]) == -12.0f);

	prepare(cpu, memory);
	emit32(memory, memory->base, 0xeeb7, 0x1a00); /* vmov s2,#1.0 */
	step_ok(cpu);
	CHECK(cpu->s[2] == UINT32_C(0x3f800000));

	prepare(cpu, memory);
	cpu->s[1] = float_bits(-2.75f);
	emit32(memory, memory->base, 0xeebd, 0x0ae0); /* vcvt.s32.f32 s0,s1 */
	step_ok(cpu);
	CHECK((int32_t)cpu->s[0] == -2);
	CHECK(cpu->fpscr & M4F_FPSCR_IXC);

	prepare(cpu, memory);
	cpu->s[0] = float_bits(1.0f);
	cpu->s[1] = float_bits(2.0f);
	emit32(memory, memory->base, 0xeeb4, 0x0a60); /* vcmp s0,s1 */
	step_ok(cpu);
	CHECK((cpu->fpscr & UINT32_C(0xf0000000)) == M4F_FPSCR_N);

	prepare(cpu, memory);
	cpu->r[0] = UINT32_C(0x40490fdb);
	emit32(memory, memory->base + 0, 0xee00, 0x0a10); /* vmov s0,r0 */
	emit32(memory, memory->base + 4, 0xee10, 0x1a10); /* vmov r1,s0 */
	step_ok(cpu);
	step_ok(cpu);
	CHECK(cpu->r[1] == UINT32_C(0x40490fdb));

	prepare(cpu, memory);
	cpu->s[1] = UINT32_C(0xdead3c00);
	emit32(memory, memory->base + 0, 0xeeb2, 0x0a60); /* vcvtb.f32.f16 */
	step_ok(cpu);
	CHECK(cpu->s[0] == UINT32_C(0x3f800000));

	prepare(cpu, memory);
	cpu->s[4] = UINT32_C(0xabcd0000);
	cpu->s[5] = float_bits(1.0f);
	emit32(memory, memory->base + 0, 0xeeb3, 0x2a62); /* vcvtb.f16.f32 */
	step_ok(cpu);
	CHECK(cpu->s[4] == UINT32_C(0xabcd3c00));

	prepare(cpu, memory);
	cpu->fpscr = M4F_FPSCR_AHP;
	cpu->s[1] = 0x7c00;
	emit32(memory, memory->base + 0, 0xeeb2, 0x0a60); /* AHP 65536 -> f32 */
	step_ok(cpu);
	CHECK(cpu->s[0] == float_bits(65536.0f));

	prepare(cpu, memory);
	cpu->s[14] = float_bits(1.5f);
	emit32(memory, memory->base + 0, 0xeebe, 0x7a44); /* vcvt.s16.f32 #8 */
	step_ok(cpu);
	CHECK(cpu->s[14] == 384);

	prepare(cpu, memory);
	cpu->s[18] = UINT32_C(0x0000ff80);
	emit32(memory, memory->base + 0, 0xeeba, 0x9a44); /* vcvt.f32.s16 #8 */
	step_ok(cpu);
	CHECK(cpu->s[18] == float_bits(-0.5f));

	prepare(cpu, memory);
	cpu->s[1] = UINT32_C(0x11111111);
	cpu->s[2] = UINT32_C(0x22222222);
	emit32(memory, memory->base + 0, 0xec55, 0x4a30); /* vmov r4,r5,s1,s2 */
	step_ok(cpu);
	CHECK(cpu->r[4] == UINT32_C(0x11111111));
	CHECK(cpu->r[5] == UINT32_C(0x22222222));

	prepare(cpu, memory);
	cpu->s[0] = float_bits(1.0f);
	cpu->s[1] = UINT32_C(0x7f800000);
	cpu->s[2] = 0;
	emit32(memory, memory->base, 0xeea0, 0x0a81); /* vfma: infinity * zero */
	step_ok(cpu);
	CHECK(cpu->s[0] == UINT32_C(0x7fc00000));
	CHECK(cpu->fpscr & M4F_FPSCR_IOC);

	prepare(cpu, memory);
	cpu->big_endian = true;
	cpu->r[2] = memory->base + 0x100;
	cpu->s[6] = UINT32_C(0x11223344);
	cpu->s[7] = UINT32_C(0xaabbccdd);
	emit32(memory, memory->base + 0, 0xed02, 0x3b00); /* vstr d3,[r2] */
	emit32(memory, memory->base + 4, 0xed92, 0x4b00); /* vldr d4,[r2] */
	step_ok(cpu);
	static const uint8_t expected[] = { 0xaa, 0xbb, 0xcc, 0xdd,
		0x11, 0x22, 0x33, 0x44 };
	CHECK(!memcmp(&memory->bytes[0x100], expected, sizeof(expected)));
	step_ok(cpu);
	CHECK(cpu->s[8] == UINT32_C(0x11223344));
	CHECK(cpu->s[9] == UINT32_C(0xaabbccdd));
}

static void test_student_program(struct m4f_cpu *cpu,
		struct test_memory *memory)
{
	prepare(cpu, memory);
	emit16(memory, memory->base + 0, 0x2005); /* movs r0,#5 */
	emit16(memory, memory->base + 2, 0x2107); /* movs r1,#7 */
	emit16(memory, memory->base + 4, 0x1842); /* adds r2,r0,r1 */
	emit16(memory, memory->base + 6, 0x4342); /* muls r2,r0 */
	emit16(memory, memory->base + 8, 0xdf42); /* svc #0x42 */
	for (unsigned i = 0; i < 4; ++i) step_ok(cpu);
	CHECK(cpu->r[2] == 60);
	struct m4f_step_result result = m4f_step(cpu);
	CHECK(result.stop == M4F_STEP_SVC && result.immediate == 0x42);
	CHECK(result.instruction_address == memory->base + 8);
	CHECK(cpu->r[15] == memory->base + 10);
}

static void test_decoder_sweep(struct m4f_cpu *cpu,
		struct test_memory *memory)
{
	for (uint32_t first = 0; first <= UINT16_MAX; ++first) {
		prepare(cpu, memory);
		for (unsigned reg = 0; reg < 13; ++reg)
			cpu->r[reg] = memory->base + 0x200 + reg * 8;
		cpu->r[1] |= 1;
		cpu->r[13] = memory->base + 0xf00;
		cpu->r[14] = memory->base | 1;
		for (unsigned reg = 0; reg < 32; ++reg)
			cpu->s[reg] = UINT32_C(0x3f000000) + reg * UINT32_C(0x10000);
		emit16(memory, memory->base, (uint16_t)first);
		emit16(memory, memory->base + 2,
				(uint16_t)(first * UINT32_C(40503) + 97));
		(void)m4f_step(cpu);
	}
	puts("m4f: decoder sanitizer sweep passed");
}

int main(int argc, char **argv)
{
	struct test_memory memory = { .base = 0x20000000 };
	struct m4f_memory interface = memory_interface(&memory);
	struct m4f_cpu cpu;
	if (argc == 2 && !strcmp(argv[1], "--sweep")) {
		test_decoder_sweep(&cpu, &memory);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "--probe")) {
		char *end;
		unsigned long encoded = strtoul(argv[2], &end, 16);
		if (*end || encoded > UINT32_MAX) return 2;
		prepare(&cpu, &memory);
		for (unsigned i = 0; i < 13; ++i)
			cpu.r[i] = memory.base + 0x200 + i * 4;
		cpu.r[1] |= 1; /* Valid Thumb target for BX/BLX corpus entries. */
		cpu.r[13] = memory.base + 0xf00;
		cpu.r[14] = memory.base | 1;
		for (unsigned i = 0; i < 16; ++i)
			CHECK(test_write(&memory, cpu.r[13] + i * 4, 4,
					memory.base | 1) == M4F_MEM_OK);
		for (unsigned i = 0; i < 32; ++i) cpu.s[i] = float_bits((float)i + 1.0f);
		if (encoded > 0xffff)
			emit32(&memory, memory.base, (uint16_t)(encoded >> 16),
					(uint16_t)encoded);
		else emit16(&memory, memory.base, (uint16_t)encoded);
		struct m4f_step_result probe = m4f_step(&cpu);
		puts(m4f_stop_name(probe.stop));
		return probe.stop == M4F_STEP_UNDEFINED ? 1 : 0;
	}
	m4f_reset(&cpu, &interface);
	m4f_set_pc(&cpu, memory.base);

	if (cpu.r[15] != memory.base || !(cpu.xpsr & M4F_XPSR_T)) {
		fail("reset state");
	}

	struct m4f_step_result result = m4f_step(&cpu);
	if (result.stop != M4F_STEP_OK || result.instruction != 0 ||
			result.instruction_size != 2 || cpu.r[15] != memory.base + 2) {
		fail("first instruction result");
	}

	puts("m4f: base API tests passed");
	test_thumb16(&cpu, &memory);
	test_thumb2_data(&cpu, &memory);
	test_multiply_and_divide(&cpu, &memory);
	test_thumb2_memory(&cpu, &memory);
	test_system(&cpu, &memory);
	test_stop_contract(&cpu, &memory);
	test_fp(&cpu, &memory);
	test_student_program(&cpu, &memory);
	puts("m4f: instruction vectors passed");
	return 0;
}
