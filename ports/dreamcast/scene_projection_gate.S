/* Integer-only mode gate before any C prologue/FPU access (AI-assisted), ISC. */
.text
.align 2
.global _dc_scene_project
.type _dc_scene_project,@function
_dc_scene_project:
 stc sr,r0
 mov.l .Lfd,r1
 tst r1,r0
 bf .Lreject
 sts fpscr,r0
 mov.l .Lmode,r1
 tst r1,r0
 bf .Lreject
 mov.l .Lbody,r1
 jmp @r1
 nop
.Lreject:
 rts
 mov #0,r0
.align 2
.Lfd: .long 0x00008000
.Lmode: .long 0x00380000
.Lbody: .long _dc_scene_project_sh4zam
.size _dc_scene_project,.-_dc_scene_project


