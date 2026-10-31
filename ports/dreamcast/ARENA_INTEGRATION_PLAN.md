(AI-assisted)
# Running the GOAL loop and the renderer in one process

Two ELFs exist today. `jak-dc-kernel-dgo.elf` (`Makefile` 814–815) loads KERNEL.CGO and GAME.CGO and runs original `display-loop` for 60 headless frames (`kernel_boot_cases.c` 347–378, `GAME_CGO.md` 268–277). `jak-dc-flythrough.elf` (`Makefile` 677–678) draws village1 and never links the GOAL runtime. This plan puts both in one new ELF. The headless ELF stays as it is.

## Where the 16 MiB goes

Stock Flycast RAM is 16 MiB (`MEMORY_PLAN.md` 3–7). Cached RAM is `0x8c000000` upward; both mains record `_arch_mem_top - 0x8c000000` (`runtime_main.c` 51, `flythrough_main.c` 75). The flythrough measures its static image as `&end - 0x8c010000` (`flythrough_main.c` 211–214), so the KOS link origin used there is `0x8c010000`. `jak-dc-kernel-dgo.elf` does not move the text origin; it only pins `.dc_stage` at `0x8cd80000` (`Makefile` 806, 815), which is 13.5 MiB into RAM and leaves a 2.5 MiB hole up to `0x8d000000`.

| Region | Where | Size | Who writes it |
| --- | --- | --- | --- |
| Low reserve | `0x8c000000`–`0x8c010000` | 64 KiB | Inferred from the flythrough formula. Not re-measured on the DGO ELF. |
| Static image | `0x8c010000` .. linker `end` | See below | KOS, GOAL C, mips2c `.a`, zstd |
| Boot romdisk | `.dc_stage` at `0x8cd80000`, then VRAM | Documented 1.5 MB, cap 2 MiB | `romdisk_stage.c` |
| GOAL arena | one `memalign` block | 13 MiB today | bump allocator |
| KOS malloc heap | from linker `end` toward `_arch_mem_top` (`flythrough_main.c` 216) | the arena block plus load peak | newlib |
| Renderer heap | newlib `mallinfo` on the flythrough ELF | 2.55 MB peak live | `malloc` |
| GOAL stacks | inside the arena | 16 KiB, 32 KiB, 32 KiB, 512 B | originals |
| KOS main stack | top of RAM | size not in this tree | KOS |

`dc_kernel_arena_bytes` returns `13 * 1024 * 1024` (`romdisk_stage.c` 31–33). `main` calls `dc_stage_romdisk` before that `memalign(32, capacity+32)` (`runtime_main.c` 44–49). The arena is a bump from that block (`goal_runtime.c` 46–58), addressed as cached P0 with the MMU off (`goal_runtime_dc.c` 7–19). A weak fallback of 12 MiB exists only when `romdisk_stage.c` is not linked (`runtime_main.c` 16). `KERNEL_BOOT.md` 62 still says the DGO boot uses 8 MiB; the source above is the current size.

The 13 MiB block holds 10.7 MB of engine data and leaves about 2.8 MiB, which the notes equate to 2.94 MB (`LEVEL_STREAMING.md` 74–75, 630). 13 MiB is 13,631,488 bytes; 10.7 MB decimal is 10,700,000; the difference is 2,931,488 bytes (2.80 MiB). Treat 10.7 as decimal MB. The historical ELF row of 0.44 MB + 0.56 MB (`MEMORY_PLAN.md` 13) is from before mips2c and counts a 400 KB damage buffer that is now `malloc(400000)` (`kernel_boot_cases.c` 401–403). Current `&end` on `jak-dc-kernel-dgo.elf` was not found in the tree. Mips2c is why the arena is 13 MiB rather than 13.5 (`MEMORY_PLAN.md` 36–38, `MIPS2C.md` 214–215). 14 MiB starves the KOS heap (`MEMORY_PLAN.md` 37).

Load-time KOS malloc is on top of the arena block and is freed before the frame loop returns: a symbol snapshot of `DC_GOAL_SYMBOL_BYTES` (16,384 × 16 = 262,144) per module (`goal_layout.h` 6–7, `goal_module.c` 2, 185–186, 264), the DCSH file buffer (`goal_native_file.c` 106–110), and the zstd context, closed with the FILE (`goal_dgo_zst.c` 28–37, `kernel_boot_cases.c` 202). `MEMORY_PLAN.md` 14 calls this peak about 1 MB. That sum was not re-measured here.

GOAL stacks inside the arena: the spawn test allocates 16,384 bytes (`kernel_boot_cases.c` 300, 319); display is activated at `*fake-scratchpad-stack* + 32 KiB` (360); `*dram-stack*` is registered for 32 KiB (310–313); the main-thread backup floor is 512 (`KERNEL_BOOT.md` 35–37, `dreamcast_thread_context.gc` 8). The KOS main stack size is not in this tree. Fibers require a working stack of at least 4,096 (`goal_kos_stack.c` 22).

The flythrough, alone, on the cell-streamed village1 path: peak live heap 2.55 MB and heap arena 2.55 MB (`LEVEL_STREAMING.md` 675–684), against that 2.94 MB. An earlier indexed-window sample peaked at 2.90 MB with the arena at 2.91 MB (`LEVEL_STREAMING.md` 629–631, `README.md` 8–16). Both notes say this is the renderer beside the arena figure, not both in one process. The 2.55 MB row is the one this plan reserves. Sampling misses file-open and startup transients (`LEVEL_STREAMING.md` 635–637). The older beach-path image was 4.28 MB including a 3.9 MB romdisk (`LEVEL_STREAMING.md` 467–475).

## What collides

**KOS malloc and the GOAL arena.** The 2.94 MB is the unused tail of the bump (`rt->used` versus `rt->capacity`), not free KOS heap. The flythrough’s blocks are `malloc` / `memalign` (`flythrough_main.c` 92–103, `instance_cells.c` 16, `render_base.c` 34, `render_block_cache.c` 17–19, `render_draw_window.c` 11–12). A 13 MiB `memalign` plus a 2.55 MB renderer heap does not fit: 14 MiB already starves KOS (`MEMORY_PLAN.md` 37). `dc_goal_alloc` never frees a single block (`goal_runtime.c` 46–58), so the renderer cannot move onto the bump without an allocator rewrite. Shim: `memalign` a 2.5 MiB renderer slab (2,621,440 bytes) before the GOAL `memalign`, and compile only the render objects with `-Dmalloc= -Dcalloc= -Dmemalign= -Dfree=` aimed at that slab. The same idea is already how the render tests replace allocators (`Makefile` 506, `render_base.c` 7–15, `render_gpu.c` 5–18). `goal_module.c` keeps libc `malloc`. 2.5 MiB is 2.62 MB decimal, above the 2.55 MB peak. New GOAL capacity is 11,010,048 bytes. Against 10,700,000 used, the pad is 310,048 bytes. The damage-test `malloc(400000)` (`kernel_boot_cases.c` 402) and a live 2.55 MB slab do not both fit in the 2,931,488-byte slack (about 390 KB remains after 2.55 MB, `LEVEL_STREAMING.md` 682). The combined ELF skips that damage malloc; `jak-dc-kernel-dgo.elf` still runs it.

**PVR init and the VRAM map.** Today’s DGO VRAM is the whole 8 MiB: framebuffer comment below 1 MiB, extension `0xa5100000` for 5 MiB, romdisk `0xa5600000` for 2 MiB (`romdisk_stage.c` 16–19, 31–33, `kernel_boot_cases.c` 196–199). `pvr_init` has not run (`romdisk_stage.c` 9). The flythrough calls `vid_set_mode(DM_640x480, PM_RGB565)` and `pvr_init` with a 2 MiB vertex buffer, three `PVR_BINSIZE_32` lists, and `opb_overflow_count` 8 (`flythrough_main.c` 73–76). `texture_budget.py` 25–28 and 59–76 models that as 7,511,296 bytes reserved and 877,312 left for textures. The two DMA buffers are 1,712 KiB each (`MEMORY_BUDGET.md` 45, check at `kernel_boot_cases.c` 246–248): 3,506,176 bytes. 877,312 does not hold them. Village1 textures want 717,312 of that 877,312 (`LEVEL_STREAMING.md` 686–688). KOS’s exact buffer addresses are not in this repo; the size model is. `VRAM_POOL.md` 11–18 and 35: `pvr_init` resets the VRAM allocator, and `0xA4000000` / `0xA5000000` are two views of one 8 MiB. A later `pvr_init` invalidates a `pvr_mem_malloc` done earlier.

Shim, combined ELF only: call `pvr_init` before any GOAL use of VRAM, then take the extension and the boot romdisk from `pvr_mem_malloc`, and copy `.dc_stage` into that loan before either large `memalign`. The heap is what reuses the main-RAM romdisk pages after the copy (`MEMORY_PLAN.md` 32–36, `runtime_main.c` 44–49). `clip_scene_main.c` 19 already uses a 256 KiB vertex buffer. With the flythrough’s three lists, `opb_overflow_count` 1, and a 256 KiB vertex buffer, the same formula leaves 6,160,128 bytes. That holds 3,506,176 of DMA buffers plus a 2 MiB romdisk cap (5,603,328) with 556,800 bytes spare during load. After `fs_romdisk_unmount`, the romdisk block returns to the texture pool. The published 2 MiB / overflow-8 setup cannot share VRAM with those DMA buffers; do not change the flythrough ELF’s parameters. Pass/fail uses `pvr_mem_available()` and `romdisk_size`, not this model. Bytes in the extension are still zeroed with 32-bit stores (`goal_runtime.c` 84–98, `MEMORY_PLAN.md` 44–45).

**Vblank and threads.** Both ELFs use `KOS_INIT_FLAGS(INIT_DEFAULT)` (`runtime_main.c` 19, `flythrough_main.c` 40). Neither main calls `thd_create`. The flythrough waits in `pvr_wait_ready` (`flythrough_main.c` 158) and then `thd_sleep` (226). GOAL’s `syncv` is `pc_zero_native` and returns 0 (`goal_bridge.c` 1235–1244, `GAME_CGO.md` 288–301). A fiber runs with IRQs unmasked and the FPU enabled: `dc_goal_kos_stack_sr_ok` requires MD=1, BL/RB/FD=0, IMASK=0 (`goal_kos_stack.c` 6–8), and that SR is installed on enter (`goal_fiber.c` 219–222, `goal_kos_stack.h` 14–17). The swap image is 60 bytes (R8–R14, PR, SP, FPUL, FPSCR, FR12–FR15) and does not include XMTRX (`native_context_swap.S` 3–9). `dc_goal_kos_stack_enter` rewrites the KOS thread’s stack record (`goal_kos_stack.c` 18–28). The header forbids OS migration for the duration of that borrow (`goal_kos_stack.h` 14–17), and `pvr_wait_ready` blocks. Shim: `syncv` and `__send-gfx-dma-chain` only store a flag and the chain pointer and still return 0. After `dispatch()` returns, the fiber pointer is clear and the KOS stack is restored (`goal_fiber.c` 222–224, `dc_goal_fiber_current` at 153). The hook there does `pvr_wait_ready` and the empty scene. No second thread.

**Romdisk size.** The boot image is documented at 1.5 MB (`MEMORY_PLAN.md` 34) and refused above 2 MiB (`romdisk_stage.c` 19–24). The flythrough romdisk is 3.9 MB inside a 4.28 MB image (`LEVEL_STREAMING.md` 472–475). 3.9 MB does not fit the 2 MiB VRAM window or the 2.5 MiB hole at `0x8cd80000`, and linking it beside a 10.7 MB arena does not fit in 16 MiB. Milestone 1 does not link it. After boot, unmount `/rd` and mount level data from the disc. That mount is not in the tree.

**FPU mode.** Fibers reject FPSCR bits `0x00380000` (`goal_fiber.c` 16, 62–71), the same PR/SZ/FR mask the render gate rejects, along with SR.FD (`render_world_view_gate.S` 6–24, `BASE_RENDERING.md` 43–45). The swap clears PR/SZ/FR before reloading the image (`native_context_swap.S` 40–55). Mips2c describes the SH-4 as flushing denormals (`MIPS2C.md` 202–206). The indexed terrain path is scalar float, not XMTRX (`render_draw_window.c` 47–65). The flythrough links that scalar world view (`Makefile` 657–660, `render_world_view.c` 18–20).

**XMTRX.** The instance draw loads XMTRX with `mat_load` and does not save the previous matrix (`instance_pass.c` 121–150, 168–175). The fiber image does not contain XMTRX (`native_context_swap.S` 3–9). GOAL’s current vf lowering does not use XMTRX; that is a planned speed pass (`VU0_LOWERING.md` 96–101). Draw only from the post-`dispatch` hook, while `dc_goal_fiber_current()` is null (`goal_fiber.c` 153). When a later pass does use XMTRX, that pass saves and restores it around the hook. No change to `instance_pass.c` until then.

**GS and DMA.** `syncv`, `sync-path`, and `dma-sync` return 0. `reset-graph`, `reset-path`, `put-display-env`, `flush-cache`, `__pc-set-levels`, and `__send-gfx-dma-chain` do nothing (`goal_bridge.c` 1235–1244). The chain pointer is the documented plug-in (`GAME_CGO.md` 299–301). The renderer reads its own catalog and sectors; it does not parse GIF/VIF packets. Leave the two 1,712 KiB buffers in the extension so those allocations still succeed (`goal_bridge.c` 186–218). Milestone 1 stores the chain pointer and does not walk it.

## Milestone 1

One new ELF. GOAL’s existing 60-frame checks still pass, and a clear PVR frame is submitted on the KOS stack between two of those frames. No village romdisk, no change to `render_base.c`, `render_block_cache.c`, `render_draw_window.c`, `instance_pass.c`, or `instance_cells.c`.

Pass when all of these hold, in `dc_backend` the way the other ELFs report:

- `completed == 60`, `rt.error == DC_GOAL_OK`, and `*display*`’s real-integral counter advances by at least 60 (`kernel_boot_cases.c` 374–378).
- `pvr_init` returns 0. After it, and before the loans, `pvr_mem_available()` is at least `romdisk_size + 3506176`. The extension pointer and the romdisk pointer came from `pvr_mem_malloc`, and neither range intersects the GOAL arena.
- The two DMA objects still land in the extension (`kernel_boot_cases.c` 246–248).
- The 2.5 MiB slab is outside `[rt->base, rt->base + rt->capacity)`, `rt->used` is unchanged by filling the slab, and `rt->used <= 10700000 + 310048`.
- One `pvr_scene_begin` / clear / `pvr_scene_finish` runs from the post-dispatch hook, with `dc_goal_fiber_current()` null before and after (`goal_fiber.c` 153).
- `dc_backend.state == 2` and `system_ram` is still 16 MiB.

Fail on the first miss. Do not free the GOAL arena before these checks (`runtime_main.c` 80 frees it today after the boot returns).

## Files

- `Makefile`: new target. Link the current `jak-dc-kernel-dgo.elf` objects plus `arena_coexist.c`. Do not link `flythrough_main.o` or `build/flythrough-romdisk.o`.
- `arena_coexist.c` (new): `pvr_init` parameters, `pvr_mem_malloc` of the extension and the romdisk, `dc_stage_romdisk` onto that pointer, then the slab `memalign`, the post-dispatch hook, and the pass/fail checks. `dc_renderer_reserve` returns 2621440 here and 0 when this file is absent.
- `romdisk_stage.c`: stage into a pointer argument. The headless path keeps `0xa5600000`. `dc_kernel_arena_bytes` returns `13 MiB - dc_renderer_reserve()`.
- `kernel_boot_cases.c`: under the new target, register the extension at the `pvr_mem_malloc` result instead of `0xa5100000` (197–198), and call the hook after each `dispatch()` (367–369). Skip the damage `malloc` on this target only (399–418).
- `goal_bridge.c`: point `syncv` and `__send-gfx-dma-chain` at two stash functions. They return 0, as now.

## Order

1. **PVR, loans, then the romdisk copy.** `pvr_init` with a 256 KiB vertex buffer, three 32-word lists, overflow 1. Check `pvr_mem_available()` against `romdisk_size + 3506176`. `pvr_mem_malloc` the extension and the romdisk. Copy `.dc_stage` and mount `/rd` (`romdisk_stage.c` 21–27) before either large `memalign`. Check the loans are disjoint and 16-byte aligned.
2. **Slab, then arena.** `memalign` 2,621,440 bytes, then the GOAL arena at 11,010,048. Check: both pointers non-NULL, slab outside the arena, `dc_goal_init_native` accepts the arena (`goal_runtime_dc.c` 15–18). Headless ELF unchanged (`reserve == 0`), and it still stages before its own `memalign` (`runtime_main.c` 44–49).
3. **Boot.** Run the existing DGO load. Check the current GAME.CGO checks, including two DMA buffers in the extension.
4. **Frames, with a clear between them.** Stash `syncv` / chain. After each `dispatch()`, on the KOS stack, `pvr_wait_ready` and one empty scene. Check the 60-frame conditions and that `dc_goal_fiber_current()` is null across the scene.
5. **Heap probe.** Fill the slab, compare `rt->used` to the pre-fill value. Check the numeric bounds in the milestone list.
6. **Village, later, same shims.** Link the existing render objects with the malloc redirects. Unmount the boot romdisk after load and mount level data from the disc. Keep the flythrough’s 2 MiB vertex buffer on `jak-dc-flythrough.elf` until a combined run shows `pvr_mem_available()` still fits textures (717,312 for village1) after the DMA loan. Do not parse the stashed DMA chain in that step either.

## Not confirmed

- Current `end`, mips2c image size, and KOS main-stack and IRQ-stack sizes on `jak-dc-kernel-dgo.elf`.
- KOS’s physical placement of framebuffers, OPBs, and the vertex buffer. The 7,511,296 / 877,312 split is the Python model of `pvr_allocate_buffers`, checked against the flythrough source text (`texture_budget.py` 99–105), not a dump of PVR registers.
- Actual `romdisk_size` of `build/kernel-dgo-romdisk.img`. The plan treats 1.5 MB as the documented size and 2 MiB as the hard cap.
- Whether `display-loop` opens `/rd` during the 60 frames. Unmount is specified after load and before the village mount, and milestone 1 keeps the boot mount for the whole 60 frames.
- `pvr_init`’s own main-RAM malloc. If that malloc reaches `.dc_stage` before the copy, the mount check fails. The slab probe is only the explicit 2.5 MiB.
- That overflow 1 and a 256 KiB vertex buffer survive village1’s triangle rate. Milestone 1 submits no geometry, so it does not show that. The flythrough ELF’s parameters stay the measured ones.
