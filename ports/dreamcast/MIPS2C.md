# Native mips2c on SH-4 (2026-10-28, AI-assisted)

The PC port replaced some original hand-written MIPS/VU0 assembly with C++
translations in `game/mips2c`. jak1 has 25 such files registering 94 functions
(`survey_mips2c_users.py`). The first two files with the most GAME.CGO callers,
`collide_cache.cpp` (14 functions) and `sky_tng.cpp` (13), now build natively for
SH-4 and run in the Dreamcast boot. The other 23 files still get the faulting stub.

## How it works

- **Unchanged sources.** `build_mips2c.py` compiles the files with `sh-elf-g++`
  against `mips2c_dc.cpp` (the services they need) and `mips2c_dc/immintrin.h`
  (a scalar stand-in for the eight SSE intrinsics `vu.h` uses). The only change to
  shared code is a `__sh__` branch in `mips2c_private.h`'s `jalr`, for calling
  back into GOAL.
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
  per name. GAME.CGO's 23 lookups of these names now get native code.
- **Calls back into GOAL.** `jalr` calls the GOAL function directly with eight
  64-bit arguments. A float-returning native GOAL function now also leaves its bits
  in r0:r1, as desktop leaves them in rax, because the translated code reads them
  from v0 (`mtc1 f3, v0` after `moving-sphere-sphere-intersect`).
- **Same arithmetic as desktop.** Built with `-ffp-contract=off`. Otherwise GCC
  fuses `a*b+c` into SH-4 `fmac` with one rounding, and the clipped sky vertices
  differ from desktop in their last bits.

## Verified

`build_mips2c.py` also builds a host oracle: `host_mips2c_sky.cpp` runs the
unchanged `sky_tng.cpp` on x86-64 with real SSE, as desktop does, over one memory
region (`mips2c_sky_case.h`):

1. `init-sky-regs` reads a test `*math-camera*` and writes `*sky-tng-data*`.
2. `set-sky-vf27` and `set-sky-vf23-value` set the two sky registers.
3. `render-sky-tri` clips a triangle with one vertex behind the near plane, through
   `draw-large-polygon` and both hyperplane helpers, into a dma-buffer, using the
   fake scratchpad as work memory.

The Flycast boot runs the same bytes through the native wrappers. The 208 bytes of
packets and the whole 32 KiB region, CRC-32 0x64e8d239, match the oracle bit for
bit (452 of 452 boot checks).

Not verified yet: the collide-cache functions have no differential case. They link
and are returned to GAME.CGO, but no loaded level calls them. A case needs a
collide-cache filled from collision data. This test ELF also links libstdc++
(`std::string` in the registration table), about 170 KB of main RAM with the two
files. That's why the boot's 400 KB damaged-file buffer is now allocated only
while its tests run.

## Next

Add files in the survey's order: `collide_edge_grab`, `collide_mesh`,
`generic_effect`, `generic_merc` and `ripple` have 4 GAME.CGO callers each.
`build_mips2c.py --files` takes the list. Give each a differential case like the
sky one before trusting it in gameplay.
