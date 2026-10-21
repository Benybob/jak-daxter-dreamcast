"""Exact restoring 32-bit division, emitted as native SH-4 (AI-assisted)."""
def emit_div32(e,signed,remainder,prefix):
    """R0 dividend, R2 divisor; result R0:R1, scratch R3..R7/T.

    Valid inputs follow the pinned x86 low-32/sign-extended-result contract.
    Faults call a native kernel status bridge; the enclosing frame saves PR.
    """
    zero,overflow,normal,positive_a,positive_b,loop,subtract,next_step,sign_done,fault,done=(
        prefix+'_'+name for name in ('zero','overflow','normal','positive_a','positive_b',
                                     'loop','subtract','next','sign_done','fault','done'))
    e.rr(0x2008,2,2); e.branch('bt',zero)
    if signed:
        e.constant(3,0x80000000); e.rr(0x3000,0,3); e.branch('bf',normal)
        e.immediate(3,-1); e.rr(0x3000,2,3); e.branch('bt',overflow)
        e.label(normal)
        e.mov(6,0); e.mov(7,0); e.rr(0x200a,7,2)
        e.compare_pz(0); e.branch('bt',positive_a); e.neg(0,0); e.label(positive_a)
        e.compare_pz(2); e.branch('bt',positive_b); e.neg(2,2); e.label(positive_b)
    e.immediate(3,0); e.immediate(5,32)
    e.label(loop)
    e.word(0x4000)  # SHLL R0 puts next numerator bit in T
    e.word(0x4324)  # ROTCL R3 consumes T and exposes the 33rd remainder bit
    e.branch('bt',subtract)
    e.rr(0x3002,3,2); e.branch('bf',next_step)  # unsigned R3 >= R2
    e.label(subtract); e.sub(3,2); e.add_immediate(0,1)
    e.label(next_step); e.word(0x4510); e.branch('bf',loop)  # DT R5
    if remainder: e.mov(0,3)
    if signed:
        e.compare_pz(6 if remainder else 7); e.branch('bt',sign_done); e.neg(0,0)
        e.label(sign_done)
    # The original backend sign-extends even unsigned quotient/remainder.
    e.mov(1,0); e.immediate(3,-31); e.rr(0x400c,1,3)
    e.branch('bra',done)
    e.label(zero); e.immediate(4,1); e.branch('bra',fault)
    if signed: e.label(overflow); e.immediate(4,2)
    e.label(fault); e.immediate(5,0)
    e.literal(0,('symbol','function:dc-c-integer-error'))
    e.word(0x400b); e.nop()  # JSR boxed bridge, return value zero/status set
    e.label(done)
