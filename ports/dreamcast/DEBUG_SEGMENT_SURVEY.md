<!-- Documentation-only debug-segment inventory (AI-assisted). -->
# GAME.CGO debug-file survey (AI-assisted)

Source baseline: `4480ccbab`. Original game.gd code-object order is read through `build_game_cgo.game_order`; source paths come from `gp_deps.load`. Only live reader forms `(declare-file (debug))` count, not matching comments or strings. Line counts include comments and blank lines. Reader branches are not evaluated.

18 matching declarations among 326 code objects; data `.go` entries are not code-source declarations.

| Object | Source/declaration | Lines |
| --- | --- | --- |
| `dma-disasm` | [goal_src/jak1/engine/dma/dma-disasm.gc:11](../../goal_src/jak1/engine/dma/dma-disasm.gc#L11) | 439 |
| `pc-debug-common` | [goal_src/jak1/pc/debug/pc-debug-common.gc:9](../../goal_src/jak1/pc/debug/pc-debug-common.gc#L9) | 82 |
| `pc-debug-methods` | [goal_src/jak1/pc/debug/pc-debug-methods.gc:11](../../goal_src/jak1/pc/debug/pc-debug-methods.gc#L11) | 61 |
| `capture` | [goal_src/jak1/engine/util/capture.gc:13](../../goal_src/jak1/engine/util/capture.gc#L13) | 100 |
| `memory-usage-h` | [goal_src/jak1/engine/debug/memory-usage-h.gc:9](../../goal_src/jak1/engine/debug/memory-usage-h.gc#L9) | 164 |
| `cam-debug-h` | [goal_src/jak1/engine/camera/cam-debug-h.gc:73](../../goal_src/jak1/engine/camera/cam-debug-h.gc#L73) | 152 |
| `cam-layout` | [goal_src/jak1/engine/camera/cam-layout.gc:11](../../goal_src/jak1/engine/camera/cam-layout.gc#L11) | 2719 |
| `cam-debug` | [goal_src/jak1/engine/camera/cam-debug.gc:15](../../goal_src/jak1/engine/camera/cam-debug.gc#L15) | 1019 |
| `menu` | [goal_src/jak1/engine/debug/menu.gc:20](../../goal_src/jak1/engine/debug/menu.gc#L20) | 1003 |
| `memory-usage` | [goal_src/jak1/engine/debug/memory-usage.gc:9](../../goal_src/jak1/engine/debug/memory-usage.gc#L9) | 368 |
| `glist-h` | [goal_src/jak1/engine/util/glist-h.gc:12](../../goal_src/jak1/engine/util/glist-h.gc#L12) | 75 |
| `glist` | [goal_src/jak1/engine/util/glist.gc:9](../../goal_src/jak1/engine/util/glist.gc#L9) | 106 |
| `anim-tester` | [goal_src/jak1/engine/debug/anim-tester.gc:33](../../goal_src/jak1/engine/debug/anim-tester.gc#L33) | 1700 |
| `part-tester` | [goal_src/jak1/engine/debug/part-tester.gc:12](../../goal_src/jak1/engine/debug/part-tester.gc#L12) | 85 |
| `default-menu` | [goal_src/jak1/engine/debug/default-menu.gc:20](../../goal_src/jak1/engine/debug/default-menu.gc#L20) | 2639 |
| `anim-tester-x` | [goal_src/jak1/pc/debug/anim-tester-x.gc:13](../../goal_src/jak1/pc/debug/anim-tester-x.gc#L13) | 474 |
| `entity-debug` | [goal_src/jak1/pc/debug/entity-debug.gc:11](../../goal_src/jak1/pc/debug/entity-debug.gc#L11) | 230 |
| `default-menu-pc` | [goal_src/jak1/pc/debug/default-menu-pc.gc:11](../../goal_src/jak1/pc/debug/default-menu-pc.gc#L11) | 774 |

## Desktop behavior when DebugSegment is zero

- [klink.cpp:203-208](../../game/kernel/jak1/klink.cpp#L203) clears the debug segment code-info offset and size instead of allocating/copying it. [klink.cpp:269](../../game/kernel/jak1/klink.cpp#L269) only processes relocations for a segment with a nonzero offset.
- [klink.cpp:64-69](../../game/kernel/jak1/klink.cpp#L64) handles an unloaded target segment by addressing GOAL zero: an address calculation yields null; a load/store through it can fault. The nearby comment explains that a null method install leaves the old method unchanged. The shared absolute relocation helper also explicitly chooses zero for an absent target at [common/klink.cpp:35-36](../../game/kernel/common/klink.cpp#L35).
- [kscheme.cpp:1738-1744](../../game/kernel/jak1/kscheme.cpp#L1738) sets `*debug-segment*` to the fixed false symbol when DebugSegment is zero, and to the fixed true symbol otherwise.

This inventories file declarations, not the size of their emitted debug segments or permission to drop an entire archive object. Mixed main/debug/top-level content and optional debug references still need compiler/linker analysis. It does not change Dreamcast loading or establish a memory saving. No new target or hardware validation is claimed.
