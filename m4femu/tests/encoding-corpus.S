	.syntax unified
	.thumb
	.cpu cortex-m4
	.fpu fpv4-sp-d16
	.text
	.global m4f_encoding_corpus
	.type m4f_encoding_corpus, %function
m4f_encoding_corpus:
	/* Data processing: shifted register. */
	and.w r0, r1, r2, lsl #3
	ands.w r3, r4, r5, lsr #7
	tst.w r6, r7, asr #11
	bic.w r0, r1, r2, ror #9
	orr.w r0, r1, r2
	orn.w r0, r1, r2
	eor.w r0, r1, r2
	teq.w r1, r2
	add.w r0, r1, r2, lsl #2
	adds.w r0, r1, r2
	cmn.w r1, r2
	adc.w r0, r1, r2
	sbc.w r0, r1, r2
	sub.w r0, r1, r2
	cmp.w r1, r2
	rsb.w r0, r1, r2
	mov.w r0, r1, lsl #7
	movs.w r0, r1, rrx
	mvn.w r0, r1, ror #11
	pkhbt r0, r1, r2, lsl #8
	pkhtb r0, r1, r2, asr #8

	/* Modified and plain immediates. */
	and.w r0, r1, #0x00ff00ff
	ands.w r2, r3, #0xff000000
	tst.w r4, #0x80000000
	bic.w r0, r1, #0x100
	orr.w r0, r1, #0x10101010
	mov.w r0, #0xabababab
	orn.w r0, r1, #0x20
	mvn.w r0, #0x400
	eor.w r0, r1, #0x8000
	teq.w r1, #0x40
	add.w r0, r1, #0x80000000
	cmn.w r1, #0x80
	adc.w r0, r1, #7
	sbc.w r0, r1, #9
	sub.w r0, r1, #0x10000
	cmp.w r1, #0x200
	rsb.w r0, r1, #3
	addw r0, r1, #0xabc
	subw r0, r1, #0xabc
	adr.w r0, corpus_literal
	movw r0, #0xabcd
	movt r0, #0x1234
	sat16a:
	ssat r0, #17, r1, asr #4
	ssat r0, #17, r1
	usat r0, #12, r1, lsl #5
	usat r0, #12, r1
	ssat16 r0, #7, r1
	usat16 r0, #6, r1
	bfi r0, r1, #7, #13
	bfc r0, #7, #13
	sbfx r0, r1, #7, #13
	ubfx r0, r1, #7, #13

	/* Register operations and DSP. */
	lsl.w r0, r1, r2
	lsls.w r0, r1, r2
	lsr.w r0, r1, r2
	asr.w r0, r1, r2
	ror.w r0, r1, r2
	sxtah r0, r1, r2, ror #8
	sxtab r0, r1, r2, ror #16
	uxtah r0, r1, r2, ror #24
	uxtab r0, r1, r2
	sxth.w r0, r2, ror #8
	sxtb.w r0, r2, ror #16
	uxth.w r0, r2, ror #24
	uxtb.w r0, r2
	sxtab16 r0, r1, r2, ror #8
	uxtab16 r0, r1, r2, ror #16
	sxtb16 r0, r2, ror #24
	uxtb16 r0, r2
	clz r0, r1
	rbit r0, r1
	rev.w r0, r1
	rev16.w r0, r1
	revsh.w r0, r1
	qadd r0, r1, r2
	qsub r0, r1, r2
	qdadd r0, r1, r2
	qdsub r0, r1, r2
	sel r0, r1, r2
	sadd8 r0, r1, r2
	qadd8 r0, r1, r2
	shadd8 r0, r1, r2
	uadd8 r0, r1, r2
	uqadd8 r0, r1, r2
	uhadd8 r0, r1, r2
	qadd16 r0, r1, r2
	qasx r0, r1, r2
	qsax r0, r1, r2
	qsub16 r0, r1, r2
	qsub8 r0, r1, r2
	shadd16 r0, r1, r2
	shasx r0, r1, r2
	shsax r0, r1, r2
	shsub16 r0, r1, r2
	shsub8 r0, r1, r2
	uqadd16 r0, r1, r2
	uqasx r0, r1, r2
	uqsax r0, r1, r2
	uqsub16 r0, r1, r2
	uqsub8 r0, r1, r2
	uhadd16 r0, r1, r2
	uhasx r0, r1, r2
	uhsax r0, r1, r2
	uhsub16 r0, r1, r2
	uhsub8 r0, r1, r2
	sadd16 r0, r1, r2
	sasx r0, r1, r2
	ssax r0, r1, r2
	ssub16 r0, r1, r2
	uadd16 r0, r1, r2
	uasx r0, r1, r2
	usax r0, r1, r2
	usub16 r0, r1, r2
	ssub8 r0, r1, r2
	usub8 r0, r1, r2

	/* Multiply, divide, and multiply-accumulate. */
	mul.w r0, r1, r2
	mla r0, r1, r2, r3
	mls r0, r1, r2, r3
	smull r0, r1, r2, r3
	umull r0, r1, r2, r3
	smlal r0, r1, r2, r3
	umlal r0, r1, r2, r3
	smlalbb r0, r1, r2, r3
	smlalbt r0, r1, r2, r3
	smlaltb r0, r1, r2, r3
	smlaltt r0, r1, r2, r3
	umaal r0, r1, r2, r3
	sdiv r0, r1, r2
	udiv r0, r1, r2
	smlaBB r0, r1, r2, r3
	smlaBT r0, r1, r2, r3
	smlaTB r0, r1, r2, r3
	smlaTT r0, r1, r2, r3
	smulBB r0, r1, r2
	smulBT r0, r1, r2
	smulTB r0, r1, r2
	smulTT r0, r1, r2
	smlawb r0, r1, r2, r3
	smlawt r0, r1, r2, r3
	smulwb r0, r1, r2
	smulwt r0, r1, r2
	smlad r0, r1, r2, r3
	smladx r0, r1, r2, r3
	smlsd r0, r1, r2, r3
	smlsdx r0, r1, r2, r3
	smuad r0, r1, r2
	smuadx r0, r1, r2
	smusd r0, r1, r2
	smusdx r0, r1, r2
	smlald r0, r1, r2, r3
	smlaldx r0, r1, r2, r3
	smlsld r0, r1, r2, r3
	smlsldx r0, r1, r2, r3
	smmul r0, r1, r2
	smmulr r0, r1, r2
	smmla r0, r1, r2, r3
	smmlar r0, r1, r2, r3
	smmls r0, r1, r2, r3
	smmlsr r0, r1, r2, r3
	usad8 r0, r1, r2
	usada8 r0, r1, r2, r3

	/* Loads, stores, stack, multiple, dual, and exclusive. */
	str.w r0, [r1, #0xabc]
	ldr.w r0, [r1, #0xabc]
	strb.w r0, [r1, #0xabc]
	ldrb.w r0, [r1, #0xabc]
	strh.w r0, [r1, #0xabc]
	ldrh.w r0, [r1, #0xabc]
	ldrsb.w r0, [r1, #0xabc]
	ldrsh.w r0, [r1, #0xabc]
	str.w r0, [r1, #-0x7f]
	ldr.w r0, [r1, #0x7f]!
	strb.w r0, [r1], #0x7f
	ldrh.w r0, [r1], #-0x7f
	ldr.w r0, [r1, r2, lsl #3]
	str.w r0, [r1, r2, lsl #2]
	ldrsb.w r0, [r1, r2]
	ldr.w r0, corpus_literal
	ldr.w r0, [pc, #-4]
	pld [r1, #0xabc]
	pld [r1, #-0x7f]
	pld [r1, r2, lsl #3]
	pld corpus_literal
	pli [r1, #0xabc]
	pli [r1, #-0x7f]
	pli [r1, r2, lsl #3]
	pli corpus_literal
	strd r0, r1, [r2, #0x40]
	ldrd r0, r1, [r2, #-0x40]!
	strd r0, r1, [r2], #0x40
	ldrd r0, r1, [r2], #-0x40
	ldrd r0, r1, [pc, #8]
	strt r0, [r1, #0x7f]
	ldrt r0, [r1, #0x7f]
	strbt r0, [r1, #0x7f]
	ldrbt r0, [r1, #0x7f]
	strht r0, [r1, #0x7e]
	ldrht r0, [r1, #0x7e]
	ldrsbt r0, [r1, #0x7f]
	ldrsht r0, [r1, #0x7e]
	stmia.w r0!, {r1-r8}
	ldmia.w r0!, {r1-r8}
	stmdb r0!, {r1-r8}
	ldmdb r0!, {r1-r8}
	push.w {r0-r12, lr}
	pop.w {r0-r12, pc}
	ldrex r0, [r1, #0x3fc]
	strex r0, r2, [r1, #0x3fc]
	ldrexb r0, [r1]
	strexb r0, r2, [r1]
	ldrexh r0, [r1]
	strexh r0, r2, [r1]
	clrex
	tbb [r0, r1]
	tbh [r0, r1, lsl #1]

	/* Branches and architectural system instructions. */
	b.w corpus_branch_target
	beq.w corpus_branch_target
	bl corpus_branch_target
	mrs r0, apsr
	mrs r0, primask
	mrs r0, basepri
	mrs r0, faultmask
	mrs r0, control
	msr apsr_nzcvq, r0
	msr apsr_g, r0
	msr apsr_nzcvqg, r0
	msr primask, r0
	msr basepri, r0
	msr basepri_max, r0
	msr faultmask, r0
	msr control, r0
	dmb sy
	dmb st
	dmb ish
	dsb oshst
	dsb sy
	isb sy
	nop.w
	yield.w
	wfe.w
	wfi.w
	sev.w
	dbg #7
	udf.w #0xabcd

	/* FPv4-SP-D16. */
	vadd.f32 s0, s1, s2
	vsub.f32 s3, s4, s5
	vmul.f32 s6, s7, s8
	vnmul.f32 s9, s10, s11
	vdiv.f32 s12, s13, s14
	vmla.f32 s0, s1, s2
	vmls.f32 s3, s4, s5
	vnmla.f32 s6, s7, s8
	vnmls.f32 s9, s10, s11
	vfma.f32 s0, s1, s2
	vfms.f32 s3, s4, s5
	vfnma.f32 s6, s7, s8
	vfnms.f32 s9, s10, s11
	vmov.f32 s0, s1
	vmov.f32 s2, #1.0
	vabs.f32 s3, s4
	vneg.f32 s5, s6
	vsqrt.f32 s7, s8
	vcmp.f32 s0, s1
	vcmpe.f32 s0, s1
	vcmp.f32 s2, #0.0
	vcvt.s32.f32 s0, s1
	vcvt.u32.f32 s2, s3
	vcvt.f32.s32 s4, s5
	vcvt.f32.u32 s6, s7
	vcvtr.s32.f32 s8, s9
	vcvt.s32.f32 s10, s10, #8
	vcvt.f32.s32 s12, s12, #12
	vcvt.s16.f32 s14, s14, #8
	vcvt.u16.f32 s16, s16, #7
	vcvt.f32.s16 s18, s18, #8
	vcvt.f32.u16 s20, s20, #7
	vcvtb.f32.f16 s0, s1
	vcvtt.f32.f16 s2, s3
	vcvtb.f16.f32 s4, s5
	vcvtt.f16.f32 s6, s7
	vmov r0, s0
	vmov s1, r1
	vmrs r0, fpscr
	vmrs apsr_nzcv, fpscr
	vmsr fpscr, r0
	vldr s0, [r1, #0x3fc]
	vldr s2, corpus_literal
	vstr s1, [r2, #-0x3fc]
	vpush {s0-s15}
	vpop {s0-s15}
	vstmia r0!, {s0-s15}
	vldmia r0!, {s0-s15}
	vstmdb r0!, {s0-s3}
	vldmdb r0!, {s0-s3}
	vmov r0, r1, d0
	vmov d1, r2, r3
	vmov r4, r5, s1, s2
	vmov s4, s5, r2, r3
	vldr d2, [r1, #0x3f8]
	vstr d3, [r2, #-0x3f8]
	vpush {d0-d7}
	vpop {d0-d7}
	vstmia r0!, {d0-d7}
	vldmia r0!, {d0-d7}

corpus_branch_target:
	nop
	.balign 4
corpus_literal:
	.word 0x12345678
	.size m4f_encoding_corpus, .-m4f_encoding_corpus
