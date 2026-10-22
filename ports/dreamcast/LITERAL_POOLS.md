# Bounded SH-4 literal islands

Historical pool checkpoint. [FAR_BRANCHES.md](FAR_BRANCHES.md) adds the later
native far-jump support while preserving compact pool layouts. (AI-assisted).

2026-10-21. (AI-assisted). SH-4 mov.l PC-relative loads address an aligned
forward 32-bit literal within 1,020 bytes. A single pool after the function
blocked original sort and mem-print even when their instructions were valid.

The emitter first tries the existing compact terminal pool. When its literal
range is exceeded, typed lowering permits bounded intermediate pools with a
BRA/NOP skip over aligned data. Pool layout and conditional expansion repeat
together until stable. All branch targets, PC-relative loads and absolute
symbol relocations use the final offsets. No registers or condition flags are
consumed by the skip. A pool is never inserted into a delayed branch/call/return
pair; PC-relative literal loads in delay slots are explicitly rejected in the
island path. Strict emitter mode still rejects unsupported ranges.

Equal literals share storage within a pool. A value used across multiple pools
may need multiple copies and relocations. Short functions keep their previous
machine bytes. Reports separate instruction bytes (including pool skips),
expanded conditional branches, literal islands, and pool/alignment bytes.
Long unconditional branches beyond SH-4 BRA/BSR range remain rejected.

Six new host tests compare real GNU assembler/disassembler output for aligned
pool skips, delay slots, simultaneous conditional expansion, duplicate external
relocations, and 400 distinct literals spread across bounded pools. The native
suite adds 105 checks executing unchanged original sort with two compiled GOAL
callbacks, proper/dotted lists, duplicates, empty/singleton lists and preserved
links. Expected ordering comes from an independent C insertion sort.

All **119 host tests, 8,193 native runtime checks and 39 numeric checks pass**
in Flycast with stock 16 MiB RAM. Original sort emits 1,432 bytes with one
literal island and three expanded conditional branches. Complete gcommon still
exports 66 functions; standalone per-function lowering now succeeds for 56
(previously 54). Mem-print lowers but is not executed: its output/platform
dependencies are not implemented. This does not establish whole-module startup.

The diagnostic totals 29,168 emitted bytes (including the retained 3,244-byte
shift baseline), arena 304,848 / 1,048,576 bytes and 235 symbols. Executable,
KOS, stacks and renderer are excluded. Full original kernel/game startup,
process heaps, scheduling, mixed/vector ABIs, sound and gameplay remain open.
Physical Dreamcast is available for later testing; no CDs are prepared yet.

Reproduce from the repository root after building the original frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

Host oracle coverage requires the SH-4 binutils/GCC and original frontend.
Missing tools skip the relevant host tests. Build one SH-4 target at a time.
