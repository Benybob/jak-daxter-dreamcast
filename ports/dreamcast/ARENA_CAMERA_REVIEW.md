(AI-assisted)

# Arena camera review

Read-only review of `arena_camera.c`, `arena_camera.h`, and `ARENA_CAMERA_NOTES.md` against `GAME_CAMERA_ROUTE.md`, using `build/arena-camera-run.txt` and `build/arena-camera-review/result.json`. No source file other than this review was edited. The host tests, `sh-elf-gcc`, and Flycast were not re-run.

The saved run does not prove that original code wrote the camera. Build exit is 0. Backend state is 3, **1135/1136**, failed case **11136**, description `camera original update-camera completed all 61 display dispatches`, observed 0, expected 1. ELF SHA-256 `12b28e43df413cd5eb9b8f6fbe66bcb7f1912a5c55170974c5cf74e7af49dc25`. The review directory has no `dump.bin`.

That observed 0 is the boolean `update_calls==61 && update_returns==61` (`arena_camera.c` 206–207). `require` passes the boolean to `dc_arena_camera_check` as actual-versus-1 (`arena_camera.c` 22–24). `arena_coexist.c` 58–64 stores the first failure as `10000+total`, which is 11136 when total is 1136.

The total matches the pre-edit 64-dispatch ELF with 60 successful output checks: 870 existing arena checks, plus 6 install checks, plus 64×3 root samples, plus 60 witness checks, plus 8 finish checks. Finish checks 1–7 returned true. Check 8 is the 61-count and is the first failure, so the later sentinel and `dispatches==65` lines did not run. `display-loop` suspends before `while *run*` (`goal_src/jak1/engine/game/main.gc` 369) and calls `update-camera` inside the loop (line 419). Sixty display dispatches are one initialization and 59 updates. The dispatch after the test process is deactivated (`kernel_boot_cases.c` 435–438) adds one update. 59+1=60. The notes’ account of this log is the same arithmetic. The current `DISPLAY_FRAMES=61` sources (`kernel_boot_cases.c` 386–390, `arena_coexist.c` 206, `arena_camera.c` 210) are not what produced 1136: that failure on the edited schedule would be 1144 checks.

Severity below: high means a later green run can be read as a game-driven camera; medium means the pass condition is weaker than the metric name; low means a host-only pass.

## Findings

### 1. High — `trans.w == 1` passes for a reset combiner copy

`arena_camera.c` 76–78 stores `0xc640e400` (−12345.0) at `DC_GAME_CAMERA_TRANS+12` once. Lines 90–94 set `original_wrote_camera` when that word is `0x3f800000` after the saved function returns. Lines 100–104 then accept any finite translation and inverse. `pose_changes` may stay 0 (line 104). The notes say so (line 57).

`trans` is decompiler offset 848 (`decompiler/config/jak1/all-types.gc` 5890). On the GOAL pointer that is 844 (`arena_game.h` 6), and w is the next word. `vector-reset!` writes `(0,0,0,1)` (`goal_src/jak1/engine/math/vector-h.gc` 491–496). Combiner init does that to its own `trans` (`goal_src/jak1/engine/camera/cam-combiner.gc` 255). The ordinary `update-camera` branch copies that quad onto `*math-camera*` (`goal_src/jak1/engine/camera/cam-update.gc` 290–292).

Concrete case: a connected neutral pad, the slave suspended in `cam-free-floating`, and the combiner still at its reset trans. The first call replaces −12345.0 with 1.0 and leaves xyz at 0. `original_wrote_camera` becomes 1. `dc_arena_camera_report` (`arena_camera.c` 215–217) prints `BOOT original-camera wrote=1`. The eye and the inverse can be the startup values for the whole run. `GAME_CAMERA_ROUTE.md` lines 172–178 ask for finite pose changes that follow input, and for neutral-input stability as a separate measurement. This pass condition treats the homogeneous 1 as the camera having been written.

The C pose script is off this link line (`Makefile` 893–894 links `arena_camera.o` and does not list `arena_game.o`; `ARENA_CAMERA_FLAGS` has no `DC_ARENA_GAME`). The store that clears the sentinel happens inside the call at `arena_camera.c` 80. A writer that runs before that call is overwritten by the sentinel. A writer that runs after the return is too late for this compare. The saved `update-camera` export, or a callee it reached, left w equal to 1. That is the real statement. It is a smaller statement than “original code wrote the camera.”

### 2. Medium — one store satisfies all 61 returns

The sentinel is written only while `diagnostic_seeds` is 0 (`arena_camera.c` 70–78). Later calls require `w==expect && original_wrote_camera` (lines 91–96) and do not plant −12345 again. Finish lines 206–209 require 61 calls, 61 returns, one seed, and the sticky flag.

Concrete case: call 1 copies the combiner quad, so w becomes 1 and the flag becomes 1. Calls 2 through 61 return at once and leave that word untouched. Each later `require` passes because the leftover 1.0 is still there. After the schedule edit, finish passes too. The 61-count then means the wrapper was entered 61 times. It means one witnessed store.

The same hole covers a mode change after the first call. `cam-update.gc` 261 leaves trans alone in pause, progress, or while `*progress-process*` is set. A later call in that arm keeps the first call’s 1.0. Finish samples `*master-mode*` from the root frame (`arena_camera.c` 173 and 201–203), so the mode during the call and the mode at the sample can differ.

### 3. Medium — state 2 and `wrote=1` omit the counters that would catch findings 1 and 2

`result.json` records the boolean. It does not record `update_calls`, `diagnostic_seeds`, `original_wrote_camera`, or `pose_changes`. The 60 above is the gap between 1136 and the checks that must already have passed.

Concrete case on the edited ELF: `verify_backend.py --tests 1148` returns state 2, and the operator stops there. The success description is `dc_arena_camera_report`, which prints `wrote` and `update_returns` (`arena_camera.c` 215–217). `pose_changes` can be 0, trans xyz can be 0, and both stay out of that string. The notes’ pass list (lines 153–156) asks for the dump fields. A pass that skips the dump repeats the blindness of this log.

### 4. Low — the host double plants the same 1.0 the device is meant to earn

`tests/arena_camera_check.c` 17–22, mode 1, writes `0x3f800000` from `fake_update`. Lines 100–102 accept `original_wrote_camera==1`. The notes (lines 38–41 and 102) call these fixtures. A green `test_arena_camera.py` is that fixture. Quoting it as the Dreamcast camera check is a pass that never entered the SH-4 export.

## What is clean

The failure diagnosis matches the log. `display-loop`’s first resume ends at the suspend on `main.gc` 369, so it does not call `update-camera`. The extra update in the count of 60 is the post-deactivation dispatch. The notes’ 1148 for a full pass of the edited sources adds up: 528 GOAL + 10 stage + 4 prepare + 65×5 dispatch + 8 finish, plus 6 install + 65×3 root samples + 61 output checks + 11 finish checks.

The wrapper is installed on the root stack after GAME.CGO (`kernel_boot_cases.c` 361–362, `arena_camera.c` 113–134). `dc_goal_code_entry` must name the saved value as the `update-camera` export (lines 116–117). The symbol is then pointed at the trampoline, and the shim calls the saved pointer (line 80). Entry requires the current fiber’s process to be `*dproc*` (lines 60–62). Re-entering the shim on the first call, before `original_wrote_camera` is set, fails the inner `require` at line 95, because the flag is still 0. That path fails closed.

Field offsets used here follow the port’s GOAL-pointer convention, decompiler `offset-assert` minus 4, the same convention as `kernel_boot_cases.c` 325 (`STATUS=32`, retail status 36). Checked against that: `trans.w` at 856, `inv-camera-rot` at 428, process status at 32, state at 52, mask at 4, `num-slaves` at 112, the slave ppointer at 116, `*CAMERA-bank*` `joypad` at +4 (retail 8, `all-types.gc` 15288), cpad `valid` at byte 0 (retail 4), cpad `number` at +32 (retail 36), cpad pointers at `pads+4+4*index` (GOAL reference array, `pad.gc` 156–171), and `time-adjust-ratio` at `display+916` (retail 920, `all-types.gc` 5114). The metric block is 22 words plus three 7-word process records, 172 bytes, with the compile-time check at `arena_camera.c` 12. The header layout matches.

On this run, finish checks before the count failure returned true. That ELF had produced a master and a combiner, one registered slave, a yielded resume of each, a slave sample whose state was `cam-free-floating` while its status was `suspended`, a serviced selected pad whose valid byte did not have bit 128, true read-analog and read-buttons, master mode `game`, and a positive finite time-adjust ratio. Those are process and pad facts. They are separate from which function wrote the eye.

`span` (`arena_camera.c` 29–34) refuses a null runtime, a null arena, `used > capacity`, a wrapping `base+capacity`, an address below `base`, and a span that is not entirely inside the allocated prefix. `math_camera` also requires the type’s allocated size to be 1060.

## What this review could not confirm

The edited 61-display / 65-dispatch ELF has not been linked or run. Whether the post-deactivation dispatch still resumes `display-loop` after that extra iteration, and whether `*display*`’s real-integral counter still advances once per added frame, is unread. The SH-4 bytes of the `update-camera` export were not disassembled, so a callee reached by that export is still a possible writer of the one word. `pose_changes`, the raw update counters, and the math-camera address for this log were not in a dump. Input response, neutral-input stability, affine readback, beach retargeting, level streaming, collision, and Jak-follow were outside this probe, as `GAME_CAMERA_ROUTE.md` leaves them.

## Next step

Rebuild `jak-dc-arena-camera.elf` from the current sources and run the notes’ Flycast command into `build/arena-camera-schedule-review` with `--tests 1148` and `--dump dc_arena_camera_metrics 172`. Read `update_calls`, `update_returns`, `diagnostic_seeds`, `original_wrote_camera`, `pose_changes`, and the master/slave/combiner blocks from `dump.bin`, then read the 16-byte `trans` at the dumped math-camera address. A 1148/1148 result with `pose_changes` 0 and trans xyz still zero confirms the schedule only. The witness that would actually show the original export wrote the camera is a poison of the whole trans quad, checked after the same call, with that quad in the dump.
