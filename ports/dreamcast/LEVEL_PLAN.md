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

Build and run (from the repo root; `build/level-disc` holds TRA-LOGIC.DGO):

```text
python ports/dreamcast/build_logic_bsp.py <TRA.DGO>
make ... jak-dc-level.elf
python ports/dreamcast/make_boot_gdi.py ports/dreamcast/jak-dc-level.elf --ip-template <ip.tmpl> \
  --staged ports/dreamcast/build/level-disc --require TRA-LOGIC.DGO --output ports/dreamcast/build/level-gdi
python ports/dreamcast/verify_backend.py <flycast> ports/dreamcast/jak-dc-level.elf \
  --disc ports/dreamcast/build/level-gdi/jak-arena.gdi --nm <sh-elf-nm> --tests 533 --timeout 300
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
