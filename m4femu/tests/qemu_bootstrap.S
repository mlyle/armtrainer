	.syntax unified
	.thumb
	.cpu cortex-m4
	.text
	.global qemu_bootstrap
	.type qemu_bootstrap, %function
qemu_bootstrap:
	ldr r0, cpacr_address
	ldr r1, cpacr_value
	str r1, [r0]
	ldr r0, fpccr_address
	movs r1, #0
	str r1, [r0]
	dsb sy
	isb sy
1:	b 1b
	.balign 4
cpacr_address:
	.word 0xe000ed88
cpacr_value:
	.word 0x00f00000
fpccr_address:
	.word 0xe000ef34
	.size qemu_bootstrap, .-qemu_bootstrap
