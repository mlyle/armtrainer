	.syntax unified
	.thumb
	.cpu cortex-m4
	.text
	.global m4f_thumb16_corpus
	.type m4f_thumb16_corpus, %function
m4f_thumb16_corpus:
	lsls r0, r1, #3
	lsrs r0, r1, #7
	asrs r0, r1, #11
	adds r0, r1, r2
	subs r0, r1, r2
	adds r0, r1, #7
	subs r0, r1, #7
	movs r0, #0xab
	cmp r0, #0xab
	adds r0, #0xab
	subs r0, #0xab
	ands r0, r1
	eors r0, r1
	lsls r0, r1
	lsrs r0, r1
	asrs r0, r1
	adcs r0, r1
	sbcs r0, r1
	rors r0, r1
	tst r0, r1
	negs r0, r1
	cmp r0, r1
	cmn r0, r1
	orrs r0, r1
	muls r0, r1
	bics r0, r1
	mvns r0, r1
	add r8, r1
	cmp r8, r1
	mov r8, r1
	bx r1
	blx r1
	ldr r0, thumb16_literal
	str r0, [r1, r2]
	strh r0, [r1, r2]
	strb r0, [r1, r2]
	ldrsb r0, [r1, r2]
	ldr r0, [r1, r2]
	ldrh r0, [r1, r2]
	ldrb r0, [r1, r2]
	ldrsh r0, [r1, r2]
	str r0, [r1, #0x7c]
	ldr r0, [r1, #0x7c]
	strb r0, [r1, #0x1f]
	ldrb r0, [r1, #0x1f]
	strh r0, [r1, #0x3e]
	ldrh r0, [r1, #0x3e]
	str r0, [sp, #0x3fc]
	ldr r0, [sp, #0x3fc]
	adr r0, thumb16_literal
	add r0, sp, #0x3fc
	add sp, #0x1fc
	sub sp, #0x1fc
	cbz r0, thumb16_forward
	cbnz r0, thumb16_forward
	sxth r0, r1
	sxtb r0, r1
	uxth r0, r1
	uxtb r0, r1
	push {r0-r7, lr}
	pop {r0-r7, pc}
	setend be
	setend le
	cpsie i
	cpsid i
	rev r0, r1
	rev16 r0, r1
	revsh r0, r1
	bkpt #0xab
	it eq
	nopeq
	nop
	yield
	wfe
	wfi
	sev
	stmia r0!, {r1-r7}
	ldmia r0!, {r1-r7}
	beq.n thumb16_forward
	b.n thumb16_forward
	svc #0xab
	udf #0xab
thumb16_forward:
	nop
	.balign 4
thumb16_literal:
	.word 0x12345678
	.size m4f_thumb16_corpus, .-m4f_thumb16_corpus
