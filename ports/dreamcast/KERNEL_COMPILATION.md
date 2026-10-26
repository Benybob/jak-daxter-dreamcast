# Complete original kernel compilation

2026-10-26. (AI-assisted, Claude Code). Every function in the original jak1
kernel files now either compiles to SH-4 or is an x86/ARM64 assembly primitive
with a named native counterpart: **176 of 184 compile**, and the other 8 are
those primitives. Each file's original top-level startup, including the
primitives' definitions, also lowers as one module (`gcommon`, `gkernel`,
`gstate`, `gstring`). Running that startup natively is the next milestone.

## New compiler support

- **128-bit values** (`int128` registers such as `vec4s` and `uint128`). Each
  value owns a dedicated 16-byte frame slot, outside slot reuse. Supported:
  128↔128 copies, zero-extending 64→128 and truncating 128→64 moves, 16-byte
  loads and stores (as four aligned words), and the PS2 packs `.pcpyld`
  (`rd.hi = rs.lo, rd.lo = rt.lo`) and `.pcpyud` (`rd.lo = rs.hi, rd.hi = rt.hi`).
  Any other 128-bit use (arithmetic, other asm, other wide classes) refuses.
  This unlocks `qmem-copy<-!`/`qmem-copy->!` and the array/`vec4s` printers.
- **128-bit call convention** between compiled functions: a 128-bit argument
  takes two consecutive integer positions (low, then high), and a 128-bit return
  comes back in r0:r1 (low) and r2:r3 (high). No C bridge takes or returns
  128-bit values.
- **Variadic calls** (`_varargs_`, such as `format`): every argument is passed in
  an integer slot, floats as their 32-bit pattern. This is the desktop GOAL
  convention, and a native `format` must read floats the same way. A 128-bit
  variadic argument refuses.
- **Native bindings** (`--native-binding ORIGINAL=NATIVE`, recorded in the build
  manifest). Every use of an uncompilable original function object becomes an
  import of the named loaded native counterpart, so the original `define` and
  `method-set!` forms install it under the original name. A bound function can't
  also be compiled, and must exist in the module.

| Original assembly primitive | Native counterpart |
| --- | --- |
| `return-from-thread` | `dc-c-return-from-thread` (fiber abandon) |
| `return-from-thread-dead` | `dc-c-return-from-thread-dead` (original deactivate, then return) |
| `reset-and-call` | `dc-c-reset-and-call` (trans/post temporary threads) |
| `(method thread-suspend cpu-thread)` | `dc-c-thread-suspend` (fiber yield) |
| `(method thread-resume cpu-thread)` | `dc-c-thread-resume` (registered fiber step) |
| `(method new catch-frame)` | `dc-c-catch-frame-new` (root catch adapter) |
| `throw-dispatch` | `throw-dispatch` (native catch escape) |
| `set-to-run-bootstrap` | `set-to-run-bootstrap` (main-start guard) |

## Verification

A test in ChatGPT's ownership case had a hard-coded `transfer.functions!=5`
guard that would silently skip 37 checks once the transfer bridge grew to 7
functions. Bisecting with the previous lowerer found it; it now expects 7.

All **24,846 native runtime checks** (no lowerer regression), the **39-check
numeric** suite and **452 host tests** (1 optional skip) pass. Five new
synthetic host tests cover quadword copies, class-mismatch and width refusals,
both packs, unknown wide classes, 128-bit arithmetic refusal, the 128-bit
parameter/return convention and argument-position limit, and native bindings
with their refusals. Three existing frontend tests now pin the new variadic and
wide-value rules instead of the old refusals. The committed kernel lowering
survey is regenerated.
