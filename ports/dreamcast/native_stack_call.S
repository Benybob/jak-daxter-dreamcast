! ISC license. (AI-assisted).
!
! Copyright (c) 2026 the authors of this proposal.
!
! Permission to use, copy, modify, and/or distribute this software for any
! purpose with or without fee is hereby granted, provided that the above
! copyright notice and this permission notice appear in all copies.
!
! THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
! WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
! MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
! SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
! WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
! OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
! CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
!
! Synchronous primitive; C validation and native acceptance are separate.
! Private raw entry: validated C wrapper in goal_stack_call.c is mandatory.
!
! uint64_t dc_goal_stack_call_raw(uint32_t entry, uint32_t stack_top);
!   r4 = trusted entry, installed only as the JSR target
!   r5 = trusted alternate stack pointer, installed verbatim as r15
!   returns the target's uint64 bits in r0 (low) : r1 (high)
!
! Incoming precondition, not checked here: FPU enabled and FPSCR.FR, FPSCR.SZ,
! and FPSCR.PR already clear. No literal pool. No SR, GBR, or MAC changes.

	.text
	.align 2
	.global	_dc_goal_stack_call_raw
	.type	_dc_goal_stack_call_raw, @function
_dc_goal_stack_call_raw:
	! Original stack, high address to low. 14 words, 56 bytes.
	sts.l	pr,@-r15
	mov.l	r14,@-r15
	mov.l	r13,@-r15
	mov.l	r12,@-r15
	mov.l	r11,@-r15
	mov.l	r10,@-r15
	mov.l	r9,@-r15
	mov.l	r8,@-r15
	sts.l	fpul,@-r15
	sts.l	fpscr,@-r15
	fmov.s	fr15,@-r15
	fmov.s	fr14,@-r15
	fmov.s	fr13,@-r15
	fmov.s	fr12,@-r15

	mov	r15,r8
	mov	r4,r9
	mov	r5,r15
	jsr	@r9
	nop

	! r8 is the private old SP. Park r0:r1 before r0 is reused.
	mov	r8,r15
	mov	r0,r2
	mov	r1,r3
	mov	#0,r0
	lds	r0,fpscr
	! Leave a non-FPU instruction before restoring single-word FPRs.
	nop
	fmov.s	@r15+,fr12
	fmov.s	@r15+,fr13
	fmov.s	@r15+,fr14
	fmov.s	@r15+,fr15
	lds.l	@r15+,fpscr
	lds.l	@r15+,fpul
	mov.l	@r15+,r8
	mov.l	@r15+,r9
	mov.l	@r15+,r10
	mov.l	@r15+,r11
	mov.l	@r15+,r12
	mov.l	@r15+,r13
	mov.l	@r15+,r14
	lds.l	@r15+,pr
	! One instruction between LDS PR and RTS. r2:r3 stay intact until here.
	mov	r2,r0
	rts
	mov	r3,r1

	.size	_dc_goal_stack_call_raw, .-_dc_goal_stack_call_raw
