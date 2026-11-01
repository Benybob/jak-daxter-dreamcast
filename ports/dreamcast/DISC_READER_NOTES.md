(AI-assisted)

Review fixes (ISC): `tests/disc_reader_check.c` now retains a real completed
reader destination while the next read blocks. Its frame consumer selects only
READY tickets, checks all previous payload bytes against the independent offset
pattern, and selects the new destination only after completion. Each of the
eight queue destinations has a full payload scan and canaries. Failed 40000-byte
reads scan all 32768 successful prefix bytes, every untouched failed-suffix
byte and both canaries, then refuse selection and retain the previous window.
This is a host consumer model; no renderer integration is claimed.

The updated combined `test_disc_*.py` run passed 12 tests (7 layout, 4 reader
groups, 1 mutation group). The mutation group builds temporary reader copies
and requires the specific expected assertion to reject previous-window damage,
slot-zero misrouting, a one-byte successful prefix, and FAILED published as
READY. Assertions exit directly to avoid Windows abort dialogs; the initial
abort-based mutation run timed out on the fourth mutation and was corrected.
The reader's existing FAILED/READY production logic required no change.
Both reader single SH-4 objects were recompiled into
`build/arena-camera-validation/` using the requested toolchain. Historical
commands/results below describe the initial implementation; the review-fix
run did not rerun the real-fixture packer or KOS adapter on a device.

# Disc reader and level extent prototype

ISC. New files only. Existing arena sources, Makefile, renderer paths and ELF
files were not edited. No git state commands, full Dreamcast build or Flycast
run were performed for this change.

## Milestone 3 result read

`build/arena-game-run.txt` records build exit 0, state 2, result PASS,
2375/2375 backend checks and failed_case 0. Its description is
`BOOT scripted=1562/1562 frames=64 slab=1255392 arena=10673360/11010048`.
Camera metrics report scripted=1, reads=64, moved=63, turned=63. This is the
supplied build/emulator result, not a new run by this author. No failure in
that result required a change to arena_game.c, guards or its Makefile target.

## Reader ownership and publication

`disc_reader.h/.c` implements a fixed eight-slot FIFO with a single frame-side
producer and one worker. The caller supplies exactly one 32-byte-aligned
32 KiB bounce buffer. The core has no heap, filesystem, renderer or PVR calls.
Submit, poll and release take a metadata mutex; the worker releases that mutex
before IO and copies. The APIs can briefly wait for this mutex, but never wait
for the drive. The platform's mutex supplies acquire/release memory ordering;
there is no volatile-only ready flag or SH-4 atomic-library dependency.

Requests use sector-aligned file-relative offsets, exact nonzero payload
lengths and caller-owned destinations. Reads round up to 2048-byte sectors in
chunks no larger than 32768 bytes. Only exact payload bytes reach destination;
the last sector's padding does not require destination headroom. File sizes
must be sector padded and fit signed 32-bit KOS offsets. No FAD adjustment is
performed.

State is QUEUED -> READING -> READY or FAILED. READY is published through the
mutex only after every chunk is copied. A short, negative or oversized read
result fails the request. FAILED may contain a partial prefix; it must never
be treated as a usable block. The destination is inaccessible to consumers
while QUEUED/READING. It remains pinned until successful release, including
after failure. Submit refuses overlap with another live destination, the
bounce buffer or reader metadata. A full queue refuses immediately, leaving
the output ticket unchanged. Tickets include a generation; a slot whose
generation would wrap is retired. Pending requests cannot be released or
cancelled, so cache eviction must leave them pinned.

`disc_reader_kos.h/.c` is an optional adapter, not linked by any existing target.
At level entry it opens one file, allocates the bounce buffer with memalign on
the KOS heap, initializes mutex/semaphore storage and starts a joinable
thd_create_ex worker with an 8192-byte stack and PRIO_DEFAULT+1. Only the
worker's read callback calls fs_seek/fs_read. Level-entry fs_open/fs_total64,
and teardown fs_close, happen outside frame retargets. Close rejects new work,
drains pending requests, joins the worker, then frees storage. Teardown can
wait for the drive; it must not run inside a frame. Init/open/close require
exclusive ownership. No API calls may outlive adapter close.

The host harness uses a real pthread worker and a retained wake notification.
Its fake drive gates each chunk with a condition variable, without timing
assumptions or sleeps. During a 70000-byte request, 64 simulated frame
submissions poll READING while the first read is blocked. A separate previous
complete output stays unchanged. The next two reads are gated too, proving
that a partial copy never publishes READY. After completion the harness checks
all destination bytes, both guard bytes and the three read offsets/sizes.
Other groups cover full FIFO, overlap, pending release refusal, generation
reuse/exhaustion, close/drain, invalid spans and IO failures. These are simulated
frame submissions, not pvr_scene_finish calls or an integration of the cache.

## DCLV v1 on-disc layout

`pack_disc_level.py SOURCE OUTPUT [--verify-only]` packs the fixture without
changing its files. It reads cell size and texture pages through the existing
binary DRM2 catalog validator, not report.json. Every .drx under sectors and
every .dci under cells is included, as are global.drx, package.dci and
manifest.drm. Geometry and instance semantics remain their existing loaders'
responsibility: the packer verifies bytes, not their decoded contents.

The initial header is exactly 2048 bytes. The plan's complete dense directory
cannot fit there for this fixture: its 394 occupied coordinates include
outliers near (6250,6250), and its minimum coordinate is negative. DCLV v1
therefore uses an explicit index extension and dense 8x8 tables for occupied
tiles. This is a documented refinement of the plan, not a claim that the
whole bounding grid fits one sector. Unoccupied tiles are implicitly absent;
each occupied tile has zero entries for absent terrain/instance cells.

All integers are little-endian. Header offsets:

| Offset | Value |
| ---: | --- |
| 0 | four bytes DCLV |
| 4, 8, 12 | u32 version=1, sector_bytes=2048, total_file_bytes |
| 16 | f32 cell_size_m |
| 20, 24 | i32 minimum cell ix, iz (grid origin) |
| 28, 32 | u32 bounding width, height in cells |
| 36, 40 | u32 texture count, occupied tile count |
| 44, 48 | u32 unpadded index bytes, index CRC32 |
| 52 | reserved u32 zero |
| 56, 68, 80 | global, manifest, resident package extents, respectively |
| 92..2043 | zero |
| 2044 | CRC32 of header bytes 0..2043 |

An extent is three u32 values: file-relative offset, exact unpadded length,
payload CRC32. A zero triple means absent only in a dense cell table. All
present extents are nonempty and sector aligned; their trailing sector
padding is zero. CRC32 is the zlib/IEEE CRC used by the Python standard library.

The index starts at file offset 2048. First are texture extents in catalog
order. Next are tile directory entries sorted by signed (tile_x,tile_z), each
`<iiI`: tile coordinates and the index-relative offset of its dense table.
Finally come the dense tables in directory order. Each table has 64 cells,
row-major local z then x, with a terrain extent followed by an instance extent
for each cell (1536 bytes/table). Tile coordinates use floor(cell/8); local
coordinates use modulo 8, including negative cells. The index is capped at
4 MiB and padded with zeros to a sector boundary. This is a tooling ceiling,
not a claim of enough resident RAM for a full-level index on Dreamcast.

Payload order is global.drx, texture pages in catalog order, occupied cells
in Morton order, original manifest.drm, resident package.dci. Morton order
interleaves the 32 bits of (ix-origin_ix,iz-origin_iz), x in even bits and z in
odd bits. Within a cell, .drx immediately precedes .dci when both exist, with
only required sector padding. Instance-only and terrain-only cells are valid.
The original manifest is preserved for later loader work; it duplicates the
texture pixels packed separately as pages. This experimental format has not
yet removed that storage duplication or supplied a native catalog adapter.

The decoder checks header/index/payload CRCs, zero padding, bounds, canonical
tile tables and payload order. It refuses gaps, overlaps, misalignment and
trailing data. Verification compares every source range byte for byte, not
only by CRC. window_runs returns coalesced whole-sector ranges for a 3x3 (or
another radius) query; it merges only physically adjacent extents. Tests
independently enumerate selected sectors to check that no unrelated bytes
are included and that every advertised run is contiguous.

## Commands run and observed results

From the repository root in PowerShell, PATH was appended, never prepended:

```powershell
$env:PATH += ';C:/msys64/mingw64/bin;C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk/sh-elf/bin'
python -m unittest discover -s ports/dreamcast/tests -p test_disc_reader.py -v
python -m unittest discover -s ports/dreamcast/tests -p 'test_disc_*.py' -v
python ports/dreamcast/pack_disc_level.py ports/dreamcast/build/arena-fly-disc/fly ports/dreamcast/build/disc-reader/fixture.bin
python ports/dreamcast/pack_disc_level.py ports/dreamcast/build/arena-fly-disc/fly ports/dreamcast/build/disc-reader/fixture.bin --verify-only
```

The standalone reader run passed 4 tests. The combined suite passed 11 tests:
7 layout tests and 4 reader groups. Host GCC compiled the C harness with
`-O2 -std=c99 -Wall -Wextra -Werror -pthread`. Tests use temporary files and
require host GCC/pthreads and the existing Python tooling dependencies.

The real fixture pack and independent verify-only invocation both reported:

```json
{"bytes":2424832,"cells":394,"global_crc32":558161692,"index_bytes":27204,"payloads":493,"texture_bytes":399168,"tiles":17}
```

Reviewer single-object commands, also run here (the pack command creates the
output directory first):

```powershell
$discWork = 'C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work'
$discCc = "$discWork/dc-sdk/sh-elf/bin/sh-elf-gcc.exe"
$discFlags = @('-O2', '-std=c99', '-Wall', '-Wextra', '-Werror', '-ml', '-m4-single-only', '-D_arch_dreamcast', '-D_arch_sub_pristine', '-D__DREAMCAST__', '-I', "$discWork/kos-source/include", '-I', "$discWork/kos-source/kernel/arch/dreamcast/include", '-I', "$discWork/kos-source/addons/include")
& $discCc @discFlags -c ports/dreamcast/disc_reader.c -o ports/dreamcast/build/disc-reader/disc_reader.o
& $discCc @discFlags -c ports/dreamcast/disc_reader_kos.c -o ports/dreamcast/build/disc-reader/disc_reader_kos.o
& "$discWork/dc-sdk/sh-elf/bin/sh-elf-nm.exe" -u ports/dreamcast/build/disc-reader/disc_reader.o ports/dreamcast/build/disc-reader/disc_reader_kos.o
```

Both objects compiled successfully with the requested compiler. nm showed
only memcpy/memset imports for the core. The adapter imports the core, KOS
thread/mutex/semaphore/filesystem APIs, memalign and free; it has no PVR calls.

## What remains unverified

Plan milestone 1's packing checks pass on the open fixture. The host reader
tests establish the IO/publication mechanics needed for milestone 5; they do
not establish milestones 2 through 10. No CDI was authored or booted, no
verify_backend.py behavior changed, and no real /cd read was performed. The
KOS adapter was compiled but not linked or executed. KOS scheduling, DMA,
drive errors, physical seek time and hardware throughput remain unmeasured.

No existing renderer/cache consumes this format or reader yet. Native index
parsing, window-level readiness across several requests, actual last-window
and empty-first-frame drawing, instance transactions, prefetch, texture upload,
colour fallback and boot files from /cd remain integration work. READY alone
does not validate DRX/DCI contents: consumers must run their existing validators
before publishing a complete window. Reader memory/slab coexistence, worker
stack fit and whole-level index/cache budgets remain unverified on target.
