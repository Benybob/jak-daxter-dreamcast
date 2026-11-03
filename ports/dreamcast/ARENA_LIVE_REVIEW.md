(AI-assisted)

# Arena live review

Read-only review of the Dreamcast arena-live target. Sources read: `arena_live.c`, `arena_live.h`, `arena_live_math.c`, `arena_live_math.h`, `arena_live_input.c`, `arena_live_assets.c`, `arena_live_assets.h`, `arena_live_render.c`, the `DC_ARENA_LIVE` regions of `goal_pad.c`, `goal_bridge.c`, `arena_coexist.c` and `kernel_boot_cases.c`, and `ARENA_LIVE_NOTES.md`. The run is `build/arena-live-run.txt` and `build/arena-live-review/result.json`. No source file other than this review was edited. The host tests, `sh-elf-gcc`, and Flycast were not re-run. `build/arena-live-review/dump.bin` is the 13312-byte evidence image the capture script writes on a terminal state. This session could not decode that binary, so the pose rows and `time_ratio_bits` below are unread.

The run does not prove that original code carried a recorded pad through 61 beach frames. It does show that the saved `update-camera` export, or a callee it reached, replaced the poisoned lanes on the updates that got as far as the travel gate, and that update 26's anchor then left the box the gate allows.

## The run

`jak-dc-arena-live.elf`, Flycast, state 3. Total 1420, passed 1419, failed case 11420, RAM 16777216, code bytes 6483216. ELF SHA-256 `602c5d65c05cab1811933861156ed6e4c7a260f5946f6426e1dddfd284fd99e1`. Observed 0, expected 1. Description `arena live original camera within staged terrain coverage and travel cap`. `verify.lua` captures `frame.ppm` only when state is 2, and the run file records that no `frame.ppm` was produced.

`arena_coexist.c` 60–68 stores the first failing coexist check as `10000 + total`. 10000 + 1420 = 11420. The description is the string at `arena_live_render.c` 148–149. `dc_live_coverage` returned 0.

`ARENA_LIVE_NOTES.md` 5–7 and 263–306 still say no ELF was linked and no Flycast run was performed, and they ask for 2598/2598. This log is that run, and it stops at 1420. The notes' 2598 arithmetic remains an unmeasured success total.

## Which update failed

The coverage check runs only after `dc_arena_live_sample` returns 1 (`arena_live_render.c` 136–149). A schedule, pad, poison, affine, initial-match, or motion-window failure returns −1 and dies on the earlier string at line 137. Reaching line 149 means update N had already passed those gates, including `delta == 2` when N is in 20..26 (`arena_live.c` 281–287).

Check ledger, success-path counts, source order:

| Block | Checks |
| --- | ---: |
| `dc_arena_stage` under the fly loan (`arena_coexist.c` 119–158) | 15 |
| `dc_arena_prepare` (`arena_coexist.c` 173–180) | 5 |
| GOAL checks before the spawn loop, the same 483 that make the known 528 with the 45 later GOAL checks | 483 |
| Spawn setup (`kernel_boot_cases.c` 335–362) | 16 |
| `dc_arena_live_begin` (`arena_live.c` 130–164) | 9 |
| Three spawn dispatches: 4 GOAL checks plus 10 coexist/live/warmup checks each | 42 |
| `dc_arena_live_initialize` (`arena_live.c` 170–195) | 6 |
| Display setup before the loop (`kernel_boot_cases.c` 385–412) | 4 |
| Display-init dispatch, warmup, no `update-camera` | 10 |
| Prefix before original update 1 | 590 |

Update 1 then pays 52 checks: the steady 32 (3 witness requires, 3 root requires, 4 dispatch wrappers, 1 sample check, 20 draw checks, 1 draw acknowledgment) plus 2 romdisk mounts, 17 `initialize` checks and 1 ledger check. Each later successful update pays 32. The failing update pays 10 checks through the coverage line (3 witness, 3 root, error, root stack, sample, coverage).

590 + 52 + 24×32 + 10 = 1420. That is 25 completed updates and a failure on update 26. One missed constant block of 32 checks would move this by one update. The schedule require (`arena_live.c` 89–90) ties the same index down from the other side: a second `update-camera` inside one dispatch fails that require, and this run got past it 26 times.

Update 26 is the 10th output that can see the strafe. `dc_live_input_advance` runs at the end of a witnessed update (`arena_live.c` 121). `display-frame-start` services pads after that, on the way to `suspend` (`drawable.gc` 1083, `main.gc` 539–570). The slave's `cam-free-floating-move` runs on that suspend. The next `update-camera` is the one that copies the new pose. Stage 1 is completed updates 16..27 (`arena_live_input.c` 9–10), so the stick is visible on updates 17..28. The 10th of those is update 26. Sample had already required `delta == 2` for it: translation changed, basis lanes within 0.00001 of update 25 (`arena_live_math.c` 111–116).

## Findings

Severity: high means this run, or a later one with the same gate, can be read as a verdict on the original camera when the gate is measuring something else; medium means a named check does not observe what its name says; low means a latent path this log did not reach.

### 1. High — update 26 leaves the ±6 m box during the recorded strafe

`arena_live_render.c` 143–149 builds the query from `c.target` and calls `dc_live_coverage`. The travel terms are `arena_live_assets.c` 54: `|target.x - initial.x| <= 6`, the same for Z, and `|target.y - initial.y| <= 1`. The initial point is `fly_frames[0].target` (−281.193, 31.276, −280.459). The crop around that point has about 32 m of room inside `live_crop` once the 14 m half-width is inset, so a camera that has only walked a few meters fails the 6 m test while the crop test still holds.

Concrete case, the recording this log was playing. Dreamcast `joyx` +64 becomes hw-cpad left X 192 (`goal_pad.c` 75, `arena_live_input.c` 19). `analog-input` of (192, 128, 48, 110, −1) is −16/62 (`pad.gc` 199–213). `cam-free-floating-input` adds `2 * *CAM_FREE-bank* speed * stick * time-adjust-ratio` on local X (`cam-states-dbg.gc` 242–247). Speed is 1600 (`cam-states-dbg.gc` 68). One frame at ratio 1 moves the eye 825.8 GOAL units, 0.2016 m, along local X. The anchor is `eye + forward * 55` (`arena_live_math.c` 33–34), so a pure strafe moves the anchor by the same vector. It does not swing the 55 m boom. At yaw 2.2 the right-axis Z factor is `sin(2.2)` ≈ 0.8085, so Z changes by 0.1630 m per unscaled frame.

`set-time-ratios` clamps the slowdown at 4 (`display.gc` 40). `display-frame-start` sets that ratio from the wall-clock EE timer with an integer division that already adds one frame (`drawable.gc` 978–997), and the PC-port clamp back to 1 applies only while the frame is under 1.3/60 s. `dc_arena_syncv` returns immediately (`arena_coexist.c` 106). The timer window still contains the C frame, and `dc_arena_fly_frame` waits on `pvr_wait_ready` twice and `pvr_wait_render_done` once (`arena_live_render.c` 163–177). A frame that spends three vblanks is about 50 ms, the integer formula yields 4, and the clamp keeps 4.

At ratio 4 the same strafe moves Z by 0.652 m a frame. Nine strafe outputs stay at 5.87 m. The tenth, update 26, reaches 6.52 m and this check returns 0. X over the same ten frames is about 4.75 m, inside the box. The ±1 m Y term stays quiet: the reconstructed right axis is `(cos yaw, 0, sin yaw)` (`arena_live_math.c` 20), so this stick has no vertical component, and `delta == 2` already forbids a pitch change on this update.

`ordinary_mode` accepts any positive finite ratio whose sign bit is clear (`arena_live.c` 72–73). The witness can therefore accept the original move at the lag cap and the travel gate can reject that same move. The notes size the cap as if the ratio stays near 1 (`ARENA_LIVE_NOTES.md` 157–159, 318–321). The original multiply is in `cam-free-floating-input` (`cam-states-dbg.gc` 247–248).

The stored ratio is word 18 of the evidence block (`arena_live.h` 16–19, `arena_live.c` 248). It was not decoded here. If that word is 1.0 or 2.0, ten strafe frames stay under 6 m (2.4 m and 3.9 m on the long axis) and this explanation is wrong. The frame index still says the gate fired on a translation-only strafe output.

### 2. Medium — the query equalities in the coverage check cannot fail

`arena_live_render.c` 146–147 writes `query` as `target.x ± half` and `target.z ± half`. `dc_live_coverage` then requires those four edges to match `target ± half` within 1e-6 (`arena_live_assets.c` 52–53). Both sides are the same expression. For any finite target and any finite positive `half`, the difference is 0.

Concrete case: a camera at (0, 0, 0) and a camera at (1e6, 0, 1e6) both pass those four comparisons. So does a camera 100 m outside `live_crop`. The comparisons do not observe a query the terrain system computed on its own. The crop tests and the travel tests in the same return are real, and finding 1 is one of those. The independent view test is `dc_live_terrain_matches`, which had already passed on updates 1..25 and does not run on the failing update (`arena_live_render.c` 162, after the coverage return).

### 3. Medium — the submitted-camera check compares a value to the convert that just produced it

`dc_arena_live_sample` fills `c` with `dc_live_convert(&pose, 55, &c)` (`arena_live.c` 272–274). `dc_arena_fly_frame` passes that same `c` to `dc_arena_live_drawn` (`arena_live_render.c` 188). `dc_live_camera_matches` converts the saved pose again at `c->distance` and compares (`arena_live_math.c` 103–109, called from `arena_live.c` 305).

Concrete case: take the pose update 25 stored and run it through `dc_live_convert` twice. Both yaw, pitch, eye and target match, including when the yaw formula is rotated 90 degrees from the original basis, because both sides share the formula. The check fails only if something edits `c` after the sample. `dc_arena_fly_frame` copies `c` into the render and instance cameras and does not write `c` back. Updates 1..25 passed this check. That pass repeats the convert. It does not add a second observation of the GOAL memory. The terrain and instance checks on those same updates do (`arena_live_render.c` 150 and 162).

### 4. Low — the yaw recording is unmeasured, and the button path is easy to misread

`arena_live_input.c` 20 sets `DC_PAD_B` for stage 3. `dc_goal_pad_fill` maps that bit to PS2 circle (`goal_pad.c` 61). Circle subtracts `*CAM_FREE-bank* rot-speed` from the yaw component, with the pressure lane at 0 (`cam-states-dbg.gc` 101–108). `rot-speed` is `(degrees 0.6)`, 109.2 of the 65536-per-turn units (`types-h.gc` 43–48, `cam-states-dbg.gc` 68). The turning window is updates 43..44 (`arena_live.c` 287). The run stopped at update 26, still in stage 1. Nothing in this log shows that circle reaches `cam-free-floating-input`, or that the resulting basis stays inside the roll-free test in `dc_live_convert`.

## What is clean

The pad hook is the hardware sample. `dc_goal_pad_read_state` is the only `DC_ARENA_LIVE` branch in `goal_pad.c` (lines 33–38). `dc_goal_cpad_get_data`, the DualShock state machine, and original `service-cpads` still run. `dc_arena_live_pad` compares validity, status, the button halfword, four stick bytes and twelve pressure bytes with the recording (`arena_live.c` 211–224). A stage number alone does not pass. Extra hardware reads in one service do not advance `completed_updates` (`arena_live_input.c` 15–24). Reaching update 26 means each of those updates saw a new selected-pad service whose bytes matched the stage for `seq - 1` (`arena_live.c` 92–95).

The store witness can fail. All 36 lanes are poisoned with a distinct quiet NaN immediately before the saved export is called, and `dc_live_read` accepts the camera only when every lane is finite and affine (`arena_live.c` 97–107, `arena_live_math.c` 60–68 and 53–58). C does not write a finite pose back on failure. The shim calls the loader pointer saved in `dc_arena_live_begin` (`arena_live.c` 132–134, 99). Entry requires the display fiber, refuses recursion, and requires `*dproc*` (`arena_live.c` 79–86). This is the witness the notes describe: the saved export or a callee replaced those words during that call. It is not a program-counter trace. Twenty-six calls passed it. A script that wrote the math camera from C outside that call would be overwritten by the poison or would miss the post-call read.

Frame 1 additionally had to match the one-time event. `sample` requires `dc_live_initial_matches` before it will return 1 for sequence 1 (`arena_live.c` 275–277). Later updates do not get past that line unless sequence 1 did. The event argument is the 32-byte transform `teleport-to-transformq` reads: translation quad, then quaternion at byte 16 (`cam-master.gc` 543–550, `arena_live_input.c` 25–36, `arena_live.c` 184–190). For `fly_yaw` 2.2, `fly_pitch` −0.45 and `fly_distance` 55, that translation is the eye which looks at `fly_frames[0].target`. The quaternion matrix in `dc_live_initial_matches` (lines 45–49) matches the basis `dc_live_convert` rebuilds (lines 18–23) for this yaw and pitch, including at yaw π/2 where a transposed formula would point the forward axis the other way. Original `update-camera` stores `inv-camera-rot` vector 3 as `trans` and `camera-rot` vector 3 as the rotated negative eye (`cam-update.gc` 315–365), which is the contract at `arena_live_math.c` 15–30.

Units on the frames that drew are the GOAL meter. `trans / 4096` with no axis swap is `arena_live_math.c` 33–34. Updates 1..25 also passed `dc_live_terrain_matches` (`arena_live_render.c` 162): every prepared vertex matched `0.25 * dot(basis row, world − eye)` and at least one vertex was off the view axis (`arena_live_math.c` 70–88). A 4096-scale miss, a swapped axis, or a 55 m boom aimed the wrong way fails that test on update 1, or fails the travel gate on update 1. Update 1 completed. The 6 m miss shows up ten strafe frames later, which is a small step repeated, not a unit explosion.

The level guard matches `all-types.gc`. `level-group` is `#x1ef4` bytes, `level0` inline at 96, `level1` at 2704, each level `#xa30` (2608) bytes, status at retail 20 (`all-types.gc` 5676–5774). The GOAL-pointer offsets used at `arena_live.c` 68–71 are those numbers minus 4, and the inline tag sits four bytes before the level pointer. `update-camera` reads the previous eye when a level is non-inactive (`cam-update.gc` 240–245). The guard runs before the poison. Both slots stayed on that path for 26 calls. `num-slaves` at GOAL offset 112 and the slave ppointer at 116 (`all-types.gc` 15539–15540, retail 116 and 120) match `arena_live.c` 174 and 253–255. Initialize's demand for one resumed free-floating slave passed, because the display loop ran.

`span` in `arena_live.c` 30–34 and `arena_live_math.c` 39–43 refuses a null runtime or arena, `used > capacity`, a wrapping `base + capacity`, an address below `base`, and a range that is not entirely inside the allocated prefix. The math reader also refuses a runtime fault and a camera whose tag, type tag, or declared size is wrong (`arena_live_math.c` 45–51). `DC_GAME_CAMERA_BYTES` is 1060 including the basic tag (`arena_game.h` 6–7). Rotation 364, inverse 428 and trans 844 stay inside that allocation. Poison and the post-call copy use those offsets only.

The bridge guards stay on this target. `goal_bridge.c` includes `arena_live.h` and calls `dc_arena_live_resume` / `dc_arena_live_pad` only under `DC_ARENA_LIVE` (lines 16–20, 759–764, 1127–1128). The `#line` directives at 22–24, 775–777 and 1135–1137 put the old site numbers back for every other mode. `kernel_boot_cases.c` 363–364 and 378–379 call begin and the one-time initialize only in this mode. The 65-dispatch schedule held for the 26 updates this log completed.

## What this review could not confirm

`dump.bin` was not decoded. `time_ratio_bits`, the 36 floats of the verified first output, and the 26 trace rows are the measurement that would confirm or drop finding 1's ratio. The control flow implies `rendered == 25`, `affine_reads == 26`, `moving_pairs == 7` (sample counts update 26 before the gate returns), and `drawn == 0` on row 26. Those words were not read back.

The SH-4 bytes of `update-camera`, `cam-free-floating-move`, `service-cpads` and `send-event-function` were not disassembled. The witness does not name which callee stored the lanes. The translation-only window plus the pad-byte match is the link to the recording.

The circle/yaw stage, the neutral windows at updates 32..38 and 48..60, the post-deactivation 61st update, the finish counters, and `frame.ppm` did not run. Original FOV, GPU matrix registers, framebuffer appearance, and a ratio measured on this Flycast log are unconfirmed. Jak, level collision, and follow cameras remain outside this target, as the notes say.
