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

Not yet: the bsp-header `relocate` (needs `*level*` loading-level), entity
birth, collision queries against it, and the level's code.

## Order

1. Done: the logic-BSP builder above, and original login of its output.
2. Make 1 MB: measure the boot again with the register allocator work or a
   smaller actor heap, and return the loader's KOS heap to the arena.
3. Load Geyser Rock's logic BSP, art and code through the original
   `load-dgo`/`login` path (RPC-DGO returned buffers, `link-begin`/`link-resume`),
   with the converted render package drawn by the existing renderer.
4. `start 'play` at Geyser Rock's continue point; collision under Jak.

None of this is implemented yet. The measurements above are the only result.
