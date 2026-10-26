/* Deliberately violate callee-save ABI, then escape without a C return.
 * Independent clobber oracle for native_catch.S (AI-assisted), ISC. */
.text
.align 2
.global _dc_goal_catch_clobber_escape
.type _dc_goal_catch_clobber_escape, @function
_dc_goal_catch_clobber_escape:
	add #-32, r15
	mov #-1, r8
	mov #-2, r9
	mov #-3, r10
	mov #-4, r11
	mov #-5, r12
	mov #-6, r13
	mov #-7, r14
	mov #-8, r0
	lds r0, fpul
	fsts fpul, fr12
	mov #-9, r0
	lds r0, fpul
	fsts fpul, fr13
	mov #-10, r0
	lds r0, fpul
	fsts fpul, fr14
	mov #-11, r0
	lds r0, fpul
	fsts fpul, fr15
	mov.l .L_dirty, r0
	lds r0, fpscr
	mov.l .L_escape, r0
	jmp @r0
	nop
.align 2
.L_dirty:
	.long 0x003c007d
.L_escape:
	.long _dc_goal_catch_escape_raw
.size _dc_goal_catch_clobber_escape, .-_dc_goal_catch_clobber_escape
