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

## Order

1. A logic-BSP builder for TRA.DGO, with host tests that the rebuilt object's
   relocations and bindings are complete and that every remaining reference
   resolves.
2. Make 1 MB: measure the boot again with the register allocator work or a
   smaller actor heap, and return the loader's KOS heap to the arena.
3. Load Geyser Rock's logic BSP, art and code through the original
   `load-dgo`/`login` path (RPC-DGO returned buffers, `link-begin`/`link-resume`),
   with the converted render package drawn by the existing renderer.
4. `start 'play` at Geyser Rock's continue point; collision under Jak.

None of this is implemented yet. The measurements above are the only result.
