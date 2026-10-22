/* Independent snapshot oracle, same stack only (AI-assisted), ISC.
 * C ABI inputs: R4=destination (may be invalid for rejection), R5=observations,
 * R6=requested FPSCR, R7=12 known raw words (7GPR,FPUL,4FR).
 * Caller enters with enabled FPU, FR/SZ/PR clear, and valid separate inputs.
 * Save C caller state before installing patterns; restore it before returning.
 */
.text
.balign 4
.global _dc_goal_snapshot_probe
.type _dc_goal_snapshot_probe,@function
_dc_goal_snapshot_probe:
 sts.l pr,@-r15
 mov.l r8,@-r15
 mov.l r9,@-r15
 mov.l r10,@-r15
 mov.l r11,@-r15
 mov.l r12,@-r15
 mov.l r13,@-r15
 mov.l r14,@-r15
 sts.l fpul,@-r15
 sts.l fpscr,@-r15
 fmov.s fr12,@-r15
 fmov.s fr13,@-r15
 fmov.s fr14,@-r15
 fmov.s fr15,@-r15
 add #-24,r15
 mov.l r4,@(0,r15)
 mov.l r5,@(4,r15)
 mov.l r6,@(8,r15)
 mov.l r7,@(12,r15)
 mov.l @r7+,r8
 mov.l @r7+,r9
 mov.l @r7+,r10
 mov.l @r7+,r11
 mov.l @r7+,r12
 mov.l @r7+,r13
 mov.l @r7+,r14
 mov.l @r7+,r0
 lds r0,fpul
 fmov.s @r7+,fr12
 fmov.s @r7+,fr13
 fmov.s @r7+,fr14
 fmov.s @r7+,fr15
 mov r15,r0
 mov.l r0,@(0,r5)
 mov.l .L_return_site,r0
 mov.l r0,@(4,r5)
 mov r5,r2
 add #76,r2
 stc sr,r0
 mov.l r0,@r2
 lds r6,fpscr
 mov.l .L_snapshot,r0
 jsr @r0
 nop
.global _dc_goal_snapshot_probe_return
_dc_goal_snapshot_probe_return:
 mov.l r0,@(20,r15)
 mov.l @(4,r15),r5
 mov.l r0,@(8,r5)
 mov r15,r0
 mov.l r0,@(12,r5)
 sts pr,r0
 mov.l r0,@(16,r5)
 sts fpul,r0
 mov.l r0,@(20,r5)
 sts fpscr,r0
 mov.l r0,@(24,r5)
 mov.l r4,@(28,r5)
 mov.l r8,@(32,r5)
 mov.l r9,@(36,r5)
 mov.l r10,@(40,r5)
 mov.l r11,@(44,r5)
 mov.l r12,@(48,r5)
 mov.l r13,@(52,r5)
 mov.l r14,@(56,r5)
 mov r5,r2
 add #80,r2
 stc sr,r0
 mov.l r0,@r2
 /* Capture default-bank FPRs only after recording tested FPSCR. */
 mov #0,r0
 lds r0,fpscr
 mov r5,r2
 add #60,r2
 fmov.s fr12,@r2
 add #4,r2
 fmov.s fr13,@r2
 add #4,r2
 fmov.s fr14,@r2
 add #4,r2
 fmov.s fr15,@r2
 add #24,r15
 fmov.s @r15+,fr15
 fmov.s @r15+,fr14
 fmov.s @r15+,fr13
 fmov.s @r15+,fr12
 lds.l @r15+,fpscr
 lds.l @r15+,fpul
 mov.l @r15+,r14
 mov.l @r15+,r13
 mov.l @r15+,r12
 mov.l @r15+,r11
 mov.l @r15+,r10
 mov.l @r15+,r9
 mov.l @r15+,r8
 lds.l @r15+,pr
 rts
 nop
.size _dc_goal_snapshot_probe,.-_dc_goal_snapshot_probe
.balign 4
.L_snapshot: .long _dc_goal_native_regs_snapshot
.L_return_site: .long _dc_goal_snapshot_probe_return
