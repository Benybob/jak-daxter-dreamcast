(AI-assisted)

# Arena game review

Read-only review of the Dreamcast `*math-camera*` readback in `ports/dreamcast`. One Flycast run is on disk: `build/arena-game-run.txt` and `build/arena-game-review/result.json`. Backend state 2, **2375/2375**, failed case 0, description `BOOT scripted=1562/1562 frames=64 slab=1255392 arena=10673360/11010048`. Camera metric words:

`207822996, 1, 64, 63, 63, 3279316963, 1113247606, 3279808525, 1074664571, 3202434990`

Decoded from those bits, the live object is a tag-4 basic (`207822996 = 16*12988937 + 4`). `scripted=1`, `reads=64`, `moved=63`, `turned=63`. The five floats are eye `(-246.4058075, 54.7024059, -253.9064484)` meters, yaw `2.220000029`, pitch `-0.44000000`.

That pose is the C script at path frame 15 (dispatch 63, `frames % 16`). `build/arena_fly_fixture.h` has `fly_yaw=2.2f`, `fly_pitch=-0.45f`, `fly_distance=55`, and frame 15 target `(-286.0439758, 31.2759285, -283.9897766)`. `arena_game.c` adds `0.02` yaw and `0.01` pitch on that frame. The vector from this eye to that target has length 55. The published camera is the beach fixture orbit, after the script has been written into GOAL memory.

No unit or axis error showed up in that round trip. Severity below: high means a passing run can be read as a game-driven camera; medium means a field or oracle the notes rely on is not enforced on device; low means a redundant check or a note the source does not support.

## Findings

### 1. High — scripted motion is counted as GOAL motion

`arena_game.c` 215–226 reads `*math-camera*`, accepts the constructor identity or the pose this function wrote last frame, then overwrites the object from `fly_yaw`, `fly_pitch`, `fly_distance`, and `fly_frames[f].target`. Lines 235–237 then set `moved` and `turned` from that post-write readback. Lines 290–291 require `moved==63` and `turned==63` under the names `arena game GOAL position changes each frame` and `arena game GOAL orientation changes each frame`. Line 240 assigns `scripted=1` on every success. It does not read a GOAL scripted-camera flag.

Failing case: leave `*math-camera*` at the constructor pose for the whole boot (`math-camera.gc` 249–252: zero XYZ, W=1, identity). Frame 0 passes the identity arm. Each later frame sees the previous C write, so `arena game headless camera has no autonomous motion` passes, and the script advances yaw by `0.02/15` and pitch by `0.01/15`. All 63 transitions change both arrays, so finish still passes. The run above is that case: yaw/pitch/eye match the fixture script, and `update-camera` in `cam-update.gc` did not have to move the camera.

`dc_arena_game_report` (lines 297–301) prints `BOOT scripted=1562/1562`, which is `passed/checks` for this file. `ARENA_GAME_NOTES.md` 147–148 says so. The check strings at lines 290–291 still describe that ratio as GOAL motion. `result.json` records the same PASS.

### 2. Medium — device readback never checks `camera-rot`

`arena_game.c` 66–68 writes `trans`, `inv-camera-rot`, and the transpose into `camera-rot`. `dc_arena_game_read` (lines 59–61) copies only `trans` and `inv`. The success compare at line 226 is `memcmp` of `dc_game_camera_pose`, which has those two arrays and nothing else. `moved` / `turned` use the same arrays (lines 236–237).

Failing case: delete the `camera-rot` `memcpy` at line 68, or store the inverse there untransposed. `tests/arena_game_check.c` 83 compares `camera-rot` lane `i` with `inv[(i%4)*4 + i/4]` and fails. The Flycast checks at lines 226 and 290–291 still pass, and the metric dump is unchanged. `ARENA_GAME_NOTES.md` 95–96 says the renderer compares the stored fields with the proposed script. The device compare does not include the transpose the notes list at line 26.

### 3. Medium — the terrain check cannot see an orientation error

`arena_game.c` 230–234 builds the streaming rectangle from `c.target[0]`, `c.target[2]`, and half of `fly_frames[0].query` (14 m on this beach fixture). Yaw and pitch are not inputs. Line 256 only requires `window.count>0`.

Failing case: after a successful convert, add `pi` to `c.yaw` and rebuild `script.inv` with `rotation()` before the draw, leaving `c.target` alone. The query box is identical, so `arena game camera window contains terrain` still passes whenever the unrotated frame did. The old fly path checked face count and window CRC (`arena_fly.c` 131–132). This ELF replaced those with nonempty checks, which `ARENA_GAME_NOTES.md` 139–142 states. The prescribed `--suite flythrough` label does not: `verify_backend.py` 143, copied into `build/arena-game-review/result.json`, still says every frame was checked against host counts and CRCs. This run's 2375 matches the notes' arithmetic, which has no per-frame CRC term, so those oracles were not what passed.

### 4. Low — `reads==64` cannot fail once `frames==64` has passed

`reads` increments only at `arena_game.c` 240, on the path that returns 1. `frames` increments only at line 283, after that return. `arena_coexist.c` 182 skips the rest of the dispatch when the frame returns 0, and `kernel_boot_cases.c` 421 jumps to `done` without `dc_arena_finish`. Finish's `reads==64` check (line 289) therefore runs only after 64 successes, which have already incremented `reads` 64 times. Line 288 passing means line 289 cannot fail.

### 5. Low — distance 0 makes the eye/target check vacuous

`tests/arena_game_check.c` 49–50 converts pitch `0.7` with distance 0 and requires `target==eye` on all three axes. `dc_arena_game_convert` (`arena_game.c` 30–32) sets `target = eye + forward * distance` and returns 0 before `*out=c` if that result is rejected. With distance 0 and a successful return, `target==eye` is the assignment. Any wrong eye that still passes the matrix check satisfies line 50. The forward-axis projection checks above it (lines 34–46) can fail; this one cannot.

### 6. Low — the stdio-free object claim does not cover this file

`ARENA_GAME_NOTES.md` 162–163 says `sh-elf-nm -u` on the new slab-redirected renderer object shows no libc heap or stdio imports. `arena_game.c` is compiled with `-DDC_ARENA_SLAB_REDIRECT -include arena_slab.h` (`Makefile` 872–874). `dc_arena_game_report` calls `snprintf` at line 298. `arena_slab.h` 19–32 renames `malloc`, `fopen`, `fread`, `fclose`, `fgetc`, and `ferror`. It does not rename `snprintf`. A link map of `arena_game.o` that omits `snprintf` would contradict this source.

## What is clean

GOAL layout matches the retail type. `decompiler/config/jak1/all-types.gc` 5872–5901 puts `camera-rot` at 368, `inv-camera-rot` at 432, and `trans` at 848 from the object base, size `#x424` (1060), flags `#x900000424`. Relative to the tag-4 pointer that is 364, 428, 844, and 1060, which is what `arena_game.h` 6–7 and `tests/test_arena_game.py` 64–68 expect. The Python walker starts at the tag and uses the field order in `goal_src/jak1/engine/gfx/math-camera-h.gc`. The same numbers fall out. Frame 0 of the run accepted the object, so those offsets held an identity inverse and a `(0,0,0,1)` translation on the real `*math-camera*`, which is what `math-camera.gc` 249–252 writes.

The basis matches `forward-down->inv-matrix` (`geometry.gc` 255–266) with down = `(0,-1,0)` and the renderer. Vector 2 is forward. For a unit forward `F` and horizontal length `h`, GOAL's right is `(F.z/h, 0, -F.x/h)` and its up is `(-F.x*F.y/h, h, -F.z*F.y/h)`. `rotation()` in `arena_game.c` 11–14 stores that same basis as `(cos yaw, 0, sin yaw)`, `(sin pitch * sin yaw, cos pitch, -sin pitch * cos yaw)`, `(-cos pitch * sin yaw, sin pitch, cos pitch * cos yaw)`. `render_draw_window.c` 54–63 and `instance_pass.c` 177–186 use those rows, times `0.25`, and add `distance * 0.25` on view Z. `slave-set-rotation!` (`camera.gc` 893–904) builds forward as follow-point minus eye, and convert places the orbit anchor at `eye + forward * distance` (`arena_game.c` 30–32), so the rendered eye stays on the GOAL eye. Dividing `trans` by 4096 is one meter per 4096 GOAL units. The dumped eye is 55 m from the fixture anchor, so this run did not apply that scale twice or drop it. Pitch `-0.44` puts the eye above the anchor (54.7 m versus 31.3 m), which is the look-down sign the renderer uses.

`span` (`arena_game.c` 39–45) refuses a null runtime, a null arena, `used > capacity`, a wrapping `base+capacity`, an address below `base`, and a span that is not entirely inside `[base, base+used)`. `camera_bytes` (lines 46–54) requires tag 4, a `type` whose own tag is `rt->type_type`, `allocated_size == 1060` at the `dc_goal_type_fields` offset (`goal_layout.h` 77–80: uint16 at byte 8 of the tagged type, which is `t+12`), and an object tag equal to that type. Copies go through `rt->memory + offset` and `memcpy`. The helper does not cast a GOAL integer to a pointer, and it does not call `dc_goal_resolve`, so the extension, the temporary bump, and the root-stack fallback in `goal_bridge.c` `object_bytes` are outside this path. Writes call `convert` and `camera_bytes` before the three `memcpy`s. Failed convert and failed read leave `*out` unchanged (`*out=c` and `*out=p` are after the checks). Host counts in `arena_game_check.c` match the notes: 39 conversion checks, 189 hostile-float checks, 58 memory checks, 286 per binary, 572 with `-ffast-math`.

`kernel_boot_cases.c` 29–33 and 460–464 only declare and call `dc_arena_game_report` under `DC_ARENA_GAME`, ahead of the fly report. `goal_bridge.c` 1229–1235 binds the runtime only under `DC_ARENA_GAME`, inside `dc_goal_install_pc_services`, which the boot calls at `kernel_boot_cases.c` 137 after core bridges have set `active_runtime`. Symbol lookup stays in `game_camera`, after GAME.CGO load. The Makefile game link (lines 878–880) uses `arena_game.o` and the same `.dc_stage=0x8cb00000` / `.dc_fly` placement as arena-fly, and it does not list `arena_fly.o`. The 2375 breakdown in the notes adds up, and 17 init + 2 lookups + `64*(8+16)` + 7 finish is 1562, which is the number the run printed.

## What this review could not confirm

The host unittest was not re-executed, and `sh-elf-gcc`, `sh-elf-nm`, and `make -n` were not re-run. The snprintf import is from the source and the redirect list, not from an `nm` listing. `arena_coexist.c`, `arena_fly.c`, and the design document were not diffed against a pre-change tree. The framebuffer was not inspected. A real slave or combiner pose, FOV, roll, smush, and a non-level `local-down` were not sampled; the run's stationary-pose guard passed because the script's own bytes came back, which is the behavior in finding 1. The axis identity with `forward-down->inv-matrix` was checked algebraically against the two renderer files. It was not executed as a separate numeric oracle, and the host projection sample (`arena_game_check.c` 34–46) places its point on forward, so view X and view Y are zero there.
