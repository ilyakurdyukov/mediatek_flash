@ -*- tab-width: 8 -*-
	.arch armv5te
	.syntax unified
	.section .text._start, "ax", %progbits
	.p2align 2
	.code 32
	.global _start
	.type _start, %function
_start:
	push	{r4,r5}
	ldr	r4, 1f
	add	r4, pc
	bx	r4
1:	.long	entry_main - .
