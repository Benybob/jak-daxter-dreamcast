(AI-assisted)

## Disc follow-up: runnable GDI input for verify_backend (AI-assisted), ISC

This section supersedes the statements below that the generic verifier cannot
mount a disc. The supplied real `build/arena-jak-review/result.json` was read:
**6996/6997 FAIL**, case **16997**, `jak art: staged /cd/EICHAR.DGO is absent`,
ELF SHA-256 `8e2aa6c2b1883a9e9f33c33f39f7c27354a81b65f9f744d8cdf33ba2099c9fa2`.
It was not rerun. No ELF was linked or executed in this follow-up.

### Choice (a), with measured sizes

`make_boot_gdi.py` now writes a three-track GDI using Python's standard library.
The inspected work tree has KOS `scramble.c`, makeip sources and its 32768-byte
`utils/makeip/rsrc/templates/ip.tmpl`; no runnable scramble, makeip, mkisofs,
genisoimage, xorriso or cdi4dc was found in the searched tree/SDK or on the
appended PATH. MinGW gcc and sh-elf-gcc are available. Windows System32 tar
(libarchive) is an independent ISO reader. No external writer was needed.

The generated `build/arena-jak-gdi/jak-arena.gdi` is:

```text
3
1 0 4 2048 track01.iso 0
2 6015 0 2352 track02.raw 0
3 45000 4 2048 track03.iso 0
```

| Generated component | Bytes | Contents |
| --- | ---: | --- |
| Track 1, low-density ISO | 12,011,520 | All 1,294 files from the three staged roots below, including EICHAR.DGO, JAKART.DGO, boot files and existing live fixture subdirectories |
| Track 2, silent audio | 705,600 | 300 sectors of 2352 bytes; starts after a 150-sector gap |
| Track 3, boot ISO at LBA 45000 | 14,823,424 | IP.BIN and 1ST_READ.BIN |
| 1ST_READ.BIN inside track 3 | 14,747,648 | Flat load image from the existing ELF, entry 0x8c010000, end 0x8ce20800 |
| EICHAR.DGO inside track 1 | 1,349,088 | Complete staged original eichar object |
| JAKART.DGO inside track 1 | 2,025,088 | Complete staged eichar and sidekick objects |

The low-density track occupies 5,865 sectors. Audio ends at LBA 6314, well
before the high-density track at 45000. The ELF's three file-backed PT_LOADs
are placed at their exact VMAs, with zero-filled gaps; an ELF header is **not**
copied to the CPU entry point. The flat image matched `sh-elf-objcopy -O binary`
byte for byte (SHA-256
`eb50bc8c460ec92fbaba9488a21da4c3bea4fb136d783a2cb85a0747c1c26c58`).
The writer rejects other entry points, non-SH ELF, overlapping segments and
segments outside the stock 16 MiB map. It changes no runtime memory reserve.

This split follows the inspected local sources: Flycast `core/imgread/gdi.cpp`
requires data/audio/data with track 3 at 45000; `Disc::GetBaseFAD` and reios
boot that track and **do not descramble GDI**. Thus 1ST_READ.BIN is unscrambled.
KOS `fs_iso9660.c:init_percd` requests `cdrom_read_toc(..., false)` and
`cdrom_locate_data_track` selects the last data track in that TOC. Flycast's
`libGDR_GetToc` exposes only tracks 1/2 for that low-density request. Hence
art must be in **track 1**, not solely in the high-density boot filesystem.
Each ISO's extents are absolute disc LBAs (base 0 or 45000). The independent
reader tests both bases, both endian path tables, multi-sector directories,
every staged file hash, bootstrap metadata/CRC and exact boot bytes. ISO names
are uppercase with ;1 versions and relaxed '-' / multiple dots to preserve
fixture paths; KOS's case-insensitive `fncompare` handles these names.

Option (b) was not selected: **EICHAR.DGO alone exceeds the 1,081,024-byte
VRAM remainder in ARENA_FLY_NOTES.md by 268,064 bytes**, before the existing
level romdisk, textures, chunk overhead or headroom. The pair exceeds it by
944,064 bytes. A new art loan would require changing the existing VRAM owners.
This GDI adds no VRAM or GOAL arena allocation. Existing embedded boot/render
romdisks remain the runtime sources; their staged copies are on disc for
inspection/future migration. This does not implement asynchronous streaming
or the packed-level reader in DISC_STREAMING_PLAN.md. It is an emulator GDI,
not a burnable CD-R image or a physical GD-ROM timing result.

### Exact reviewer commands

From the repository root in a configured KOS shell, first rebuild the current
player ELF if needed. **This make/link command was not run here.** Keep the
existing boot and live fixture staging. Regenerate art from your own extracted
GAME.CGO if `build/jak-art-disc` is missing (the `stage_jak_art.py` command below).

```sh
make -C ports/dreamcast CFLAGS='-std=c99 -Wall -Wextra -Werror' ARENA_JAK_RENDER_MARGIN=65536 jak-dc-arena-jak.elf
```

From the repository root, these image generation and host audit commands were
run here. The template path points at the inspected work-tree KOS checkout;
another checkout can use `$KOS_BASE/utils/makeip/rsrc/templates/ip.tmpl`.
All output, including private retail bytes, stays under `build/`.

```sh
python ports/dreamcast/make_boot_gdi.py ports/dreamcast/jak-dc-arena-jak.elf --ip-template ../../kos-source/utils/makeip/rsrc/templates/ip.tmpl --staged ports/dreamcast/build/jak-art-disc --staged ports/dreamcast/build/kernel-dgo-rd --staged ports/dreamcast/build/arena-live-disc --output ports/dreamcast/build/arena-jak-gdi
python -m unittest discover -s ports/dreamcast/tests -p test_boot_gdi.py -v
```

Rebuild the GDI after **every ELF change**. `manifest.json` binds the ELF,
boot payload and all disc tracks by SHA-256. The verifier rejects an ELF hash
mismatch or changed disc/track bytes before launching; moving the image
directory is allowed. Keep the GDI, all three tracks and manifest together.

Reviewer execution, **not run here**; `FLYCAST` and `NM` name the reviewer's
working executables. The ELF still supplies nm addresses and `elf_sha256`.
`--disc` supplies the **only** Flycast content argument. Default bare-ELF
behavior, Reios, the 16 MiB setting, strict pass criteria and timeout limits
are unchanged. `result.json` additionally records the disc and track hashes.
Use new output directories to preserve older evidence.

```sh
python ports/dreamcast/verify_backend.py "$FLYCAST" ports/dreamcast/jak-dc-arena-jak.elf --disc ports/dreamcast/build/arena-jak-gdi/jak-arena.gdi --nm "$NM" --output ports/dreamcast/build/arena-jak-disc-player-review --tests 7652 --timeout 300 --dump dc_arena_jak_player_evidence 7804
python ports/dreamcast/verify_arena_jak_player.py ports/dreamcast/build/arena-jak-disc-player-review/dump.bin
```

The source-derived complete-success count remains **7652/7652**, including
651 original-player checks. This is not an observed result. The disc supplies
the missing file so execution can attempt original relocation/login and then
the existing authenticated `start` / `init-target` spawn route. A real login,
spawn, sidekick or world dependency failure must remain FAIL, with its first
fault preserved. The offline auditor must also pass; process allocation alone
is not player success. The bare-ELF negative control still expects 6996/6997
at the supplied absent-file refusal with this ELF.

If the player run fails, repeat the same command with new output directories
and `--dump dc_arena_jak_evidence 212` (reason at offset 116),
`--dump dc_arena_jak_art_evidence 100` (admission/relocation/login), or
`--dump dc_arena_live_evidence 39476` (inherited camera proof). The backend
runner captures the selected dump on terminal failure too. Do not change
`--tests` to make an earlier refusal pass.

### Validation actually run in this follow-up

- GDI generation command above; **7 host tests passed**, including a real
  1,294-file byte/hash audit and mock-only GDI/CDI/default verifier launches.
  Terminal failure stayed nonzero; stale ELF and altered disc were rejected
  before subprocess launch. No test launched an emulator.
- `sh-elf-readelf -l` on the existing ELF, `sh-elf-objcopy -O binary`, and a
  Python byte comparison with the generated boot image: exact match.
- Windows `C:/Windows/System32/tar.exe` listed 1,304 track-1 entries and
  extracted EICHAR.DGO byte-identically (SHA-256
  `8a898bf490ab0e8c394d7e15abea04d66250fbf0e185ba949edbdf6ea5857532`).
  Directly listing isolated track03.iso failed because its extents are absolute
  LBAs. A host-only view with the track at LBA 45000 and its PVD also at sector
  16 let libarchive list/extract 1ST_READ.BIN and match all 14,747,648 objcopy
  bytes. That view is an audit artifact, not the boot image.
- `python ports/dreamcast/tests/compile_arena_jak_player_objects.py`: **10
  individual SH-4 objects** compiled at O2/C99/Wall/Wextra/Werror, using the
  requested full sh-elf-gcc path. Tool directories appended to PATH.
- `python ports/dreamcast/verify_backend.py --help`, Python `py_compile` on
  the writer/verifier/tests, and `git diff --check` passed.

Unverified: actual GDI acceptance/boot, the KOS `/cd` read on SH-4, successful
original art login and target spawn, sidekick deficit/behavior, the 7652 target
count, player input/motion, original world/collision loading, follow rendering,
and physical hardware. Host filesystem/payload checks cannot establish these.
No build_dreamcast.py, Flycast, ELF link or Git-state-changing command ran.

## Current follow-up: fixture-derived split and guarded original-target replay

ISC. This section supersedes the memory proposal and explicit blocker 11 in the
historical notes below. Only **`jak-dc-arena-jak.elf`** selects
`DC_ARENA_JAK_PLAYER` and the new private objects. Other ELF recipes retain their
dependencies, flags, split and execution path, including the earlier optional
`jak-dc-arena-jak-art.elf`. No ELF was linked or executed here. A replay runner
and strict receipts have been implemented and compiled; **successful original
target execution has not been observed**. There is no passing player dump.

### Measured CPU renderer need and the split

The latest recorded numeric slab peak found in the supplied evidence is still
**1,255,392 bytes**: `build/arena-game-review/result.json`, a 2438/2438 PASS.
`build/arena-live-fix-review/result.json` records a newer **6984/6984** PASS, but
its 39,476-byte camera dump contains no renderer slab counter. Do not invent a
new target high-water from that dump. Its actual 181 camera positions were
decoded and replayed through the host CPU allocation probe instead.

`measure_arena_jak_renderer.py` compiles the real CPU package reader, block
cache, draw window, instance cells and slab allocator, then feeds the private
fixture and original-camera positions through them. GPU rasterization and
original SH-4 execution are excluded. GPU slot pointers and material header
allocations use SH-4 byte sizes; host cache pointers make its metadata larger
than SH-4. File tokens use the same 8-byte payload/32-byte slab accounting.
Every malloc is charged alignment and the real slab header. The selected
capacity is replayed again to check fragmentation/allocation failure.

| Executed host replay | Peak slab bytes |
| --- | ---: |
| Current 181-row fixture prediction | 1,367,584 |
| Supplied completed original-camera dump | 1,367,584 |
| 169 queries sweeping the accepted +/-6 m X/Z domain | 1,367,584 |
| Busier 49-position grid, +/-24 m at 8 m spacing | **1,441,856** |

The busier grid stays inside the staged beach crop, using the same 4960 faces,
5112 vertices, 10899 remap entries, 539192-byte cache budget, 400 instance
records and 363044-byte instance package. It is a host stress route, not an
original recording. It **failed** `block cache window residency` at the first
proposed 1,433,120-byte slab: its real host need exceeded that reserve by
**8,736 bytes**. The final split includes this measured need:

| Final default | Bytes |
| --- | ---: |
| Renderer measured host maximum | 1,441,856 |
| Explicit margin | **65,536** |
| Renderer slab (32-byte aligned) | **1,507,392** |
| GOAL arena | **12,124,096** |
| Combined budget, unchanged | **13,631,488** |
| Returned from old 2,621,440-byte slab | **1,114,048** |

No renderer geometry, texture, recording, window or cache capacity was reduced.
The generated `build/arena-jak-budget/budget.h` and `budget.json` record the
fixture SHA-256, relevant private-file hashes, replay results and margin.
The supplied completed camera dump SHA-256 was
`1f90338d6bbfa309d2eda6f342b8b1a6201723cc60517ec8d36175e57dd609e2`.
Make regenerates the budget before compiling its private objects.
`ARENA_JAK_RENDER_MARGIN` defaults to 65536. Optional
`ARENA_JAK_RENDER_SLAB_BYTES` may increase the aligned reserve; compilation
rejects a value below measured need plus margin. `ARENA_JAK_RENDER_EVIDENCE`
optionally adds a completed live dump to the fixture replay. The default build
uses fixture data and the two host stress routes without requiring an old dump.

This is a budget for this fixture. An attempted 81-position +/-32 m host sweep
failed the **existing 400-record instance capacity**, even with the old slab.
No fit is claimed for other levels, larger catalogs, a real player camera path
outside these bounds, or later texture/animation rendering allocations.

### Boot staging and exact art admission

The boot romdisk is **1,939,456 bytes**, larger than the final renderer slab.
Simply shrinking its old backing allocation would overflow it. Jak's private
main now uses one allocation for GOAL, a 16-byte end gap and the slab. Before
any GOAL writes, `memmove` places the boot image across the unused GOAL tail
and the slab, handling overlap with the ELF's high staged source. Its
**432,064 borrowed bytes** are excluded from runtime capacity during boot:
**11,692,032 bytes** are initially allocatable. This uses capacity protection,
not `temporary`, because the code loader releases temporary startup segments
after each object. Only after all boot files close and `/rd` unmounts is full
12,124,096-byte capacity restored and the renderer slab initialized. The real
host allocator passed an overlapping-copy/exact-capacity/OOM/copy-integrity
test of this layout. Final KOS link placement remains unverified.

At the old **10,673,360-byte** snapshot, the real host allocator now has
**1,450,736 free bytes**. Complete eichar (1,318,288 resident bytes) loads with
**132,448 bytes left**; eichar plus sidekick (1,979,744 bytes) remains
**529,008 bytes short**. These are executed admission checks at the old
snapshot, not a claim about current Jak RAM. Debug startup, symbol strings,
continue creation, observers and original login add allocations.

`stage_jak_art.py` now also emits `/cd/EICHAR.DGO`: one complete original retail
object, **1,349,088 archive bytes**. The original two-object `JAKART.DGO` remains
byte-identical. The single-object loader uses the same native retail data
relocations and authenticated original `art-group.relocate`/login/registration;
it does not execute retail MIPS. Host tests compared **all 1,318,288 linked
bytes**, including **18,523 pointer fixes**, against an independent decoder.
Original login was a double in that test. No animation/resource indices were
trimmed, and no placeholder skeleton or target is allocated by C.

At the actual load boundary the runner records whole-pair free bytes and exact
payload shortfall. It selects the pair only with an additional 64 KiB available;
otherwise it admits complete eichar. The 100-byte art receipt records actual
post-login arena use. The player receipt records `sidekick_available` and
`sidekick_shortfall = max(0, 661456 - sidekick_available)` after the replay (or
its first failure), unless sidekick was loaded. Thus subsequent allocations
are reflected in the reported remaining deficit. **There is no current
on-target sidekick deficit measurement yet.**

Original `init-target` still unconditionally spawns `init-sidekick`; it has not
been patched or bypassed. With eichar alone, that child cannot acquire sidekick
art and may enter its original art-error path. Sidekick is not counted as a
working character. Whether its failure permits this original target path to
continue is unverified. If it blocks/faults, the runner must FAIL; no C repair
or suppressed runtime fault turns it into success.

### Original execution checks and receipts

After the completed digital free-camera proof, while the original ownership
and lexical scope ledgers remain registered, the Jak-only runner:

1. Authenticates original `start`, `init-target`, `kernel-dispatcher`, `read-pad`
   and the existing original continue/art lookup exports.
2. Binds the original listener as caller and calls original `start 'play cp`.
   It requires original `*target*` publication, target type, and the original
   armed main-thread image containing exact `init-target` and `cp` words.
3. Imports that existing main thread into a native fiber on its original
   DRAM working stack, preserving the original dispatcher/resume-hook route.
4. Runs **160 dispatcher returns**, feeding a separate hardware recording:
   slots 0..31 neutral, 32..63 joyx=+96, 64..95 neutral, 96..127 joyx=-96,
   128..159 neutral. Extra hardware reads never advance a slot. Original
   `service-cpads` and byte mapping remain the input route.
5. Observes the authenticated original `read-pad` body through a temporary
   call wrapper. Both main and original post-hook execution must have the
   runtime's authenticated target identity. It requires completed calls,
   neutral/moving magnitude differences, original-created control/cpad linkage,
   increasing real target resumptions, finite real positions, moving X/Z
   responses and settling during final neutral input. It rejects art-error.
   The original binding is restored on success and failure.

No target/control/state/position/follow matrix is written by this runner. The
earlier camera proof is frozen while player callbacks collect separate data.
The C beach renderer is not run for these extra player dispatches. Original
display/camera code continues; **no following-camera renderer proof or active
GOAL level/BSP/collision world is supplied**. Those dependencies may produce
the first genuine original-code refusal, which must be preserved.

`dc_arena_jak_player_evidence` is version 1, **7804 bytes**: 31 u32 words named
by `verify_arena_jak_player.WORDS`, then 160 48-byte rows (`<6IiI4f`). Each row
holds sequence/state/status, completed target resumes, original read-pad
returns, valid pad services, joyx/buttons, real control translation and pad
magnitude. The existing 212-byte Jak and 39476-byte camera ABIs remain intact;
the 100-byte art receipt is now linked by this ELF too. Success requires the
standalone player auditor and all backend checks, not just a spawned object.
Zero player counters, a missing disc, art-error or incomplete replay cannot pass.

### Reviewer commands and expected counts

These host/single-object commands were run here (PATH entries appended):

```powershell
$env:PATH += ';C:/msys64/mingw64/bin;C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk/sh-elf/bin'
python ports/dreamcast/measure_arena_jak_renderer.py --evidence ports/dreamcast/build/arena-live-fix-review/dump.bin
python -m unittest discover -s ports/dreamcast/tests -p test_arena_jak_player.py -v
python -m unittest discover -s ports/dreamcast/tests -p test_arena_jak.py -v
python -m unittest discover -s ports/dreamcast/tests -p test_jak_art.py -v
python -m unittest discover -s ports/dreamcast/tests -p test_arena_live.py -v
python ports/dreamcast/tests/compile_arena_jak_player_objects.py
python ports/dreamcast/tests/compile_jak_art_objects.py
python ports/dreamcast/tests/check_arena_jak_isolation.py
git diff --check
```

Expected host results: **5 new player tests**, 6 Jak tests, 5 art tests and 4
live tests pass. Ten private SH-4 objects compile, plus three legacy art
objects, at O2/C99/Wall/Wextra/Werror with the requested full sh-elf-gcc path.
Isolation compares **49** old-mode preprocessing outputs, including legacy
Jak, and all existing Makefile content except Jak's dependency line. Existing
non-sound ELF hashes were checked against the prior-session snapshot.

The player host test also ran `stage_jak_art.stage` on the manifest's owner's
GAME.CGO. To regenerate manually, use the earlier `stage_jak_art.py` command
below. Keep both staged archives at the root of the reviewer's mounted disc;
neither is embedded in either romdisk. No disc image was created here.

Build command for a configured KOS reviewer shell, **not run here**, from
`ports/dreamcast`:

```sh
make CFLAGS='-std=c99 -Wall -Wextra -Werror' ARENA_JAK_RENDER_MARGIN=65536 jak-dc-arena-jak.elf
```

The source-derived all-success backend expectation is **7652/7652**:
the old 6996 passing checks, 2 extra split/staging checks, 2 original art load/
lookup checks, the successful art-object check, and **651 player checks**.
Two inherited ledger teardown checks execute after the player phase. This is
an expected count, **not an observed target result**. With no mounted art
disc, expect **6996/6997 FAIL**, case **16997**, with the explicit absent-file
reason. Actual original login/spawn/world failures may occur sooner.

The generic `verify_backend.py` launches an ELF without mounting a disc. Its
old commands below are insufficient for successful player review. A reviewer
must provide `/cd/EICHAR.DGO` (and `/cd/JAKART.DGO` if pair admission is wanted)
in their configured Dreamcast session and capture `dc_backend`,
`dc_arena_jak_player_evidence` (7804), `dc_arena_jak_art_evidence` (100),
`dc_arena_jak_evidence` (212), and `dc_arena_live_evidence` (39476). After such a
capture, offline command (**not run on a real player receipt here**):

```sh
python3 verify_arena_jak_player.py build/arena-jak-player-review/dump.bin
```

Unverified: ELF link/placement/16 MiB KOS fit; physical GD-ROM staging and timing;
SH-4 slab peak/fragmentation; real art relocate/login/merc texture dependencies;
current actual art/sidekick deficit; successful target init, main/post
resumptions, states and pad response; sidekick omission behavior; original
world/collision/level loading; following camera and player rendering; hardware.
No build_dreamcast.py, Flycast, git add/commit/stash/checkout or other Git
mutation was run.

## Historical prior-session record (superseded where stated above)

ISC. **2026-11-03 follow-up: complete retail art staging and a separate memory
admission probe.** The supplied `build/arena-jak-review/result.json` records the
user's **6996/6997** missing-eichar result. It was read, not rerun. Every existing
ELF, including `jak-dc-arena-jak.elf`, remains byte-identical. The new optional
target is `jak-dc-arena-jak-art.elf`. Only three individual SH-4 objects were
compiled here; no ELF link, build_dreamcast.py, Flycast, or Git mutation ran.

## Real archive, real sizes

The owner's extracted `iso_data/jak1/CGO/GAME.CGO` contains the v4 **data** objects
`eichar` (zero-based object 306) and `sidekick` (307). `ART.CGO` contains them too.
A scan of the extracted CGO/DGO archives found no eichar in a level DGO.
GAME.CGO also has an earlier **v3 code** object named sidekick: selection must
check the version, not just the name. `goal_src/jak1/dgos/game.gd` lists
`eichar-ag.go` and `sidekick-ag.go` after the common texture pages. The native
326-module GAME.CGO staging contains compiled sources, not these data objects.

`stage_jak_art.py` extracts just the two complete data objects, in their original
order, into ignored `build/jak-art-disc/JAKART.DGO`. It validates the v4/v2 link
headers, every pointer and binding, unique relocation sites, and the root art
type/name. The manifest records original object indices and SHA-256s; tests
compare every staged object byte to the owner's source archive. No frames,
joints, models or resources were replaced or removed. This is minimal at the
**whole retail object** granularity, not a proven minimal animation working set.

| Object | File bytes | Resident data bytes | Link bytes | Pointer fixes |
| --- | ---: | ---: | ---: | ---: |
| eichar | 1,348,960 | 1,318,288 | 30,656 | 18,523 |
| sidekick | 675,936 | 661,456 | 14,464 | 9,778 |
| Total | 2,024,896 | **1,979,744** | 45,120 | **28,301** |

The archive is **2,025,088 bytes**, including its three 64-byte headers. It stays
on disc staging, outside the boot/render romdisks; it must not be added to the
2.5 MiB renderer slab or the existing VRAM image. Texture pages and level BSP /
collision / entity data are not staged by this tool. Original merc texture login
and any additional dependencies have not been exercised.

## Original loading path and native data counterpart

`game/kernel/jak1/klink.cpp`'s `jak1_work_v2` copies the data, adds its new base
to pointer words, and applies symbol/type links using `common/klink.cpp`'s
`c_symlink2`. Its finish calls method **7**, `relocate`, on the root basic.
`goal_art.c` implements that native data path for this two-object archive.
It streams each payload directly into the arena and holds only one compact
link table on the C heap (at most 30,656 bytes for this input). It never loads or
executes retail MIPS instructions. The unchanged `goal_dgo.c` remains the DCSH
**code** loader; no retail-format fallback was added to existing ELFs.

Original `joint.gc:683` implements `art-group.relocate`. It validates the file,
calls original `login` for the default loading level, and calls original
`set-loaded-art` on that level's directory. `joint.gc:667` logs in the group's
elements; `loader.gc:73` registers it. `level.gc:1392` normally selects the
default loading level during common loading. The isolated adapter selects that
same original inline level only around the authenticated original relocate
call, restores the previous loading level even on a runtime fault, and never
writes directory entries or target/control fields itself. The following lookup
uses the same original loader method as the existing Jak prerequisite probe.

`init-target` unconditionally spawns sidekick after initializing Jak's skeleton
(`logic-target.gc:1259` vicinity). Staging eichar alone would omit that next
art dependency. The new native data loader has host relocation evidence; its
original SH-4 relocate/login callback has **not** run here.

## Memory result and smallest proposed fix

`ARENA_INTEGRATION_PLAN.md`'s combined split is still authoritative:
**11,010,048 bytes GOAL + 2,621,440 bytes renderer slab**. The 2.8 MiB spare
figure in `LEVEL_STREAMING.md` describes the older 13 MiB arena before the slab
was split off. Its village renderer measurements are not free GOAL memory.

Using the exact existing `build/arena-review/result.json` snapshot of
**10,673,360 bytes used**, the native allocator was executed on the host with
that capacity/use, not simulated with a different allocation rule:

| Admission at the recorded snapshot | Bytes |
| --- | ---: |
| Available arena tail | 336,688 |
| eichar alone shortfall | **981,600** |
| eichar + sidekick shortfall | **1,643,056** |

It refused with `OUT_OF_MEMORY`, requested 1,979,744 bytes, and left the bump,
symbol count and next arena byte unchanged. A 16-byte-smaller exact-fit test
refused by exactly 16 bytes; exact fit passed after resource symbols had already
been interned. Resource symbol strings are **additional** allocations when
missing (the host fixture's 65 new symbols used 2,000 bytes). Login, texture and
level allocations are not included in this payload-only admission.

These are exact numbers **at that recorded snapshot**, not a new Jak RAM dump.
The supplied Jak dump is only the old 212-byte evidence and has no arena-used
field. The Jak debug initializer also consumes 8,000 bytes, and live observation
has additional allocations. The new probe records current `used`, `capacity`,
`temporary`, available bytes and exact payload shortfall at the actual load
boundary, plus renderer slab live/high-water. It invokes the real allocator on
refusal, before opening `/cd/JAKART.DGO`, before interning art symbols, and before
any original art callback or target spawn. It does not turn this refusal into
a passing test. **Current Jak shortfall awaits that probe; no exact current
on-target number is claimed.**

The smallest proposed change is a **private budget for a subsequent new ELF**:
return unused renderer reserve to the arena, then trim the shared actor heap
by the remaining shortfall plus measured startup headroom. For scale, the
recorded arena-game slab high-water is 1,255,392 bytes. A 1.25 MiB slab would
return 1,310,720 bytes, leaving 332,336 bytes of the recorded pair deficit.
Reducing `PROCESS_HEAP_SIZE` by 384 KiB (984 KiB to 600 KiB) would return another
393,216 bytes and leave **60,880 bytes** at that old snapshot, before the new
debug/observer/symbol/login needs. The target and sidekick use their existing
48/16 KiB fixed pools, not this shared actor heap. This is a concrete candidate,
**not a demonstrated fit**: the live slab high-water, fragmentation, actor-heap
use, load transients and current deficit must first be read from the new probe.
It may need more headroom. No shared budget constant, slab size, original GOAL
body or boot asset was changed here. Animation paging is a larger alternative;
removing animations without preserving original art indices is not this fix.

## Reviewer commands for this follow-up

These first commands were run here (the source path below is the owner's copy):

```powershell
$env:PATH += ';C:/msys64/mingw64/bin;C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk/sh-elf/bin'
python ports/dreamcast/stage_jak_art.py 'C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/jak-project/iso_data/jak1/CGO/GAME.CGO'
python -m unittest discover -s ports/dreamcast/tests -p test_jak_art.py -v
python -m unittest discover -s ports/dreamcast/tests -p test_arena_jak.py -v
python ports/dreamcast/tests/compile_jak_art_objects.py
git diff --check
```

New tests: five passed. The host compares **all 1,979,744 linked bytes**, including
28,301 pointer fixes and 14,501 symbol/type bindings, against a separate Python
decoder, and exercises real missing-symbol allocation, exact-fit, temporary-top
accounting, prior fault preservation, malformed data rejection and the new
budget wrapper. The original art login callback is a double in these host
tests. Existing Jak tests: six passed, 926 assertions each at O2/fast-math plus
768 observer assertions. Three separate SH-4 objects compiled at
O2/C99/Wall/Wextra/Werror with KOS headers and the requested full compiler path.
Seven old-mode preprocessing comparisons (including Jak) and the complete
existing Makefile prefix match HEAD. Existing non-sound ELF hashes match the
pre-edit snapshot in `build/jak-art-validation/before.json`.

The following build/reviewer execution commands were **NOT run here**. In a
configured KOS shell, from `ports/dreamcast`:

```sh
make CFLAGS='-std=c99 -Wall -Wextra -Werror' jak-dc-arena-jak-art.elf
python3 verify_backend.py "$FLYCAST" jak-dc-arena-jak-art.elf --nm "$NM" --output build/jak-art-review --tests 6997 --timeout 300 --dump dc_arena_jak_art_evidence 100
```

Expected at the unchanged budget: **FAIL, 6996 passed / 6997 total**, case 16997,
`jak art RAM: need 1979744 free <actual> short <actual> (payload only)`. This
expectation inherits the user's completed digital camera recording; it is not
an observed run. No mounted disc is needed to obtain this memory refusal. If a
later private budget admits the pair, the disc must expose `JAKART.DGO` at its
root (`/cd/JAKART.DGO`). The staging directory is not a built/mounted disc image.
That path calls original relocation/login, then original lookup, but still
ends at the existing explicit spawn-integration blocker 11; it is **not a
player-state implementation**. Do not call `start` merely to replace a missing
art refusal with an unmeasured target error state.

Decode the 100-byte memory evidence without starting an emulator:

```python
from pathlib import Path
import struct
w = struct.unpack('<25I', Path('build/jak-art-review/dump.bin').read_bytes())
assert w[0] == 1 and w[1:3] == (1318288, 661456)
before, capacity, temporary, needed, available, short = w[4:10]
assert available == capacity - temporary - before
assert needed == 1979744 and short == needed - available > 0
assert w[3] == max(0, 1318288 - available)
assert w[10:17] == (0,) * 7  # objects/linked/relocated/fixes/art pointers
assert w[17:22] == (before, 2, needed, 0, 0)  # after, OOM, request, entry, calls
print('used/capacity/temp', before, capacity, temporary)
print('pair shortfall', short, 'eichar shortfall', w[3])
print('slab reserve/live/high-water', w[22:25])
```

Unverified: new ELF link/placement, current on-target deficit, raw-data load
timing and C-heap peak, original art relocate/login and texture dependencies,
successful spawn, target states/resumptions, original player response to recorded
pad input, follow camera, real level data, and hardware. Existing player counters
remain zero. This follow-up establishes the data and a reproducible memory
blocker; it does not claim that original player code ran.

## Earlier milestone 5 record

ISC. Milestone 5: original camera debug data and an original-player prerequisite
probe. `jak-dc-arena-jak.elf` is a new Makefile target, **not a completed Jak
gameplay milestone**. No ELF was linked and no Flycast was run in this session.
The user's reported live baseline is 6984/6984; it was not rerun on target here.

## Step A: original data initializer, digital input

Only the new target links `arena_jak_dgo.o` instead of `goal_dgo.o`. That wrapper
compiles the existing loader with its two startup-call boundaries redirected to
`arena_jak_debug.c`. At the original `dc-game-cam-debug-h` load position it:

1. Authenticates the exact resident temporary startup through the loader's code
   registry and requires debug off, layout value zero, and no previous call.
2. Temporarily enables `*debug-segment*`, calls the original startup once, and
   restores its old value even on initializer failure.
3. Checks the original five disjoint 1600-byte tables, their zero indices, the
   8000-byte allocation, and the `define-perm` result `*cam-layout* = GOAL #f`.

The initializer is retained by the existing `--no-debug-segment` module; its
debug functions are discarded. This enables that original file's **data**
startup, not the rest of debug gameplay. The existing malloc bridge maps a
request for the absent debug heap to the global arena, as its null-heap fallback
already does. No debug heap, debugger process, or replacement initializer is
invented. The loaded DCSH has just the 948-byte original startup export.

In this target `arena_live.c` requires that evidence and the resulting false
value. It has no C layout repair. Removing startup interception therefore fails
before camera observation; the host regression explicitly checks that refusal.
The typed-IR test replays the original startup and original
`cam-free-floating-input`: the same circle+L2 samples produce zero rotation
with skipped/truthy numeric zero, and nonzero digital rotation after startup.
It also checks `define-perm` preserves an already-active layout.

Only Jak's input object changes completed slots 42/43 to B/circle+synthetic L2.
Analog sticks are centered during those slots; right X is 128, hardware buttons
are 0x2100 and pressure bytes are zero. Original pad hardware decoding,
`service-cpads`, digital input, free-camera movement, combiner, `update-camera`
and renderer remain the execution route. L2 adds no translation at centered
sticks. All strafe samples, stage boundaries and timing/motion/draw checks are
inherited. Output 41/42 must stay neutral, and 43/44 must each turn by 0.6
degrees times the consumed display ratio, with the existing quantization bound.
These are target checks awaiting execution, not a new observed digital run.

## Step B: precise refusal before attempting an uninitialized player

After the completed camera/renderer proof and restoration of `update-camera`,
`arena_jak.c` authenticates original exports for `start`, `init-target`,
`get-or-create-continue! game-info` and `load-to-heap-by-name load-dir-art-group`.
It requires `*target*` false and both original target pools present. It calls
the original continue factory with the existing `*game-info*`, validates a
128-byte continue point (the factory writes through tagged offset 116), then
calls the original art lookup for the exact skeleton dependency. Both calls
are counted after returning; faults and any unexpected target change fail.

The dependency comes from original source and emitted typed IR:

- `logic-target.gc:1280` defines `start`: stop the old target, then process-spawn
  target with `init-target`, the target pools and `*kernel-dram-stack*`.
- `logic-target.gc:1164..1230` constructs control/collision data, selects original
  cpad 0, then calls `initialize-skeleton self *jchar-sg* '()`.
- `target-util.gc:12` defines that descriptor for **eichar**, with
  `eichar-lod0-jg`, `eichar-lod0-mg`, `eichar-shadow-mg`, texture-level 2.
- `process-drawable.gc:324` selects `*level*.level-default` for texture-level 2,
  irrespective of a player entity. Its original IR uses tagged group+5312,
  level+48 for art directory, skeleton+0 for name and +61 for version. The
  probe also checks that directory's method at type+52 is the original export.
- `level.gc` startup allocates an empty 50-entry art directory for that default
  level. `loader.gc:52` looks up the name; absent art calls
  `art-group-load-check`, which returns #f with debug off (`loader.gc:113`).
  The probe bounds both arrays and requires debug off before that call.
- Missing art makes `initialize-skeleton` transition to
  `process-drawable-art-error`, before a usable player skeleton is initialized.

The expected terminal failure is:

```
jak spawn blocked: missing eichar art-group in default level (init-target:1230)
```

This is an executed original art-lookup prerequisite check **when run on target**,
not an observed failure inside `init-target`. This harness does not call `start`
or allocate a placeholder target. It does not establish the first fault that a
real spawn attempt would hit before the skeleton call. Merely allocating a
target process could reach an error state; that is not a running original player.
The probe never writes target state, control position or follow-camera matrices.
Even supplying an art-group alone reaches a second explicit integration refusal
(blocker 11), so it cannot silently turn this fixture into gameplay success.

## Remaining implementation, in dependency order

1. Add native loading/relocation and original login for complete common art from
   the owner's GAME.CGO: start with `eichar-ag.go` (retail object name `eichar`),
   including its joint geometry, mesh, shadow and subsequent animation resources.
   Register it through original `set-loaded-art` in the default level directory.
   Include sidekick art for `init-target`'s later `init-sidekick` spawn. The
   326-module native GAME.CGO contains compiled GOAL sources, not these original
   common-art objects. Existing `anim_case_data.py` samples are only differential
   test inputs, not a resident complete art group or player skeleton.
2. Supply an original world/continue path with BSP, collision/PAT, level info and
   entity/resource links. The private C beach renderer package is not a GOAL
   level. Normal loading needs real RPC-DGO returned buffers plus `link-begin` /
   `link-resume` support (`load-dgo.gc`, `level.gc`); the existing no-display RPC
   bridges do not provide it. A staged resident alternative must perform the
   corresponding relocation/login and original level birth honestly. Measure
   arena, process heap and render/VRAM budgets before claiming it fits.
3. In a new guarded phase, call original `start 'play <continue>`, with process
   ownership/scope ledgers registered and the kernel stack/fiber context valid.
   Dispatch until original `init-target` reaches `target-continue`, then a
   controllable state. Record returned target identity, type, process state,
   runtime fault/site, resumptions and original control/cpad linkage. Do not
   set those fields from C. A direct `start` takes no entity argument; an actor
   or camera region entity must come from original level data when used.
4. Let `target-continue` (`target-death.gc:34`) drive the original follow events:
   `change-target self`, `change-state cam-string`, `teleport`, `no-intro`,
   `clear-entity` for the default continue. A named continue also waits for active
   levels and calls `cam-start #t`; `reset-cameras` births active BSP camera
   entities (`entity.gc:996`). Do not force free-camera state to count as follow.
   `cam-string` (`cam-states.gc:1579`) needs the original target spline and LOS /
   collision route. `cam-master` reads real control, surface, neck and water data.
5. Add a player recording at the same hardware boundary, preserving original
   `service-cpads` and target control input (`logic-target.gc:272..281`, selected
   at 1225). Require neutral/moving input comparisons, increasing target resume
   counts, legitimate state transitions and measured control position changes.
   Rework the Jak-only camera observer: the inherited observer deliberately
   requires the free-floating slave and inactive levels before poisoning outputs.
   A following camera needs a safe store witness for the active-level branch,
   and may require support for original roll/FOV/local-down in the adapter.
   Feed the resulting original affine camera into bounded terrain/instance draws
   and capture a frame. Then replace the integration refusal with measured
   player and follow checks. None of these steps is already proved by this ELF.

## Expected target counts and evidence (unmeasured)

With the existing assets and accepted recording timing, the source-derived
expectation is **FAIL, 6996 passed / 6997 total, failed_case 16997**:
6984 inherited checks + 3 original debug startup checks + 9 successful player
prerequisite checks + 1 missing-eichar failure. No all-pass target count is
claimed. An earlier failure is a distinct regression/dependency to diagnose.
The live proof should retain 181 draws, 185 dispatcher returns, 6516 witnessed
output lanes, 50 neutral / 71 moving / 2 turning pairs. Its existing 6 m X/Z,
1 m Y, 4.5..20 m travel and timing limits are unchanged; a constant PAL 4.8
full recording still exceeds its travel budget, as documented in live notes.

`dc_arena_jak_evidence` is version 1, **212 bytes**: 29 little-endian u32 values,
then a 96-byte NUL-terminated missing-dependency string at offset 116.

| Words | Values |
| --- | --- |
| 0..8 | version, debug_calls, debug_entry, debug_before, debug_after, layout_before, layout_after, debug_bytes, debug_restored |
| 9..14 | blocker, start_entry, init_entry, continue_entry, continue_calls, continue_point |
| 15..19 | lookup_entry, lookup_calls, art_directory, art_name, art_result |
| 20..26 | target_before, target_after, spawn_calls, target_resumes, target_state, pad_position_updates, follow_frames |
| 27..28 | runtime_error, error_site |

Expected: debug_calls/restored=1, debug_bytes=8000, debug_before=debug_after=
layout_after=GOAL #f, layout_before=0, blocker=10, continue_calls=lookup_calls=1,
art_result=target_before=target_after=GOAL #f; words 22..28 all zero.
Zero player counters mean **no player execution**, never a passing player check.
Blockers 1..9 name the earlier failing prerequisite; blocker 11 means art was
present but the original spawn/follow integration remains unimplemented.
`dc_arena_live_evidence` retains its separate v3, 39476-byte ABI.

## Reviewer commands (build/link/Flycast commands below were NOT run here)

From the repository root, host and single-object checks:

```powershell
$env:PATH += ';C:/msys64/mingw64/bin;C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk/sh-elf/bin'
python -m unittest discover -s ports/dreamcast/tests -p test_arena_jak.py -v
python -m unittest discover -s ports/dreamcast/tests -p test_arena_live.py -v
python -m unittest discover -s ports/dreamcast/tests -p test_mmi_pad.py -v
python ports/dreamcast/tests/check_arena_jak_isolation.py
python ports/dreamcast/tests/compile_arena_jak_objects.py
git diff --check
```

In a configured KOS shell, from `ports/dreamcast`, with the existing 326-module
boot staging and live beach fixtures. `FLYCAST` and `NM` must name the reviewer's
Flycast executable and SH-4 nm. Use new output directories to preserve old runs.

```sh
make -B CFLAGS='-std=c99 -Wall -Wextra -Werror' jak-dc-arena-jak.elf
python3 verify_backend.py "$FLYCAST" jak-dc-arena-jak.elf --nm "$NM" --output build/arena-jak-review --tests 6997 --timeout 300 --dump dc_arena_jak_evidence 212
```

The generic backend runner must exit nonzero and write FAIL at the precise
dependency above. Do not change its success test to accept this failure. Decode
the new dump separately (no emulator launch):

```sh
python3 - <<'PY'
from pathlib import Path
import struct
b = Path('build/arena-jak-review/dump.bin').read_bytes()
assert len(b) == 212
w = struct.unpack_from('<29I', b)
print('debug/version', w[:9], 'blocker', w[9])
print('original exports / continue / lookup', w[10:20])
print('target before/after', tuple(hex(x) for x in w[20:22]))
print('spawn/resumes/state/pad-position/follow', w[22:27], 'runtime fault/site', w[27:29])
print(b[116:].split(b'\0', 1)[0].decode())
PY
```

For the complete digital free-camera evidence, repeat the same ELF into another
directory; this is also expected to exit nonzero at the player dependency:

```sh
python3 verify_backend.py "$FLYCAST" jak-dc-arena-jak.elf --nm "$NM" --output build/arena-jak-camera-review --tests 6997 --timeout 300 --dump dc_arena_live_evidence 39476
python3 -c 'from pathlib import Path; from verify_arena_live import audit; print(audit(Path("build/arena-jak-camera-review/dump.bin").read_bytes()))'
```

The imported audit checks camera records only. The existing live CLI requires
an all-pass 6984-check live backend and is intentionally unsuitable for this
expected-failing Jak probe. No Jak-follow framebuffer is expected yet.

## Validation actually run

- First commands: `git diff`, `git status --short`; inspected all unfinished
  `arena_jak*`, its C/Python tests and modified files. No sound files edited.
- `test_arena_jak.py`: **6 tests passed**; **926** host assertions at O2 and
  **926** with fast-math, plus **768** observer/hardware assertions. Original
  startup and loader IR are replayed on the host; native exports pass DCSH CRC
  decoding and six relevant modules match the packed GAME.CGO byte for byte.
  Call-boundary doubles prove precise refusal, fault preservation, no silent
  target mutation, wrong-method rejection and no C layout fallback. These do
  not execute original SH-4 instructions.
- `test_arena_live.py`: **4 tests passed**; **6232** math/input/assets assertions
  and **4008** observer assertions at both O2 and fast-math, plus full synthetic
  evidence auditing and its refusal cases.
- `test_mmi_pad.py`: **4 tests passed**.
- `check_arena_jak_isolation.py`: **18** old-mode SH-4 preprocessing comparisons
  against HEAD identical, including live; existing Makefile prefix identical.
  Existing ELF hashes recorded in `build/arena-jak-validation/isolation.json`.
- `compile_arena_jak_objects.py`: six separate SH-4 objects compiled with the
  requested sh-elf-gcc, KOS headers, O2/C99/Wall/Wextra/Werror. Objects are under
  `build/arena-jak-validation`; no link step. Tool directories were appended to
  PATH, never prepended.
- `sh-elf-nm --undefined-only` on the Jak DGO and debug objects confirmed the
  loader calls the Jak wrappers and the wrappers call the normal initializer
  functions. Final readback matched all nine existing non-sound ELF hashes
  against the earlier isolation snapshot.
- `git diff --check` passed (only the existing CRLF-to-LF advisory); new files
  also passed AI-assisted/ISC header, trailing-whitespace and final-newline checks.

Final link size/placement, 16 MiB fit, extra startup allocation on target, digital
renderer output, original prerequisite calls, actual spawn faults, player motion,
following camera and physical hardware remain unverified. No build_dreamcast.py,
Flycast or Git-state-changing command was run.

## Texture page directory before art login (AI-assisted)

Jak's art login (`adgif-shader-login-fast`, texture.gc:2037) writes through
`*texture-page-dir*`. Nothing in this boot loaded `dir-tpages`, so the global
stayed `#f` and login wrote a packed shader pointer into the `symbol` type
object (num_methods 9 became 52201); the following export check then refused
every original export. See JAK_CORRUPTION_DIAGNOSIS.md.

`stage_jak_art.py` now also writes `DIR-TPAGES.DGO` (the retail v4
`dir-tpages` object from GAME.CGO, 19,328 resident bytes) and
`DC_JAK_TPAGE_DIR_BYTES`. `dc_arena_jak_art_load` loads it first and runs the
original `texture-page-dir` relocate (method 7), which sets
`*texture-page-dir*`; a directory that is absent or not installed refuses the
art load with its own reason. Restage, rebuild the ELF and the GDI as above.

Observed in Flycast with the commands above: 7002 checks pass; the export check
(17001) now passes. The run stops at 17003, "jak ORIGINAL start creates and
publishes an original typed target": `start` returns a target, but the runtime
error is BAD_ARGUMENT from goal_bridge.c:582 (`note_root_member` refuses the
spawned target's scope).

## Start runs init-target; the thread is armed at enter-state (AI-assisted)

Observed in Flycast after the span ledger follows original shrink-heap: 7003
checks pass. `start` creates and publishes the typed target (17003). 17004
fails on one field only: `dc_arena_jak_thread_probe` shows the target's thread
word +36 is `enter-state`, not `init-target`. The original process-spawn
already ran init-target in the process and its `go` armed the thread for the
first state. Stack top, the continue point (+44) and the zero words match.
Checks 16 and 17 still describe the earlier plan, where init-target had not
run; they need to expect the original enter-state arming instead.

## Target armed at enter-state (AI-assisted)

Checks 16 and 17 in `arena_jak_player.c` now follow that image. `process-spawn`
runs `init-target` before `start` returns (`gstate.gc`, `run-now-in-process`).
`init-target` finishes with `(go target-continue arg0)`, and `arg0` is the
continue point passed to `start`. `go` stores that state in process
`next-state` and calls `enter-state`. On the initialize path `enter-state`
calls `set-to-run` (`gkernel.gc`) and throws; `run-function-in-process` then
sets status back to `waiting-to-run`.

`set-to-run` writes the function and its arguments into the main thread's
uint64 `rreg` slots. In this port those slots begin at thread+36 (`rreg` is
retail offset 40). rreg 0 is `enter-state`: word +36 is the entry and word +40
is the zero high half. rreg 1 is `go`'s one argument, the continue point: word
+44 is that pointer and word +48 is the zero high half. Word +28 is still the
stack top. Those three words are the ones the previous run already matched, so
check 16 still requires them. It does not require rreg 2..6. `enter-state` was
called with one argument, and those slots were not part of the matched check.

Word +36 must be the exact original `enter-state` entry. The check resolves
the `enter-state` symbol, accepts that value through `dc_goal_code_entry`, and
requires the registry record's owner to be that same symbol, as
`arena_jak_exports.c` does for an export. `next-state` is process field +72
(retail 76). It must be a 36-byte `state` (parent `protect-frame`) whose name
symbol's value is that same object, which is how `defstate` publishes a state.
Check 17 still imports the main thread with `dc_goal_fiber_init_main`. That
import copies the uint64 at thread+36 into `target_fiber.entry`
(`goal_main_start.c`), so the fiber entry must be `enter-state`.
`init_entry` is still stored from the authenticated
`init-target` export. The probe block, case numbers 16 and 17, and the 7652
total are unchanged. `verify_arena_jak_player.py` was not edited: `init_entry`
still means `init-target`.

This was not compiled and not run. The last Flycast stop was check 16 comparing
thread+36 with `init-target`; +28, +40, +44 and +48 had already matched the
stack top, zero, the continue point and zero. The new `enter-state`
authentication, the `next-state` predicate, and the fiber import have not
executed. That import also requires `waiting-to-run`, thread+20 equal to
original `set-to-run-bootstrap`, and sp at the stack top; those were not
reached. Dispatcher, pad and motion checks remain unobserved.

Observed in Flycast with that change: checks 16 to 19 pass (7011 checks pass).
The run then never completes: it stops inside the first call to the original
`kernel-dispatcher` (just before check 20), which is where Jak's first state
runs. Flycast logs no exception; the verifier times out.

`verify_backend.py --dump` now also writes `dump.bin` at each progress
sample, so a run that never completes still leaves the latest bytes.
Observed during the hang (`dc_arena_jak_player_evidence`): active 1, phase 2,
attempts 2, resumes 2, pad_services 1, pad_valid 1, frames 1, error 0. Trace
frame 0 holds Jak's translation (-1017659.9, 211967.2, -1055179.9) from the
continue point. So the original target ran enter-state and its first frame;
the dispatcher then does not return during the second frame.
`dc_goal_integer_trap_probe` stays empty: no integer trap or (break) fired.

## Second-frame dispatcher hang (AI-assisted)

The C loop in `arena_jak_player.c` records a trace frame only after
`kernel-dispatcher` returns. During the hang the receipt is still frame 1,
with attempts 2, resumes 2, pad_services 1, pad_valid 1, and evidence error 0.
`dc_arena_jak_player_resume` counts only the target process. Those two
resumes are the target fiber's two completed yields. `attempts` and `resumes`
stay at 2, so the dispatcher is not re-entering the target. `pad_services`
increments in `cpad_get_data_native`, which `service-cpads` calls from
`display-frame-start`. That runs inside the display process, and the active
tree visits display after the target.

`change-parent` prepends. The pools under `*active-pool*` are therefore
walked default, entity, target, camera, display. `execute-process-tree`
saves the next brother before the recursive call. A brother cycle that is
already linked is followed in that same call. A brother pointer stored
during the visit is the link the next dispatcher call walks. A child cycle
is followed in the same call, after the node's function returns.
`start` / `init-target` link the target under `*target-pool*` and the
sidekick under the target. That linking returns before the first dispatch.
The source of `change-parent` does not build a cycle. A cycle among
camera, sidekick, or display would leave the target counters at 2 and would
show up as a repeating resume pair in the heartbeat ring below.

`dc_goal_fiber_step` is what `thread_resume` runs. On a suspended fiber it
restores the stack image and context-swaps into the fiber. `suspend` marks
the process `suspended` and `dc_goal_fiber_yield` swaps back through
`dc_goal_context_leave_raw`. The step then sees `DC_FIBER_SUSPENDED`,
backs up the stack, and returns `DC_FIBER_YIELDED`. `goal_main_thread.c` is
only the arming preflight (stack, pc, sp, status). It does not perform that
swap. The second target yield took this path and returned: resumes is 2 and
the runtime error stayed 0. The context swap is not where this hang sits.

Boot already ran the display process for `DC_LIVE_UPDATES` frames and left
it in `display-loop`'s `while *run*` body. Each player dispatch is one more
iteration of that body. `swap-display` → `display-sync` →
`display-frame-start` calls `service-cpads` before `level-update`. The
second call is stuck after the target yield and before that pad read:
target post, the sidekick, the camera pool, or display code above
`service-cpads`.

`get-or-create-continue!` runs before `start`. Its `'play` plus
`current-continue` branch is the progress-menu path. This boot takes the
other branch and passes `*default-continue*` (name `"default"`).
`target-continue` then takes the default arm. The first resume enters the
state and suspends after `move-to-point!` and `change-state` of `cam-fixed`.
That matches the one trace frame. The second resume is the code after that
suspend: load-state restore, `(execute-command '(teleport))`,
`change-target`, `change-state` of `cam-string` with parameter 0,
`*spawn-actors*`, `*teleport*`, then `suspend-for`. `suspend-for` suspends
on its first iteration. That yield is the one the counters already recorded,
so that GOAL ran and returned to the dispatcher.

`change-state` with parameter 0 sends `'change-state-no-go`. The handler
runs the new state's `:enter` and installs its event hook. It does not call
`enter-state`, so the slave's main thread stays in `cam-free-floating`
(`cam-master-init` spawned it there) and its trans/post hooks stay the old
ones. `cam-string`'s `:enter` ran inside the target fiber, before the yield
that returned. `cam-string`'s `:trans`, which can `cam-slave-go`, is not the
hook that is installed.

On the first player frame the camera master still has `master-options` bit 2
clear (`cam-master-init` stores 4). `*target*` is set, and the frame-1
events are `clear-entity` and `cam-fixed`, which do not set bit 2.
`master-track-target` therefore takes its short arm, sets bit 2, calls
`reset-target-tracking`, and returns: the first dispatch did return, and
display then serviced the pad, so the master got through that arm. The
second frame is the first time bit 2 is already set. `change-target` of
`*target*` stores a false drawable target, sets bit 2 again, and resets
tracking once more, still inside the fiber that returned. After the yield,
`master-track-target` takes the long arm (`tracking-spline-method-17` and
`method-22`, then region checks). That arm did not run on frame 1. It is
the leading place the second call can spin. `method-9`, `method-14`,
`method-18`, and `method-20` walk `point[].next` until a sentinel or an
endpoint. `method-18` stops on the end point, not on the sentinel
`-134250495`. A cycle in that index list stays inside the spline and raises
no trap. `method-10`, called by `reset-target-tracking`, rebuilds a 0..31
chain ending at the sentinel, and that call already returned inside the
target fiber. A cycle would have to be introduced by the long arm itself.
That is not established.

The same window still includes code that did run on frame 1: target
`target-no-move-post` (its `(the int time-ratio)` countdown;
`set-time-ratios` stores `(fmin 4.0 slowdown)` from the previous
`display-frame-start`), the sidekick, the slave's still-`cam-free-floating`
body with the fields `cam-string`'s `:enter` rewrote, and the display while
body above `swap-display`. `level.gc`'s non-suspending `load-continue` loop
is inside `level-update`, which is after `service-cpads`, so it is outside
this window.

No original GOAL was changed. The fiber engine was not changed.
`goal_fiber.c`, `goal_main_thread.c`, `native_context_swap.S`, and
`pp_scope.c` are not built with `DC_ARENA_JAK_PLAYER`; a guard there would
not be in this ELF. The Makefile already compiles `goal_bridge.c` as
`arena_jak_player_bridge.o` with that flag, so the heartbeat lives there
and no other ELF's object changes.

Heartbeat symbol: `dc_arena_jak_player_heartbeat`. Size: 132 bytes.
`verify_backend.py --dump dc_arena_jak_player_heartbeat 132` rewrites
`dump.bin` on each progress sample. Little-endian `uint32` fields:

| offset | field |
| --- | --- |
| 0 | `seq`, count of completed samples, stored last |
| 4 | `site` |
| 8 | `process` |
| 12 | `name`, the basic at process+0 |
| 16 | `thread` |
| 20 | `phase`, fiber phase (suspended is 3) |
| 24 | `event` |
| 28 | `depth`, lexical pp depth |
| 32 | `status`, the basic at process+32 |
| 36 | `ring_process[8]` |
| 68 | `ring_site[8]` |
| 100 | `ring_name[8]` |

The latest ring slot is `(seq - 1) & 7`. Sites: 1 resume before
`dc_goal_fiber_step`, 2 step returned yielded and suspended, 3 step returned
another way (`event` is the fiber event: fail 0, yielded 1, completed 2),
4 `suspend` about to yield, 5 scope enter (`process` is the entered
process), 6 scope leave (`process` is the lexical process being left,
`thread` is the restored outer process), 7 `reset-and-call` before the
trans/post function (`event` is that function, `thread` is the temporary
cpu-thread), 8 that call returned, 9 `cpad-get-data` entered (`thread` is
the cpad-info, `process` is the current fiber), 10 `thread-resume` refused.
A frozen `seq` is the last bridge before a loop that no longer calls one.
The process and site of that sample are the loop's entry. A climbing `seq`
whose ring repeats one process at sites 1, 4, and 2 is that process being
resumed again inside the same dispatcher call. Sites 5 and 6 repeating are
a scope bracket spinning. Site 7 frozen on the target is the post body.
Site 1 frozen on the camera master is the long `master-track-target` arm
(or whatever that resume called that does not come back to a bridge).

This was not compiled and not run. The 132-byte image has not been dumped.
Which of the post-yield bodies is the loop, whether a spline `next` index
cycles, and whether `time-ratio` is the clamped value the post counts, are
unverified. Flycast, the link, and the other ELFs were not run.

Observed heartbeat (`--dump dc_arena_jak_player_heartbeat 180`; the struct
now ends with 24-byte `type_name` and `process_name`): `seq` stops at 7953.
The last events are the target (scope leave, suspend, yield, post call and
return) and then the `sidekick` process: resume, suspend, yield (site 2,
event 1, phase 3). No bridge is called after that, so the dispatcher loops in
GOAL code that resumes nothing: the camera master is never resumed. Leading
suspect: the process tree walk after the sidekick (a brother/child cycle
formed during frame 2, or a run of nodes the walk skips).
