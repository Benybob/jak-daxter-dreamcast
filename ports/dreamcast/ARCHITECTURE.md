# Dreamcast resource priorities

2026-10-21. AI-assisted. These priorities apply to the full native port.

## Compact SH-4 code

Exploit fixed 16-bit encoding by eliminating redundant copies, unnecessary
frames/saves and duplicate literals, and filling valid delay slots. Preserve
GOAL semantics: narrowing a 64-bit value requires proof. Future register
allocation and hot-function placement need emitted-size and workload evidence.

The unchanged original `identity` from `goal_src/jak1/kernel/gcommon.gc` now
coalesces to `mov r4,r0; rts; mov r5,r1`: **6 bytes, zero stack bytes**.
The final move occupies the useful return delay slot. With `--no-optimize`, the
same source produces **72 bytes and a 40-byte frame**. Both versions agree on
seven Flycast cases, including tagged strings/pairs and a full 64-bit value.
The reduction comes from frame/copy elimination and delay-slot use; it is not
a PS2 benchmark or a measured speedup. The 4-byte float return leaf is checked
against GNU assembler; the original integer leaf also executes on SH-4.

Per-function manifests record instruction bytes, literal/alignment bytes,
stack bytes and minimum 32-byte code-span lines. The latter is a size bound,
not measured cache misses. The SH7750/7750S model has 8 KiB instruction and
16 KiB data caches with 32-byte lines; do not assume larger SH7750R caches.
Relocation is followed by data/instruction cache synchronization covering the
function header and body. Compact allocation retains the GOAL header at -4.
Out-of-range conditions expand to an inverted condition and BRA; short branches
remain compact. Code-size reports include these expansion bytes.
CFG liveness now reuses stack slots and removes copies between identical slots.
The same 365-check module set shrank from 15,410 to 11,850 emitted bytes;
`lerp-scale`'s frame shrank from 296 to 80 bytes. Loops and calls pass native
regressions; these figures are code/stack measurements, not timing estimates.

Primary CPU references: [Renesas SH-4 software manual](https://www.renesas.com/en/document/mas/sh-4-software-manual),
sections 1/9, and [SH7750 hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware), sections 3/4.

Native GOAL references use cached P0 aliases below bit 31 so signed object-cell
loads agree with pointer constants. Bridges convert KOS P1 pointers explicitly.
`dc_goal_init_native` requires MMU-off operation and stock main-RAM bounds;
see `RUNTIME.md` for execution checks and address-domain details.

## Separate VRAM and main-RAM budgets

Budget **8 MiB VRAM** for framebuffers, tile/parameter storage, textures and
graphics allocations; **16 MiB main RAM** for code, objects, CPU geometry,
stacks, KOS and streaming buffers. Sound RAM is separately 2 MiB. VRAM texture
residency can release CPU copies, but cannot enlarge the ordinary code/object
heap. Record free VRAM after initialization and content loading; do not assign
the entire 8 MiB to textures. The pinned KOS retail VRAM size is defined by
`PVR_RAM_SIZE_MB` in `kernel/arch/dreamcast/include/dc/pvr/pvr_regs.h`.

The terrain renderer already uploads native twiddled textures into VRAM and
immediately frees CPU pixels. The measured Geyser Rock grass subset uses a
32,768-byte 128x128 ARGB1555 texture and 213,120 bytes of indexed CPU geometry,
down from the earlier 373,632-byte expanded geometry. Its PVR allocator reports
**4,335,272 free bytes** after setup. This is one material subset, not whole-game
residency. The separate runtime probe does not measure a combined workload.

Offline full-codebook VQ now reduces that same texture to **6,144 bytes**,
including its 2,048-byte codebook. The native VQ flag makes PowerVR decode it;
the renderer uploads compressed bytes directly and frees the CPU copy.
Flycast reports **4,361,896 free VRAM bytes**, exactly 26,624 more than the
same-source ARGB reference. RGB5 PSNR is 32.53 dB with zero alpha mismatches.
Geometry, input and cleanup checks pass in stock 16 MiB; a truncated VQ payload
is rejected on the target. See `TEXTURES.md` for format and reproduction.

Next graphics work includes material/texture deduplication, bounded streaming,
palette formats, and aligned store-queue/DMA transfers. VQ currently supports
square power-of-two textures with a full codebook and no mipmaps. Compression
does not establish whole-level residency or hardware throughput.
Pinned KOS upload APIs and format flags are in `dc/pvr/pvr_txr.h` and `dc/pvr.h`.
Only Flycast is available; physical throughput/cache measurements remain open.
