(AI-assisted)

# Arena live review 2

Read-only review of the Dreamcast arena-live target after the 181-update recording. Sources read: `arena_live.c`, `arena_live.h`, `arena_live_math.c`, `arena_live_math.h`, `arena_live_input.c`, `arena_live_assets.c`, `arena_live_assets.h`, `arena_live_render.c`, the `DC_ARENA_LIVE` regions of `goal_pad.c`, `goal_pad.h`, `goal_bridge.c`, `arena_coexist.c` and `kernel_boot_cases.c`, `ARENA_LIVE_REVIEW.md`, `ARENA_LIVE_NOTES.md`, `build/arena-live-run.txt` and `build/arena-live-fix-review/result.json`. The original yaw and ratio code checked against those files is `cam-states-dbg.gc`, `cam-combiner.gc`, `pad.gc`, `display.gc` and `display-h.gc`. No source file other than this review was edited. Host tests, `sh-elf-gcc` and Flycast were not re-run. `build/arena-live-fix-review/dump.bin` is a binary this session could not decode, so this run's pose rows and `time_ratio_bits` are unread.

The run shows that original `update-camera` replaced the poisoned lanes on 43 calls, that 42 of those poses were drawn, and that output 43 was rejected by the readback sample before coverage, the later strafe legs, the finish counters or `frame.ppm`.

## The run

`build/arena-live-run.txt` and `build/arena-live-fix-review/result.json` are the same failure. State 3. Total 2090, passed 2088, failed case 12089, RAM 16777216, code bytes 6483216. ELF SHA-256 `8e756dcbb462c16f844f2fcc990bd73218b7cd7342321aab04962edf35949bc2`. Observed 0, expected 1. The stored description is `live readback rejects stale output, reset pose, stalled processes or wrong inpu`. That is the 80-character backend field cutting the string at `arena_live.c` 304. `verify.lua` captures `frame.ppm` only when state is 2 (`verify.lua` 27–28). State 3 produced no `frame.ppm`.

`require` stores its reason in the evidence block and then checks the boolean against 1 (`arena_live.c` 24–26). Expected 1 with observed 0 is that check. The description selects which `require` it was. `arena_coexist.c` 60–68 keeps the first failure as `10000 + total`. 10000 + 2089 = 12089, so the readback `require` is check 2089. `arena_live_render.c` 136–137 then fails `status >= 0` as check 2090 and does not replace the description. Passed stays 2088.

`ARENA_LIVE_NOTES.md` 5–12 still calls the latest run total 1964, passed 1962, failed case 11963, and says no ELF was linked and no Flycast run was performed after the edit. This log is a later image. The notes' 6982 figure remains an unmeasured success total. The notes' circle-plus-L2 recording (lines 29–46 and the table at line 93) is also behind the source that is on disk now: `arena_live_input.c` 34 sets `DC_PAD_RX_POS`, and `goal_pad.c` 78–79 turns that bit into hw-cpad right X 207.

The earlier review's image was ELF `602c5d65c05cab1811933861156ed6e4c7a260f5946f6426e1dddfd284fd99e1`, total 1420, failed case 11420, the coverage string. Reported code size is the same 6483216 bytes. The hash is not.

## Which update failed

`dc_arena_live_sample` returns −1 through `arena_live.c` 304. `dc_live_coverage` (`arena_live_render.c` 148) runs only after that function returns 1. Reaching the coverage string would have replaced this description. Output 43 is the first sequence whose sample requires `delta == 4` and a raw-basis yaw step in 0.009..0.043 rad (`arena_live.c` 288–293). Sequences 4..42 that sit in a neutral or moving window had already passed `delta == 1` or `delta == 2`.

Check ledger, success-path counts, current source order. The prefix before update 1 is the earlier review's 590. The display-loop length is now 181 (`kernel_boot_cases.c` 393), which adds frames inside the loop and does not add checks before update 1. The cross-check at the bottom is what pins the index.

| Block | Checks |
| --- | ---: |
| Prefix before original update 1, from the earlier review | 590 |
| Update 1: steady 35, plus 2 romdisk mounts, 17 `initialize` checks and 1 ledger check | 55 |
| Each later successful update | 35 |
| Failing update through the sample `require` | 8 passed, then the failing check |

Steady 35 is 3 witness requires (`arena_live.c` 90, 93, 106), 3 root requires (`arena_live.c` 234, 241, 243), 2 dispatch checks before the draw (`arena_coexist.c` 195–196), the sample status check (`arena_live_render.c` 137), 22 draw checks (`arena_live_render.c` 148–190) and 2 draw acknowledgements (`arena_live.c` 308 and 313). The earlier review's steady 32 had 20 draw checks and 1 acknowledgement. The three additions are the cache-query match (`arena_live_render.c` 159), the post-submit instance match (`arena_live_render.c` 190) and the second acknowledgement. Update 1 inserts the mounts, `initialize` and the ledger between the pre-submit instance match and the cell update (`arena_live_render.c` 150–154). `initialize` is the 17 `CHECK`s at `arena_live_render.c` 88–122. The two mounts are `dc_arena_fly_release_boot` (`arena_coexist.c` 82–85).

The failing dispatch pays the 3 witness requires, the 3 root requires and the 2 pre-draw dispatch checks, then the sample `require`. Those 8 passed. The status check is the extra failing check that makes total 2090.

590 + 55 + 41×35 + 8 = 2088. That is 42 completed updates and a failure on update 43. One missed block of 35 would move the index by one update.

The notes' 1964 run failed the same description at check 1963 (failed case 11963). 2089 − 1963 = 126 = 42×3. Those are the three new success-path checks on updates 1..42. The failing sample did not move. The input edit, whichever bytes were in this ELF, still dies on the first required turn.

`dc_live_input_advance` runs at the end of a witnessed update (`arena_live.c` 122). The pad service in that same iteration reads `update_returns`, which is already that sequence (`arena_live.c` 218). The slave consumes that pad on the following suspend, and the next `update-camera` is the output that shows it. Completed 42 is the first stage-3 sample with a non-zero control (`arena_live_input.c` 12 and 34). It is visible on output 43. Output 43 is also the first sequence the turn window inspects. There is no earlier turn sample in this log.

The witness require at `arena_live.c` 106–108 passed for sequence 43. At the return from the saved export, `dc_live_read` had already accepted a finite, roll-free basis (`arena_live_math.c` 17–23 and 53–58). This rejection is the sample that runs later. The same string also covers a re-read mismatch (`arena_live.c` 270–275), the turn window (`arena_live.c` 288–293) and a stalled master, slave or combiner resume count (`arena_live.c` 297–300). The unread trace row is what separates those three.

## Earlier findings

Severity follows the earlier review. High means this run, or a later one with the same gate, can be read as a verdict on the original camera when the gate is measuring something else. Medium means a named check does not observe what its name says, or a contract hole can reject a legal original step. Low means a latent path this log did not need.

### 1. Partly fixed — the recorded +64 strafe no longer reaches the ±6 m box on update 26

The stick is now +52 and −52 (`arena_live_input.c` 32–33). The comment at lines 26–28 says +64 at ratio 4 moved about 0.806 m per update and left the cap on update 26. The cap itself is unchanged: `|target − initial| <= 6` on X and Z and `<= 1` on Y (`arena_live_assets.c` 52).

This log did not die there. Updates 20..26 require `delta == 2` (`arena_live.c` 287) and those samples returned 1, because the failure string is the readback string and the coverage check never ran. Translation-only strafe still happens under the new stick.

`ordinary_mode` still accepts any positive finite ratio whose sign bit is clear (`arena_live.c` 73–74). `dc_arena_syncv` still returns immediately (`arena_coexist.c` 106). A ratio above the ratio the notes modeled can still reject a legal original strafe on a later moving window. The notes' host prediction, maximum anchor Z 5.633331 m at ratio 4, is a model measurement (`ARENA_LIVE_NOTES.md` 122–125). It is not a measurement from this Flycast log. The stored ratio is `time_ratio_bits` (`arena_live.c` 249, `arena_live.h` 19). The notes' older dump had that word equal to 4.0. This run's word was not decoded.

### 2. Fixed — the coverage check no longer compares a query to the expression that built it

`dc_live_coverage` now returns crop containment and the travel cap (`arena_live_assets.c` 50–52). The four edges are not compared with `target ± half`.

The independent check is `dc_live_query_matches` (`arena_live_math.c` 118–127), called on `window.coverage` (`arena_live_render.c` 159). It rebuilds the anchor in double precision from raw `trans / 4096` and inverse forward × 55, and allows 0.0002 m. Updates 1..42 passed it, because it sits before the draw acknowledgement on every successful frame. Update 43 did not reach it.

The notes' host mutation, `c.target[0] += 1` exiting 42, was not re-run here (`ARENA_LIVE_NOTES.md` 72–78).

### 3. Fixed in source — the submitted camera is no longer compared with a second convert of the sample

`dc_arena_fly_frame` copies the terrain camera `rc` after the draw and passes that copy to `dc_arena_live_drawn` (`arena_live_render.c` 190–192). `dc_arena_live_drawn` calls `dc_live_camera_matches` (`arena_live.c` 313). That function checks yaw and pitch against the raw inverse lanes at 0.00001 rad, eye and target against `trans / 4096` and forward × distance at 0.0001 m, then four noncollinear probes through `dc_live_instance_matches` (`arena_live_math.c` 103–115). The instance camera `ic` is also checked before the draw and again after submit (`arena_live_render.c` 150 and 190).

Updates 1..42 reached both acknowledgements. Update 43 did not. The notes' host mutation, `c.yaw += 0.1` exiting 43, was not re-run here.

### 4. Open — the first orientation sample still fails, and the recording is no longer circle plus L2

The earlier review stopped at update 26, while stage 3 was still ahead, and treated circle as unmeasured (`ARENA_LIVE_REVIEW.md`, finding 4). This log reaches that sample and rejects it.

The recording on disk does not set `DC_PAD_B` or `DC_PAD_L2`. For stage 3 and `completed >= 42` it sets `DC_PAD_RX_POS` (`arena_live_input.c` 34, `goal_pad.h` 12). `dc_goal_pad_fill` writes right X = 207 and leaves the button halfword at 0 unless B or the synthetic L2 bit is set (`goal_pad.c` 61–62, 71–72, 77–79). The L2 mapper is still compiled. This recording never sets bit 16, so `cpad-hold?` of L2 is false and `cam-states-dbg.gc` 238–241 does not run. The notes still say the recording supplies `DC_PAD_B | DC_PAD_L2` for completed updates 42 and 43.

The branch that does run is the non-modifier else (`cam-states-dbg.gc` 242–246). `analog-input(207, 128, 48, 110, -1)` is −31/62 (`pad.gc` 199–213). The else branch then sets `rv.y` from `2 * rot-speed * −(that value)`, which is one `*CAM_FREE-bank* rot-speed`, and sets `rv.x` from the centered right Y, which is 0. Left X and left Y are centered on this stage, so the translation adds are 0. Both vectors are then multiplied by `*display* time-adjust-ratio` (`cam-states-dbg.gc` 247–248). `rot-speed` is `(degrees 0.6)` (`cam-states-dbg.gc` 68).

At an NTSC adjust of 4 that step is 2.4 degrees, 0.04189 rad. The window accepts 0.009..0.043 (`arena_live.c` 293). A correct single step at adjust 1, 2, 3 or 4 fits. The upper bound has about 2.7% of room over the ratio-4 step. The same magnitude is what a working circle-plus-L2 path would produce: circle subtracts one `rot-speed` (`cam-states-dbg.gc` 101–108) and the L2 branch does not overwrite `rv.y`. This tree does not take that path. Either path, if it actually turned the roll-free basis by that amount and left translation alone, would satisfy `delta == 4` and the angle test. Output 43 was rejected anyway.

The pad-byte witness for the service that used `update_returns == 42` passed. A mismatch there dies at `arena_live.c` 229 with a different string, and the stage require at lines 93–95 also passed (`pad_stage == dc_live_input_stage(42)`, which is 3). The serviced cpad matched the bytes this ELF recorded, including whichever of right X 207 or circle|L2 the image contained. The check counters cannot tell those two recordings apart: both become non-neutral at completed 42, and this ELF was not disassembled. The source that would be compiled now is the right-X recording.

The notes' decoded dump, a different image, measured yaw 2.1999999427825365 on updates 41, 42 and 43 and a yaw difference of 0 (`ARENA_LIVE_NOTES.md` 16–20). That row would fail this window. This run's row was not decoded. A yaw that is still 2.2, a step above 0.043, a basis that changed after the witness, and a stalled resume counter are the same backend string.

## New defects

### 5. High — output 43 fails the only orientation contract in the 181-update recording

Concrete case: this log. Failed case 12089, description from `arena_live.c` 304, ledger 590 + 55 + 41×35 + 8 = 2088 successful checks before that `require`. Forty-two draws completed. The first sample that requires an orientation-only step (`arena_live.c` 288–294) is the one that stops the run. No later leg, neutral tail, finish counter or `frame.ppm` runs. The witness at lines 106–108 had already accepted sequence 43 as a roll-free affine camera, so this is not the poison, the 4096-unit scale, or the 181-long schedule. Those passed for 43 calls (`poison_batches == witnessed_updates == 43` is part of the sample preamble at lines 270–272, and that preamble's failure uses this same string only if it fails; the distinct witness string did not).

A correct right-X step at the previously measured adjust of 4.0 is inside 0.009..0.043. The sample rejected the output. The original free camera, under the bytes this image recorded, did not produce an orientation-only step the window accepts on the first button output.

### 6. Medium — the turn window starts on the first output that can see the control

The required turn samples are sequences 43 and 44 only (`arena_live.c` 288). Completed 42 is the first non-neutral stage-3 sample, and the pipeline above makes it visible on output 43. The strafe windows are inset: stage 1 is completed 16..27, visible on outputs 17..28, and the required moving run is 20..26 (`arena_live_input.c` 10, `arena_live.c` 287). The turn window has no inset sample.

Concrete case: if the first yaw is still absent on output 43 and appears on output 44, sequence 43 fails `delta == 4` or the 0.009 floor, the run stops, and sequence 44 is never sampled. The single-slave combiner copies the slave tracking inverse and translation on the non-blend path (`cam-combiner.gc` 214–246). The strafe that passed updates 20..26 followed the one-frame delay this pipeline describes. A second frame of delay is not what those updates showed. The window still has no spare output if the first yaw lands one frame later than that.

Completed 40 and 41 are already stage 3, with `joyx == 0` and buttons 0, because the right-X bit waits for `completed >= 42` (`arena_live_input.c` 12 and 34). They sit outside every required window. The stage witness compares `dc_live_input_stage` with itself, so the stage number does not catch a byte that is still neutral.

### 7. Medium — the 0.043 rad ceiling rejects a legal PAL step, and any adjust above about 4.1

`set-time-ratios` clamps slowdown at 4 (`display.gc` 40). NTSC stores that clamp as `time-adjust-ratio` (`display.gc` 49–50). PAL stores `1.2 * ratio` (`display.gc` 43–44). The retail field is `time-adjust-ratio` (`display-h.gc` 138). The C read is `display + 916` (`arena_live.c` 73 and 249), the same offset-assert-minus-4 slot the earlier review used. The sample multiplies the 0.6-degree step by that adjust (`cam-states-dbg.gc` 247).

Concrete case, a correct else-branch step. Adjust 4 gives 0.04189 rad and passes 0.043. Adjust 4.8, the PAL value when the clamp is saturated, gives 0.0503 rad and fails `angle > 0.043` at `arena_live.c` 293 even though `delta == 4` and the basis is the original one. The ratio that hits 0.043 is about 4.11 times one `rot-speed`. `ordinary_mode` still treats every positive finite word as ordinary (`arena_live.c` 73–74). The notes' older dump measured 4.0, which is the NTSC adjust at the clamp, so PAL is not required to explain that older zero-yaw row. This run's word was not decoded. `dc_arena_syncv` still returns without waiting (`arena_coexist.c` 106), which is how a frame can sit on the clamp. This is the travel-cap hole from finding 1, now on the yaw window that actually failed.

### 8. Medium, latent — the compiled L2 branch is a translation branch, and the notes still say the recording holds L2

`cam-states-dbg.gc` 238–241, taken only while L2 is held, adds left-stick X and Z and right-stick Y into translation at full `*CAM_FREE-bank* speed`. It does not write `rv`. With this recording's centered sticks the addends are 0, and the recording does not hold L2, so this branch does not explain output 43.

Concrete case for a recording that followed the notes instead of `arena_live_input.c` 34: hold L2, and put a non-zero left stick in the same completed update. Circle or a prior `rv.y` can still change the basis, and the stick changes translation. `dc_live_delta` then sets both bit 2 and bit 4 (`arena_live_math.c` 129–135). The turn window requires `delta == 4` alone (`arena_live.c` 293). The original move is legal and the sample rejects it. The notes' table at line 93 will send the next diagnosis at this branch. The source that is compiled from the current recording will not enter it.

### 9. High for acceptance, not the cause of failed case 12089 — the capture still saves 13312 bytes of a 38752-byte record

`verify.lua` 31 writes `13312` bytes from the evidence address. `sizeof(dc_live_evidence)` is fixed at 38752 (`arena_live.c` 12): 236 bytes of header, the initial pose, and 181 rows of 212 bytes (`arena_live.h` 8–26, `ARENA_LIVE_NOTES.md` 160–161). `verify_arena_live.py` 22 and 66 require `len(blob) == 380 + 212*181`. A dump of 13312 bytes cannot pass that auditor.

`analyze_arena_live_path.py` 24–27 accepts 13312 or the full size. 13312 is 61 rows. Version 2 with 61 slots is an allowed prefix, so the analyzer will score those rows and will not report that rows 62..181 were never saved.

Concrete case for this log: output 43 is row index 42, at file offset 380 + 42×212 = 9284, inside the 13312-byte prefix. Truncation did not hide the failing row and did not decide check 2089. Concrete case for a run that actually finishes: the dump ends at row 61, the auditor rejects the length, and the neutral tail at updates 176..180 is not in the file. This session did not decode the prefix that was saved.

## What is still clean

The pad hook is still the hardware sample. `dc_goal_pad_read_state` is the only `DC_ARENA_LIVE` branch that replaces the maple read (`goal_pad.c` 33–38). `dc_arena_live_pad` compares status, the button halfword, the four stick bytes and twelve pressure bytes (`arena_live.c` 221–223). A stage number alone does not pass. Extra reads do not advance `completed_updates` (`arena_live_input.c` 37–38). Forty-two successful updates each had a new selected service whose bytes matched the recording for `update_returns`.

The store witness can fail. Thirty-six distinct quiet NaNs are written immediately before the saved export (`arena_live.c` 98–100, `arena_live_math.c` 60–68). `dc_live_read` accepts the camera only when every lane is finite and the basis matches the roll-free yaw/pitch reconstruction (`arena_live_math.c` 17–23). Forty-three calls passed that witness, including the call whose sample later failed. The shim calls the loader pointer saved in `dc_arena_live_begin` (`arena_live.c` 132–134 and 100).

Units on the frames that drew are the GOAL meter. `trans / 4096` with no axis swap is `arena_live_math.c` 33–34. Updates 1..42 also passed `dc_live_terrain_matches` and both instance matches. A 4096-scale miss or a swapped axis fails those on update 1. Update 1 completed, and its initial pose matched `dc_live_initial_matches` (`arena_live.c` 276–278).

The level guard still runs before the poison (`arena_live.c` 65–72) and held for 43 calls. `span` still refuses a range outside the allocated prefix (`arena_live.c` 31–35). The bridge calls `dc_arena_live_resume` and `dc_arena_live_pad` only under `DC_ARENA_LIVE`.

## What this review could not confirm

`dump.bin` was not decoded. `time_ratio_bits`, evidence `reason` (the 8 passed to `require` is not the backend expected word), and row 43's 36 floats are the measurement that separates a yaw still at 2.2, a step above 0.043, a basis edited after the witness, and a stalled resume counter. The control flow implies `witnessed_updates == 43`, `rendered == 42`, `affine_reads == 42` and `turning_pairs == 0`. Those words were not read back.

The SH-4 bytes of this ELF were not compared with `arena_live_input.c`. The check index matches either the notes' circle-plus-L2 recording or the current right-X recording. The file that is on disk is the right-X one.

The moving windows after update 43, the neutral tails, both turning outputs as a pair, the 4.5..20 m finish path (`arena_live.c` 349–355) and `frame.ppm` did not run. Original FOV, GPU matrix registers and framebuffer appearance are unconfirmed. Jak, level collision and follow cameras remain outside this target.
