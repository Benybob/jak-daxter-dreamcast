(AI-assisted)

# Sound probe: first keyed AICA channel

This is the first Dreamcast sound that keys an AICA channel. `aica_sfx.c` copies mono 4-bit ADPCM from `vag_adpcm.py` into sound RAM and starts, stops, and reports one channel. `sound_probe_main.c` loads two synthetic clips, plays each one looped long enough to observe key-on, stops it, and publishes the same 128-byte `dc_backend` record the other ELFs use.

The clips are build-time fixtures. The square is `encode_aica` of 8192 samples at amplitude 6000 with a half-period of 32. The vag clip is `convert_vag` of 160 filter-0, shift-0, flags-0 SPU blocks whose nibbles are `1,0,0,0` repeated. Filter 0 uses coefficients `(0, 0)` (`vag_adpcm.py` 23–24), so nibble 1 at shift 0 is the existing literal 4096 and nibble 0 is 0 (`vag_adpcm.py` 153–157, `tests/test_vag_adpcm.py` 80–84). Both lengths are even, so `encode_aica` adds no high-nibble pad (`vag_adpcm.py` 254–255 and 286–287), and both images are already multiples of 32 bytes: square 4096 bytes / 8192 samples, vag 2240 bytes / 4480 samples. The probe rate is 48000 Hz, the synth constant in `game/sound/common/sound_types.h` 9. It is a probe rate.

Out of scope: the `.sbk` bank parser, `rpc-call`, grain programs, name tables, and `goal_bridge.c`. Milestone 1 in `SOUND_PLAN.md` is the converter. This ELF is the first keyed channel of those bytes.

This session did not compile, link, run `tests/test_sound_fixture.py`, or launch Flycast. The check count below is a count of call lines in `sound_probe_main.c`.

`build/snd.log` stops because `dc_aica_sfx_status` was both the snapshot typedef and the query function. A typedef and a function are one ordinary identifier, so the function line was a different kind of symbol and `wait_playing` never became a definition. The snapshot type is now `dc_aica_sfx_info`. The function name is still `dc_aica_sfx_status`. The 59 `check_u32` / `check_i32` lines are unchanged. That edit was not compiled, linked, or run.

## Reviewer commands

Source the KallistiOS `environ.sh` the other Dreamcast targets use (`README.md` 200–213), then run these from `ports/dreamcast`. `FLYCAST` and `NM` are the same Flycast binary and `sh-elf-nm` the other `verify_backend.py` commands use.

```sh
python3 make_sound_fixture.py build/sound_fixture.h
make -f Makefile -f sound_probe.mk jak-dc-sound-probe.elf
python3 tests/test_sound_fixture.py
python3 verify_backend.py FLYCAST jak-dc-sound-probe.elf --nm NM --output build/sound-probe --suite codegen --tests 59 --timeout 60
```

From the repository root the host test is `python3 ports/dreamcast/tests/test_sound_fixture.py`. It imports `ports/dreamcast` by path. It does not compile C and it does not launch Flycast.

`sound_probe.mk` is a second makefile. It does not replace `Makefile` and it does not add a goal to `all`. The header rule depends on `make_sound_fixture.py` and `vag_adpcm.py`, so the explicit `python3` line and the `make` line agree. Recipes are tab-indented and use `mkdir`, `rm`, and `$(KOS_CC)` the same way `Makefile` does. `PYTHON ?= python3` in the snippet does not override `Makefile`, which sets the same name first.

`make -f Makefile -f sound_probe.mk sound-probe-clean` removes only `jak-dc-sound-probe.elf`, `sound_probe_main.o`, `aica_sfx.o`, and `build/sound_fixture.h`. It still needs `environ.sh`. The parent refuses any goal other than `host`, `test`, `fixture`, or `clean` when `KOS_BASE` is unset (`Makefile` 32–37). `clean` at `Makefile` 296 does not list these files, and the parent makefile was left unchanged.

## Expected check count

`DC_SOUND_PROBE_CHECKS` is 59. `sound_probe_main.c` lines 107–165 contain 59 indented `check_u32(` / `check_i32(` calls, one check per line. The host test counts that pattern and compares it with the define, and it requires the count to be something other than 17.

Pass `verify_backend.py` with `--tests 59`. The pass predicate is `state==2`, `total==--tests`, `passed==--tests`, and `system_ram==16777216` (`verify_backend.py` 155). `failed_case` is the 1-based index of the first miss. Later checks still increment `total` and `passed` and leave `failed_case`, the observed/expected words, and the description alone. Negative error codes are stored as their uint32 bit patterns.

`dc_backend.state` stays 1 until every check has run. The vblank reader treats only state 2 or 3 as terminal (`verify_backend.py` 72–73), so a mid-run sample cannot freeze a partial total. On a clean run `code_bytes` is 4096+2240 and the description is `AICA square and vag keyed then stopped` (38 bytes plus a NUL, inside the 80-byte field). `verify_backend.py` does not read `code_bytes` or the description. The record is the same layout as `backend_main.c` 17–22: magic `0x34485347`, then version, state, total, passed, failed_case, system_ram, code_bytes, observed_lo, observed_hi, expected_lo, expected_hi, and `description[80]`. A file-scope typedef requires `sizeof` 128. The ELF stores that terminal state, prints one line, and then loops in `thd_sleep(100)`. The print is not the contract: the verifier disables the serial console (`verify_backend.py` 100) and then terminates Flycast once the 128-byte file appears.

| # | Description stored on failure |
| --- | --- |
| 1 | stock 16 MiB RAM |
| 2 | snd_init |
| 3 | snd_init again |
| 4 | sample pool |
| 5 | null voice |
| 6 | null adpcm |
| 7 | zero bytes |
| 8 | unaligned bytes |
| 9 | zero samples |
| 10 | samples above 65534 |
| 11 | samples past bytes |
| 12 | bytes above 2 MiB |
| 13 | bytes above sample pool |
| 14 | rate 0 |
| 15 | rate above guard |
| 16 | square bytes |
| 17 | square samples |
| 18 | square rate |
| 19 | square sum |
| 20 | vag bytes |
| 21 | vag samples |
| 22 | vag rate |
| 23 | vag sum |
| 24 | clips differ |
| 25 | load square |
| 26 | square sound RAM |
| 27 | square readback |
| 28 | pool after square |
| 29 | volume 256 |
| 30 | pan -1 |
| 31 | loop 2 |
| 32 | play square |
| 33 | square status |
| 34 | square channel |
| 35 | square keyed |
| 36 | square position |
| 37 | stop square |
| 38 | square status after stop |
| 39 | square released |
| 40 | square channel freed |
| 41 | load vag |
| 42 | vag sound RAM |
| 43 | vag readback |
| 44 | pool with both |
| 45 | play vag |
| 46 | vag keyed |
| 47 | vag channel |
| 48 | vag position |
| 49 | stop vag |
| 50 | vag status after stop |
| 51 | vag released |
| 52 | vag channel freed |
| 53 | unload vag |
| 54 | pool after vag unload |
| 55 | unload square |
| 56 | pool restored |
| 57 | play unloaded |
| 58 | unload unloaded |
| 59 | null status |

Checks 35 and 46 wait up to 200 ms for `snd_is_playing` to read key-on. Checks 37 and 49 wait up to 200 ms for key-on to clear and only then return 0. After the channel is released, status reports `playing==0` and `channel==-1` and does not call `snd_is_playing` on a negative channel. The vag channel check accepts any channel in 0..63. `snd_sfx_chn_alloc` may hand the same channel back after `snd_sfx_chn_free`.

The square is freed last. `snd_mem_free` coalesces a block with the free neighbor after it (`snd_mem.c` 252–262), so freeing the higher vag block first joins it to the tail and the following square free restores one 1900544-byte block.

## verify_backend.py suite

There is no `sound` suite. `--suite` choices are the list at `verify_backend.py` 19. For every suite other than `arena-live`, `capture_contract` leaves a provided `--tests` alone and substitutes 17 when `--tests` is omitted (`verify_arena_live.py` 28–31). Omit `--tests` and a finished 59-check ELF fails the predicate. `--suite arena-live` also forces that suite's own test count and a dump. Use the command above.

`--suite codegen --tests 59` uses the real pass predicate. The scope sentence is the tests!=17 default at `verify_backend.py` 129–130, because `codegen` has no scope override. Ignore that sentence. The result boolean does not read it.

A matching suite is a two-site edit in `verify_backend.py`, left for the reviewer:

- Add `'sound'` to the `choices` tuple on line 19.
- Before the pass predicate, set `result['scope']` when `args.suite=='sound'` to a sentence that this ELF loaded two synthetic AICA ADPCM clips, observed key-on, and stopped them. Suggested text: `Native AICA ADPCM load, key-on and key-off of two synthetic clips; no bank, stream, or die measurement`.

`--timeout 60` is inside the 1..300 range checked at `verify_backend.py` 30. The default is 30. The ELF's own waits are `snd_init`'s 10 ms plus at most 200 ms of key-on and 200 ms of key-off on each clip.

`verify_backend.py` 99–101 also passes `audio:backend=null`. Whether that build of Flycast still runs the AICA ARM and updates KEYONB is unverified. A keyed-check failure (`square keyed` or `vag keyed`, cases 35 and 46) with a good readback is the symptom that flag would produce. This ELF does not select another backend.

## Loader and playback contract

`dc_aica_sfx_load` rejects sizes before it reads the caller's bytes. The oversize checks pass the real square pointer with a lying length. Order, shared with `classify_load`:

1. Null voice returns `DC_AICA_ERR_ARG` and does not reset.
2. An already-loaded voice returns `DC_AICA_ERR_STATE`.
3. Reset, then a null ADPCM pointer returns `DC_AICA_ERR_ARG`.
4. `byte_count == 0`, `byte_count > 2097152`, or `byte_count > 1900544` returns `DC_AICA_ERR_SIZE`.
5. `byte_count` not a multiple of 32 returns `DC_AICA_ERR_ALIGN`.
6. `sample_count == 0`, `sample_count > 65534`, or `sample_count > byte_count * 2` returns `DC_AICA_ERR_SAMPLES`. The multiply happens after the size cap, so it stays inside uint32.
7. `rate == 0` or `rate > 96000` returns `DC_AICA_ERR_RANGE`. 96000 itself is accepted.
8. `snd_init` failure returns `DC_AICA_ERR_INIT`.
9. `byte_count > snd_mem_available()` or `snd_mem_malloc` returning 0 returns `DC_AICA_ERR_NOMEM`.
10. An offset outside `[0x030000, 0x200000 - byte_count]` is freed and returns `DC_AICA_ERR_RANGE`.
11. `memalign` failure frees the sound block and returns `DC_AICA_ERR_NOMEM`.
12. The bytes are copied into that 32-byte-aligned buffer and `spu_memload_sq` writes them. Readback uses `spu_memread` into a second 32-byte-aligned buffer. A mismatch frees the sound block, resets the voice, and returns `DC_AICA_ERR_READBACK`.

`dc_aica_sfx_play` checks volume 0..255, pan 0..255, and looping 0 or 1 before it checks that the voice is loaded. A bad volume on an unloaded voice is `DC_AICA_ERR_RANGE`, matching `classify_play`. A good volume on an unloaded voice, or a second play while a channel is held, is `DC_AICA_ERR_STATE`. The channel comes from `snd_sfx_chn_alloc`. The start packet is the public `AICA_CMD_CHAN` / `AICA_CH_CMD_START` packet, with `type = AICA_SM_ADPCM`, `base` the sound-RAM offset, `length` and `loopend` the sample count, and `loopend` never 0.

Both probe plays pass `looping=1`. At 48000 Hz the square is about 171 ms and the vag clip is about 93 ms, so a one-shot can reach its loop end before the poll observes KEYONB. The loop is only for that window. Stop sends `AICA_CH_CMD_STOP` with the same placeholder fields `snd_sfx_stop` sends (`type` 0, `length` 0, `freq` 44100, `vol` 0, `pan` 0), waits for KEYONB to clear, then always calls `snd_sfx_chn_free` so a timed-out stop cannot leak the channel mask.

Error values are 0 or the negative of `DC_AICA_ERR_ARG` 1, `SIZE` 2, `ALIGN` 3, `SAMPLES` 4, `RANGE` 5, `INIT` 6, `NOMEM` 7, `READBACK` 8, `CHANNEL` 9, `STATE` 10.

## KallistiOS assumptions

jak-project does not vendor KallistiOS (`Makefile` 51). The line numbers below are from the sibling checkout `work/port-tools-validation/kos-source`, whose `include/kos/version.h` 100–102 says 2.2.1. A different `KOS_BASE` is unverified against this list.

Every call in `aica_sfx.c` and `sound_probe_main.c` was matched to a declaration in that tree or in the SH-4 newlib headers next to it. None was left without a declaration. The matched declarations are `int snd_init(void)`, `uint32 snd_mem_malloc(size_t)`, `void snd_mem_free(uint32)`, `uint32 snd_mem_available(void)`, `int snd_sh4_to_aica(void *, uint32)`, `uint16_t snd_get_pos(unsigned int)`, `bool snd_is_playing(unsigned int)`, `int snd_sfx_chn_alloc(void)`, `void snd_sfx_chn_free(int)`, `void spu_memload_sq(uintptr_t, void *, size_t)`, `void spu_memread(void *, uintptr_t, size_t)`, `uint64_t timer_ms_gettime64(void)`, `void thd_pass(void)`, `void thd_sleep(unsigned)`, `void *memalign(size_t, size_t)` and `void free(void *)` (`sh-elf/include/malloc.h` 69 and 45), plus `KOS_INIT_FLAGS` / `INIT_DEFAULT` and `_arch_mem_top`. `memset`, `memcpy`, `memcmp`, and `printf` are the C library declarations from `string.h` and `stdio.h`. Arguments that are `uint32_t` or a small `int` are passed where the header says `uint32`, `uintptr_t`, `size_t`, or `unsigned`; those are the usual conversions, and the declared parameter types are the ones above. `snd_sfx_stop` is not called. The file-scope command check compares both sides as `uint32_t`, because `AICA_CMDSTR_CHANNEL_SIZE` is a `size_t` expression and `AICA_CMD_MAX_SIZE` is a signed `256`.

Public declarations used by `aica_sfx.c`:

- `int snd_init(void)` (`include/dc/sound/sound.h` 100). Body at `sound/snd_iface.c` 37–62: if not yet initted, `spu_disable`, `spu_memset_sq(0, 0, AICA_RAM_START)`, load `snd_stream_drv` with `spu_memload_sq`, `spu_enable`, `timer_spin_sleep(10)`, `snd_mem_init(AICA_RAM_START)`. The second call leaves `initted` set and returns 0 without rebuilding the pool. The header documents no failure code. The body returns 0.
- `uint32 snd_mem_malloc(size_t size)` (`sound.h` 50). Body `sound/snd_mem.c` 138–205. Size rounds up to 32 (line 153). Returns the sound-RAM offset, or 0 on failure (lines 144–145 and 167). Metadata lives in a main-RAM `snd_block_t`, so the returned offsets are contiguous. The first split returns `best->addr` and inserts the remainder at `addr + size` (lines 190–205).
- `void snd_mem_free(uint32 addr)` (`sound.h` 58). Body `snd_mem.c` 210–263. Address 0 returns immediately (lines 215–216).
- `uint32 snd_mem_available(void)` (`sound.h` 71). The header describes the largest block `snd_mem_malloc` can still take. The body (`snd_mem.c` 266–284) returns the largest `e->size` and the loop at 278–280 does not skip `inuse` blocks. After these two small clips the free tail is still the largest block, so the probe's deltas are 4096 and then 2240. `snd_mem_init` (`snd_mem.c` 70–107) aligns `reserve` to 32 and sets one free block of `2 * 1024 * 1024 - reserve` (lines 81–97).
- `int snd_sh4_to_aica(void *packet, uint32 size)` (`sound.h` 116–119). `size` is in 32-bit increments. Header retval is 0 and "no error conditions defined". The body (`snd_iface.c` 74–117) writes that many dwords and returns 0. It asserts if `size >= AICA_CMD_MAX_SIZE` (line 78) and if the queue valid word is 0 (line 84).
- `uint16_t snd_get_pos(unsigned int ch)` (`sound.h` 229). Body `snd_iface.c` 215–216 reads the channel struct `pos` field in sound RAM and masks to 16 bits. The comment says the SPU firmware updates it.
- `bool snd_is_playing(unsigned int ch)` (`sound.h` 240). Body `snd_iface.c` 219–221 reads `MEM_AREA_P2_BASE + 0x00700000 + 0x80 * ch` and tests `AICA_CHANNEL_KEYONB`. `MEM_AREA_P2_BASE` is `0xa0000000` (`include/arch/memory.h` 88), so the address is `0xA0700000 + 0x80 * ch`.
- `int snd_sfx_chn_alloc(void)` and `void snd_sfx_chn_free(int chn)` (`include/dc/sound/sfxmgr.h` 257 and 266). Body `sound/snd_sfxmgr.c` 846–870. Alloc scans a `uint64_t` mask, initially 0 (line 49), for channels 0..63 and returns -1 when none are free. It does not require a prior `snd_sfx_load`.
- `void snd_sfx_stop(int chn)` (`sfxmgr.h` 240). Body `snd_sfxmgr.c` 816–833 sends the stop packet this file copies, including `freq = 44100`. It does not clear the alloc mask. This file calls `snd_sfx_chn_free` itself after the stop.
- `void spu_memload(uintptr_t to, void *from, size_t length)` (`include/dc/spu.h` 46), `void spu_memload_sq(...)` (line 60), `void spu_memread(void *to, uintptr_t from, size_t length)` (line 84). Offsets do not include `0xA0800000` (comments at lines 40–41, 54–55, 79–80). `length` on the PIO and SQ paths is rounded up to a multiple of 4 (`spu.h` 43–44 and 58; `hardware/spu.c` 45 and 76–78). `spu_memload_sq` below 32 bytes falls back to `spu_memload` (`spu.c` 70–72). `SPU_RAM_BASE` is `0x00800000` (`spu.h` 33). The SQ path ORs that base and then calls `sq_cpy` (`spu.c` 85–97).
- Store-queue copy: dest 32-byte aligned, src at least 4-byte aligned (8-byte uses the fast path), `n` a multiple of 32 (`include/dc/sq.h` 121–124). The copy buffer is `memalign(32, byte_count)`, the same allocator `disc_reader_kos.c` 34 uses. Accepted `byte_count` values are multiples of 32 and at least 32.
- Channel command: `AICA_CMDSTR_CHANNEL` declares a `uint32` array of `(sizeof(aica_cmd_t) + sizeof(aica_channel_t)) / 4` and points `aica_channel_t *` at `cmd_data` (`include/dc/sound/aica_comm.h` 92–95). `AICA_CMDSTR_CHANNEL_SIZE` is that same dword count (line 98). `aica_cmd_t.size` is "command data size in dwords" (line 52). `AICA_CMD_MAX_SIZE` is 256 (line 61). `AICA_CMD_CHAN` is 2 (line 106). `AICA_CH_CMD_START` is 1 and `AICA_CH_CMD_STOP` is 2 (lines 126–127). `AICA_SM_ADPCM` is 2 (line 158). `aica_channel_t` is 16 `uint32` fields: cmd, base, type, length, loop, loopstart, loopend, freq, vol, pan, pos, `pad[5]` (lines 68–81). Vol and pan comments say 0–255 (lines 77–78). This file includes `kos.h` before `aica_comm.h`, so the `uint32` fallback typedef at `aica_comm.h` 22–25 is not the one in force. A file-scope typedef fails the compile if the channel struct is not 16 `uint32`s or the command size is not below `AICA_CMD_MAX_SIZE`.
- `snd_sfx_play_ex` fills that same packet and calls `snd_sh4_to_aica(tmp, cmd->size)` (`snd_sfxmgr.c` 776–798). For `bitsize == 4` it sets `AICA_SM_ADPCM` and `effect->len = (len * 2) / channels` (lines 652–654), clamps a length of 65535 or more to 65534 (line 780), and uses `loopend ? loopend : size` (line 792). This file sets `loopend` to the sample count on every start so the 0 default is never sent. The warnings that a clip must be at most 65534 samples and a multiple of 32 bytes per channel are at `sfxmgr.h` 97–98 and 151–152. `load_raw_buf` only warns above 65534 (`snd_sfxmgr.c` 668–670) and then `aligned_alloc(32, read_len)` (line 680). This file therefore rejects a non-multiple of 32 before any KOS transfer, and it does not call `snd_sfx_load_raw_buf` or `snd_sfx_play`. The handle from those functions is the private `snd_effect_t` at `snd_sfxmgr.c` 32–41, which is not in a public header.
- ARM `process_chn` (`sound/arm/main.c` 69–83) on `AICA_CH_CMD_START` copies the channel struct, sets `pos = 0`, and calls `aica_play`. `AICA_CMD_CHAN` reaches it at lines 143–144. `aica_play` (`sound/arm/aica.c` 93–166) writes `loopstart` and `loopend` masked with `0xffff` (lines 126–127). The comment at line 86 says the count is samples. The `length` field is not stored to a channel register in this function. Looping sets bit `0x0200` (lines 157–158). A start without `AICA_CH_START_DELAY` writes `0xc000 | playCont` (line 164). `aica_stop` clears bit `0x4000` and sets bit `0x8000` (lines 182–184).
- `AICA_RAM_START` `0x030000`, `AICA_RAM_END` `0x200000`, `AICA_CHANNEL_KEYONB` `0x4000`, `AICA_CHANNEL_KEYONEX` `0x8000` are in `sound/arm/aica_cmd_iface.h` 37–38 and 44–45. That header is not public. `aica_sfx.h` repeats `0x030000` and `2097152 - 196608 = 1900544`. Usable sample RAM is `0x200000 - 0x030000`.
- `hardware_periph_init` calls `spu_init` (`hardware/hardware.c` 64–66). `spu_init` disables the ARM, `spu_memset_sq(0, 0, 0x200000)`, parks `0xeafffff8` at the start of sound RAM, enables the SPU, and sleeps 10 ms (`hardware/spu.c` 342–363). `INIT_DEFAULT` is IRQs, preemption, filesystems, libraries, and `INIT_DEFAULT_ARCH` (`include/kos/init.h` 143–144). `INIT_DEFAULT_ARCH` is maple plus cdrom (`include/arch/init_flags.h` 91). It does not call `snd_init`. The probe calls `dc_aica_sfx_init`, which calls `snd_init`, from `main`. `KOS_INIT_FLAGS(INIT_DEFAULT)` matches `main.c` 6 and `backend_main.c` 16.
- `uint64_t timer_ms_gettime64(void)` (`include/arch/timer.h` 276). `void thd_pass(void)` and `void thd_sleep(unsigned ms)` (`include/kos/thread.h` 484 and 499). The sleep argument is milliseconds (comment at 486–497).
- Main RAM size: `startup.S` 84–105 keeps the 16 MiB stack when `0xadffffff` mirrors `0xacffffff`, and stores that stack pointer as `__arch_mem_top`. The 16 MiB value is `0x8d000000` (`kernel/arch/dreamcast/kernel/stack.c` 20–23). `HW_MEMSIZE` is `_arch_mem_top - 0x8c000000` (`include/arch/arch.h` 195). Without `__KOS_GCC_32MB__`, `arch.h` 42 defines `_arch_mem_top` as `0x8d000000`. The probe uses the same subtraction as `backend_main.c` 69. `verify_backend.py` 99 passes `Dreamcast.RamMod32MB=no` and requires 16777216.

Port documents used the same way: sound RAM is 2 MiB and separate from main RAM and VRAM (`README.md` 242–243, `SOUND_PLAN.md` 93 and 122, `MEMORY_PLAN.md` 1–7). `spu_init` clears it before `main` (`MEMORY_PLAN.md` 32–33, `SOUND_PLAN.md` 93). G2 access is 32-bit with FIFO waits, and mixing stays on the AICA side (`MEMORY_PLAN.md` 69–75, `SOUND_PLAN.md` 93).

## Unverified

- Flycast with `audio:backend=null` (`verify_backend.py` 101) still executing the AICA ARM program and updating KEYONB. No other backend was selected.
- 200 ms being long enough for the ARM to consume the queue. `snd_init` waits 10 ms (`snd_iface.c` 54) and `snd_sh4_to_aica` returns without waiting for the tail (`snd_iface.c` 115–117). The 1000000-iteration `thd_pass` backstop was not timed.
- The AICA die's nibble order, the 254/256 history filter, and the 32767 magnitude cap. `wav2adpcm.c` 8–9 says YMZ280B and AICA differ by a nibble swap. `adpcm2pcm` still takes the low nibble first (`wav2adpcm.c` 93–101) and applies `history * 254 / 256` (`wav2adpcm.c` 102). `ymz_step` caps the difference at 32767 before the sign (`wav2adpcm.c` 72–84). `pcm2adpcm` writes the high nibble first (`wav2adpcm.c` 122–123). `vag_adpcm.py` copies the low-nibble `ymz_step` behavior and leaves the 254/256 filter out. `SOUND_PLAN.md` 99 and 243 say the step table and key-on reset were not measured on the die. This probe compares sound-RAM bytes with the buffer it wrote and reads KEYONB. It does not compare the waveform.
- A click-free loop. `SOUND_PLAN.md` 116 and 244 leave the loop method unchosen. This probe loops only so the status poll can see KEYONB, then stops.
- `AICA_RAM_START` staying `0x030000` on the reviewer's KOS. The constant is in the private ARM header cited above. A different reserve fails the pool and offset checks.
- `DC_AICA_MAX_RATE` 96000. That guard is local. It is not a limit in the KOS headers read here. The ARM converts `freq` with the loop at `aica.c` 110–118 (`freq_base` starts at 5644800). The resulting pitch at 48000 Hz was not measured.
- 64 channels on the die. KOS allocates `0..63` (`snd_sfxmgr.c` 851, `aica.c` 81). `SOUND_PLAN.md` 243 says the die count was not measured.
- `snd_sh4_to_aica` returning a failure. The header defines none. The body returns 0 and asserts on a full packet or an invalid queue (`snd_iface.c` 78, 84, 117). An assert ends the ELF before state 2 or 3, which the verifier reports as a timeout.
- An unaligned `sq_cpy` source faulting. The code follows `sq.h` 121–124 and `snd_sfxmgr.c` 680 and always copies from a 32-byte-aligned buffer. The fault itself was not re-derived.
- `g2_read_block_32` destination alignment. `spu_memread` casts the destination to `uint32_t *` (`spu.c` 171). No alignment comment was found on that function. The readback buffer is 32-byte aligned.
- `spu_memload` rounding a non-multiple of 4 (`spu.c` 45). Every transfer this file issues is a multiple of 32, so the round-up adds no bytes on these calls.
- `snd_mem_available` once an in-use block is larger than every free block. The implementation can then report a size `snd_mem_malloc` will refuse (`snd_mem.c` 278–280 against the header at `sound.h` 60–69). The probe's clips stay smaller than the free tail.
- Retail `.sbk` / `.vag` bytes matching these fixtures. `SOUND_PLAN.md` 239 says those files are absent from this tree. The probe generates both clips.
- Any KOS revision other than the sibling 2.2.1 tree cited above.

`snd_effect_t` was read (`snd_sfxmgr.c` 32–41) so the public command path could be used instead of that private object.
