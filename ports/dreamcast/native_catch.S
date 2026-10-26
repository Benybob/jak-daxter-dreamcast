/* Closed same-stack invoke/escape return sites (AI-assisted), ISC.
 * Layout and preconditions: goal_catch_raw.h. No guest RA/SP is accepted. */
.text
.align 2
.global _dc_goal_catch_invoke_raw
.type _dc_goal_catch_invoke_raw, @function
_dc_goal_catch_invoke_raw:
	sts.l pr, @-r15
	mov.l .L_body, r0
	jsr @r0
	nop
.global _dc_goal_catch_invoke_return
_dc_goal_catch_invoke_return:
	lds.l @r15+, pr
	nop
	rts
	nop
.align 2
.L_body:
	.long .L_capture
.L_capture:
	mov.l r8, @(0, r4)
	mov.l r9, @(4, r4)
	mov.l r10, @(8, r4)
	mov.l r11, @(12, r4)
	mov.l r12, @(16, r4)
	mov.l r13, @(20, r4)
	mov.l r14, @(24, r4)
	sts pr, r0
	mov.l r0, @(28, r4)
	mov r15, r0
	mov.l r0, @(32, r4)
	sts fpul, r0
	mov.l r0, @(36, r4)
	sts fpscr, r0
	mov.l r0, @(40, r4)
	mov #44, r0
	fmov.s fr12, @(r0, r4)
	mov #48, r0
	fmov.s fr13, @(r0, r4)
	mov #52, r0
	fmov.s fr14, @(r0, r4)
	mov #56, r0
	fmov.s fr15, @(r0, r4)
	/* R8 is an ordinary callee-saved holder on the normal-return path. */
	mov r4, r8
	mov r5, r0
	jsr @r0
	mov r6, r4
	mov r8, r5
	bra .L_restore
	nop
.size _dc_goal_catch_invoke_raw, .-_dc_goal_catch_invoke_raw

.align 2
.global _dc_goal_catch_escape_raw
.type _dc_goal_catch_escape_raw, @function
_dc_goal_catch_escape_raw:
	mov r4, r5
	mov r4, r2
	add #64, r2
	mov.l @r2+, r0
	mov.l @r2, r1
.L_restore:
	/* R6/R7 hold result throughout restore. Clear mode before fmov.s; the
	 * following integer mov supplies the required non-FPU instruction gap. */
	mov r0, r6
	mov r1, r7
	mov #0, r2
	lds r2, fpscr
	mov #44, r0
	fmov.s @(r0, r5), fr12
	mov #48, r0
	fmov.s @(r0, r5), fr13
	mov #52, r0
	fmov.s @(r0, r5), fr14
	mov #56, r0
	fmov.s @(r0, r5), fr15
	mov.l @(36, r5), r2
	lds r2, fpul
	mov.l @(40, r5), r2
	lds r2, fpscr
	mov.l @(0, r5), r8
	mov.l @(4, r5), r9
	mov.l @(8, r5), r10
	mov.l @(12, r5), r11
	mov.l @(16, r5), r12
	mov.l @(20, r5), r13
	mov.l @(24, r5), r14
	mov.l @(28, r5), r2
	lds r2, pr
	mov.l @(32, r5), r3
	mov r3, r15
	mov r6, r0
	rts
	mov r7, r1
.size _dc_goal_catch_escape_raw, .-_dc_goal_catch_escape_raw
