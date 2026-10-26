/* Test-only integer mode invocation with real output fences (AI-assisted). */
.text
.align 2
.global _dc_render_world_bad_mode
.type _dc_render_world_bad_mode,@function
_dc_render_world_bad_mode:
 sts.l pr,@-r15
 sts.l fpscr,@-r15
 stc sr,r1
 mov.l r1,@-r15
 mov r4,r0
 mov.l .Lwtestfd,r1
 tst r1,r0
 bt .Lwfpscr
 stc sr,r1
 or r0,r1
 ldc r1,sr
 bra .Lwcall
 nop
.Lwfpscr:
 lds r0,fpscr
 nop
.Lwcall:
 mov r5,r4
 mov r6,r5
 mov #3,r6
 mov.l .Lwentry,r1
 jsr @r1
 nop
 mov.l @r15+,r1
 ldc r1,sr
 lds.l @r15+,fpscr
 lds.l @r15+,pr
 nop
 rts
 nop
.align 2
.Lwtestfd: .long 0x00008000
.Lwentry: .long _dc_render_world_prepare
.size _dc_render_world_bad_mode,.-_dc_render_world_bad_mode
