# Compiling and loading the original GAME.CGO (AI-assisted)

2026-10-27, Claude Code. The original engine is being compiled file by file in
GAME.CGO order:

- **144 of GAME.CGO's 326 code files** compile to native SH-4 (through path-h).
- **All 144 load and run their original top-level startup** after KERNEL.CGO in
  stock 16 MiB Flycast, with the Dreamcast memory budget ([MEMORY_BUDGET.md](MEMORY_BUDGET.md)). This includes the 141 lane-exact
  vector cases ([VU0_LOWERING.md](VU0_LOWERING.md)) and the original process
  spawn and dispatch.
- **244 of 244 boot checks pass.**

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
- **`(& var)` of a register variable** (res): desktop `IR_RegValAddr` forces the
  variable onto the stack and yields its home's address; its `to_rai()` leaves
  the variable out of the reads. The exporter emits a typed `register_address`
  op naming the source register. Every virtual register already has a stack slot
  on SH-4, so the lowerer pins the addressed one outside slot reuse (like desktop
  `force_on_stack`), roots its writes for dead-code removal, and stores the
  slot's native address.
- **`(go ...)` from a method** (fact-h's `fact-info` new): the frontend resets
  `pp` (r13) at its first use, after a call and a branch. Desktop `IR_ValueReset`
  emits no code and `pp` is preserved across calls, so in a read-only context
  (nothing but resets writes `pp`) that reset names the entry value, which the
  lowerer already reads from the authenticated getter. An earlier read or write
  of `pp` still refuses, and a lexical bracket (save, select, restore) keeps the
  strict layout.
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
- float-derived value types (`meters`, `degrees`) take the `float` ABI, like
  bitfields take `int`/`uint`;
- `--batch`;
- a (none) function with value early returns (timer-reset's PC-only
  `(return (the uint 0))`) stays (none), since callers cannot observe the value.

Runtime:

- **`structure` new**: the native counterpart of kscheme `new_structure`.
  `(new 'global 'matrix)` failed until it existed. It allocates `allocated_size`
  bytes, 16-byte rounded and zeroed, from the global heap.
- **kscheme default methods**: `object` asize-of (the zero function) and copy
  (`copy_fixed`), `basic` asize-of (`asize_of_basic`) and copy (`copy_basic`),
  propagated to the fixed kernel types like structure new. surface-h's
  `(copy *walk-mods* 'global)` needed them. Desktop `copy_basic` asks the type's
  asize-of method for the size; calling a GOAL method from C is not verified yet,
  so a type that overrides asize-of refuses with `UNSUPPORTED_TRANSFER`, as does
  desktop's raw-address ("DANGER COPY BASIC") destination. The runtime suite's
  find-parent zero-slot check moved to slot 7 (relocate), since slot 6 is now
  filled, as on desktop.
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

- **Loading:** all 144 compiled files now load and start (244 of 244 boot checks),
  after the memory budget in [MEMORY_BUDGET.md](MEMORY_BUDGET.md): the corrected
  `PROCESS_HEAP_MULT`, PS2-sized DMA buffers, native `kmalloc`/`scf-get-time`, and
  a faulting `__pc-get-mips2c` stub. The boot arena is now 10 MiB (8 MiB filled
  at projectiles-h, object 116) and holds 9.27 MB after 144 files.
- **Lowering stops at navigate-h (object 144)**: "implicit process/self entry
  ABI not implemented for this signature". Behaviors and event handlers receive
  the process implicitly; the same family accounts for most of the 54 exporter
  alias refusals (`refusal_report.py` groups them).
- **Exporter refusals.** 56 of the 331 dependency-graph files refuse; 54 of them
  for one reason, "conflicting or non-fixed virtual register alias". That is one
  exporter feature (rlet register aliasing) to add next.

No hardware claims. Everything is verified in Flycast.
