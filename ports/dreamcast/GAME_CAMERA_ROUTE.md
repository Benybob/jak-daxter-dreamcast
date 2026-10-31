(AI-assisted)

# Route to an original GOAL camera

ISC. Source/artifact investigation for the Milestone 3 headless boot. No route
is implemented here. No original camera process was executed or sampled in this
investigation. The statements about reachable behavior below are source-derived;
they are not a claim that a new device run reached those states.

## What is already present

The staged `build/game_cgo_order.h` has `DC_GAME_PREFIX=326`; the staged report
lists all camera modules with no compilation failure. I decoded the 326 staged
DCSH module headers, checked their lengths and body CRCs, and found native code
exports for `cam-start`, `cam-master-init`, `cam-slave-init`,
`cam-free-floating-input`, `cam-free-floating-move`, `cam-combiner-init`,
`update-camera` and `service-cpads`. These are existing disk artifacts, not a
fresh GAME.CGO build or proof of the contents of a running ELF.

The boot loads GAME.CGO and runs its original startups
(`ports/dreamcast/kernel_boot_cases.c:201-237`). In the original source,
[goal_src/jak1/engine/camera/cam-start.gc:17](../../goal_src/jak1/engine/camera/cam-start.gc#L17)
stops existing cameras and spawns the combiner and master under `*camera-pool*`;
line 32 calls `cam-start #f` at top level. The false argument skips
`reset-cameras`; it does **not** skip spawning. A new port path should inspect
these processes, not blindly spawn duplicates.

The master can initialize without Jak: it clears target handles, initializes
seekers and a target spline, spawns a list keeper and a **free-floating slave**,
and enters its active state
([goal_src/jak1/engine/camera/cam-master.gc:965](../../goal_src/jak1/engine/camera/cam-master.gc#L965),
especially 993-1006). This corrects the idea that the headless boot necessarily
has no camera slave. Slave initialization copies the combiner pose and reports
`slave-activated` to the master before entering the requested state
([goal_src/jak1/engine/camera/camera.gc:510](../../goal_src/jak1/engine/camera/camera.gc#L510),
574-596); the master registers that slave in its list at
[goal_src/jak1/engine/camera/cam-master.gc:785](../../goal_src/jak1/engine/camera/cam-master.gc#L785).

The native boot already dispatches spawned processes, then starts the original
`display-loop` for 60 frames and performs another dispatch after killing its test
process (`kernel_boot_cases.c:356-441`). GOAL's dispatcher resumes waiting or
suspended processes under the active tree
([goal_src/jak1/kernel/gkernel.gc:1606](../../goal_src/jak1/kernel/gkernel.gc#L1606)).
Camera descendants belong to the camera pool at lines 2478-2482. Execution still
depends on process masks and `run-logic?` (lines 1543-1557); a future probe must
record actual master/slave/combiner status, masks and resumptions instead of
inferring that they ran from a stationary final pose. Default master mode is
`game` (line 270); default settings block `execute` and `sleep`, not all camera
updates ([goal_src/jak1/engine/game/settings.gc:259](../../goal_src/jak1/engine/game/settings.gc#L259)).

## How far it can run without a level

The source permits the complete **original free-floating process path**:
master -> free-floating slave -> combiner -> display-loop `update-camera` ->
`*math-camera*`. This path does not require a spawned target or active level.
The slave calls `cam-free-floating-move` and suspends every iteration
([goal_src/jak1/engine/camera/cam-states-dbg.gc:277](../../goal_src/jak1/engine/camera/cam-states-dbg.gc#L277)).
Despite the filename, these functions exist in the staged native module; the
file is not marked `declare-file (debug)` like `cam-layout.gc:10-11`.

With one slave, the combiner copies its eye/FOV and orientation; with two it
blends them
([goal_src/jak1/engine/camera/cam-combiner.gc:103](../../goal_src/jak1/engine/camera/cam-combiner.gc#L103),
214-246). It initializes eye zero and inverse identity at lines 252-262. The
original display loop calls `update-camera` at
[goal_src/jak1/engine/game/main.gc:418](../../goal_src/jak1/engine/game/main.gc#L418).
That copies the combiner pose, applies smush, and builds camera outputs
([goal_src/jak1/engine/camera/cam-update.gc:290](../../goal_src/jak1/engine/camera/cam-update.gc#L290),
315, 353-365). With inactive levels, the level/BSP work is gated by status
(`cam-update.gc:99-113`, 240-257); that does not inherently prevent free movement.

A successful C-scripted Milestone 3 run establishes none of those resumptions:
it samples constructor/previous-script bytes and writes a new fixture pose after
each dispatch. The idle guard will also deliberately fail if real camera input
produces another pose. The current motion counters describe that C script.

## What stops useful motion

**Input is available, but its use is unmeasured here.** The port's
`goal_pad.c:33-42` reads Maple controllers, and `goal_bridge.c:1089-1100` connects
`cpad-open`/`cpad-get-data`. GOAL polls through `service-cpads`
([goal_src/jak1/engine/ps2/pad.gc:272](../../goal_src/jak1/engine/ps2/pad.gc#L272)),
called by `display-frame-start`
([goal_src/jak1/engine/draw/drawable.gc:1082](../../goal_src/jak1/engine/draw/drawable.gc#L1082)).
Early dispatches before display starts need not have fresh pad data. The free
camera returns immediately for pad-invalid bit 128 or menu mode
([goal_src/jak1/engine/camera/cam-states-dbg.gc:257](../../goal_src/jak1/engine/camera/cam-states-dbg.gc#L257));
neutral valid input produces no displacement. Analog and button reading default
to true (`cam-interface-h.gc:12-14`), and movement is scaled by display time
(`cam-states-dbg.gc:212-247`). Verify pad validity, selected joypad, read flags,
mode and time-adjust ratio rather than assuming absent input services.

The Dreamcast bridge centers the PS2 right stick (`goal_pad.c:64`) and does not
map L2/R2/L3/R3. That limits controls but is not a complete motion blocker: the
left stick translates, and original cross/triangle and square/circle buttons
pitch/yaw (`cam-states-dbg.gc:70-109`, 212-243). Current mappings are
A/B/X/Y = cross/circle/square/triangle; triggers = L1/R1. No second-stick service
is required for the smallest free-camera demonstration.

**Following Jak requires game state absent from this headless setup.** `*target*`
starts false
([goal_src/jak1/engine/target/target-h.gc:155](../../goal_src/jak1/engine/target/target-h.gc#L155));
`start` is what spawns and initializes it
([goal_src/jak1/engine/target/logic-target.gc:1280](../../goal_src/jak1/engine/target/logic-target.gc#L1280)).
Without a target, the master clears tracking and unsets camera regions rather
than inventing player motion (`cam-master.gc:333-340`, 471-474). Target-follow
initialization reads actual control quaternions, surface/status and water
fields (`cam-master.gc:48-90`); allocating a position vector alone cannot satisfy
that contract. `target-cam-pos` falling back to `camera-pos` is not a player
([goal_src/jak1/engine/target/target-util.gc:789](../../goal_src/jak1/engine/target/target-util.gc#L789)).

The beach render package is C-owned tfrag/instance data, not a GOAL level with
BSP, camera entities, target art, continue-point and collision resources.
`reset-cameras` births camera entities only from active levels
([goal_src/jak1/engine/entity/entity.gc:996](../../goal_src/jak1/engine/entity/entity.gc#L996)).
The normal `cam-string` state uses the master's target spline and LOS/collision
machinery (`cam-states.gc:1579-1607`, 1690 onward); a free-floating camera avoids
that data dependency. For example, collision probing already appears at
`cam-states.gc:1652-1661`. A real player-follow milestone must supply those original
resources and prove the corresponding collision/target paths.

**Level loading still needs services beyond the boot loader.** The boot's C
DCSH DGO loader is not the game's asynchronous GOAL level-loader service.
Original level loading sends RPC-DGO requests and requires returned object
buffers
([goal_src/jak1/engine/load/load-dgo.gc:220](../../goal_src/jak1/engine/load/load-dgo.gc#L220),
240-254), then calls `link-begin` (line 339) and `link-resume`
([goal_src/jak1/engine/level/level.gc:317](../../goal_src/jak1/engine/level/level.gc#L317)).
The current bridge drops `rpc-call`/answers `rpc-busy?` with zero
(`goal_bridge.c:1243-1245`), and its file open records descriptor -1
(lines 1208-1221). It installs no `link-begin`/`link-resume` bridge, and neither
name is a staged GAME code export. These missing services block the normal
level path; claiming successful headless boot proves streaming would be wrong.
They are not intrinsically needed for an already-spawned free-floating slave.
The exact first runtime fault/wait of an attempted level load was not measured.

**The current readback adapter is deliberately narrower than the original.**
`update-camera` first transposes rotation, then writes rotated negative eye into
`camera-rot` vector 3 and GOAL-unit eye into `inv-camera-rot` vector 3
([goal_src/jak1/engine/camera/cam-update.gc:353](../../goal_src/jak1/engine/camera/cam-update.gc#L353)).
Milestone 3 validates all inverse lanes as a pure unit rotation and all rotation
lanes as its full transpose. Thus a translated real final camera will fail even
if the original processes work. This is a required future adapter change, not
a reason to overwrite or zero the original matrices. The upper 3x3 is the
orientation; the last vectors are affine translation. FOV, smoothing, roll and
nonstandard local-down remain outside the renderer's current representation.

## Smallest route, not implemented

1. Keep the existing original startup/dispatcher/display loop. Probe the master,
   slave and combiner process states, registered slave handle, pause masks,
   selected pad, validity/read flags and display time. Wait for initialization
   and a serviced controller; confirm the first slave really entered
   `cam-free-floating`. Avoid duplicating `cam-start`.
2. Remove the per-dispatch C pose producer and idle/previous-script guard in a
   separate future game-driven mode. If the beach needs an initial view, use a
   **single explicit initialization**, through a GOAL camera event/initial pose,
   then let the original slave own every subsequent pose. Do not seed
   `*math-camera*` every frame: the combiner will overwrite it. A one-time spawn
   matrix can use the existing `*camera-init-mat*` path
   (`camera.gc:531-541`); inspect lifecycle before applying it to the already
   spawned slave. An original teleport-to-transform event also exists at
   `cam-master.gc:543-550`.
3. Adapt readback to the **final affine** matrices: validate basis/homogeneous
   lanes, upper-3x3 transpose and translation relationships independently;
   extract the eye from `trans` and orientation from inverse basis vectors.
   Apply the 1/4096 scale once. Preserve current finite/range/arena guards and
   explicit rejection of unsupported roll/poles. Do not require an affine
   inverse's last vector to be zero or a full 4x4 transpose. Keep the independent
   terrain view oracle based on eye and upper basis.
4. Use the real Maple left stick and face buttons for an initial demonstration;
   extra controls can come later. Read the final math-camera only after the
   original display update has run, with root-stack/owner checks intact. Require
   observed process resumptions and finite pose changes responding to input;
   also verify neutral-input stability. Diagnostic input injection, if used,
   belongs at the pad boundary and must be labeled synthetic, with all camera
   motion still computed by original GOAL. Publish producer/source and stage
   counters so a C-script run cannot pass as this mode.
5. Retarget bounded render streaming from that readback. Seed/cap travel so the
   demonstration stays inside the available beach package, and distinguish a
   known-empty/out-of-coverage window from a camera failure. This reaches a
   game-code-driven, user-controlled original **free camera**, without first
   implementing level streaming or Jak. A genuinely Jak-following camera is a
   subsequent milestone: native level-loader/link services plus original level,
   target, art and collision data, then switch through the normal camera state
   and region machinery. Do not label free movement as Jak-following gameplay.

Checks actually run for this investigation: source searches and numbered reads;
read-only DCSH length/CRC/export decoding. No runtime camera experiment was run.
