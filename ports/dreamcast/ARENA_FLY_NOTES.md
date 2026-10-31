(AI-assisted)

# jak-dc-arena-fly.elf

**Built and run (2026-10-31).** On beach's noon fixture with a 6 m path: 1,986 of 1,986 checks, 64 frames, 1,173 of 1,173 fly checks, no PVR errors, slab high water 1,247,712 of 2,621,440 bytes, no slab failures, GOAL arena 10,673,360 of 11,010,048. Two limits below were changed to get there, and supersede this note's figures: for this ELF alone `.dc_stage` links at 0x8cb00000 (`_end` is 0x8c16d558, and the image is copied out before the heap grows), and the vertex buffer is 512 KiB, so the image ceiling is 1,700,000 bytes. The note below predates the run. `jak-dc-arena.elf` and `jak-dc-kernel-dgo.elf` are still the objects in `Makefile` 814–828. `jak-dc-flythrough.elf` is still `Makefile` 663–678. The non-fly branch of `arena_coexist.c` is the `#else` at 148–158, 184–187, and 204–206.

## What changed

`verify_backend.py` 94–96 starts Flycast with `UseReios=yes` and the ELF path. It does not mount a CDI, so `/cd/fly` is empty and `jak-dc-arena-fly.cdi` cannot satisfy `--tests`. The CDI rule is gone. The level image is a second romdisk.

`Makefile` 860–869 builds `build/arena-fly-romdisk.img` with the same `genromfs -x '*.json'` form as the flythrough romdisk (`Makefile` 671–673), runs `bin2c` with the symbol prefix `flydisk`, and puts `flydisk_data` in `.dc_fly`. That object is compiled without `-lromdiskbase`, so it does not auto-mount over `/rd`. The boot object `build/kernel-dgo-romdisk.o` still links `-lromdiskbase` (`Makefile` 806–808). `.dc_fly` is linked at the next 32-byte boundary after the boot image:

`0x8cd80000 + align32(size of build/kernel-dgo-romdisk.img)`

The address is computed in Python because `0x8cd80000` does not fit in a signed 32-bit shell `$(( ))`. The recipe writes it to `build/arena-fly-section.txt` and the link line reads it (`Makefile` 863, 869). The build stops if the fly image is empty or larger than 1,081,024 bytes, or if the aligned boot image plus the aligned fly image passes `0x8d000000`. A Python read of the first 4096 bytes of the generated `.c` stops the build if the `section(".dc_fly")` edit did not land (`Makefile` 866). That keeps the image out of `.rodata`, where it would move `end`.

`arena_coexist.c` 26–45, 115–147, under `DC_ARENA_FLY` only: after `pvr_init`, loan `EXTENSION_BYTES` (3,506,240), then loan the fly image from whatever `pvr_mem_available()` still reports, copy it with 32-bit stores, and only then `memalign` the slab. The boot romdisk is copied into that slab and mounted at `/rd` (`romdisk_stage.c` 22–30 via `dc_stage_romdisk_at`). The copy does not read past `flydisk_size` (`arena_coexist.c` 34–44). The boot copy in `romdisk_stage.c` 25–29 is unchanged and can still read one tail word; the next section is 32-byte aligned, so that tail stays in the gap.

On the first post-`dispatch` frame, `dc_arena_fly_release_boot` (`arena_coexist.c` 70–76) unmounts `/rd`, reinitializes the slab, and mounts the VRAM image at `/rd`. It returns 0 or 1. `*available` is `pvr_mem_available()` after that mount and can be 0. `arena_fly.c` 77 does not treat 0 as failure.

`arena_slab.c` 70–73: if `fs_open` fails and `errno` is already 0, set `errno` to `ENOENT`. `instance_cells.c` 73–76 treats `ENOENT` as an empty cell and any other errno as a hard open error.

`arena_coexist.c` 134–138: the level image must start at or after the boot image and end by `0x8d000000`, and its bytes must be disjoint from the current KOS stack. The stack size is not in this tree (`ARENA_INTEGRATION_PLAN.md` 19).

`kernel_boot_cases.c`, `goal_bridge.c`, `romdisk_stage.c`, and `arena_slab.h` were not edited. `DC_ARENA_FLY` already calls `dc_arena_fly_report` at `kernel_boot_cases.c` 458–459. That call is compiled only into `arena_fly_kernel_cases.o` (`Makefile` 854–855).

## Audit

### Disc path could not be verified

The previous arena-fly link rule produced `jak-dc-arena-fly.cdi` and the asset echo told the reviewer to stage files for `/cd/fly`. `verify_backend.py` 19 accepts `--suite flythrough`, and 147 passes only when `state==2`, `total==--tests`, `passed==--tests`, and `system_ram==16777216`. Suite choice only changes the `scope` string (`verify_backend.py` 143). There is no arena-fly suite. Launching the ELF is the whole test. The finished target is `jak-dc-arena-fly.elf`.

### Renderer allocations

CPU `malloc`, `calloc`, `memalign`, and `free`, and renderer `FILE`s, are redirected only in the objects built with `-DDC_ARENA_SLAB_REDIRECT -include arena_slab.h` (`Makefile` 836, 842–847): `arena_fly.c`, `instance_cells.c`, `render_base.c`, `render_block_cache.c`, `render_indexed.c`, `render_draw_window.c`, `render_gpu.c`, `render_cache.c`, `render_package.c`, `sector_inflate_stored.c`.

Those TUs route through `arena_slab.h` 19–32. Test allocators stay out of this ELF: `DC_RENDER_BASE_TEST_ALLOC`, `DC_RENDER_BLOCK_CACHE_TEST`, `DC_RENDER_GPU_TEST_ALLOC`, `DC_RENDER_CACHE_TEST_IO`, and `DC_RENDER_TEST_IO` are not defined. With the test macros off, those files call `malloc` / `free` (`render_base.c` 24, 34), `ALLOC` / `OPEN` (`render_block_cache.c` 17–20), `ALLOC` (`render_draw_window.c` 12–13), `CACHE_MALLOC` / `CACHE_FOPEN` (`render_cache.c` 40–43), `GPU_CALLOC` (`render_gpu.c` 15–16), and `RENDER_READ` (`render_package.c` 21–23). The redirect renames those identifiers. Opens use `"rb"` (`arena_fly.c` 81 and 102, `instance_cells.c` 73, `render_block_cache.c` 120 and 169, `render_cache.c` 379). `dc_arena_fopen` rejects any other mode (`arena_slab.c` 69).

`instance_pass.c`, `render_world_view.c` (linked as `render_world_view_scalar.o`), `render_world_view_gate`, and `render_frustum.c` are on the link line (`Makefile` 868) and are not redirected. They do not call `malloc`, `calloc`, `memalign`, `free`, or `fopen`. `render_indexed.c` and `sector_inflate_stored.c` are redirected and do not allocate; the latter defines `crc32` (`sector_inflate_stored.c` 15). The link uses `-lm` and not `-lz` (`Makefile` 869).

The slab rejects alignment above 32 (`arena_slab.c` 17). Production renderer `memalign` asks for 32 (`render_base.c` 34, `arena_fly.c` 101, 108, 109). Each live block is a 32-byte header plus a payload rounded up to 32 (`arena_slab.c` 6–7, 19). `dc_arena_slab_failures` counts refused requests (`arena_slab.c` 32, 37, 57).

These do not come from the slab:

- Level bytes and the DMA extension are `pvr_mem_malloc` (`arena_coexist.c` 123, 128).
- Textures are `pvr_mem_malloc` (`render_gpu.c` 17, 45). Format 2 is not uploaded (`render_gpu.c` 36, 44). `arena_fly.c` 88–92 charges `align32(bytes+4)` for every other texture, plus 4096, against `pvr_mem_available()` after the romdisk loan.
- `pvr_init`, the KOS allocator, and GOAL `malloc` are other translation units. The redirect is per object. `goal_module.c` stays on libc, as in `ARENA_INTEGRATION_PLAN.md` 33.

`render_gpu.h` 4 and `arena_fly.c` 2 include `<kos.h>` while the redirect macros are active. External KOS functions still link to libc. A static inline in those headers that called `malloc` or `fopen` would hit the slab. That was not checked against the KallistiOS headers, which are not in this tree.

### VRAM budget

`texture_budget.py` 59–76 is the model, with this ELF's parameters (`arena_coexist.c` 101–106, `arena_slab.h` 7): 640x480, height aligned to 32, three bins of 32, 0, 32, 0, 32 words, `opb_overflow_count` 1, vertex buffer 1,048,576. Two buffer sets, each region aligned to 128.

| Piece | Bytes |
| --- | ---: |
| Tiles | 300 |
| OPB, one set | 230,400 |
| Tile matrix, one set | 7,272 |
| Framebuffer, one set | 614,400 |
| Vertex buffer, one set | 1,048,576 |
| One set, after the 128-byte aligns | 1,900,672 |
| Both sets, reserved | 3,801,344 |
| 8 MiB minus reserved | 4,587,264 |
| DMA loan `EXTENSION_BYTES` (`arena_coexist.c` 21–23) | 3,506,240 |
| Modeled remainder for the level romdisk, its chunk overhead, and textures | 1,081,024 |

`EXTENSION_BYTES` is the 3,506,176-byte DMA payload plus the 64-byte type pad. The non-fly ELF still loans that same extension and still uses a 256 KiB vertex buffer (`arena_coexist.c` 101–102, 148–158). The flythrough ELF stays at a 2 MiB vertex buffer and overflow 8 (`texture_budget.py` 99–105). Do not change those.

Pass and fail use `pvr_mem_available()`, not the table (`arena_coexist.c` 119–127, `arena_fly.c` 92). The 1,081,024 figure is also the Makefile ceiling on the romdisk file alone (`Makefile` 857–863). Textures and the 4096-byte headroom have to fit in what remains after the loan. A romdisk of 1,081,024 bytes leaves nothing for them. Village1 at the 64-pixel cap is 717,312 bytes of textures (`LEVEL_STREAMING.md` 686–688) before chunk rounding. A full flythrough romdisk is about 3.9 MB (`ARENA_INTEGRATION_PLAN.md` 41) and cannot be linked: it is over 1,081,024 and over the 2,621,440-byte hole at `0x8cd80000`.

The hole is `0x8d000000 - 0x8cd80000` = 2,621,440. The boot image is documented at 1.5 MB and refused above 2 MiB (`ARENA_INTEGRATION_PLAN.md` 14, `arena_coexist.c` 114). A boot image of 1,572,864 bytes leaves 1,048,576 bytes in the hole, which is tighter than the 1,081,024 VRAM ceiling. The Makefile applies both limits to the real file sizes.

### GOAL checks

`ARENA_INTEGRATION_PLAN.md` 51–55 is the source for today's results: `jak-dc-arena.elf` 870 of 870, `jak-dc-kernel-dgo.elf` 532 of 532. This session did not re-run either. The damage block is four `CHECK`s (`kernel_boot_cases.c` 436, 440, 445, 449) and is compiled only for `DC_KERNEL_DGO` without `DC_ARENA_COEXIST` (`kernel_boot_cases.c` 428). Both arena ELFs skip it. 532 − 4 = 528 GOAL checks. The fly object uses the same `kernel_boot_cases.c` checks; the only added lines are the report call at 458–459.

Non-fly coexistence is 342 checks: stage 10, prepare 4, dispatch 5×64, finish 8. 528 + 342 = 870.

The fly ELF still runs those 528 GOAL checks on the success path. `dc_arena_prepare` runs once before load (`kernel_boot_cases.c` 121). `dc_arena_finish` runs once after the frames (`kernel_boot_cases.c` 453). A failed fly or coexist check returns 0, and the caller does `goto done` (`kernel_boot_cases.c` 113, 359, 395, 419, 453). Later GOAL checks are then not reached. 1986 is the success path only. If `dc_arena_stage` returns 0, `runtime_main.c` 56–57 adds one more failed check, id 1001, and returns before the GOAL cases. The success path does not add that check. `runtime_main.c` 81 sets `state` to 2 when `failed_case` is still 0.

### Frame loop

Each draw is after `dispatch()` returns (`kernel_boot_cases.c` 356–360, 391–396, 417–420). That is 3 spawn frames (`kernel_boot_cases.c` 321), 60 `display-loop` frames (`kernel_boot_cases.c` 376), and one frame after the spawned process dies (`kernel_boot_cases.c` 417). `dc_arena_dispatch` requires `on_root()` before the draw and again after it (`arena_coexist.c` 89–93, 180, 189): `dc_goal_fiber_current()` is null and the KOS stack record is the one saved at stage. `syncv` and `__send-gfx-dma-chain` only stash and return 0 (`goal_bridge.c` 1270–1272, `arena_coexist.c` 95–97). The chain is not walked.

The fly path calls `dc_arena_fly_frame` instead of the empty scene (`arena_coexist.c` 181–183) and then checks `rt->used` is unchanged. Terrain goes through `dc_render_block_cache_window` and `dc_render_draw_window_prepare` (`arena_fly.c` 129–134). Instances go through `dc_instance_cells_update` and `dc_instance_draw_list` (`arena_fly.c` 127–128, 140). The translucent list is terrain only (`arena_fly.c` 137–141), matching `flythrough_main.c` 187–190. The window path reads `.drx` and has no `.drs` fallback (`render_block_cache.h` 36–38). `--draw-files` is required.

`flythrough_main.c` 205–219 submits the last camera again because the display is one scene behind. This ELF does not. Each frame does `pvr_wait_render_done` (`arena_fly.c` 150), and finish waits again (`arena_coexist.c` 199–200). A `--capture` framebuffer can still be the previous scene. That was not checked.

The first of the 64 draws calls `initialize` (`arena_fly.c` 126). That unmounts the boot romdisk before the 60 `display-loop` frames. `ARENA_INTEGRATION_PLAN.md` 93 says it is unknown whether `display-loop` opens `/rd`. The damage tests that reopen `/rd/KERNEL.CGO` are the four checks compiled out above. GAME.CGO is closed at `kernel_boot_cases.c` 223, before the spawn loop.

## Expected counts

`--tests` compares `dc_backend.total` (`verify_backend.py` 147). `dc_arena_fly_metrics.checks` counts only `arena_fly.c`'s `CHECK` (`arena_fly.c` 47–52). `dc_arena_fly_report` prints that pair, not 1986 (`arena_fly.c` 171–175). The string replaces the `BOOT GOAL=` line (`kernel_boot_cases.c` 455–459). For 1173/1173, 64 frames, a full slab, and a full arena the text is 65 characters plus NUL, inside the 80-byte description.

Success path: **1986** `dc_backend` checks, of which **1173** are fly metrics.

| Group | Checks | Where |
| --- | ---: | --- |
| GOAL, damage block omitted | 528 | `kernel_boot_cases.c`, same as the 870 ELF |
| Stage | 15 | `arena_coexist.c` 108, 110, 113, 114, 122, 124, 126, 129, 130, 134, 137, 139, 143, 144, 147 |
| Prepare | 5 | `arena_coexist.c` 163–168 |
| Boot romdisk released, level mounted | 2 | `arena_coexist.c` 71, 73. Not in `metrics.checks` |
| Dispatch wrapper | 256 | 4 × 64 at `arena_coexist.c` 179, 180, 183, 189 |
| Finish wrapper | 7 | `arena_coexist.c` 196–201, 208. The non-fly slab fill is not in this ELF |
| Fly initialize | 17 | `arena_fly.c` 82, 84, 91, 92, 93, 96, 98, 100, 101, 102, 104, 105, 106, 107, 108, 109, 116 |
| Fly frame | 1152 | 18 × 64 at `arena_fly.c` 128, 129, 131, 132, 133, 134, 135, 144, 145, 146, 147, 148, 149, 150, 151, 153, 154, 155 |
| Fly finish | 4 | `arena_fly.c` 165–168 |

528 + 15 + 5 + 2 + 256 + 7 + 1173 = 1986. 17 + 1152 + 4 = 1173. The 16-frame path is drawn four times (`arena_fly.c` 120, 165). `arena_fly.c` 20 refuses any other `FLY_FRAMES`.

`dc_arena_fly_metrics` is 20 little-endian `uint32_t` values, 80 bytes, in this order (`arena_fly.c` 26–30): `frames`, `checks`, `passed`, `last_frame`, `slab_high_water`, `slab_live`, `slab_failures`, `vram_after_boot`, `vram_after_textures`, `texture_bytes`, `boot_bytes`, `dma_bytes`, `max_faces`, `max_vertices`, `max_instances`, `max_triangles`, `max_vertex_bytes`, `pvr_errors`, `cache_peak_bytes`, `cache_evictions`. `vram_after_boot` is `pvr_mem_available()` after the level romdisk is mounted and before textures (`arena_coexist.c` 75, `arena_fly.c` 77). `dma_bytes` is 3,506,240 (`arena_fly.c` 76). `checks` and `passed` are the 1173, not 1986.

## Reviewer steps

From `ports/dreamcast`, after KallistiOS `environ.sh`. `Makefile` 3 defaults `PYTHON` to `python3`. The recipe needs a shell with `sed -i` and `cat`, which the existing boot-romdisk rule already uses.

Generate a 16-frame fixture. `--frames` must be 16. `--draw-files` writes `.drx` and appends `FLY_WINDOW_FACES`, `FLY_WINDOW_VERTICES`, `FLY_WINDOW_REMAP`, and `FLY_CACHE_BUDGET` (`make_flythrough_fixture.py` 133–144, 159–168). `--instance-cells` defines `FLY_INSTANCE_RECORDS` (`make_flythrough_fixture.py` 112–113). The header path for this ELF is `build/arena_fly_fixture.h` (`Makefile` 838–839). The query initializer is `x0,z0,x1,z1` (`make_flythrough_fixture.py` 104–108, `render_cache.h` 57–58).

```text
python3 make_flythrough_fixture.py BACKGROUND.glb PACKAGE.dci2 build/arena-fly-disc/fly --frames 16 --draw-files --instance-cells --header build/arena_fly_fixture.h
```

`make_flythrough_fixture.py` has no texture-size switch. It calls `prepare_render_sectors.py` at that file's 64-pixel default. `tfrag_glb.cut` keeps terrain inside the path rectangle (`make_flythrough_fixture.py` 65–72). A short `--length` and a small `--half` are the way to shrink the image. `--frames` stays 16. The package must not be DCI4: this ELF calls `dc_instance_cells_make` (`arena_fly.c` 107), not `dc_instance_cells_palette_make`. Flythrough wires palette lighting only under `FLY_PALETTE_LIGHTING` (`flythrough_main.c` 111–113).

`build/kernel-dgo-romdisk.o` has to exist; the fly romdisk rule depends on it (`Makefile` 860). Then:

```text
make jak-dc-arena-fly.elf
```

If the recipe prints a size or `.dc_fly` error, the image does not fit the hole or the 1,081,024-byte ceiling. Shrink the fixture. Do not build a CDI and pass it to `verify_backend.py`.

```text
python3 verify_backend.py FLYCAST jak-dc-arena-fly.elf --nm NM --output OUT --suite flythrough --tests 1986 --timeout 300
```

`--timeout` defaults to 30 and the maximum is 300 (`verify_backend.py` 20–25). Optional dump:

```text
python3 verify_backend.py FLYCAST jak-dc-arena-fly.elf --nm NM --output OUT --suite flythrough --tests 1986 --timeout 300 --dump dc_arena_fly_metrics 80
```

`ARENA_INTEGRATION_PLAN.md` 55–56 says the milestone 1 frontend needs `-Wl,--stack,16777216` or it exits `0xC00000FD`. This ELF was not run, so that host link is unverified here. Use the same frontend that already runs `jak-dc-arena.elf`.

## Assumptions

- `bin2c`'s third argument `flydisk` emits `const unsigned char flydisk_data[` and a size symbol compatible with `extern const int flydisk_size` (`arena_coexist.c` 30–31), the same way `romdisk` works for `Makefile` 805–806. The 4096-byte scan assumes that declaration is near the start of the generated `.c`.
- `pvr_mem_malloc` returns a 16-byte-aligned pointer in `[0xa4000000, 0xa4800000)` so `loan` (`arena_coexist.c` 85–88) accepts it, and `fs_romdisk_mount(..., 0)` can use that pointer the way `romdisk_stage.c` 30 already does for the boot image. The third argument 0 means the mount does not take ownership of the buffer.
- `pvr_list_begin`, `pvr_list_finish`, `pvr_get_stats`, and `pvr_wait_render_done` return 0 on success (`arena_fly.c` 138–151). Milestone 1 already requires `pvr_init`, `pvr_wait_ready`, and `pvr_scene_finish` to return 0 (`arena_coexist.c` 113, 199). Those three are part of the 870 result. The list and stats calls are not.
- The ASIC names at `arena_fly.c` 72–73 exist in the reviewer's KallistiOS. They are not declared anywhere in this repo. A wrong name fails the compile. The handler sets `failed_case` to `12000+code` and `state` to 3 without incrementing `dc_backend.total` (`arena_fly.c` 60–68). The frame check on `pvr_errors` is what counts in `total`.
- `fs_seek` whence values match newlib `SEEK_SET`, `SEEK_CUR`, and `SEEK_END` (`arena_slab.c` 95–97).
- A failed `fs_open` that leaves `errno` at 0 is a missing file (`arena_slab.c` 73). `instance_cells.c` 73 sets `errno` to 0 before the open. A nonzero errno is kept, so a real `EIO` stays a hard error.
- `display-loop` does not open `/rd` after the first frame has swapped the mount.
- The KOS heap grows up from `end` and did not reach `.dc_stage` during `pvr_init` on the 870 ELF (`ARENA_INTEGRATION_PLAN.md` 94). `.dc_fly` is placed after that image, so the same growth would have to pass `.dc_stage` first. The copy still happens after `pvr_init` (`arena_coexist.c` 113, 141).
- The main stack might occupy the top of the hole. Check 137 fails the run instead of copying a stack-corrupted image. The image is placed as low in the hole as the boot romdisk allows. A failure there means this boot image plus this fixture do not fit under the stack.
- The 1,048,576-byte vertex buffer holds the fixture (`arena_fly.c` 152–153). Overflow is 1, not the flythrough's 8.
- Cell-streamed village1's published 2.55 MB live heap (`LEVEL_STREAMING.md` 675–678) is a newlib figure. The slab adds a 32-byte header and rounds payloads to 32. 2,621,440 − 2,550,000 is 71,440 if 2.55 MB is decimal. That margin may not cover the headers. Finish requires the slab high-water to be nonzero and at most 2,621,440 (`arena_fly.c` 166). A smaller level from that table (beach, village2) has a lower published peak.
- `dc_render_base_frame_make_draw` does not allocate, so the frame loop does not leak a window every frame. The package, view, and bounds blocks stay allocated through finish (`flythrough_main.c` 246 is the same idea).
- Drawing stays outside the fiber, so the missing XMTRX in the fiber image (`ARENA_INTEGRATION_PLAN.md` 45) is acceptable. `on_root` checks the fiber pointer. It does not check FPSCR.

## Unverified

- The compile, the link address, `bin2c` spelling, and every check count above. No command was run.
- Actual `pvr_mem_available()` after this `pvr_init`, against 4,587,264. KallistiOS's buffer placement is not in the repo (`ARENA_INTEGRATION_PLAN.md` 91).
- Actual `build/kernel-dgo-romdisk.img` size, and whether a real fixture's image plus rounded textures plus 4096 fit in the remainder.
- `dc_arena_slab_high_water` for any real level. `dc_arena_slab_failures==0` does not show that KOS or PVR avoided libc.
- `vtx_buffer_used_max` for any real frame under a 1,048,576-byte buffer and overflow 1.
- Whether `display-loop` touches `/rd`, and whether the first unmount is late enough for every boot `fclose`.
- FPU mode after the fiber returns. The gate in `render_world_view_gate` rejects SR.FD and FPSCR PR/SZ/FR (`ARENA_INTEGRATION_PLAN.md` 43).
- `--capture` output. This ELF does not resubmit the last scene.
- The host frontend stack flag on this ELF.
- Current `end`, the mips2c image size, and the KOS main-stack size (`ARENA_INTEGRATION_PLAN.md` 19, 90). The runtime stack check is the guard this tree can express.
- VRAM romdisk byte reads under sector traffic. Milestone 1 already serves the boot romdisk from a `pvr_mem_malloc` loan. This image is larger and is read as many `.drx` and `.dci` files.
