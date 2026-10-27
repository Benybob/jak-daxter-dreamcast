# Compiling and loading the original GAME.CGO (AI-assisted)

2026-10-27, Claude Code. The original engine is being compiled file by file in
GAME.CGO order:

- **92 of GAME.CGO's 326 code files** compile to native SH-4 (through res-h).
- **48 of those load and run their original top-level startup** after KERNEL.CGO
  in stock 16 MiB Flycast (through decomp-h). This includes the 141 lane-exact
  vector cases ([VU0_LOWERING.md](VU0_LOWERING.md)) and the original process
  spawn and dispatch.
- **148 of 148 boot checks pass.**

```bash
python ports/dreamcast/build_kernel_boot.py --msys C:/msys64 --verify
```

## Compile order is not load order

The desktop build compiles each file after its `game.gp` dependency closure. That
closure can include files the DGO loads much later: pc-debug-methods needs
target-util, for example. `gp_deps.py` reads `goal_src/jak1/game.gp` (`goal-src`
and `goal-src-sequence`), and `build_game_cgo.py` uses it in two passes:

1. **One frontend run.** The exporter's new `--batch` mode compiles the
   Dreamcast-configured kernel, then every needed file once, in a
   dependency-first order, exporting typed IR for each GAME.CGO file. All **331**
   files of the GAME.CGO dependency graph compile in about 8 seconds. Batch IR is
   identical to the per-file exporter's (checked on math and vector-h).
2. **SH-4 lowering** of each GAME.CGO file in DGO order. The modules are then
   packed into `build/kernel-dgo/GAME.CGO`.

An export refusal marks only that file: it compiled, so later files still see its
definitions. Objects commented out of `game.gd` are skipped.

## What the engine files needed

Lowering (`lower_sh4_ir.py`):

- **VU0 vf registers**: see [VU0_LOWERING.md](VU0_LOWERING.md).
- **PS2 MMI 128-bit integer ops**:
  - 18 three-operand kinds (pextlb/h/w, pextub/h/w, pceq*, pcgt*, psubw, por,
    pxor, pand, packuswb, paddb);
  - 9 immediate kinds (pw/ph shifts, byte shifts, word shuffles).

  They call the `dc-c-mmi` helper (`goal_mmi.c`), which follows the exact x86
  instruction and VEX operand order the desktop backend emits (several are
  "swapped"). A host test compares it with an independent Python model of those
  x86 instructions: all 27 kinds, about 1,250 cases. pcpyld/pcpyud stay inline.
- **xor/blend/swizzle/splat on int128**: bit copies, valid on either 128-bit class.
- **float ↔ int128 moves**: `movss`, lane 0, as for vf.
- **`.nop.vf` and `.wait.vf`**: x87 FNOP/FWAIT on desktop, with no state; they lower to nothing.
- **`(inline-array T)`** parameters are addresses, like `(pointer T)`.
- **none-typed values carry nothing**: copies of a discarded `method-set!` result
  are dropped.
- **Dead pure code**: a pure register-only op that no memory, call, return,
  branch or asm op depends on is removed, cycles included. For example,
  disable-irq's arithmetic on a register that the PC-only `.mfc0` never assigns.
- **Top-level values**: every loader discards a file's top-level value. A
  `(declare-file (debug))` body of only macros returns an unassigned register on
  its debug path, so such a top level runs as void. This applies only to a
  top-level function, and only to an unassigned return.

Exporter (`export_ir.cpp`):

- `static_load` for vectors;
- `--batch`;
- a (none) function with value early returns (timer-reset's PC-only
  `(return (the uint 0))`) stays (none), since callers cannot observe the value.

Runtime:

- **`structure` new**: the native counterpart of kscheme `new_structure`.
  `(new 'global 'matrix)` failed until it existed. It allocates `allocated_size`
  bytes, 16-byte rounded and zeroed, from the global heap.
- **Type references before their deftype**: a static array's element type, as
  in dma-disasm.
  - The lowerer finds the types a module's own top level defines: calls of
    `type`'s method 0 with a quoted symbol.
  - Static data referencing those types imports them as `deftype:NAME`.
  - The linker interns a placeholder only for `deftype:`, as desktop `klink`
    does with `intern_type_from_c`. A plain `type:` import of a missing type
    still refuses, as the runtime suite asserts. Desktop interns any type; this
    is deliberately narrower.
  - DCSH carries no method count, so placeholders get the kscheme default of 12.
    A later deftype needing more fails, never shrinks.
- **`cpad-open`, `cpad-get-data`** (`goal_pad.c`):
  - desktop `CPadOpen`/`CPadGetData`, with the same state machine;
  - libpad answers as desktop `libpad.cpp` gives them;
  - the controller comes from the KOS maple bus;
  - no actuators yet, which the original handles by skipping vibration.

  The first mapping:

  | Dreamcast | PS2 |
  | --- | --- |
  | A | cross |
  | B | circle |
  | X | square |
  | Y | triangle |
  | Start | start |
  | D-pad | d-pad |
  | L/R past half travel | L1/R1 |
  | stick | left stick (right stick centered) |

  Host-tested: the mapping, open, and disconnect behavior.
- **Limits raised**:
  - DCSH: 4,096 functions, 8,192 data objects, 262,144 relocations, 8 MiB files
    (gs has 314 static objects);
  - DGO: 512 objects;
  - the boot arena: 8 MiB.

## Where it stops

- **Loading stops at display (object 48).** Its startup allocates two PS2 DMA
  frame buffers of `DMA_BUFFER_GLOBAL_SIZE`: 2,664 KiB each under PC settings,
  1,712 KiB on PS2. Together that's 5.2 MiB, on top of 5.2 MiB already used, in
  a 16 MiB machine. Later engine files allocate level and actor heaps that are
  larger still. A deliberate Dreamcast memory budget is the next runtime step,
  and these PS2 GS buffers belong to the renderer the PowerVR path will replace.
  The arena is not simply being enlarged to hide this.
- **Lowering stops at res (object 92).** `mov igpr-11, &igpr-6` takes the
  address of a register-held local, which needs a stack home for that variable.
- **Exporter refusals.** 56 of the 331 dependency-graph files refuse; 54 of them
  for one reason, "conflicting or non-fixed virtual register alias". That is one
  exporter feature (rlet register aliasing) to add next.

No hardware claims. Everything is verified in Flycast.
