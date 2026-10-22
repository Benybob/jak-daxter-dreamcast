/*
 * Copyright (c) 2026 (AI-assisted)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * uint32_t dc_goal_native_regs_snapshot(struct dc_goal_native_regs *out);
 *
 * Leaf. Reads registers and writes only the caller buffer. Does not push,
 * pop, or write R15, R8..R14, PR, FPUL, FPSCR, or any FR. Scratch is R0,
 * R1, and SR.T. R4 is left unchanged.
 *
 * Assemble as SH-4 (the measured -m4-single target). FPU enabled is a
 * caller precondition: this file does not read or write SR.FD.
 */

        .text
        .balign 4
        .global _dc_goal_native_regs_snapshot
        .type   _dc_goal_native_regs_snapshot, @function
_dc_goal_native_regs_snapshot:
        tst     r4, r4
        bt      .L_reject
        nop

        mov     r4, r0
        and     #3, r0
        tst     r0, r0
        bf      .L_reject
        nop

        sts     fpscr, r0
        mov     #0x38, r1
        shll16  r1
        tst     r1, r0
        bf      .L_reject
        nop

        /* Mode gate passed. r0 still holds the full FPSCR word. */
        mov.l   r0, @(40, r4)

        mov.l   r8, @(0, r4)
        mov.l   r9, @(4, r4)
        mov.l   r10, @(8, r4)
        mov.l   r11, @(12, r4)
        mov.l   r12, @(16, r4)
        mov.l   r13, @(20, r4)
        mov.l   r14, @(24, r4)

        sts     pr, r0
        mov.l   r0, @(28, r4)
        mov     r15, r0
        mov.l   r0, @(32, r4)
        sts     fpul, r0
        mov.l   r0, @(36, r4)

        mov     #44, r0
        fmov.s  fr12, @(r0, r4)
        mov     #48, r0
        fmov.s  fr13, @(r0, r4)
        mov     #52, r0
        fmov.s  fr14, @(r0, r4)
        mov     #56, r0
        fmov.s  fr15, @(r0, r4)

        rts
        mov     #1, r0

.L_reject:
        rts
        mov     #0, r0

        .size   _dc_goal_native_regs_snapshot, .-_dc_goal_native_regs_snapshot
