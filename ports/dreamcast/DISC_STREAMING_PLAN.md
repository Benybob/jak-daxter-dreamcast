(AI-assisted)

# GD-ROM streaming plan

Level bytes today are a second romdisk copied into a VRAM loan and mounted at `/rd`. A retarget reads them on the frame thread. This plan puts one level in one disc extent, fills a fixed bounce buffer on a KOS thread, and leaves the last complete window up while a block is still on the drive.

## What the frame does today

`dc_render_block_cache_window` walks the query, and a missing block calls `read_draw_file` before it returns (`render_block_cache.c` 307–317). That call `fopen`s `sectors/<ix>_<iz>.drx` or `global.drx`, seeks to the end, seeks back, and reads the whole file (`render_block_cache.c` 117–127). `dc_instance_cells_update` does the same for `cells/<ix>_<iz>.dci`, 52-byte records or a DCL4 palette cell (`instance_cells.c` 69–82, 49–54). On the arena-fly ELF those calls are the slab wrappers: `fs_open` / `fs_read`, and the `FILE` token is a slab object (`arena_slab.c` 67–88, `arena_slab.h` 24–26). `arena_fly_frame` runs both updates, then `CHECK`s a null error (`arena_fly.c` 127–129). A miss fails the frame.

The window contract already keeps the previous faces, vertices, and readiness when the build fails (`render_block_cache.h` 40–41). The fly path aborts the frame instead of drawing that window.

`/cd` exists. `INIT_DEFAULT` includes `INIT_DEFAULT_ARCH`, which includes `INIT_CDROM` (`runtime_main.c` 19, `kos/init.h` 143–144, `arch/init_flags.h` 91, 107). `arch_auto_init` calls `fs_iso9660_init` (`init.c` 217–218), and the VFS name is `/cd` (`fs_iso9660.c` 1059–1062). `main.c` already tries `/cd/JAKPROBE.DGO` before `/rd` (`main.c` 18–29). The arena ELF is `runtime_main.c` (`Makefile` 775–776), so that init runs. `INIT_SIP` and `INIT_CDROM` are both `0x00100000` (`arch/init_flags.h` 104, 107). The CD flag is set either way. Whether the shared bit is intentional was not confirmed.

## On-disc layout

KOS’s driver reads the CD area of a bootable CDR, one audio track plus one XA1 data track, and does not read the high-density GD area (`cdrom.c` 30–38, `cdrom.h` 23–26). `cdrom_read_toc` takes a high-density flag (`cdrom.c` 381–388). `cdrom_read_sectors_ex` does not. The first image is a CDI data track. A high-density GD session is out of reach of this driver.

One file per level, `/cd/levels/<name>.bin`, not one ISO file per cell. `fs_iso9660` caches 16 data sectors and 16 inode sectors (`fs_iso9660.c` 206–208). A 32-byte-aligned read of whole 2048-byte sectors skips that cache and calls `cdrom_read_sectors_ex` with the extent plus 150 (`fs_iso9660.c` 690–702). Many small files still pay a directory walk per `fs_open`. One open file pays it once.

Inside the file, 2048-byte aligned:

1. Header: magic, cell size, grid origin, and a dense `(ix, iz)` table of file-relative offset and length for `global.drx`, each sector, each cell, and each texture page.
2. `global.drx`. The window loop visits it first (`render_block_cache.c` 307–308, `n == 0`).
3. Texture pages, in catalog order. They are shared across the level, so they sit in one run.
4. Cells in Morton order of `(ix, iz)`. Each cell’s `.drx` is followed by its `.dci`, both padded to a sector. A forward step along one axis is a short seek. A 3×3 is a handful of adjacent runs, not nine directory lookups. Empty cells stay absent, matching today’s missing file (`instance_cells.c` 74–76).

The resident prototype package stays one read at level start (`arena_fly.c` 102–106). Instance streaming already leaves geometry in the package and puts records in the cells (`LEVEL_STREAMING.md` 667–671). Offsets in the header are file-relative. No KOS call in the headers read here returns a file’s extent, so the image builder does not have to patch FADs. The reader adds nothing to those offsets. `fs_read` on that one `file_t` does the +150 itself.

The open fixture this tree has is a crop, not a full level: `build/arena-fly-disc/fly/report.json` records 27 local sectors, cell size 16 m, 7,611 sources, `texture_bytes` 399,168, `package_bytes` 1,511,807. `FLY.json` records a 6.0 m, 16-frame path and a max window of 2,916 faces. Those files are the first packing test. Full-level `.drx` trees were not re-statted in this session.

## Fixed buffer and the reader thread

`cdrom_read_sectors_ex` in DMA mode blocks the caller: it takes `_g1_ata_mutex` and `sem_wait`s until the G1 IRQ or vblank completes the command (`cdrom.c` 396–412, 750–784). The default sector size is 2048 (`cdrom.c` 86). The buffer must be 32-byte aligned (`cdrom.c` 457–459). `cdrom_stream_request(..., false)` can return before the DMA finishes (`cdrom.c` 587–602), but stream state is one global command (`cdrom.c` 503–505). The reader uses one-shot `cdrom_read_sectors_ex` through `fs_read`, not the stream API.

One KOS thread, `thd_create_ex` (`kos/thread.h` 399–418). Scheduling is preemptive (`kos/init.h` 143–144, 154). Priority is a number above `PRIO_DEFAULT` (10); a larger number is a lower priority (`kos/thread.h` 90–99). The reader sits below the frame thread so `cdrom_poll`’s `thd_pass` (`cdrom.c` 106–107) does not sit on top of `pvr_wait_ready`.

RAM that is not the slab:

- One bounce buffer, `memalign(32, 32 * 1024)`, from the KOS heap, before the renderer slab. 16 sectors, the same count as the ISO cache. The published village1 DRX2 sector is 129 KB (`LEVEL_STREAMING.md` 511), so one block is several trips. The bounce buffer is reused. It is not a second copy of the level.
- The block-cache slots stay the existing budget, at most 2 MiB, 128 slots (`render_block_cache.h` 6–7). The path floor for village1 is 520,992 bytes (`LEVEL_STREAMING.md` 626–627).

The slab walker takes no lock (`arena_slab.c` 15–32). The frame thread is the only caller that allocates a cache slot. On a miss it evicts unheld slots the way `read_draw_file` already does (`render_block_cache.c` 123–125), allocates the empty slot, enqueues `(file offset, length, destination)`, and returns. The reader DMAs into the bounce buffer and copies into that destination, then sets a ready flag. The frame thread never calls `fs_read`, `cdrom_*`, or `fopen` for a retarget.

The 2.55 MB live-heap sample (`LEVEL_STREAMING.md` 675–678) sits in a 2,621,440-byte slab (`arena_coexist.c` 21). That margin does not hold another large buffer. The bounce buffer stays on the KOS heap so the slab high-water test is unchanged.

## Prefetch

The published village path moves the target 2 m a frame for 30 frames along the view (`LEVEL_STREAMING.md` 371–372). The arena fixture is 6.0 m over 16 frames (`FLY.json`). Prefetch uses the last two targets’ XZ delta, not a constant speed. Along that delta it enqueues two 16 m cells at the 12× estimate below, or three at the 4× estimate, plus the ring one cell outside the current query. It does not enqueue cells behind the motion. `global.drx` and the texture run are enqueued at level entry, before the first retarget.

A 2 m step crosses a 16 m cell about every eight frames. The read budget below is longer than eight frames at the slow estimate, which is why the enqueue happens before the query needs the block. Flycast frame times in the notes are not a 60 Hz hardware rate (`LEVEL_STREAMING.md` 638).

## What is drawn while a block is late

If any sector of the new query is not ready, the frame does not call `read_draw_file` and does not merge a partial window. Replicas are checked across a full build (`render_block_cache.h` 37–39). The last window with `valid` set stays. `dc_render_draw_window_prepare` still runs on it (`render_draw_window.h` 27–28, `arena_fly.c` 134). The first frames, before any window exists, submit the clear color already set (`arena_fly.c` 117) and no terrain polys.

Instance cells that are not ready are left as they were. `pkg->instances` keeps the last successful update (`instance_cells.c` 136). A missing cell is not a `CHECK` failure.

Textures are different. `dc_render_base_bank_make` refuses a catalog texture whose VRAM slot is missing (`render_base.c` 45–46). The instance drawer already falls through to an untextured colour header when `materials[i].header` is null (`instance_pass.c` 224–229). Until terrain has the same fallback, the texture run is read and `pvr_txr_load`ed before the first textured frame (`render_gpu.c` 42–46). That read may block. It is not a retarget. After the fallback exists, a page that is still in the bounce buffer draws with the colour header for that material only.

`pvr_txr_load` from the reader thread, concurrent with `pvr_scene_begin`, was not confirmed safe. Upload stays on the frame thread, from a buffer the reader has marked ready, a few pages a frame.

## Sizes measured in the repo

These are the DRX2 figures recorded in `LEVEL_STREAMING.md` 583–601 and 649–660. This session did not rebuild the levels. The note’s “MB” is treated as decimal where a byte count is derived below. Window KiB are the note’s own units. “Worst” is the worst 3×3 plus global at 16 m, except the three caves, which use a smaller cell.

| Level | Faces | DRX2 | Worst window | 28 m window budget |
| --- | ---: | ---: | ---: | ---: |
| village3 | 161,943 | 6.83 MB | 0.90 MB | 1,456 KiB |
| village1 | 84,747 | 3.54 MB | 0.56 MB | 980 KiB |
| village2 | 47,228 | 2.00 MB | 0.41 MB | 821 KiB |
| sunkenb | 33,936 | 1.53 MB | 0.43 MB | 660 KiB |
| sunken | 89,914 | 3.83 MB | 0.39 MB | 640 KiB |
| beach | 119,921 | 4.78 MB | 0.37 MB | 595 KiB |
| misty | 73,631 | 2.83 MB | 0.27 MB | 572 KiB |
| rolling | 79,855 | 3.49 MB | 0.21 MB | in the ≤415 KiB group |
| lavatube | 19,063 | 1.09 MB | 0.05 MB | in the ≤415 KiB group |
| darkcave (12 m) | 172,634 | 6.88 MB | — | 1,626 KiB |
| maincave (6 m) | 317,110 | 13.09 MB | — | 2,386 KiB |
| robocave (6 m) | 247,405 | 10.10 MB | — | 1,822 KiB |
| finalboss | 223,594 | 21.0 MB | global 11,519 faces | does not fit a 4,096-face global (`LEVEL_STREAMING.md` 715–720) |

Named DRX2 rows sum to about 81 MB. The other ten levels have no byte totals in that note; rolling and lavatube are in that group, whose 28 m samples are 415 KiB or less (`LEVEL_STREAMING.md` 662). Village1’s biggest single DRX2 sector in the format row is 129 KB (`LEVEL_STREAMING.md` 511). Per-level biggest sectors were not in the table.

Textures at the 64-pixel cap: village1 717,312 bytes, village2 787,712, and the note says every level fits in the 877,312 bytes that model leaves (`LEVEL_STREAMING.md` 686–688). That model is the flythrough `pvr_init` (2 MiB vertex buffer, overflow 8) in `texture_budget.py` 25–28 and `flythrough_main.c` 80–81. Other levels’ exact texture bytes were not listed. The arena fly uses a 512 KiB vertex buffer (`arena_slab.h` 5, `arena_coexist.c` 104–105).

Cell records on the village1 path: 835 of 7,342, 0.72 MB of package plus 43 KB of cells, peak live heap 2.55 MB (`LEVEL_STREAMING.md` 672–678). The same five-level path table is at `LEVEL_STREAMING.md` 692–698. Those paths follow instances and are not the worst windows.

maincave’s 2,386 KiB window is above `DC_RENDER_BLOCK_CACHE_MAX`. Disc streaming does not raise that cap.

## Seek and read budgets (estimates)

No seek time and no bytes-per-second figure is in this repo or in the KOS CD sources read here. The rates below are planning estimates, not measurements.

Mode-1 1× is 75 × 2048 = 153,600 bytes/s. The usual published GD-ROM ceiling is 12×, 1,843,200 bytes/s, at the outer edge. A CDI session and the inner tracks are slower. The slow column is 4×, 614,400 bytes/s. Short seek 100 ms, long seek 300 ms. Both seeks are guesses.

| Transfer | Bytes used | 12× read | 4× read | 12× + 100 ms | 4× + 100 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Village1 one sector, 129×1024 | 132,096 | 72 ms | 215 ms | 172 ms | 315 ms |
| Village1 worst 3×3, 0.56 MB decimal | 560,000 | 0.30 s | 0.91 s | 0.40 s | 1.01 s |
| Village1 all DRX2, 3.54 MB decimal | 3,540,000 | 1.92 s | 5.76 s | 2.02 s | 5.86 s |
| Village1 textures | 717,312 | 0.39 s | 1.17 s | 0.49 s | 1.27 s |
| 835 records, 52 bytes | 43,420 | 24 ms | 71 ms | 124 ms | 171 ms |

A level change adds the 300 ms guess once, then the texture run, then `global.drx`. The whole-level row is the transfer play does not do. The working transfer is one sector plus its adjacent `.dci`, coalesced when the header says they are contiguous. At a 16.7 ms frame the 12× single-sector estimate is about 11 frames (about 22 m at 2 m a frame), so two 16 m cells cover it. The 4× estimate is about 19 frames (about 38 m), so three cells cover it. Those frame counts assume 60 Hz, which this tree has not measured on hardware.

## How the romdisk and the VRAM stand-in go away

The boot image is `KERNEL.CGO` and `GAME.CGO.zst` (`Makefile` 801–803), refused above 2 MiB (`romdisk_stage.c` 19–27, `arena_coexist.c` 114), copied with 32-bit stores and mounted at `/rd` (`romdisk_stage.c` 22–30). The level image is a second loan: `pvr_mem_malloc`, a 32-bit copy, and on the first frame `fs_romdisk_unmount("/rd")` then `fs_romdisk_mount` of the VRAM pointer (`arena_coexist.c` 70–74, 125–141). Readers then use `/rd` (`arena_fly.c` 81, 102).

The disc ELF:

- Links neither `build/kernel-dgo-romdisk.o` nor `build/arena-fly-romdisk.o`. Today those objects are on `jak-dc-arena-fly.elf` (`Makefile` 868). The boot object is built with `-lromdiskbase` (`Makefile` 808). The fly image comment already omits that library (`Makefile` 859). Nothing is placed at `.dc_stage` or `.dc_fly`.
- Does not call `dc_stage_romdisk_at` or the fly VRAM copy. `dc_renderer_reserve` and the DMA loan can stay. The 2 MiB boot loan and the level loan are not taken, so that VRAM stays in the pool `pvr_mem_malloc` uses for textures.
- Boot, before the frame loop, reads `/cd/boot/KERNEL.CGO` and `/cd/boot/GAME.CGO.zst` with the same loader that now opens `/rd/KERNEL.CGO` and `/rd/GAME.CGO.zst` (`kernel_boot_cases.c` 147–149, 207–208). Those reads may block. The damage reopen of `/rd/KERNEL.CGO` is compiled out when `DC_ARENA_COEXIST` is set (`kernel_boot_cases.c` 430–436).
- After that load, no romdisk is mounted. Level bytes live in the cache slots and in VRAM texture slots. The bounce buffer is reused.

`dc_kernel_arena_bytes` stays `13 MiB - dc_renderer_reserve()` (`romdisk_stage.c` 45–46) until a run measures the heap without the linked image. The notes’ 1.5 MB boot size (`MEMORY_PLAN.md` 34) was not re-statted here. The Makefile fly rule read in this session used a 1,700,000-byte image cap and `.dc_stage` at `0x8cb00000` (`Makefile` 857–869), while `arena_coexist.c` 116–119 still describes a 1,081,024-byte remainder and the notes still say `0x8cd80000`. The disc ELF depends on neither ceiling.

## Flycast CDI, then `verify_backend.py`

Flycast keeps one content path. A `.cdi` becomes `settings.content.path` and the rest of the command line is ignored (`flycast-source/core/cfg/cl.cpp` 196–214). An ELF sets `UseReios` and the loader then calls `gdr::initDrive("")` (`emulator.cpp` 632–636), so `/cd` is empty. That is the current `verify_backend.py` command (`verify_backend.py` 94–96), which is why a CDI cannot satisfy `--tests` today (`ARENA_FLY_NOTES.md` 9).

A CDI takes the other branch: `gdr::initDrive(path)` mounts the disc, and `UseReios` still HLE-boots (`emulator.cpp` 609–617). For a non-GD disc, reios descrambles the boot file (`reios.cpp` 60, 106–110) and copies it to `0x8c010000` (`reios.cpp` 101). The initial PC is `0xac008300` (`reios.cpp` 672), the IP.BIN image loaded at `0x8c008000` (`reios.cpp` 74). The ELF-only path sets PC to `0x8c010000` and mounts no disc (`reios.cpp` 658–663, `emulator.cpp` 632–636). The jump from `0xac008300` into the copied ELF was not traced. KOS ships `makeip` (boot file `1ST_READ.BIN` in `utils/makeip/rsrc/inputs/ip.txt` 7) and `scramble` (`utils/scramble/scramble.c`, 2 MiB chunks at line 5). No CDI packer (`mkisofs`, `cdi4dc`) is in this tree. The image is track 1 audio, track 2 the ISO: `IP.BIN`, scrambled `1ST_READ.BIN`, `/boot`, `/levels`. Whether Flycast accepts a data-only CDI was not tried. The driver comment asks for the audio track (`cdrom.c` 37–38).

`verify_backend.py` cannot pass the ELF and the CDI. The second path is dropped (`cl.cpp` 212–214). The run that mounts the disc passes the CDI as the only content argument, keeps `-config config:UseReios=yes` and `Dreamcast.RamMod32MB=no` (`verify_backend.py` 94–96), and points `--nm` at the unscrambled ELF so `dc_backend` is still found (`verify_backend.py` 26–29). Pass remains `state==2`, `total==passed==--tests`, `system_ram==16777216` (`verify_backend.py` 147). Timeout stays in 1..300 s (`verify_backend.py` 25). The script’s positional argument is the ELF today (`verify_backend.py` 15, 123). Teaching it to `nm` one file and boot another is a milestone, not this change.

Symbol lookup assumes the copied image matches the ELF’s VMA. The flythrough note treats `0x8c010000` as that origin (`ARENA_INTEGRATION_PLAN.md` 8). The ldscript VMA was not re-read here. Flycast CDI timing is not a GD-ROM seek time.

## Milestones

Each one is a pass/fail check. Later steps wait on the earlier pass.

1. **Pack the open fixture.** Morton-pack `build/arena-fly-disc/fly` into one file with a 2048-byte header. Pass: every `.drx` and `.dci` byte range matches the source file, pairs that share a cell are adjacent, and a 3×3’s runs are contiguous where the header says so. Fail: a gap inside a pair, or a byte mismatch.
2. **CDI boots in Flycast.** `1ST_READ.BIN` is the scrambled arena-fly ELF, with `/cd/levels/fixture.bin` the packed file. Launch Flycast on the CDI alone, `UseReios=yes`, 16 MiB. Pass: `dc_backend` magic `0x34485347` (`runtime_main.c` 26) and `system_ram==16777216`. Fail: the process only starts when the ELF path is the content path.
3. **`verify_backend.py` mounts that CDI.** One content argument, the CDI. `--nm` reads the ELF. Pass: `result.json` records the CDI boot and the same 16 MiB check. Fail: the command still ends in the ELF path, or a second positional path is required.
4. **One blocking read matches the host.** Before the frame loop, read `global.drx` from the packed file through `fs_read` into a 32-byte-aligned buffer. Pass: CRC equals the host file. Fail: short read or a non-zero `cdrom` status.
5. **The reader thread does not stall the frame.** Start the thread, enqueue a multi-sector read, and count frames that reach `pvr_scene_finish` before the ready flag. Pass: that count is at least 1, and the frame function does not call `fs_read`. Fail: the frame returns only after the CRC is done.
6. **A miss keeps the last window.** The first retarget finds an empty cache. Pass: a PVR frame is submitted, `dc_backend.state` stays 1, and the terrain face count stays 0 until the ready flag. The next retarget’s face count and window CRC match the fixture. Fail: the `CHECK` at `arena_fly.c` 129 fires, or the CRC changes before every sector is ready.
7. **Prefetch on the 6 m path.** Enqueue two cells ahead of the target delta. Pass: the frame whose query first includes the next cell sees it ready, and cache misses on that frame stay 0. Fail: that frame takes the late path.
8. **Textures before the first textured frame.** Read the fixture’s 399,168 texture bytes (`report.json`) into VRAM with `pvr_txr_load` on the frame thread. Pass: the upload completes, the level-romdisk pointer stays null, and bank make does not return `"base texture role"`. Fail: `pvr_mem_malloc` was called with the level image size.
9. **Boot files from `/cd`.** `KERNEL.CGO` and `GAME.CGO.zst` open from `/cd/boot`. The ELF links neither romdisk. Pass: the existing GOAL checks pass, and `fs_romdisk_mount` is not called. Fail: either `fopen` still uses `/rd`, or a VRAM romdisk pointer is non-null.
10. **Terrain colour fallback, then drop the blocking texture read.** A null texture slot compiles a colour header instead of failing `dc_render_base_bank_make`. Pass: a frame whose texture ready-flag is clear still submits, and the instance and terrain draws both run. Fail: bank make returns an error, or the frame waits on `fs_read`.

Hardware seek times are not a milestone. The estimates above stay estimates until a drive is timed. maincave, robocave, and finalboss stay over the cache and global caps. This plan does not change those caps.
