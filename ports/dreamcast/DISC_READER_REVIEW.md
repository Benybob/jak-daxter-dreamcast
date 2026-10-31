(AI-assisted)

# Disc reader review

Read-only review of `disc_reader.h`, `disc_reader.c`, `disc_reader_kos.h`, `disc_reader_kos.c`, `pack_disc_level.py`, `tests/disc_reader_check.c`, `tests/test_disc_reader.py`, and `tests/test_disc_layout.py` against `DISC_STREAMING_PLAN.md` and `DISC_READER_NOTES.md`. KOS behavior was checked in the tree’s `kos-source` (mutex, semaphore, ISO 9660 read, GD-ROM DMA, thread priority). No source file other than this review was edited. Tests, the packer, `sh-elf-gcc`, and Flycast were not re-run.

## Findings

### 1. Medium — the “previous window stays unchanged” checks cannot fail

`tests/disc_reader_check.c` lines 89 and 99–106, and the `submitted == 64` check on line 104.

`previous` and `displayed` are 16-byte stack arrays. Line 89 copies one into the other. The only reader destination is `destination+1` (line 90). Nothing in the reader, the worker, or the 64-iteration loop writes `previous` or `displayed`. `memcmp(displayed, previous)` is therefore true for every reader outcome, and `submitted` only counts loop trips.

Failing case that still passes those assertions: a worker that, on each poll during `DC_DISC_READING`, copies the in-flight bounce into the caller’s last completed window. The harness never passes that window in, so the assertion stays green. The notes still describe this group as proving a previous complete output stays unchanged while 64 frames observe `READING`.

The surrounding checks can fail. `poll == DC_DISC_READING` while `read_chunk` is blocked (lines 100, 105, 108), the post-join byte scan (lines 111–114), and the overlap refusal at line 95 are real. They do not cover the previous-window claim.

### 2. Low — the eight-slot group never looks at payload bytes

`tests/disc_reader_check.c` lines 136–139.

Completion order is checked with `f.offsets[i]`, which `read_chunk` records from the offset argument (lines 36–37). The eight `destination[i]` buffers are never read.

Failing case that still passes `queue()`: `memcpy` in `dc_disc_reader_work` always writes `requests[0].destination` and leaves `destination[1]` through `destination[7]` untouched. Every `offsets[i] == i*2048` check still holds, every poll is `DC_DISC_READY`, and `calls == 9` still holds. `late()` does not catch it: that group has one live destination, which is slot 0.

### 3. Low — a failed multi-chunk read is accepted after two byte probes

`tests/disc_reader_check.c` line 162.

For a 40000-byte request, chunk 0 is 32768 bytes and chunk 1 starts at destination index 32769. The test requires `destination[1] == pattern(0)`, `destination[32769] == 0xac`, the two canaries, and final state `DC_DISC_FAILED`.

Failing case that still passes: on the successful chunk, copy one byte and leave the other 32767 bytes as the `0xac` fill. Index 1 matches `pattern(0)`, the failed chunk’s first byte is still `0xac`, and the state is `FAILED`. A one-byte overrun of the failed chunk is caught; a short successful prefix is not.

`late()` lines 111–112 do scan all 70000 success bytes and both canaries. That path is sound. The failure path is the weak one.

## What is clean

Publication. `dc_disc_reader_work` (`disc_reader.c` 86–96) drops the metadata mutex before `read` and `memcpy`. It stores `DC_DISC_READY` or `DC_DISC_FAILED` under that mutex only after the loop. A short, negative, or oversized return fails the request (`disc_reader.c` 90–91). `DC_DISC_FAILED` is a different state from `DC_DISC_READY`. The first failed chunk is not copied. KOS `mutex_lock` / `mutex_unlock` call `irq_disable` (`kos-source/kernel/thread/mutex.c`), which is an external function, so the compiler cannot sink the `memcpy` past the ready store. `poll` copies `state` out under the same mutex (`disc_reader.c` 62–67).

No bounce overrun for the current constants. `DC_DISC_BOUNCE_BYTES` is 32768, a multiple of 2048. The loop caps `copy` at 32768, and the sector round-up of any `copy <= 32768` is at most 32768. The destination receives `copy` bytes, so the last sector’s padding is not required to fit in the caller buffer. The 70000-byte split expected by the harness (32768 + 32768 + 6144 at file offsets 2048, 34816, 67584) matches that arithmetic: `align(70000) == 71680` and `2048 + 71680 == 73728`.

Submit bounds. Sector alignment, non-zero length, `length <= capacity`, span checks, and `offset + align(length) <= file_bytes` are enforced (`disc_reader.c` 33–38). `file_bytes` must be non-zero, sector-aligned, and `<= INT32_MAX` (`disc_reader.c` 24), which matches a signed 32-bit KOS `off_t` (`sys/_types.h`: `__off_t` is `long` off Cygwin). The adapter applies the same cap to `fs_total64` before the `uint32_t` cast (`disc_reader_kos.c` 32–38). `fs_total64`’s error return is `(uint64)-1`, which fails that cap.

Queue and lifetime. One producer fills an eight-deep index ring; the worker is the only consumer. Release refuses `QUEUED` and `READING` (`disc_reader.c` 73). Generation 0 is not a valid ticket; a slot whose generation is already `UINT32_MAX` is not selected (`disc_reader.c` 44–45, 15). Wake is after unlock (`disc_reader.c` 55–56). KOS `sem_signal` keeps the count if the worker is not yet in `sem_wait` (`sem.c` 171–179), and both paths take `irq_disable`, so the submit/close wakeup is not dropped. `close` sets `closed`, wakes, and the adapter `thd_join`s before `fs_close`, `sem_destroy`, `mutex_destroy`, and `free` (`disc_reader_kos.c` 50–55).

Frame blocking and priority. The worker is the only caller of `fs_seek` / `fs_read` (`disc_reader_kos.c` 11–14, 16–22). `fs_read` of a 32-byte-aligned whole-sector buffer takes the direct DMA path (`fs_iso9660.c` 690–702) and `cdrom_read_sectors_ex` waits on that thread (`cdrom.c` 396–412). The frame APIs wait on the metadata mutex only. The main thread is created at priority 0, which `thd_create_ex` rewrites to `PRIO_DEFAULT` (10) (`thread.c` 371–387 and 1004–1052). The worker asks for `PRIO_DEFAULT + 1` (`disc_reader_kos.c` 26–27). A larger priority value is lower priority (`thread.h` 89–99), so the reader does not outrank the main thread. `pvr_wait_ready` blocks in `genwait_wait` (`pvr_scene.c` 395–403), which is when this lower-priority worker can run. `_g1_ata_mutex` is taken by the CD and G1 ATA paths; the PVR ready wait does not take it.

ISO layout versus the DMA rules. The adapter seeks to the file-relative byte offset and does not add 150 (`disc_reader_kos.c` 13). `iso_read` adds 150 itself (`fs_iso9660.c` 699–700). `memalign(32, 32768)` satisfies the 32-byte DMA rule (`cdrom.c` 457–459). `dcache_inval_range` steps 32-byte lines and stops at the end address (`cache.s` 98–116); a 32-byte-aligned 32768-byte range does not invalidate the following line.

On-disc layout. The header struct `<4sIIIfiiIIIIIII` is 56 bytes. Extents at 56, 68, and 80 are global, manifest, and package (`pack_disc_level.py` 119–124), matching the notes. Payload order is global, catalog textures, Morton cell pairs, manifest, package (lines 100–118). Morton puts x in even bits and z in odd bits (lines 31–35); the 4×4 order in `test_disc_layout.py` lines 81–83 matches that interleave. Padding between a `.drx` and its `.dci` is only the sector pad, and `window_runs` merges runs only when `offset + padded length` of one extent equals the next offset (lines 222–241). Empty payloads are rejected (line 92). File size is capped at `0x7FFFF800`, the largest sector multiple that fits in a positive signed 32-bit offset (line 19).

Negative cells. Tile id is floor division and the local index is Python `%` (lines 79 and 111–112). For a positive tile size, `(x // 8) * 8 + (x % 8) == x`, including negatives: cell −9 is tile −2, local 7, and decodes back to −9. `read_index` rebuilds names from that pair (lines 188–193). The layout test’s −9 cluster would miss `set(expected)` if that round trip changed the name. The 6250 outliers make a dense full-grid directory larger than one 2048-byte sector; the index extension at file offset 2048 is the documented DCLV v1 shape, and the extent offsets are file-relative so a reader does not assume payload starts at 2048.

`test_disc_layout.py` can fail closed on a byte mismatch, a broken pair, a run that includes a stranger sector or drops a selected sector, a resigned gap, and a flipped payload or padding byte. Those checks call `read_index`, which rejects bad magic, CRC, alignment, overlap, gaps, and trailing bytes.

## What I could not confirm

I did not re-run `test_disc_reader.py`, `test_disc_layout.py`, `pack_disc_level.py`, or the `sh-elf-gcc` / `nm` commands in the notes. The reported pack result (`2424832` bytes, 394 cells, 17 tiles, index 27204, 74 textures, `texture_bytes` 399168) is consistent with the index formula `ntex*12 + ntile*(12+1536)` but was not regenerated here.

`disc_reader_kos.c` is not in the host link (`test_disc_reader.py` lines 23–25). It was not executed. `fs_read` on hardware, GD-ROM status codes, seek time, and DMA throughput are unmeasured.

The 8192-byte worker stack (`disc_reader_kos.c` 26) is below `THD_STACK_SIZE` (32768, `arch/arch.h` 84). `fs_read` → `iso_read` → `cdrom_read_sectors_ex` has small frames in the C I inspected. The BIOS GD-ROM syscall’s stack use was not measured, so an overrun on that stack is unconfirmed.

No block-cache or instance-cell caller submits, polls, or draws from this reader. Milestone 5 (a real `pvr_scene_finish` before ready, with the frame free of `fs_read`) and milestone 6 (last window kept, empty terrain until ready) are not exercised. `READY` means the copy finished. Extent CRC32 is stored by the packer and checked by `read_index`; the runtime reader does not recompute it.

There is no C decoder of DCLV v1. A later SH-4 decoder has to use floor division for negative cells. C’s `/` and `%` truncate toward zero, and `-9 / 8` is −1 with remainder −1, which is a different tile than the Python encoding. That decoder does not exist in this change.

The eight-slot cap is intentional (`DC_DISC_REQUESTS`). A 3×3 of separate `.drx` and `.dci` requests plus the plan’s prefetch ring does not fit in eight live tickets. `submit` returns 0 and leaves the ticket unchanged. Milestone 7 is not implemented; this prototype does not claim it.
