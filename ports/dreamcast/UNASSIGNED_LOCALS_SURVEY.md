<!-- Source-only review candidates; not an allowlist change (AI-assisted). -->
# Unassigned local candidates (AI-assisted)

Source baseline: `4480ccbab`. Run `python ports/dreamcast/survey_unassigned_locals.py --json` to regenerate the live inventory.

Scanned all 312 engine .gc files. Found 15 local candidates in 8 functions. This is lexical evidence, not a proof of definite assignment or a proposed automatic zero-fill policy.

The scanner uses the existing GOOS reader, ignores strings, examines semicolon and nested block comments, keeps conditional branches syntactically live, and separates nested functions. Explicit assignments and destination-first asm writes suppress a candidate; shadowing, register aliases and address-taking are uncertain. Unused declarations are also marked uncertain. It does not expand macros or establish reachability; multiline comment instructions split between lines and unknown setter macros need human follow-up. No runtime, original source or reviewed JSON is changed.

The existing `process-particles` entry is already reviewed in [dreamcast_unassigned_locals.json](dreamcast_unassigned_locals.json); it is retained below as a positive control. All other entries require typed IR/liveness and behavior review before any allowlist addition. No hardware or new Flycast validation is claimed.

| Source/function | Local | Status / live mentions | Commented-out setter |
| --- | --- | --- | --- |
| [goal_src/jak1/engine/gfx/background/subdivide.gc:276](../../goal_src/jak1/engine/gfx/background/subdivide.gc#L276) `end-perf-stat-collection` | `a0-1` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L283: `;;(.mfpc a0-1 pcr0)` |
| [goal_src/jak1/engine/gfx/background/subdivide.gc:276](../../goal_src/jak1/engine/gfx/background/subdivide.gc#L276) `end-perf-stat-collection` | `a0-3` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L286: `;;(.mfpc a0-3 pcr1)` |
| [goal_src/jak1/engine/gfx/sky/sky-tng.gc:692](../../goal_src/jak1/engine/gfx/sky/sky-tng.gc#L692) `render-sky-tng` | `v1-84` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L810: `; (.mov v1-84 vf23)` |
| [goal_src/jak1/engine/gfx/sprite/sparticle/sparticle.gc:436](../../goal_src/jak1/engine/gfx/sprite/sparticle/sparticle.gc#L436) `process-particles` | `gp-0` | candidate, 1; no explicit live setter; macro effects not proven | L442: `;;(.mfc0 gp-0 Count)` |
| [goal_src/jak1/engine/gfx/sprite/sparticle/sparticle.gc:436](../../goal_src/jak1/engine/gfx/sprite/sparticle/sparticle.gc#L436) `process-particles` | `v1-29` | candidate, 1; no explicit live setter; macro effects not proven | L469: `;;(.mfc0 v1-29 Count)` |
| [goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc:160](../../goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc#L160) `draw-drawable-tree-trans-tfrag` | `a0-35` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L267: `(.mfpc a0-35 pcr0)` |
| [goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc:160](../../goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc#L160) `draw-drawable-tree-trans-tfrag` | `a0-37` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L269: `(.mfpc a0-37 pcr1)` |
| [goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc:309](../../goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc#L309) `draw-drawable-tree-dirt-tfrag` | `a0-35` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L418: `(.mfpc a0-35 pcr0)` |
| [goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc:309](../../goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc#L309) `draw-drawable-tree-dirt-tfrag` | `a0-37` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L420: `(.mfpc a0-37 pcr1)` |
| [goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc:460](../../goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc#L460) `draw-drawable-tree-ice-tfrag` | `a0-35` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L557: `(.mfpc a0-35 pcr0)` |
| [goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc:460](../../goal_src/jak1/engine/gfx/tfrag/tfrag-methods.gc#L460) `draw-drawable-tree-ice-tfrag` | `a0-37` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L559: `(.mfpc a0-37 pcr1)` |
| [goal_src/jak1/engine/gfx/tie/tie-methods.gc:244](../../goal_src/jak1/engine/gfx/tie/tie-methods.gc#L244) `draw-drawable-tree-instance-tie` | `sv-16` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L305: `;; (set! sv-16 (-> (the-as drawable-inline-array-node v1-16) length)) ;; number of instances` |
| [goal_src/jak1/engine/level/level.gc:436](../../goal_src/jak1/engine/level/level.gc#L436) `level-update-after-load` | `current-timer` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L446: `;;(.mfc0 current-timer Count)` |
| [goal_src/jak1/engine/level/level.gc:436](../../goal_src/jak1/engine/level/level.gc#L436) `level-update-after-load` | `initial-timer` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L444: `;;(.mfc0 initial-timer Count)` |
| [goal_src/jak1/engine/level/level.gc:436](../../goal_src/jak1/engine/level/level.gc#L436) `level-update-after-load` | `v1-154` | uncertain, 0; no live use either; unused declaration, not a proven uninitialized read | L593: `; (.mfc0 v1-154 Count)` |
