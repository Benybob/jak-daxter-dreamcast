.text
.align 2
/* Test-only integer entry: reject modes without any floating ABI operations. */
.global _dc_scene_projection_bad_mode
.type _dc_scene_projection_bad_mode,@function
_dc_scene_projection_bad_mode:
 sts.l pr,@-r15
 sts.l fpscr,@-r15
 stc sr,r1
 mov.l r1,@-r15
 mov r4,r0
 mov.l .Ltestfd,r1
 tst r1,r0
 bt .Lfpscr
 stc sr,r1
 or r0,r1
 ldc r1,sr
 bra .Lcall
 nop
.Lfpscr:
 lds r0,fpscr
 nop
.Lcall:
 mov #0,r4
 mov #12,r5
 mov #0,r6
 mov #0,r7
 mov.l .Lentry,r1
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
.Ltestfd: .long 0x00008000
.Lentry: .long _dc_scene_project
.size _dc_scene_projection_bad_mode,.-_dc_scene_projection_bad_mode


