# Jak and Daxter on Dreamcast: an experimental native port

**Help wanted.** Jak walks through the original game's levels in Flycast, and the
whole game is staged. To finish it, we need people who know Dreamcast hardware,
SH-4 code generation, KallistiOS/PowerVR, AICA sound or GOAL. If you have
an hour, a console or a good eye, the [open tasks](#help-wanted) are below.

This is an independent fork of [OpenGOAL](https://github.com/open-goal/jak-project)
working toward a native Dreamcast build of **Jak and Daxter: The Precursor Legacy**.
The original game code, decompiled by OpenGOAL, is compiled to SH-4 and runs
unchanged in 16 MiB alongside a PowerVR renderer written for the port.
It is not an official OpenGOAL, Sony or Naughty Dog project, and it is not
playable from start to finish yet.

The Dreamcast work is extensively **AI-assisted** (OpenAI Codex, Grok Build and,
since 2026-10-26, Claude Code). Every change is tested locally in Flycast, but
much of it has had no expert human review. Reviews are some of the most useful
help there is. See [AI_POLICY.md](AI_POLICY.md).

## Where it stands (2026-11-18)

All results are from **Flycast with a stock 16 MiB console**. The port has not yet
been seen running on real hardware ([why](#help-wanted)).

**What runs**

- **The original kernel and game code.** All 8 KERNEL.CGO and all 326 GAME.CGO
  files are compiled to SH-4 and booted in their original order. Each level's
  own code is compiled to SH-4 as well. The original dispatcher, processes,
  states, events, level loading, actor birth and camera run unchanged.
- **Original PS2 helpers.** All 24 mips2c files (collision, bones, particles,
  sky, ocean, shadows, text) run natively. Each one is checked case by case
  against the desktop build, matching bit for bit.
- **Rendering.** Terrain (tfrag) is drawn from a disc-streamed block cache,
  TIE and shrub instances by cell with their LODs and lighting, and Jak
  and the actors as skinned merc models built from the game's joints.
  Also drawn: the sea, translucent materials, blue-eco particles, and PowerVR
  textures resident in VRAM.
- **Play.** `jak-dc-level-play.elf` reads a real controller:
  - Jak walks, runs, jumps and collects orbs on Geyser Rock's original collision.
  - The tutorial cameras work.
  - Dialogue streams and plays on the AICA.
  - Jak walks from Sentinel Beach into Sandover across a seam.
- **Every level is staged.** `stage_level.py` builds a level's code, collision,
  art and render package from your own disc, plus the "seam" levels that join
  neighbours. The original keeps two levels resident; 16 MiB holds one whole
  level and a seam.

| Levels sent to their continue point from Geyser Rock | Result |
| --- | --- |
| Geyser Rock, Sandover, Sentinel Beach, Forbidden Jungle, Misty Island, Fire Canyon | Pass |
| Rock Village, Lost Precursor City (both parts), Boggy Swamp, Precursor Basin, Mountain Pass | Pass |
| Volcanic Crater, Snowy Mountain, Spider Cave's crystal and robot caves, Lava Tube, Citadel, final boss | Pass |
| Spider Cave (main cave) | Stops: the renderer slab fragments (1.42 of 1.54 MB) |
| Precursor temple (jungleb), title, intro | Not staged yet |

"Pass" means the level and its seam load and are active, the actors are born,
and Jak stands at the continue point with no fault. It does not mean the level
can be played through.

**What doesn't run yet**

- **Speed.** A frame on Geyser Rock takes about 63 ms of game logic and 45 ms
  of drawing, roughly 9 fps in Flycast, while moving into new ground costs more.
  The goal so far has been a complete port, and speed comes next.
- **Seams are collision only.** The neighbouring level's ground isn't drawn.
- **Sound.** Dialogue streams play, but sound effects, music and banks don't yet
  ([SOUND_PLAN.md](ports/dreamcast/SOUND_PLAN.md)).
- **Missing systems:** Daxter on Jak's shoulder (his art fails to load), HUD, menus, memory card saves, the PS2 renderers' effects
  (reflections, eyes, ocean waves, the sky), L2/R2 camera control, and the
  levels in the table above that aren't staged.
- **Testing.** The automated runs only send Jak to each continue point, so
  nobody has played a level through on this port yet.

The detailed engineering log is [LEVEL_PLAN.md](ports/dreamcast/LEVEL_PLAN.md).
Each section in it records what was measured, what was changed and what is
still missing. The October milestones (kernel, compiler, first renderer) are in
[PROGRESS_2026-10.md](ports/dreamcast/PROGRESS_2026-10.md).

The current suites, all in Flycast with 16 MiB: kernel 90 checks, the game in
one ELF with the renderer 871, the original player code 7,652, and the level
ELF 2,745.

## Help wanted

Each task can be done separately. Most of the compiler and runtime work needs no
game data. Please say which one you're taking in a
[discussion or pull request](https://github.com/Benybob/jak-daxter-dreamcast) so
nobody duplicates work.

| Task | Good for someone who knows | What done looks like |
| --- | --- | --- |
| **Boot it on a real Dreamcast** | Dreamcast hardware, GDEMU/MODE, CD-R self-boot | `make_boot_cdr.py` + `burn_cdr.py` build a disc whose TOC and sectors check out, but a 1999 US console plays it as a music CD. Run the level ELF from a GDEMU, a broadband adapter/serial loader, or a CD-R that boots, and report the results screen (it shows fps, memory and checks; see LEVEL_PLAN "A real-hardware test disc") |
| **Make it faster** | SH-4 code generation, register allocation, profiling | GOAL is lowered to SH-4 by `lower_sh4_ir.py`; logic is ~63 ms a frame on Geyser Rock. LEVEL_PLAN's "Where a frame goes" and "A sampling profile" sections list the hot paths. Any measured saving that keeps every suite passing |
| **Spider Cave's renderer slab** | Allocators, memory budgets | The first-fit slab in the level ELF fragments around Spider Cave's 4 m sectors. Make `maincave-start` pass in the same 1,540,096 bytes, or show what it needs |
| **Draw the seam's ground** | Rendering, level streaming | Draw the neighbouring level's terrain near the seam (LEVEL_PLAN "Seams") |
| **Sound effects and music** | AICA, ADPCM, the 989snd format | Milestones 2 to 7 of [SOUND_PLAN.md](ports/dreamcast/SOUND_PLAN.md): parse banks, then play one effect, then follow the game's commands |
| **Float arguments in spawned code** | GOAL calling conventions | Processes entered by `run-function-in-process` take floats in integer positions. `*-init-by-other` functions are handled; 59 anonymous functions with float parameters, and states entered with floats through `go`, are not |
| **HUD, menus, saves** | GOAL, PowerVR 2D, VMU | Draw the HUD and pause menu through the port's renderer; memory card saves to a VMU |
| **Play a level through** | Patience and Flycast | Build the play ELF, play one staged level, and report what breaks (with the results or crash screen) |
| **Review AI-written code** | Any of the above | Read a subsystem (the SH-4 lowering, `goal_bridge.c`, the renderer) and open a discussion on what is wrong or fragile |

Older and more specific compiler tasks are on the
[task board](ports/dreamcast/HELP_WANTED.md).

## Trying it

You need your own copy of the game: nothing from the disc is in this repository.

1. Build the SDK, KallistiOS, the desktop OpenGOAL tools and the port by
   following [BUILDING.md](ports/dreamcast/BUILDING.md).
2. Extract your disc with the OpenGOAL decompiler, then stage a level, for
   example `python ports/dreamcast/stage_level.py VI1`.
3. Build the play ELF and a GDI image, then open it in Flycast:

```
make jak-dc-level-play.elf
python make_boot_gdi.py jak-dc-level-play.elf --ip-template <kos>/utils/makeip/rsrc/templates/ip.tmpl --staged build/level-disc --require TRAINING.DGO --output build/play-gdi
flycast build/play-gdi/jak-arena.gdi
```

Controls: A jumps, the stick moves, the D-pad moves the camera, and
A+B+X+Y+Start ends the run on the results screen.

On Windows, keep the workspace in a short path such as `C:\dcw`. Deep paths go
over the 260-character limit.

**Please never upload or attach game data** (ISOs, BIOS, DGOs, extracted or
converted assets, or ELFs and GDIs built with them) to commits, discussions or
bug reports. The `build/` folders are git-ignored for that reason.

## Contributing

Start with [CONTRIBUTING.md](CONTRIBUTING.md). Keep changes small enough to
review, say what you tested and how, and state any use of AI tools. Discuss the
Dreamcast work on **this fork**. OpenGOAL's maintainers have not agreed to
support it, so please don't take it to them.

## Attribution and license

OpenGOAL and the port source keep the [ISC license](LICENSE) and existing
copyright notices; dependencies keep their own licenses. The original project's
overview is in [README_UPSTREAM.md](README_UPSTREAM.md), and the upstream AI
policy is in [AI_POLICY_UPSTREAM.md](AI_POLICY_UPSTREAM.md).
