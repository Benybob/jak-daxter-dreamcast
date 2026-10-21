# Native Jak 1 runtime foundation

2026-10-21. AI-assisted work. Full kernel and gameplay remain unfinished.

`goal_runtime.c` implements a bounded global arena, original symbol layout,
mutable symbol values, fundamental type fields/hierarchy, raw method slots,
tagged basic/pair allocations and GOAL strings. On SH-4, stored references are
native 32-bit addresses in the supplied arena. Host tests resolve the same
layout using an explicit 32-bit base; they do not execute or interpret GOAL.

`generate_runtime_layout.py` reads `common/goal_constants.h` and
`common/symbols.h`, checks the tagging/alignment assumptions, and fingerprints
the original headers. The committed `goal_layout.h` is checked against those
sources. Type fields follow `game/kernel/jak1/kscheme.h`; hashing, fixed slots,
metadata offsets and probing follow `game/kernel/{common,jak1}/kscheme.cpp`.

- 16,384 symbol slots, eight bytes per value/type-tag slot, with the metadata
  half giving a 262,144-byte table allocation.
- `s7` at table + 65,540; false at `s7`, true at `s7+8`.
- Symbol name references at symbol + 131,072; original CRC hashing.
- Basic type tag at object - 4; pair car/cdr at object - 2 / object + 2.
- Type symbol/parent fields at 0/4, allocation/padded/heap-base/method count
  at 8/10/12/14, and 32-bit method slots beginning at 16.
- Sixteen-byte aligned zeroed bump allocations, bounded pointer resolution,
  explicit allocation and table failures, and bounded type-chain traversal.

The eleven bootstrap types are object, structure, basic, type, symbol, string,
function, pair, number, integer and binteger. Their original hierarchy, fixed
symbol positions and sizes are retained. Other builtins are not initialized
yet; their fixed slots are reserved. The C foundation leaves method slots empty;
the separate bridge profiles install bounded native pair/basic/type constructors.
Original `deftype`/`defmethod` compilation now creates bounded classes and native
methods, including optional `method-set!` propagation. See `TYPE_RUNTIME.md` for
its supported flags, controls and limits; full kernel initialization remains open.
New child slots beyond the parent's declared table are zeroed rather than
copying beyond the parent as the upstream code's documented bug does.

## Verified

The same 81 C checks pass on the Windows host and SH-4/Flycast with 16 MiB RAM.
They include original fixed offsets/hashes, intern identity, mutable values,
strings, tagged basics/pairs, type inheritance, method bounds, cyclic types,
corrupt name data, integer/address overflow, arena exhaustion, full table
probing and outer memory canaries. The broader host suite passes 93 tests, with
55 additional shared C type/method checks in the dedicated type runner.

The dedicated target allocates a 1,048,576-byte arena plus 32 guard bytes.
Bootstrap consumes **263,104 bytes**; the positive cases use **263,328 bytes**
before deliberately filling the arena and corrupting test metadata. This is a
diagnostic footprint, excluding the executable, KOS, C context, stacks, allocator
overhead and renderer. It is not a whole-game memory measurement.

```powershell
./build_host_windows.ps1
python build_dreamcast.py --target jak-dc-runtime.elf
python verify_backend.py ../../../flycast/flycast.exe jak-dc-runtime.elf --nm ../../../dc-sdk/sh-elf/bin/sh-elf-nm.exe --output build/verify-runtime --tests 81 --suite runtime
```

Run from `ports/dreamcast` in the surrounding documented workspace layout.
The runtime target checks native C kernel primitives. Process heaps, top
allocation, remaining builtins, kernel C bridges, full symbols/types semantics,
original object linking, process switching and gameplay remain open.

## Compiled GOAL integration

`python build_runtime_goal.py --verify` regenerates the fixture and unchanged
original `identity`, builds `jak-dc-runtime-goal.elf` and checks it in Flycast.
The optional `--frontend`, `--flycast`, `--nm` and `--output` paths override the
surrounding workspace defaults. Build the original frontend first using
`build_goal_frontend_windows.py` when absent.

The combined target passes **7,932/7,932 checks at 16 MiB RAM**: 425 shared C cases
(81 foundation, 55 type/method and 289 fixed-kernel-metadata cases) plus 7,507 compiled-GOAL/module/startup/
memory/static/math/bridge/quote/void/class cases in a reinitialized arena. These prove
native function tags, mutable symbol reads/writes, signed 32-bit symbol cells,
false/true/empty constants, truthiness, native calls through mutable function
cells, function rebinding and original `identity` values. Optimized original
code (6 bytes, no frame) agrees with the baseline (72 bytes, 40-byte frame) on
seven cases. The modules emit 22,198 total code bytes including both identity bodies,
startup, memory/static/math/bridge probes and original list/math routines.
Positive arena use is 295,424 bytes with 200 installed symbols (including fixed
bootstrap slots); capacity is 1 MiB.
These figures exclude KOS, executable, stacks, allocator overhead and renderer.
See `ARCHITECTURE.md` for code-density and graphics-memory priorities.
The optional full fixed-type metadata stage is described in `KERNEL_TYPES.md`;
it does not complete the process/heap runtime or remaining original methods.
The shift matrix and its separate 3,244-byte baseline module are included in
these diagnostic totals; see `INTEGER_SHIFTS.md` for the actual coverage and
the same-source 836-byte optimized module. Large vector counts do not imply
complete game coverage.
Signed/unsigned low-32 division and original quotient/mod/rem wrappers now run
with an explicit bootstrap fault-status bridge. See `INTEGER_DIVISION.md` for
valid-domain guarantees and remaining exception semantics.

`goal_module.c` loads in-memory compiler modules, **not the original GOAL object
format**. It allocates tagged functions, publishes names, patches `cell:` and
`pointer:` imports, plus direct `function:` object references, and synchronizes
caches after relocation. A temporary
**262,144-byte** symbol-table snapshot supports atomic rollback of arena usage,
interned names and replaced bindings. Failed loads do not execute or synchronize
partial code. Loading requires a quiescent runtime. The host runner checks 85
relocation/bounds/rollback assertions, including late failure, OOM and builtins.

The runtime compiler profile uses native GCC uint64 register pairs for scalar
containers, including 32-bit tagged pointers; C bridges must marshal this ABI.
At most eight homogeneous scalar arguments are supported. The first two integer
containers use register pairs; the rest use eight-byte GCC stack slots. Float
arguments use the eight verified single-precision positions. Mixed parameter
lists remain rejected. Runtime calls use the original call's concrete ABI
metadata and require a function-typed value; the numeric profile still requires
declared immutable dependencies. Bound functions must retain a compatible
signature; there is no runtime signature enforcement. Symbol cells retain 32
bits, with signed reads driven by original IR.

## Explicit native initialization

`--runtime --initializer UNIQUE-NAME` exports the original frontend's single
zero-argument top-level function in addition to selected functions. Only its
export name changes; instructions retain original source order. An unsupported
operation or omitted function dependency causes compilation to fail. Function
signature types are scalar tagged pointers at the native ABI boundary.

Linking and initialization are separate. `dc_goal_load_module` installs code
and relocations; `dc_goal_run_initializer` invokes the named native zero-argument
entry after cache synchronization. The caller decides when to run it. Startup
is **not transactional**: arbitrary native side effects cannot be rolled back
by the linker's symbol snapshot. Invalid/missing function entries are rejected
without changing the output argument. Host builds refuse SH-4 execution.

The test compiles the unchanged original `identity` definition selected directly
from `gcommon.gc`, then runs that definition's top-level binding and checks its
function-object return. The separate synthetic startup file initializes 37,
sets a tagged true value, calls a function that changes the global to 42,
stores that result, increments an existing external global and installs a
native function alias. Compiled readers observe the initialized values. A
deliberately rebound function cell is restored by the original direct object
reference, proving that it differs from a mutable symbol-value import.

No-result functions and original top-level bodies ending in `(none)` now use
the C void ABI. `dc_goal_run_initializer_void` invokes them without reading an
undefined result. Typed void calls retain their side effects and argument ABI.
Original phantom result copies become no-ops; using a nonexistent result is
rejected by definite-assignment checks. Native tests cover empty void bodies,
indirect callbacks, live locals across calls, rebinding and repeat startup.
Value-returning functions with a reachable return-free fallthrough are rejected.
Initializer-only files need no `--function` selection. Truly empty original IR
and no-op-only void bodies emit RTS plus its delay slot: four bytes, no frame.

This is scalar top-level execution, not complete kernel initialization. General
static structures, automatic inspectors requiring formatting bridges,
broader ABIs, processes and the original object format remain
open. The older numeric runtime fixture still intentionally loads without an
initializer; its zero-valued global tests that distinct path.

## Native fields and original lists

Typed memory IR lowers scalar byte, 16-bit, 32-bit, 64-bit and float loads/stores,
including sign extension and tag-adjusted negative offsets. Native tests cover
every integer width/sign, exact-width stores with outer canaries, float field
mutation/signed zero, a 320-byte offset, type headers and pair mutation. Typed
addresses must satisfy SH-4 natural alignment (64-bit integers use two four-byte
accesses); emitted code does not dynamically check invalid/unaligned addresses.

Unchanged original `ref`, `last`, `member` and `assoc` from `gcommon.gc` execute
on native pairs, covering matches at all positions, absent/empty lists,
singletons and false-valued elements. Bodies occupy 224/164/512/552 bytes and
use 48/40/48/48 stack bytes. Distant conditions expand automatically while
near conditions stay compact. GNU assembler checks forward, backward and
cascading relaxation with literal references. BRA/literal-pool range limits
still fail explicitly when exceeded.

`dc_goal_init_native` selects cached **P0** aliases for GOAL references; the C
resolver can retain its KOS P1 allocation pointer. Original signed object-cell
loads then agree with pointer constants because arena/code references are
below bit 31. Negative bintegers retain signed semantics. The first native list
test caught the previous P1/sign-extension mismatch; both pointer objects and
negative binteger cells now pass. These are direct SH-4 hardware aliases, not
pointer interpretation. The helper rejects MMU-enabled operation and arenas
outside stock 16 MiB main RAM. Bridges must convert KOS pointers to the GOAL
address domain; enabling the MMU later requires an explicit mapping design.

## Static strings and float constants

The frontend exports exact float32 payloads and a stable inventory of static
objects. The backend copies float bits to literal pools and selects only data
objects referenced by compiled functions/startup. String objects retain their
original type header, length, raw bytes and terminator. Unsupported referenced
static kinds fail explicitly; general static structures are not supported yet.
Quoted pair graphs are described below.

`dc_goal_load_module_data` allocates private data in the same bounded arena,
patching `type:` and module-local `data:` imports after all allocations. Local
data IDs do not occupy global symbol slots. Rollback includes data allocations
and replaced function bindings. The temporary address table costs four bytes
per data object in addition to the 262,144-byte symbol snapshot.

Native checks cover two strings (54 original payload/header bytes), literal
identity across calls, initialized string/float globals, exact float bits,
float-literal arithmetic, typed byte/length fields and visible string mutation.
A late missing-type failure restores the old code/data on SH-4. Host checks
also cover embedded NUL bytes, local data-pointer relocation, invalid metadata,
missing data/type targets and allocation failure. The API consumes trusted
compiler descriptors; it is not a parser for arbitrary binary object files.

## Floating-point control and conversions

Float branches now preserve the pinned frontend's UCOMISS/unsigned-condition
contract, including unordered EQ/LT/LE behavior. This is intentionally different
from substituting C predicates. An independent x86 instruction oracle checks
nine relation vectors and eleven conversion vectors; SH-4 executes all six
relations for finite values, signed zero, infinities and a NaN payload.
MIN/MAX preserve second-operand selection for equal zeros and NaNs. Scalar
FSQRT, signed low-32-bit int-to-float and truncating float-to-int also execute;
positive overflow/NaN conversions explicitly produce the pinned SSE invalid
result `INT32_MIN`, matching negative overflow. Native tests verify RM=0
(round to nearest) in the pinned KOS startup profile. Exception flags and all
subnormal/NaN payload behavior are not a complete cross-platform equivalence
claim; negative square root is checked for NaN rather than a particular payload.

Unchanged original `truncate`, `integral?`, `fractional-part`, `seek`,
`lerp-clamp` and `lerp-scale` execute. Tests cover both movement directions,
target capture, lower/upper/interior interpolation and the original division-
by-zero saturation path. Their bodies occupy 184/252/244/630/460/888 bytes.
They join original `identity`, the earlier `lerp` checkpoint and four native
list routines; these are kernel/math building blocks, not a gameplay slice.

## Allocation and typed kernel bridges

The original `cons` macro now calls the pair type's new method through a typed
function pointer. Two boxed 12-byte SH-4 tail stubs bridge to native C pair
allocation and a namespaced type-of helper, preserving PR and stack arguments.
Pair allocation accepts the original fixed `'global` selector and uses the
bounded arena; other selectors and exhaustion report explicit errors. This
does not install an original GOAL heap object or implement process/debug heaps.
Fixed-symbol installation refuses slot/name collisions and preserves an
existing global symbol value during bridge setup.

Native checks cover third/eighth incoming integer arguments, an eight-argument
compiled call, original `cons`, subsequent original list traversal, typed C
type results, constructor method replacement by compiled GOAL, restoration,
OOM and unsupported heap selection. GCC assembly independently verifies all
eight integer positions, and GNU assembler checks the tail stub. The simple
argument-return optimization deliberately skips stack-returned arguments.

Bridge installation is quiescent and supports one active runtime. Its context
must outlive calls and be cleared before destruction; the diagnostic does so.
This is a small explicitly marshalled kernel interface, not the original full
kernel bridge ABI or an implementation of general process scheduling.

## Quoted pair graphs

Original frontend static pairs now export their typed car/cdr values directly,
retaining signed 32-bit boxed constants, interned symbol references, data
references and selected function references. The linker selects the transitive
data closure and allocates each static ID once before patching references.
Forward/shared references are preserved; the host closure test also terminates
on a cyclic descriptor. Missing data/functions and unsupported atoms fail.

The native fixture contains nine objects (eight pairs and one string, 77 raw
bytes). It passes original reader quote -> typed IR -> native initialization
with a nested list, a negative boxed integer, string, repeated symbols, canonical
empty tail, persistent literal identity and mutable pair data. The original
`ref`/`last` routines consume this graph. This extends the compiler bootstrap
data format, without implementing the original game object-file format.

## Stack-slot reuse and copy coalescing

CFG liveness now includes loops and live values across calls, allowing virtual
values with disjoint lifetimes to reuse eight-byte stack slots. Incoming
arguments remain mutually distinct during entry stores. Same-class moves
between identical slots disappear; conversions and side effects remain.
`--no-optimize` retains the original separate-slot baseline.

For the identical 365-check runtime module set, emitted code fell from 15,410
to 11,850 bytes (3,560 bytes saved). Original `lerp-scale`'s frame fell from 296
to 80 bytes; `assoc` fell from 200 to 48. The separate numeric/call/`lerp`
regression still passes all 39 native checks, including values live across
multiple calls, with code reduced from 2,730 to 1,886 bytes. The original
`identity` remains six bytes with no frame. Reports retain baseline frame sizes,
used-slot counts and eliminated-copy counts. These are verified size/behavior
results, without claiming a measured physical-console speedup.

## Kernel and native file continuation (2026-10-21, AI-assisted)

The current diagnostic passes 7,982 native checks and 98 host tests. New support
includes void/empty startup, compiled classes and method dispatch, original
kernel type metadata, 64-bit shifts, 32-bit division/remainder and buffered
native module loading. See KERNEL_TYPES.md, INTEGER_SHIFTS.md,
INTEGER_DIVISION.md and NATIVE_MODULES.md for scope, counts and ABI limits.
The separate original-data diagnostic passes 182 native checks; the read-only
parser accepts all 668 V4 objects across 36 local retail archives. This validates
data relocations, not execution of original MIPS code or a full kernel boot.
Earlier numbered measurements above describe their named historical probes.

The named-local continuation is documented in LOCAL_VARIABLES.md. It preserves
entry ABI argument order independently of named locals, rejects unsupported
fixed process/self entries and retains definite-assignment validation. The
current runtime suite is 8,014 checks with 106 host tests; earlier counts refer
to their dated checkpoints. Complete gcommon exports 66 typed functions, but
full-module SH-4 lowering and kernel execution remain unfinished.
