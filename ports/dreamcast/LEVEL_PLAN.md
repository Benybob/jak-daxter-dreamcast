# Level plan: what a retail level needs in Dreamcast RAM (2026-11-04, AI-assisted)

The Jak arena now runs the original target, sidekick, HUD and camera processes
with original dead-pool compaction (7652 of 7652, see
[ARENA_JAK_NOTES.md](ARENA_JAK_NOTES.md)). After GAME.CGO and the player art,
the GOAL arena has about 100 KB free and the KOS heap a few KB. A level is the
next blocker. This page measures what one needs.

`audit_level_dgo.py <DGO>` reads the owner's extracted level DGO and prints
each object's kind and size, then attributes the BSP's bytes to GOAL types
from the v4 link table (each basic's tag is a type binding; bytes up to the
next tag count toward it, so trailing inline data is included in the basic
before it). It writes nothing.

## Measured (retail DGOs, bytes)

| Level | BSP | of which render-only | BSP for logic | Texture pages | Other data (art, entities) | Code |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| TIT (title) | 193,216 | 1,200 | 185,856 | 643,440 | 462,160 | 26,048 |
| INT (intro) | 789,776 | 728,308 | 54,028 | 564,704 | 291,952 | 4,336 |
| TRA (Geyser Rock) | 4,507,296 resident | 4,143,496 (91.9%) | 363,800 | 1,603,264 | 570,800 | 47,968 |
| BEA (Sentinel Beach) | 6,378,976 | 5,365,780 (84.9%) | 954,684 | 2,192,192 | 1,690,416 | 237,584 |
| VI1 (Sandover Village) | 7,479,408 | 5,951,212 (80.3%) | 1,464,180 | 1,982,816 | 1,606,592 | 259,968 |

Render-only types are the ones only the PS2 renderers read: `instance-tie`,
`tfragment`, `tie-fragment`, `time-of-day-palette`, `instance-shrubbery`,
`generic-shrub-fragment`, `shrubbery`, the tie/shrub prototypes and
`adgif-shader-array`. For Geyser Rock the rest is mainly `collide-frag-mesh`
(164,848) and `collide-fragment`, the `draw-node` tree (23,296), `nav-mesh`
(13,720), `level-vis-info` (9,072), 152 `entity-actor`s and their
`drawable-actor`s, ambients, and floats (94,880, attribution includes the data
that follows them).

## What this means

On PS2 a level is one 10 MiB level heap and two are resident. On a 16 MiB
Dreamcast running all of GAME.CGO that cannot work, but most of a level is
render data the Dreamcast draws differently anyway:

1. **Render data stays out of the GOAL arena.** The port already draws levels
   from offline-converted packages (`render_package`, sector and instance
   caches, the beach/village flythroughs) streamed from VRAM or disc. A level
   keeps those packages, built from the same BSP by the existing converters.
2. **A logic BSP.** Rebuild the level's BSP object without the render-only
   trees: the same bsp-header, collision, nav, vis info, entities and actors,
   with the drawable trees that held tfrag/tie/shrub emptied. For Geyser Rock
   that is about 364 KB instead of 4.5 MB. It must be a valid v4 object
   (pointer and type tables rebuilt) so original `login`, entity birth and
   collision run unchanged.
3. **Textures in VRAM.** The 1.6 MB of Geyser Rock texture pages go to VRAM as
   PowerVR textures; the GOAL side keeps the texture-page records.
4. **Level code** (48 KB for Geyser Rock) compiles to SH-4 like GAME.CGO.
5. **Art and entity data** (571 KB for Geyser Rock: scarecrow, sharkey,
   platforms...) are loaded as now, through original art login.

Even the smallest real level needs about 1 MB resident (logic BSP, art, code),
against about 100 KB free. That room has to come from the options in
[MEMORY_PLAN.md](MEMORY_PLAN.md): smaller GAME.CGO code (a register
allocator), the 984 KiB `*nk-dead-pool*` sized for the level actually loaded,
and the KOS loader buffers returned to the arena after boot.

## Logic BSP builder (done)

`build_logic_bsp.py <DGO>` writes `build/logic-bsp/<LEVEL>-LOGIC.DGO` (one
retail-format v4 object) and a JSON report. `bsp_graph.py` splits the BSP into
objects at 16-aligned basic tags, folding pieces nothing points into
(inline-array elements, array content-type tags) into the object before them.
The builder removes the render trees from `drawable-trees` (length rewritten,
kept trees first, freed slots zeroed and no longer relocated), keeps
everything reachable from the bsp-header without entering a render tree,
compacts it, remaps every pointer and binding, re-encodes the link table and
decodes the result to check that the pointer and binding tables round-trip.
It refuses if any kept pointer reaches dropped data.
`tests/test_logic_bsp.py` round-trips synthetic tables (runs of 255 and
more, an exact multiple of 255, 1- to 4-byte binding deltas).

| Level | BSP data | Logic BSP | Kept trees | Dropped trees |
| --- | ---: | ---: | --- | --- |
| INT | 782,336 | 44,576 | actor, collide-fragment, ambient | render |
| TRA | 4,507,296 | 378,128 | actor, collide-fragment, ambient | tfrag, 2 instance-tie, lowres-tfrag, instance-shrub |
| BEA | 6,320,464 | 994,832 | actor, collide-fragment, ambient | tfrag, trans-tfrag, instance-tie, instance-shrub |
| VI1 | 7,415,392 | 1,509,632 | actor, collide-fragment, ambient | tfrag, trans-, lowres-, dirt-tfrag, 3 instance-tie, instance-shrub |

Not yet shown: that original `login`, entity birth and collision accept it.
Tie collision (11,744 bytes of `collide-frag-mesh` under Geyser Rock's ties)
is dropped with the tie trees.

## Original login accepts it (Flycast)

`jak-dc-level.elf` is the DGO boot (532 checks) plus one phase
(`level_logic.c`, `-DDC_LEVEL_LOGIC`): it reads `/cd/TRA-LOGIC.DGO`, links it
with `dc_goal_load_v4_single` (`goal_art.c`), checks the root is a typed
`bsp-header` whose `drawable-trees` is a `drawable-tree-array`, and calls the
original `(method login bsp-header)` (method 9, authenticated by its code
entry). Result: **533 of 533**. 378,128 bytes linked, 3,545 pointers and 3,111
bindings, 3 trees (actor, collide-fragment, ambient), login returned the header
and left no fault. `--dump dc_level_logic_evidence 128`.

Two things it needed:
- Level data binds actor types its own code defines later (`tra-pontoon`,
  `training-cam`, `warpgate`, `scarecrow-a`...). The original linker interns a
  forward type for those; the art loader refused them. `links()` now takes a
  flag, and only the single-object level loader interns forward types
  (`dc_goal_intern_type`, the binding's method count). 20 new symbols.
- A disc boot loads the flat binary, so this ELF links its boot romdisk at
  0x8cb00000 like the Jak ELF; at 0x8cd80000 `KERNEL.CGO` did not open.
  `make_boot_gdi.py --require` names the staged files the disc must have.

Build and run (from the repo root). Current disc, in `build/level-disc`:
`TRAINING.DGO` (`TRA-LEVEL.DGO`, art groups and logic BSP), `VILLAGE1.DGO`
(`VI1-LOGIC.DGO`), `TRA-CODE.DGO`, `VI1-CODE.DGO`, `TRA.VIS`, `VI1.VIS`,
`EICHAR.DGO`, `DIR-TPAGES.DGO`:

```text
python ports/dreamcast/build_logic_bsp.py --art <TRA.DGO>
python ports/dreamcast/build_logic_bsp.py <VI1.DGO>
python ports/dreamcast/build_level_code.py TRA   (and VI1)
make ... jak-dc-level.elf
python ports/dreamcast/make_boot_gdi.py ports/dreamcast/jak-dc-level.elf --ip-template <ip.tmpl> \
  --staged ports/dreamcast/build/level-disc --require TRAINING.DGO --require VILLAGE1.DGO --output ports/dreamcast/build/level-gdi
python ports/dreamcast/verify_backend.py <flycast> ports/dreamcast/jak-dc-level.elf \
  --disc ports/dreamcast/build/level-gdi/jak-arena.gdi --nm <sh-elf-nm> --tests 543 --timeout 300
```

### The level's code

`build_level_code.py TRA` compiles the `.o` entries of `dgos/tra.gd`
(training-obs, training-part) from goal_src after all of GAME.CGO, through the
same frontend batch and whole-module SH-4 lowering as `build_game_cgo.py`, into
`build/level-code/TRA-CODE.DGO` (DCSH: 65,520 + 36,742 bytes). Both compiled on
the first try. `jak-dc-level.elf` loads it from `/cd` with the native DGO loader
before the BSP, as TRA.DGO orders code before data. Result: **537 of 537**:
both startups run, the seven training actor types the BSP binds
(`tra-pontoon`, `training-cam`, `scarecrow-a`/`-b`, `tra-iris-door`,
`training-water`, `training-part`) are the code's deftypes, not forward types,
and login still passes. The code costs 75 KB of arena. `warpgate` and
`warp-gate-switch` come from `villagep-obs` (VILLAGEP.CGO) and stay forward
types.

### Original level loading (Flycast, 538 of 538)

`jak-dc-level.elf` now loads Geyser Rock the way the game does. Before the
headless display-loop frames it loads TRA-CODE.DGO, installs the DGO server
(`level_dgo_rpc.c`) and calls the original `want-levels` and
`want-display-level` of `*load-state*` for `'training`. Then display-loop's
original `level-update` does the rest: `load-state update!` calls
`level-get-for-use`, `alloc-levels!` takes two level heaps, `load-begin` sends
the RPC-DGO request, `load-continue` links the object, `login-begin` and
`level-update-after-load` log it in, and the next frame births it and makes it
active. `dc_level_check` then asks the original `level-status`: `'active`; the
bsp-header the original relocate attached points back at the level, with all
152 actors.

Pieces:
- **Level heaps.** `dreamcast_memory_budget.gc` sets `LEVEL_HEAP_SIZE` and
  `LEVEL_HEAP_SIZE_DEBUG` to 1 MiB (PS2 10,416 KiB). GAME.CGO was rebuilt; the
  kernel boot still passes 90 and 532.
- **RPC-DGO server** (`level_dgo_rpc.c`). The shared bridge binds original
  `rpc-call`, `link-begin` and `link-resume` to optional hooks
  (`dc_goal_rpc_handler`, `dc_goal_link_begin_handler`); without them every
  other ELF behaves as before. The server reads each object to the request's
  `bt` (align64 of the heap's current) and links a v4 data object in place
  there (`dc_goal_link_v4_at`); `link-begin` advances the heap past it and,
  with execute-login, calls its original relocate (method 7). Loading is
  synchronous, so `rpc-busy?` stays 0.
- **The file name.** With vis off, `load-begin` asks for `<name>.DGO`, so the
  disc holds the logic BSP as `TRAINING.DGO`.
- **A pending fault.** `load-begin` kmallocs two 2 MiB load buffers from the
  1 MiB heap; they are refused (as PS2 would refuse) and leave a BAD_ARGUMENT
  pending. The server records it (`prior_error`) and starts clean, as the IOP
  never saw EE faults. The buffers are not needed: objects go straight to `bt`.

### Toward actors: birth's entity table

After the level was active no actor spawned, and `bsp-header birth` (method
18, wrapped by a diagnostic probe) left the level's `entity` field (+280) at 0
with BAD_ARGUMENT: its `(new 'loading-level 'entity-links-array ...)` failed.
- `'loading-level` is the C kernel's fixed heap symbol, whose value is a kheap:
  the global heap, or a level's heap while that level loads and births
  (`load-begin`, level `birth`). The bridge refused it. `new` on it now
  kmallocs from that kheap (16-byte aligned from `current`, refused at `top`,
  zeroed), or uses the arena when its value is the global heap's.
- The port does not bind the fixed slot, so compiled code links an interned
  `loading-level`; the bridge finds that symbol by name.
- `alloc-levels!` called `load-package "art"` and `"common"`, which reached the
  unbound C `dgo-load` (BAD_FUNCTION from `load-package`, found with
  `dc_goal_missing_call_probe`, which records the GOAL caller of a call to a
  non-function). The DGO boot now records the resident packages in
  `*kernel-packages*` after GAME.CGO, as the C kernel does: kernel, engine,
  art, common.

Observed after these (Flycast, 538 of 538): `bsp-header birth` runs once with
no fault and allocates the level's `entity-links-array` (152 links) and
ambient table from the level heap; no call reaches a non-function.

### Why no actor is born yet

`actors-update` (wrapped: it runs every one of the 60 frames) has
`*vis-actors*` on, and the level's `vis-info` 0 is set while `all-visible?` is
`'loading`, so its vis branch births nothing. That is the original behaviour
until a VIS file arrives: `init-vis` marks the level `'loading`, and
`load-state update!` only calls `load-vis-info` (a ramdisk RPC fill of the
level's VIS file, then `ramdisk-load` chunks) once the camera is inside the
level's boxes. In this headless boot nothing puts a camera there: there is no
target or camera process, and the camera position is the origin, about
1,500 m from Geyser Rock's first actor (-1,199 m, 16 m, 925 m). Actors need the
player in the level (its camera inside the boxes) and an IOP ramdisk server
for the VIS file, as the PC port's overlord provides.

### Jak in the level ELF (Flycast, 539 of 539)

To put a camera in the level, `jak-dc-level.elf` now spawns Jak too
(`level_player.c`, the Jak ELF's player path without its arena renderer):
the texture-page directory and eichar load from `/cd` through their original
relocate methods, the original `get-or-create-continue!` gives the continue
point, original `start 'play` runs with the listener as caller, and the
target's armed main thread on `*dram-stack*` is imported as a native fiber;
the display frames then run him beside the loading level.
- Room: `LEVEL_HEAP_SIZE` is now 512 KiB per level (Geyser Rock uses about
  400 KB after birth), so eichar (1,318,288 bytes) fits beside the two level
  heaps; 1.54 MB of arena is free after the player starts.
- The level ELF links the Jak player's bridge and span ledger
  (`level_goal_bridge.o`, `level_process_spans.o`, `-DDC_ARENA_JAK_PLAYER`
  without the arena renderer flags): start's spawns (target, sidekick, HUD)
  are admitted under the bound listener and the ledger follows shrunk heaps.
  With the generic bridge the scope entry refused them (BAD_ARGUMENT).
- The disc adds EICHAR.DGO and DIR-TPAGES.DGO from `build/jak-art-disc`.

### Actors are born (Flycast, 539 of 539, 180 frames)

With Jak at Geyser Rock's `training-start` (original `get-continue-by-name`),
the original camera follows him into the level, the level asks the IOP
ramdisk for its VIS file, and original `actors-update` births actors: 90
processes under `*entity-pool*` after 180 frames (training-cam, viewer, eco,
...). Jak is in `target-stance-ambient` at (-1316.8, 6.9, 1057.8) m, the camera
at (-1325.1, 11.3, 1063.3) m. What it took:
- **Process spawns from a running process.** `run-function-in-process` runs
  every init under a catch frame; the catch bridge only supported catches on
  the root C stack, so a spawn from a process's own code (target-continue's
  `cam-start`, every actor birth) left the child `initialize-dead` without
  running its init. A catch now lives on whichever stack makes it: a running
  fiber's working stack, or the KOS thread's. Its body cannot yield (the pp
  bracket blocks stack transfers), so it is thrown to and returns on the same
  stack. The Jak bridge's spawn admission now also applies under a fiber's
  bound process context.
- **loado.** Original `texture-page-login` asks the C kernel's `loado` for any
  texture page not resident; the port had none, so a missing-function stub
  returned 0, which GOAL treats as a page and relocates. It now answers #f, as
  a disc kernel with nothing to load from would.
- **The IOP ramdisk** (RPC port 2, `level_dgo_rpc.c`): fill opens `/cd/` plus
  the requested name (`TRA.VIS`, staged at `build/level-disc/VIS/TRA.VIS` and
  the disc root) and load reads the requested range on demand.
- **The continue point.** `get-or-create-continue!` gave the default continue
  at the origin; Geyser Rock's new-game point is `training-start`.
- `jak-dc-level.elf` runs 180 display frames.

### village1 beside Geyser Rock (Flycast, 543 of 543, 2026-11-05)

Every Geyser Rock continue point also wants village1 displayed (`lev1`), and
`target-continue` waits for it. Until now the test cleared `lev1`, because
village1's 1,509,632-byte logic BSP did not fit a 512 KiB level heap. The code
shrinking freed the room, so the original continue point now runs unchanged:
- **Level heaps** are 1,664 KiB each (`dreamcast_memory_budget.gc`). Either
  level can land in either heap, so both take village1's size.
- **village1's code** (`build_level_code.py VI1`: 12 objects, 576,416 bytes,
  from villagep-obs to sequence-a-village1) loads at boot after TRA-CODE.
- **The disc** adds `VILLAGE1.DGO` (VI1-LOGIC.DGO), `VI1-CODE.DGO` and
  `VI1.VIS`.

Measured after 180 frames: both levels `'active`; Geyser Rock's heap holds
389,664 bytes and village1's 1,526,064 (178 KB to spare); 115 actors born
under `*entity-pool*` (90 before). The arena ends at 13,076,032 of 13,631,488
bytes: **555 KB free**. village1's art groups and texture pages are not loaded
(as for Geyser Rock), so none of its actors would draw, and none is near Jak.

### Geyser Rock's art groups (Flycast, 543 of 543, 2026-11-05)

`build_logic_bsp.py --art TRA.DGO` writes `TRA-LEVEL.DGO`: the level's 11 art
groups (570,800 bytes: ecovalve, jng-iris-door, plat-eco, pontoonfive,
scarecrow-a/b, sharkey, trainingcam, warp-gate-switch, warpgate,
water-anim-training) in retail order, then the logic BSP. The disc serves it as
`TRAINING.DGO`. Texture pages stay out (1.6 MB; they belong in VRAM). Original
level loading links each one in the level heap and calls its relocate method;
art-group relocate adds it to the loading level's art directory, and level
login logs them in. After 180 frames all 11 are in Geyser Rock's directory
(`dc_level_spawn_probe.art_groups`), its heap holds 955,552 of 1,703,936 bytes,
and the arena is unchanged.

Loading 12 objects made the load take minutes in Flycast: the in-place linker's
validation pass read each pointer word from the disc with its own seek.
`dc_goal_link_v4_at` now reads an object once, into its destination (heap space
nothing owns until the link succeeds), and validates from there; symbols are
still interned only by the apply pass.

### Actors run their init (Flycast, 543 of 543)

The art loaded, but of 115 processes under `*entity-pool*` 103 stayed `'ready`:
entity `birth!` activated them and `init-from-entity!` never ran. The earlier
"actors are born" count had measured processes, not initialized actors. Four
port faults, found by wrapping original `init-entity`:

1. **`format` ~S and ~A were swapped.** kprint.cpp prints a string's characters
   for ~S and the object (quoted) for ~A. med-res actors build their skeleton
   group's name with `"med-res-~S~S"` and `"*~S-sg*"`, which came out as
   `*"med-res-jungle"""-sg*`.
2. **`string->symbol` was unbound** (kscheme.cpp's `intern`), so
   `initialize-skeleton-by-name` called a non-function. The first actor born is
   such a village1 scenery actor; its fault stayed set and refused every later
   birth. Now a bridge interns the string.
3. **Stale process-span rows refused new actors.** The ledger never retired a
   row when its process died or moved, so when the original dead-pool heap
   reused that memory the new process's note refused (line 100 overlap).
   Original compaction (`actors-update` → `compact`) copies a live process
   lower and repoints its pool record, leaving a whole stale copy, with a
   registered spawned fiber, at the old address. The ledger now takes an
   optional `unheld` hook: a row the fresh extent overlaps is dropped when
   nothing holds its process (catch frame, fiber, scope frame or allocation
   binding). Before answering, the bridge releases spawned fibers of a process
   whose `ppointer` (+20) record no longer names it, or that is `'dead`.
4. **Too few slots.** The ledger had 32 rows, the bridge 28 spawned fibers and
   36 registered thread fibers, sized for the Jak ELF's 18 live processes. Now
   256 rows, 192 spawned and 200 registered fibers (about 60 KB of bss). With
   the old sizes the first 15 actors initialized and the rest silently stayed
   `'ready`.

After 180 frames all 103 `init-entity` calls leave their process
`'waiting-to-run`; no `*entity-pool*` actor is `'ready`; 6 actors made a
draw-control from a Geyser Rock art group; one is in
`process-drawable-art-error`; the actor heap holds 230,776 bytes after their
heaps shrank. `dc_level_check` now requires no `'ready` actor and at least one
draw-control. With live actors the 180 frames take about 5.5 minutes in
Flycast, so the level suite runs with a 900 s deadline (verify_backend.py's cap
is now 900).

### Level heaps sized per slot (Flycast, 543 of 543, 2026-11-06)

Original `alloc-levels!` takes two equal level heaps, so either level fits
either slot; at 1,664 KiB each, Geyser Rock left about 700 KB of its heap
unused. The level ELF now allocates them itself before the first frame
(`alloc_level_heaps` in `level_logic.c`): level0 1 MiB for Geyser Rock (wanted
first; 955,584 bytes used) and level1 1,664 KiB for village1 (1,526,064).
`alloc-levels!` allocates only while level0's heap base is 0, so it keeps
these; the art and common packages it would also note are recorded by the DGO
boot. `dc_level_check` requires Geyser Rock in level0. The arena ends at
12,157,216 of 13,631,488 bytes: **1.47 MB free** (819 KB before), room for
the renderer's measured 1.37 MB slab. This is a choice for this pair of
levels, not the original's symmetric layout: a level larger than 1 MiB in
level0 would not fit.

### Actors run each frame; no call reaches a non-function (Flycast, 543 of 543)

- **Born actors never ran.** After their init every actor stayed
  `'waiting-to-run`: the dispatcher reached them (prevent-from-run is the normal
  execute|sleep, run-logic? is true), but `thread_resume` found no fiber,
  because registering the imported one refused it as a duplicate. The fiber
  release above ran inside the display fiber, where
  `dc_goal_unregister_thread_fiber` refuses, yet still zeroed the fiber: the
  registry kept a slot that looked free and was handed out again. The release
  now clears that registry slot itself (the released fiber is never the running
  one). Now the cameras are `idle`, eco pills `wait`, the scarecrows `idle`,
  sharkey `nav-enemy-idle`, the pontoon `rigid-body-platform-idle`, the warp
  gate switch `basebutton-up-idle`, the particle spawners active.
  `dc_level_check` requires actors that ran and suspended.
- **The streamed-art loader called a non-function every frame.**
  `external-art-buffer update` asked the IOP's STR server (RPC port 4) for
  spooled animation chunks; nothing answered, the reply kept its `invalid`
  result, `str-load-status` read that as complete and the buffer called the
  C kernel's `link` (unbound) on PS2 kernel address #x84000. The bridge now
  answers every STR request with load-msg-result error, as an IOP with no
  .STR files would, and the buffer drops it. Missing calls after 180 frames: 0
  (163 before).
- **Why only some actors draw.** Of the first 24 actors, the five with a
  Geyser Rock art group (scarecrow-b ×2, sharkey, tra-pontoon,
  warp-gate-switch) have a draw-control. Cameras, eco pills and particle
  spawners have none by design; `money` stays `'waiting-to-run` because its
  original run-logic? waits for `was-drawn`, which only a renderer sets.
  `dc_level_actor_table` lists type, state, draw-control and mask.

The actor heap (`*nk-dead-pool*`, 984 KiB as on PS2) is not a place to save:
after those 180 frames original `memory-used` reports 844,120 of 1,007,616
bytes, from Geyser Rock's actors alone (`dc_level_spawn_probe`).

Not yet: level code served through the same RPC (it loads at boot), the
level's art groups and texture pages, actor spawning over later frames,
collision queries, and the level inside the Jak ELF (its arena has no room for
two level heaps).

## Order

1. Done: the logic-BSP builder above, and original login of its output.
2. Make 1 MB: measure the boot again with the register allocator work or a
   smaller actor heap, and return the loader's KOS heap to the arena.
3. Load Geyser Rock's logic BSP, art and code through the original
   `load-dgo`/`login` path (RPC-DGO returned buffers, `link-begin`/`link-resume`),
   with the converted render package drawn by the existing renderer.
4. `start 'play` at Geyser Rock's continue point; collision under Jak.

None of this is implemented yet. The measurements above are the only result.

### The level ELF beside the PVR (Flycast, 1480 of 1480, 2026-11-06)

First step toward drawing Geyser Rock. `jak-dc-level.elf` is now built with
`DC_ARENA_COEXIST` and `arena_coexist.c`'s new `DC_LEVEL_DRAW` variant, the
same path `jak-dc-arena.elf` uses: `pvr_init`, VRAM loans from
`pvr_mem_malloc` for the PS2 DMA buffers and the boot romdisk (checked to read
back), a renderer slab of 1,507,392 bytes (the Jak ELF's measured high water
plus its margin) allocated before the GOAL arena, and a PVR scene after every
one of the 184 dispatcher calls. The GOAL arena is the rest of the 13 MiB the
level ELF had: 12,124,096 bytes, of which the run ends using 12,048,464 (75 KB
spare). The arena ELF's syncv and DMA-chain checks are skipped here: the level
ELF's frames reach neither. Nothing is drawn yet; the scenes are clears.

Next: Geyser Rock's render data (`tie_lods.py --textured --compact` on
`training-background.glb` gives an 817,744-byte DCI3 instance package; the
terrain sectors and cell split come from `make_flythrough_fixture.py`), read
from the disc, and a level renderer that takes its camera from the game's
`*math-camera*` each frame, with the same render libraries as
`arena_live_render.c`.

### Geyser Rock drawn from the game camera (Flycast, 3157 of 3157, 2026-11-06)

![first frame](build/geyser-first-frame.png) (git-ignored capture)

`level_draw.c` draws Geyser Rock in `jak-dc-level.elf` with the render
libraries `arena_live_render.c` draws beach with: the catalog and textures,
base bank, block cache, draw window, DCI3 instance package and instance cells.
Its camera is the original `*math-camera*` each frame (`dc_live_read`,
`dc_live_convert` from `arena_live_math.c`), the target 12 m ahead of the eye;
terrain comes from a ±14 m window around that target and instances out to
60 m. At the first draw the boot romdisk's VRAM loan is freed for textures and
the slab is handed to the renderer; all its allocations and file handles stay
in the slab (`DC_ARENA_SLAB_REDIRECT`). Level render data is read from the
disc, `/cd/GEYSER`:

```text
python tie_lods.py --textured --compact training-background.glb TRA.DGO debug_out --out build/geyser-render/package.dci3
python make_level_render_assets.py training-background.glb build/geyser-render/package.dci3 build/geyser-render/assets --at=-1316.8,6.9,1057.8 --radius 100
copy build/geyser-render/assets (without *.json) to build/level-disc/GEYSER
```

Measured over 184 frames: every frame drawn from the game camera; about 730
terrain faces, 485 instances and 38,915 instance triangles a frame; textures
383,872 bytes of VRAM with 2,268,136 left; renderer slab high water 1,454,048
of 1,507,392 bytes; no PVR errors. The GOAL arena still ends at 12,048,464 of
12,124,096. The capture shows the grass slope, palms, rocks and the warp gate.

Not yet: Jak and the actors (their art is loaded but nothing draws merc
models), distant terrain (only the ±14 m window around the target), sky and
water, and the frame rate (180 frames take about 7 minutes in Flycast).

### Jak and actors skinned from the game's joints (Flycast, 3164 of 3164, 2026-11-07)

![Jak at Geyser Rock](build/geyser-jak-frame.png) (git-ignored capture)

`level_models.c` draws the merc models of the level ELF's processes. Each frame
it walks the original process tree from `*active-pool*` and keeps every
`process-drawable` that is not hidden, lies within the 60 m draw distance and
the viewport, and whose draw-control merc model (`mgeo` name, e.g.
`eichar-lod0`) is in the disc bundle `GEYSER/MODELS.DSK`. Skinning reads only
the game's own data: for each cspace of the node-list, the joint's bind-pose
times the cspace bone's transform (what `bones-mtx-calc` uploads to VU1),
divided to meters, folded with the instance camera; each vertex blends up to
three joints. The original `ja-post` / `do-joint-math!` fill the bones every
frame, so no animation data is converted. A model loads the first time a
process shows it: geometry to the KOS heap, textures to VRAM.

The bundle comes from the decompiler's skinned GLB exports:

```text
python make_skin_models.py common/eichar-lod0.glb training/{jng-iris-door,plat-eco-lit,plat-eco,pontoonfive,scarecrow-a,scarecrow-b,sharkey,warp-gate-switch,water-anim-training-lake}-lod0.glb --out build/geyser-render/MODELS.DSK
copy MODELS.DSK to build/level-disc/GEYSER
```

1,179,616 bytes for 10 models (Jak: 2,998 vertices, 3,593 triangles). At a
model's first use its GLB inverse bind matrices are compared with the game's
joint bind-poses: the largest difference is 0, so the bundle's joint numbers
are the game's.

Measured over 184 frames: Jak drawn in 180 frames, an actor in 171; up to 2
models and 3,655 triangles a frame; 2 models loaded (88,068 bytes of geometry,
77,824 bytes of textures); no load failure, missing joint or invalid clip;
skinning Jak takes up to 2.6 ms in Flycast. The KOS heap had 1,312,184 bytes
free before the first model and 1,184,784 after. The 5 other drawables in the
tree were outside the draw distance or the viewport.

Not yet: lighting (vertex colours only, no merc lights or environment map),
lower LODs, eyes and blend shapes, shadows, back-face culling (all models are
double-sided), village1's models, and collision under Jak.

### Jak walks on Geyser Rock's original collision (Flycast, 3168 of 3168, 2026-11-07)

The level ELF's controller is now a scripted stick (`dc_level_pad_state`,
`goal_pad.c` with `-DDC_LEVEL_PAD`): neutral, then held forward from frame 12
for 100 frames, then neutral, through the original cpad service. The original
target code does the rest: `target-stance` -> `target-walk` -> `target-stance`,
moved by the original `target-post` and `integrate-and-collide!` against the
level's original `collide-fragment` tree (kept by the logic BSP) through the
ported collide-cache functions. `dc_level_target_trace` records Jak's position,
state, control status, poly-pat, coverage and surface angle every frame
(`--dump dc_level_target_trace 6216`).

Measured: 88 frames in `target-walk`, 3.95 m travelled up the grass slope, his
height following the ground (6.85 m to 7.05 m). Offline, against the
`training-background.glb` terrain under each sampled position, Jak stands 0 to
12 cm above the render mesh the whole way (the collision mesh is coarser); he
never sinks below it. New checks: target-walk for 40+ frames, 2+ m travelled,
within 1 m of his start height, on a surface for two thirds of the walk.

What it took:
- **A go from a trans hook.** `target-stance`'s trans does `(go target-walk)`.
  Trans hooks now run as a temporary fiber (`reset_and_call`, entered through a
  `dc-c-run-trans-hook` trampoline because the original trans thread lives inside
  the main stack, which a thread view refuses). enter-state re-arms the main
  thread with set-to-run; when a state change is pending after the trans
  (`next-state` != `state`, main thread armed at set-to-run-bootstrap) the
  bridge imports the armed main thread again in place (`rearm_main`), so the
  next resume runs enter-state on the main thread. 13 re-arms in the run.
- **Idle ambient.** Before this, Jak went idle into `target-stance-ambient`
  (30 game seconds standing), whose code waits for a spooled animation that
  never streams (STR requests are refused), so he never left it. The walk now
  starts before that; streaming is still missing.
- **Renderer memory.** The 652,592-byte instance package moved from the slab to
  the KOS heap, so the block cache can load terrain as the camera follows Jak
  (slab high water 1,033,728).
- **Stale ledger rows.** Walking births more actors. `dc_goal_enter_known_scope`
  now drops a row a newer, larger process outgrew at the same address, shrinks a
  row whose process was shrunk (shrink-heap) when nothing holds it, and a
  spawned fiber imported for an earlier PID at that address no longer counts as
  holding it. Before, 12 actors stayed 'ready (their birth hit BAD_ARGUMENT in
  process-heap validation). `dc_level_check` now asks that no actor stays
  'ready from one frame to the next (`dc_level_ready_probe`), since actors born
  in the last frame have not run yet.

### Contact normals: float arguments through jalr (Flycast, 3160 of 3160, 2026-11-07)

The cached collision tris under Jak were right (a `dc_level_cache_snapshot`
at frame 60: 18 grass tris on a 2 m grid, 6.7 to 7.1 m, pat 0x1c0, poly-normal
(0.048, 0.999, -0.013)), but the contact point was not: coverage and surface
angle were 0.20 to 0.27. When the projected point falls outside a tri,
`moving-sphere-triangle-intersect` hands over to `collide-do-primitives`, which
calls the GOAL functions `ray-sphere-intersect` (a float radius) and
`ray-cylinder-intersect` (float radius and length) through `jalr`. The SH-4
`jalr` passed eight u64, the desktop convention; native SH-4 GOAL takes float
arguments in FPU registers and counts only the integers on the stack, so the
radii were garbage and the edge result was written through a float's bits; the
contact point fell back to a tri corner, 2 to 3 m away.

`build_mips2c.py` now finds every GOAL function translated code calls through
`jalr` and, for those with a float argument or result in `all-types.gc`
(moving-sphere-sphere-intersect, atan, cos, sin, ray-sphere-intersect,
ray-cylinder-intersect, quaternion-axis-angle!), generates a thunk with the
native prototype. `dc_mips2c_jalr` calls it when the address is that function's
own export entry (named by its symbol), so the differential cases' stand-ins
keep the integer convention. sin, cos and atan were broken the same way for
ripples and particles.

Now: coverage 1.0, surface angle 0.99, `onground` in 171 of 181 frames (the 10
are target-continue). Jak walks at full speed: 8.27 m in 25 frames along the
hilltop, 0.56 m up, then stance and stance-ambient. With the longer 100-frame
walk he crosses the hill and goes down the far side to the shore. A new check
asks for onground in most frames; the walk is 25 frames so he stays on the
hilltop.

The camera that leaves Jak is the game's: a `training-cam` grabs Jak when he
comes in range and spawns a `pov-camera` (orbcam and the others) whose
animation is spooled. STR requests are refused, so it rests at its base pose
near sea level, under the hill, and the last frame shows the hill from below.
Streaming (spooled animations) is the next gap: it also holds
`target-stance-ambient`.

### Streaming: spooled animations and game text (Flycast, 3163 of 3163, 2026-11-07)

`level_str.c` is the IOP's STR server and the C kernel's `link` for the level
ELF, through two optional bridge hooks (`dc_goal_str_handler` for rpc-call port
4, `dc_goal_link_handler` for `link`, a new PC-port service; without them port 4
answers error and `link` refuses, as before).

- **STR server.** A chunk request (load-chunk-msg: address, chunk, length,
  name) maps the animation name to its ISO name as the overlord does
  (`eichar-ambient-4` -> `EIA4`), reads `/cd/STR/EIA4.STR`'s 64-entry chunk
  table (start sectors, then sizes) and the chunk into the buffer, and answers
  done with the length. A chunk id of -1 loads a whole file by name from `/cd`
  or `/cd/TEXT` (the game text `0COMMON.TXT`; the PC-only `0subtit.TXT` is not
  on the disc and is answered error, as the overlord would).
- **link.** Streamed chunks and text files are retail v2 objects (klink.cpp
  `jak1_work_v2`): `{0xffffffff, link block length, 2}`, a byte-coded pointer
  table, then symbol and type names each followed by the offsets of the words
  (0xffffffff) that take them, then the data. The data moves to the heap's
  current, pointers get its address, names are interned, and link returns data+4.
- **Spool heaps.** Original external-art-buffer puts its two heaps at #x84000 +
  index * 247 KiB, PS2 kernel memory the game reuses. That is not RAM here. The
  first request into it moves both heaps to 128 KiB each of KOS memory (the
  runtime's resident region) and is answered error; the buffer drops it and asks
  again into its new heap. A chunk larger than 128 KiB is refused the same way.

Measured: three objects link (the game text twice, then the chunk: 1,638
pointers, 12 types, 3 symbols in all); when Jak goes idle,
`eichar-ambient-4` chunk 0 (49,152 bytes) is read and linked, and the buffer is
'active for the last 16 frames: the original target-stance-ambient plays its
spooled animation. On the disc: `STR/EIA1..4.STR` (Jak's four idle animations)
and `TEXT/0COMMON.TXT`, copied from the extracted ISO (git-ignored).

Not yet: every other .STR (197 on the retail disc, 107 MB), larger chunks
(Dreamcast spool heaps are half the PS2's), and the hint dialogue the training
cameras wait for, which is sound streaming (a different RPC), not STR.

