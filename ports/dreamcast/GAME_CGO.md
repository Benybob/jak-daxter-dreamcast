# Compiling and loading the original GAME.CGO (AI-assisted)

2026-10-27, Claude Code. The original engine is being compiled file by file in
GAME.CGO order:

- **192 of GAME.CGO's 326 code files** compile to native SH-4 (through entity-table).
- **All 192 load and run their original top-level startup** after KERNEL.CGO in
  stock 16 MiB Flycast, with the Dreamcast memory budget ([MEMORY_BUDGET.md](MEMORY_BUDGET.md)). This includes the 141 lane-exact
  vector cases ([VU0_LOWERING.md](VU0_LOWERING.md)) and the original process
  spawn and dispatch.
- **292 of 292 boot checks pass.**

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
- **Behaviors with arguments** (navigate-h's nav-mesh-connect): `self` sits
  beside ordinary arguments, with any return the generic ABI accepts. The
  prologue stores arguments before IR runs, so the entry `current-process` call
  cannot clobber them. The frontend's combined entry value-reset is split into
  the arguments (defined by the prologue) and `self`'s own reset. `self` must
  not alias an argument, and the exporter now decides "is a process" with the
  type system (`process_type`, so camera-slave or target behaviors qualify and a
  basic does not), instead of the type name `process`.
- **PS2 constant idioms**: `.pceqw x,x,x` (all ones) and the `pcgt*`, `psubw`,
  `pxor` self forms (zero) read no value, so `x` may be unassigned (bsp).
- **A value stored unassigned** (bsp-camera-asm stores its back-flags without
  setting them when the root is already a leaf, whatever the PS2 register held):
  a register assigned on another path and read only as a store's value is
  zero-filled at entry in a pinned slot, like vf. Returns, branches and
  arithmetic on unassigned registers still refuse.
- **Shared 128-bit slots** (font's draw-string has 287 128-bit values): int128
  and vf values now share 16-byte slots by the same liveness as desktop xmm
  allocation, colored apart from 8-byte values. vf values read before any write
  stay live from entry, where every vf slot is zero-filled; addressed and
  zero-filled ones stay pinned.
- **Constant branches**: an eq/ne branch on two values that each have exactly one
  writer, a constant or symbol pointer (through copies), has one possible edge
  (the `loop` macro's `#t != #f` test, `(b! #t ...)`). Definite assignment skips
  the impossible edge (decomp).
- **Reviewed never-assigned locals** (`dreamcast_unassigned_locals.json`): the PC
  port comments out some original asm, leaving `local-vars` no op assigns
  (sparticle's `.mfc0 ... Count` cycle-counter reads). Each listed function and
  local has a reason; those are zero-filled at entry and recorded in the
  manifest. A listed local that is assigned or missing fails, unused entries are
  reported, and anything unlisted still refuses, as the host tests assert.
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
- **`'debug` heap**: the kernel now binds the fixed `debug` symbol as desktop's
  `set_fixed_symbol(FIX_SYM_DEBUG_HEAP, "debug", ...)`. Without a debug segment
  (MasterDebug off) its kheap is null and desktop kmalloc falls back to the
  global heap, so native `malloc 'debug` does the same (subdivide's perf stats).
- **kscheme default methods**: `object` asize-of (the zero function) and copy
  (`copy_fixed`), `basic` asize-of (`asize_of_basic`) and copy (`copy_basic`),
  propagated to the fixed kernel types like structure new. surface-h's
  `(copy *walk-mods* 'global)` needed them. Desktop `copy_basic` asks the type's
  asize-of method for the size; calling a GOAL method from C is not verified yet,
  so a type that overrides asize-of refuses with `UNSUPPORTED_TRANSFER`, as does
  desktop's raw-address ("DANGER COPY BASIC") destination. The runtime suite's
  find-parent zero-slot check moved to slot 7 (relocate), since slot 6 is now
  filled, as on desktop.
- **Sized type placeholders**: level-h's static data referenced `level` before its
  deftype, so the placeholder got the default 12 methods and the 29-method
  deftype failed. That failure was invisible: the next runtime call reset
  `rt->error`. Now `deftype:NAME:COUNT` carries the module's own deftype method
  count, an explicit `(type-ref NAME :method-count N)` carries its count (goalc
  records it on the static type record; desktop links these through
  `intern_type_from_c`), and a failed type bridge is sticky
  (`dc_goal_sticky_error`) so the DGO loader reports it. A basic's own type tag
  and other plain type pointers stay strict `type:` imports.
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

- **Loading:** all 192 compiled files now load and start (292 of 292 boot checks),
  after the memory budget in [MEMORY_BUDGET.md](MEMORY_BUDGET.md): the corrected
  `PROCESS_HEAP_MULT`, PS2-sized DMA buffers, native `kmalloc`/`scf-get-time`, and
  a faulting `__pc-get-mips2c` stub. The test ELF now carries GAME.CGO as zstd
  (4.9 MB → 0.66 MB, 128 KiB window), streamed object by object through the
  vendored zstd decompressor (`goal_dgo_zst.c`), so the ELF fell from 6.2 to
  2.0 MB and the boot arena is 12 MiB; 192 files use 11.45 MB of it.
- **Stops at loader (object 192)**: the exporter refuses `ja-play-spooled-anim`
  ("conflicting or non-fixed virtual register alias").
- **Exporter refusals**: 56, 54 of them "conflicting or non-fixed virtual
  register alias", mostly event handlers and state code (`refusal_report.py`).
- **Exporter refusals.** 56 of the 331 dependency-graph files refuse; 54 of them
  for one reason, "conflicting or non-fixed virtual register alias". That is one
  exporter feature (rlet register aliasing) to add next.

No hardware claims. Everything is verified in Flycast.
