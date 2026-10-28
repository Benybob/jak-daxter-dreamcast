# Compiling and loading the original GAME.CGO (AI-assisted)

2026-10-27, Claude Code. The original engine is being compiled file by file in
GAME.CGO order:

- **All 331 files** of the GAME.CGO dependency graph export (no refusals).
- **275 of GAME.CGO's 326 code files** compile to native SH-4 (through task-control).
- **The first 237 load and run their original top-level startup** after KERNEL.CGO in
  stock 16 MiB Flycast, with the Dreamcast memory budget ([MEMORY_BUDGET.md](MEMORY_BUDGET.md)). This includes the 141 lane-exact
  vector cases ([VU0_LOWERING.md](VU0_LOWERING.md)) and the original process
  spawn and dispatch.
- **347 of 347 boot checks pass.** That includes process spawn from a file's startup
  (time-of-day), and that spawned process running and suspending under the original
  dispatcher, described below.

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
- **Register aliases** (54 exporter refusals): several frontend RegVals share one
  register, most often a behavior's `self` and `pp` on r13. They always agree on
  class and ABI, so the exporter writes one record whose type is their lowest
  common ancestor; differing class or ABI still refuses. The `pp` role check now
  asks the type system whether each view is a process.
- **`(suspend)` in a behavior**: the existing suspend idiom now also covers `pp`
  as a behavior's `self`. The entry value-reset stays the entry read, the binding
  stays, and the post-resume re-read writes `self`. The KERNEL_DEBUG stack check
  reads `rsp`, which is now also defined at entry: the native SP is fixed across
  the IR body.
- **A void call's result as a value** (game-save's `(('die) (deactivate self))`):
  copying a void call's phantom result into a value register defines it as `#f`
  (desktop leaves the callee's rax). A `none` register nothing produces still
  refuses.
- **No debug segment**, as a retail kernel: desktop klink with DebugSegment 0
  never copies debug-segment code and makes references into it null, and
  `*debug-segment*` is `#f`. `lower_sh4_ir.py --no-debug-segment` drops those
  functions (915 in the first 222 files), loads 0 for references in code and
  removes static relocations to them; `method-set!` of 0 is a no-op as on desktop.
- **MIPS `$zero` locals**: the decompiler keeps pipeline padding such as
  `(.mov r0-0 f31-0)` and names `$zero` as `r0-N` or `zero`. Writes to such a
  local define it as 0 without reading the source, and a never-assigned one reads
  as 0, as the hardwired register does (main-collide feeds it to `.ppach` and
  `.pextlh` as the zero operand).
- **Fixed-register resets**: an `r15` reset reads the native memory base 0 (any
  real write still refuses); `(go ...)`'s `.push (r15 + return-from-thread)` jump
  drops its own `r15` reset. A non-behavior `pp` used by several `(suspend)`s
  (the frontend emits one reset for all of them) gets one authenticated read at
  entry. `#f` into an int128 register is the pointer in the low 64 bits, high 64
  zero, as desktop `movq`.
- **Integer value types** (`seconds`, `time-frame`) take the int/uint ABI of their
  signedness, like float aliases take float.
- **Reviewed stale locals** (`dreamcast_unassigned_locals.json`): entries may set
  `assigned_elsewhere` for a local the original sets on other paths but reads
  stale on one, and `false_locals` for one only tested for truth, which starts as
  `#f`. The current entries are original compiler-bug patterns the source itself
  documents (update-vis-volumes, birth-pickup-at-point) and find-overlapping-shapes'
  stale `s2-0`.
- **Float top levels** run as void initializers (mood-tables): every loader
  discards a file's top-level value.
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
- **Heaps as desktop `alloc_from_heap`**: `'debug` with its null kheap allocates
  globally; any value outside the symbol table is "treated as a stack address"
  and the object is zeroed in place (dead-pool-heap's get-process passes the gap
  address). Level kheaps still refuse.
- **State-valued methods**: `method-set!` accepts an instance of the GOAL `state`
  type (virtual states, game-save's auto-save) as well as a function.
- **System-config services**: `scf-get-language/aspect/volume/territory/timeout/
  inactive-timeout` with desktop kboot's defaults (English, 4:3, 100, SCEA, 0, 0).
- **Diagnostics**: the first BAD_ARGUMENT refusal while GAME.CGO loads is kept as
  `dc_boot_game_error_site` (file code << 16 | line), readable with
  `verify_backend.py --dump dc_boot_game_error_site 4`.
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

- **Lowering:** all 326 GAME.CGO code files lower to SH-4. The last fixes were:
  - a void call's phantom result copied into an integer register is 0
    (process-taskable);
  - `number` and `binteger` are 64-bit GPR values (pov-camera, progress-pc);
  - a reviewed partial return in `draw-subtitle`, whose true path ends in a
    `#when` that compiles away;
  - a behavior's own `(protect (pp) (set! pp ...))` brackets in subtitle, each
    lowered to a lexical scope enter and leave (`self_process_brackets`).
- **Loading:** the first 325 files load and start (439 of 439 boot checks), and the
  processes they spawn run their dispatcher frames. Memory is no longer the limit:
  see [MEMORY_PLAN.md](MEMORY_PLAN.md) for the freed startups, the VRAM-staged
  romdisk, the 13.5 MiB arena and the PS2 DMA buffers in VRAM.
- **hud-classes-pc (object 325) stops at a nested spawn.** Its startup spawns the PC
  HUD, whose init spawns a manipy icon process (`hud-pc-make-icon`). That child's
  `go` throws `'initialize`, and `throw` finds no matching catch frame
  ("ERROR: throw could not find tag initialize", then `(break)`, which is
  `(/ 0 0)`). A process spawned from inside another spawned process's init, from a
  startup, is the next native initialization case to support.
- **Spawned processes.** A camera asks `stack-size-set!` for 512 bytes (sized for PS2
  frames) but suspends with 668 bytes of native stack in use, so the backup
  counterpart passes `max(2 * request, 512)` to the original method.

## Smaller native code (2026-10-28, AI-assisted)

The lowering keeps every value in an 8-byte stack slot, and a slot access used to
take five instructions. These changes shrink code without changing the slot model:

- A 64-bit or 128-bit slot within 60 bytes of the frame base uses
  `MOV.L @(disp,R14)`: two instructions per 64-bit slot.
- Float slots use `FMOV.S @(R0,R14)`. No caller holds a value in R0 across one.
- Slot colors are numbered by access count, so hot slots land in range.
- A slot reloaded right after its store, into the same registers and with no branch
  target between, isn't reloaded.
- Field accesses load only the base's 32-bit address word, skip a zero offset, and
  use `MOV.B/W/L @(disp,Rm)` in range.

The 237 files that loaded before went from 12.44 MB to 10.54 MB of arena. Anonymous
functions referenced from static data (structure relocations and pair atoms) are now
qualified like the functions themselves, which let target-death lower again. A
register allocator is the larger next step.

## Process spawn from a startup (2026-10-27, AI-assisted)

time-of-day's startup runs `(process-spawn time-of-day-proc :init init-time-of-day)`.
Desktop `call_goal` enters a file's top level from C with `pp = #f` (r13 is the
symbol table), and the original `run-function-in-process` saves that `#f`, sets
`pp` to the new process and runs the init function inside its catch. Native
support for this:

- **The root.** The root has these conditions:
  - no execution context bound;
  - no fiber running;
  - the kernel context names no current process;
  - the scope and span ledgers are registered.

  In that state, `dc-c-current-process` returns `#f`.
- **Root scopes.** A lexical pp scope may be entered from the root, with `#f` as its
  scheduler. The root owns no reservation or PID, and only the root C stack can
  establish it. `(set! pp old)` restores `#f`.
- **Spawned extents.** Dead-pool-heap members are carved by original GOAL from the
  pool's C-allocated heap, so no C allocator notes them. The root's scope entry
  notes the process's own record (allocated-length + 112). The process heap
  validator checks it first.
- **Process subtypes.** The process heap validator accepts any process subtype. For
  those, `activate` starts the heap at `(-> stack)` plus the type's heap-base.
- **Root stack objects.** In-place `new` also accepts the live part of the current
  KOS stack. sky-tng's startup builds sky-vertex objects in a
  `(new 'stack-no-clear ...)` array.
- **The boot.** It registers both ledgers for the GAME.CGO load, checks that every
  startup scope was left, and detaches them before the later dispatcher test.

Two runtime checks expected an unbound C caller to refuse: original
`send-event-function` and `deactivate`. They now check the desktop root behaviour
instead: the body runs with `pp` set to the target, and `pp` is `#f` again afterwards.

Not verified yet: `shrink-heap` after init leaves the noted span at its pre-shrink
extent.

## Running spawned processes, and faults the loader used to miss (2026-10-27, AI-assisted)

time-of-day's spawned process now runs under the original kernel-dispatcher: its
state code updates the clock and suspends each frame, and its `:post` hook
`time-of-day-update` runs on the original DRAM stack. Getting there fixed several
real defects. Two of them mean earlier "loads" were wrong.

- **Hidden faults (a correction).** `dc_goal_find` resets `rt->error`, and bridges call
  it mid-GOAL. So a call to a missing function could fault and then be erased before
  the loader checked. gsound's and pckernel's startups had done exactly that since
  they first "loaded". A call to a non-function is now a sticky fault the loader
  always sees, and the root `pp` probe keeps a pending fault too.
- **Method propagation.** Desktop raises `*enable-method-set*` while it loads
  KERNEL.CGO (kscheme.cpp) and GAME.CGO (kmachine.cpp). That lets a method defined
  on a parent after its child types exist reach the children. The native boot never
  did, so `cpu-thread` had no `stack-size-set!` (method 9 was 0). The boot now
  brackets both loads the same way.
- **PC-port services.** pckernel's `*pc-settings*` construction calls the PC kernel's
  `pc-*` functions. On a Dreamcast there is no PC window, display manager, keyboard,
  mouse or writable storage, so each service gives the answer desktop gives with no
  main display. Getters return those defaults, the 23 setters change nothing,
  `pc-get-os` is `#f`, `pc-filepath-exists?` is `#f`, and `file-stream-open` records a
  failed open. Desktop's three `InitMachine_PCPort` strings are set too: user and
  settings folders `/vmu/a1`, build `dreamcast`.
- **No IOP.** gsound's startup loads sound banks over IOP RPC. `rpc-busy?` is 0 and
  `rpc-call` returns at once with nothing received. Sound and loader RPCs are dropped
  until a Dreamcast AICA sound driver serves them.
- **`format` destinations, as desktop kprint.** A string destination is appended to
  (`kstrncat`, bounded by its allocated length). Before, it went to the console,
  which left pc-settings' path strings empty. A file stream with no open file takes
  nothing, and an unknown destination refuses. `~A`/`~S` of a string print up to its
  NUL, not its whole allocated length.
- **Root stack objects.** A `(new 'stack ...)` file-stream or string in a top-level
  startup lives on the root KOS stack. `kopen` and `format` read it there, like
  in-place `new`.
- **The main-thread backup floor.** A suspended native main thread needs 348 bytes,
  and `dreamcast_thread_context.gc` makes 512 every activate's default. The game asks
  `stack-size-set!` for 128 bytes in 10 places (time-of-day among them). The native
  counterpart passes the unchanged original method `max(request, 512)`: smaller
  requests keep 512, and larger ones (1,024) still grow.
- **Spawned fibers.** The first time the dispatcher resumes a thread with no
  registered fiber, it imports it. That happens only for an armed main thread on
  the shared fake-scratchpad stack, which is activate's PC-port stack for
  process-spawn's default. The extent comes from the process's own record (noted in
  the ledger), and the fiber from a pool of 16. Anything else still refuses.
- **The DRAM post stack.** Original post hooks run on `*kernel-dram-stack*`, which is
  `*dram-stack*` + 32 KiB from the array's tagged pointer. The boot registers the
  array's aligned data span. `reset-and-call` treats the original top, up to 15
  bytes above it, as that stack.
- **Module-local lambdas.** Eighteen GAME.CGO files each define `anon-function-1`.
  Desktop links lambdas by address and never makes them global, but native modules
  export by name, and code entry is checked against the symbol that still holds the
  function. The GAME build now lowers with `--qualify-anonymous`, which names them
  `MODULE:anon-function-N` (`tests/test_qualify_anonymous.py`).

No hardware claims. Everything is verified in Flycast.
