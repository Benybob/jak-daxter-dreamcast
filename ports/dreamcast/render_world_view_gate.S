/* Reject unsupported FPU state before a floating C ABI (AI-assisted), ISC. */
.text
.align 2
.global _dc_render_world_prepare
.type _dc_render_world_prepare,@function
_dc_render_world_prepare:
 stc sr,r0
 mov.l .Lworld_fd,r1
 tst r1,r0
 bf .Lworld_reject
 sts fpscr,r0
 mov.l .Lworld_mode,r1
 tst r1,r0
 bf .Lworld_reject
 mov.l .Lworld_body,r1
 jmp @r1
 nop
.Lworld_reject:
 rts
 mov #0,r0
.align 2
.Lworld_fd: .long 0x00008000
.Lworld_mode: .long 0x00380000
.Lworld_body: .long _dc_render_world_prepare_sh4zam
.size _dc_render_world_prepare,.-_dc_render_world_prepare
