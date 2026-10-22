/* Native cooperative context shims (AI-assisted), ISC. */
/* SH-4 cooperative image switch for the C yield/resume engine.
   Image is the 60-byte dc_goal_native_regs record:
     R8..R14 at 0,4,8,12,16,20,24; PR 28; SP 32; FPUL 36; FPSCR 40;
     FR12..FR15 at 44,48,52,56.
   Raw entry preconditions are enforced by the C caller. This file does
   not branch on them. SR, GBR, MACH, MACL, and the other FPR bank are
   outside the image. */

.text
.align 2
.global _dc_goal_context_swap_raw
.type _dc_goal_context_swap_raw, @function
_dc_goal_context_swap_raw:
	mov.l	r8, @(0, r4)
	mov.l	r9, @(4, r4)
	mov.l	r10, @(8, r4)
	mov.l	r11, @(12, r4)
	mov.l	r12, @(16, r4)
	mov.l	r13, @(20, r4)
	mov.l	r14, @(24, r4)
	sts	pr, r0
	mov.l	r0, @(28, r4)
	mov	r15, r0
	mov.l	r0, @(32, r4)
	sts	fpul, r0
	mov.l	r0, @(36, r4)
	sts	fpscr, r0
	mov.l	r0, @(40, r4)
	mov	#44, r0
	fmov.s	fr12, @(r0, r4)
	mov	#48, r0
	fmov.s	fr13, @(r0, r4)
	mov	#52, r0
	fmov.s	fr14, @(r0, r4)
	mov	#56, r0
	fmov.s	fr15, @(r0, r4)

	/* FPSCR = 0 via lds (not an FPU op) so FR/SZ/PR are clear before fmov.s.
	   The mov that builds the FR12 offset is the non-FPU gap after lds. */
	mov	#0, r0
	lds	r0, fpscr
	mov	#44, r0
	fmov.s	@(r0, r5), fr12
	mov	#48, r0
	fmov.s	@(r0, r5), fr13
	mov	#52, r0
	fmov.s	@(r0, r5), fr14
	mov	#56, r0
	fmov.s	@(r0, r5), fr15
	mov.l	@(36, r5), r0
	lds	r0, fpul
	mov.l	@(40, r5), r0
	lds	r0, fpscr

	mov.l	@(0, r5), r8
	mov.l	@(4, r5), r9
	mov.l	@(8, r5), r10
	mov.l	@(12, r5), r11
	mov.l	@(16, r5), r12
	mov.l	@(20, r5), r13
	mov.l	@(24, r5), r14
	mov.l	@(28, r5), r0
	lds	r0, pr
	mov.l	@(32, r5), r2
	mov	r2, r15
	mov	#0, r0
	rts
	mov	#0, r1
.size _dc_goal_context_swap_raw, .-_dc_goal_context_swap_raw

.align 2
.global _dc_goal_context_enter_raw
.type _dc_goal_context_enter_raw, @function
_dc_goal_context_enter_raw:
	sts.l	pr, @-r15
	mov.l	.L_enter_swap, r0
	jsr	@r0
	nop
.global _dc_goal_context_enter_return
_dc_goal_context_enter_return:
	lds.l	@r15+, pr
	nop
	rts
	nop
.align 2
.L_enter_swap:
	.long	_dc_goal_context_swap_raw
.size _dc_goal_context_enter_raw, .-_dc_goal_context_enter_raw

.align 2
.global _dc_goal_context_leave_raw
.type _dc_goal_context_leave_raw, @function
_dc_goal_context_leave_raw:
	sts.l	pr, @-r15
	mov.l	.L_leave_swap, r0
	jsr	@r0
	nop
.global _dc_goal_context_leave_return
_dc_goal_context_leave_return:
	lds.l	@r15+, pr
	nop
	rts
	nop
.align 2
.L_leave_swap:
	.long	_dc_goal_context_swap_raw
.size _dc_goal_context_leave_raw, .-_dc_goal_context_leave_raw

.align 2
.global _dc_goal_context_boot_raw
.type _dc_goal_context_boot_raw, @function
_dc_goal_context_boot_raw:
	mov.l	.L_boot_fn, r0
	jmp	@r0
	nop
.align 2
.L_boot_fn:
	.long	_dc_goal_fiber_bootstrap
.size _dc_goal_context_boot_raw, .-_dc_goal_context_boot_raw
