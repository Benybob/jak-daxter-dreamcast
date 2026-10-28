# Native mips2c on SH-4 (2026-10-28, AI-assisted)

The PC port replaced some original hand-written MIPS/VU0 assembly with C++
translations in `game/mips2c`. jak1 has 25 such files registering 94 functions
(`survey_mips2c_users.py`). Nine files, the survey's first nine by GAME.CGO
callers, now build natively for SH-4 and run in the Dreamcast boot: 53 functions,
returned for 49 of GAME.CGO's lookups.

| File | Functions | Differential case |
| --- | --- | --- |
| collide_cache | 14 | none yet |
| sky_tng | 13 | init-sky-regs, set-sky-vf27/vf23, render-sky-tri (clipping) |
| collide_edge_grab | 4 | edge-work methods 16 and 15, hold-list add-to-list! |
| collide_mesh | 4 | methods 14 and 15 |
| generic_effect | 4 | generic-light-proc |
| generic_merc | 4 | high-speed-reject |
| ripple | 4 | ripple-execute-init, apply-wave-table, matrix-scale |
| bones | 3 | draw-bones-check-longest-edge-asm (four radii) |
| generic_effect2 | 3 | generic-no-light-dproc |

The other 16 files still get the faulting stub.

## How it works

- **Unchanged sources.** `build_mips2c.py` compiles the files with `sh-elf-g++`
  against `mips2c_dc.cpp` (the services they need) and `mips2c_dc/immintrin.h`
  (a scalar stand-in for the eight SSE intrinsics `vu.h` uses). The only change to
  shared code is a `__sh__` branch in `mips2c_private.h`'s `jalr`, for calling
  back into GOAL, and an SH-4-only PS2 hardware-register window (below).
- **Memory model.** GOAL addresses on the Dreamcast are cached P0 aliases of the
  same RAM and the MMU is off, so a GOAL address is a C address and
  `g_ee_main_mem` is 0. `intern_from_c` returns the native symbol address.
- **Calls into C.** On desktop, GOAL calls a trampoline (`_mips2c_call_*`) that
  builds an `ExecutionContext`: arguments in a0–t3, `pp` in s6, the symbol table in
  s7 and a stack in sp. The build generates one C wrapper per registered name with
  the GOAL function's own native signature, so GCC passes and returns exactly what
  the lowered GOAL code does. Float arguments come in FPU registers and are stored
  as their bits, since desktop GOAL passes every argument in a GPR, and a float
  result is returned from v0's bits. Signatures come from each file's
  `def-mips2c` type, or a method's `;; N` entry in `all-types.gc` (searching
  ancestors for inherited methods). Helpers typed only `function` are called from
  other mips2c code with integer registers.
- **Lookups.** `__pc-get-mips2c` asks `dc_mips2c_find` for the name and loads a
  12-byte GOAL function (`dc-mips2c:NAME`) that jumps to the wrapper, made once
  per name. GAME.CGO's 43 lookups of these names now get native code.
- **Calls back into GOAL.** `jalr` calls the GOAL function directly with eight
  64-bit arguments. A float-returning native GOAL function now also leaves its bits
  in r0:r1, as desktop leaves them in rax, because the translated code reads them
  from v0 (`mtc1 f3, v0` after `moving-sphere-sphere-intersect`).
- **The PS2 entry convention for `at`.** The generic and merc functions save
  registers at `12432(at)` and up (the generic work's save area in the scratchpad)
  before they set `at`. On the PS2 their callers enter with `at` holding the
  scratchpad. Desktop's trampoline leaves it uninitialized, so those stores go
  wherever stack garbage points. Every native call (and the host oracle) now enters
  with `at` = `*fake-scratchpad-data*`.
- **The PS2 hardware-register window.** `bones`, `generic_tie`, `joint`,
  `merc_blend_shape`, `shadow`, `tfrag`, `tie_methods` and `time_of_day` poll PS2
  DMA registers (such as `0x1000d400`, the VIF1 channel). On the Dreamcast that
  address is the tile accelerator's area, so on SH-4 `mips2c_private.h` makes word
  reads of `0x10000000`-`0x1000ffff` return 0 (channel idle) and drops word writes.
  Desktop is unchanged.
- **Same arithmetic as desktop.** Built with `-ffp-contract=off`. Otherwise GCC
  fuses `a*b+c` into SH-4 `fmac` with one rounding, and the clipped sky vertices
  differ from desktop in their last bits.

## Verified

`build_mips2c.py` also builds a host oracle, `host_mips2c_oracle.cpp`. It runs the
unchanged files on x86-64 with real SSE, the way desktop does. It calls through the
same generated wrappers, with host GOAL addresses biased to a native magnitude
(0x0c300000 + offset).

The cases are in `mips2c_cases.h`, written once for both sides. Each fills a zeroed
64 KiB region, points the symbols it needs into it, and calls only functions whose
paths stay in C++ (no `jalr` back into GOAL). Afterwards every pointer into the
region becomes its offset, and `#f` becomes a marker. The boot runs the same cases
through the native SH-4 build. All eight match the oracle bit for bit, both the
region CRC and each case's result (466 of 466 boot checks).

Found on the way:

- **A real translation bug, fixed in the shared source.**
  `high_speed_reject::vcallms_454` wrote `vis[5]` twice and never wrote `vis[6]`,
  so `high-speed-reject` read `vis[6]` uninitialized, on desktop too. ChatGPT's
  `lint_mips2c.py` (straight-line read-before-write and `vis` checks, candidates for
  a human to confirm) finds this class mechanically. It also lists more `vis`
  overwrite candidates in `mercneric_convert` to review.
- **Two test-harness traps:**
  - three random arguments in one call came out in different orders on x86 and
    SH-4, because C leaves argument evaluation order unspecified;
  - small host addresses let packed data look like region pointers to the
    normalization.
- **A lint false positive.** `lint_mips2c.py` flags `t6` in generic_effect2;
  it is written in a branch the straight-line scan reads after the use.
- **Memory.** The ELF now carries seven more files. The GOAL arena is 13 MiB (it
  holds 10.7 MB), so the KOS heap has room to load GAME.CGO.

Not covered yet:
- the 14 collide-cache functions, which need a cache filled from collision data;
- `ripple-create-wave-table` and the merc conversion functions, which call back
  into GOAL;
- the two `generic-prepare-dma-*` functions, which mercneric-convert reaches;
- `bones-mtx-calc`, which follows the joint records' pointers (the oracle asserts
  16-byte alignment on them) and needs a real skeleton, and the hardware window,
  which only its DMA wait reaches;
- `high-speed-reject`'s reject path. Its case runs the function on real data, but
  none of the three plane sets rejects.

## Next

Next in the survey's order: the 2-caller files. Each gets a case in `mips2c_cases.h`, and `lint_mips2c.py` gets
checked against its findings first.
