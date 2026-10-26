# Playable cached terrain slice

2026-10-25. (AI-assisted). The native walking prototype now draws original
terrain through the cached world renderer. Jak walks, runs and jumps with the
existing native C controller and six original animation clips. This is a
temporary bounded gameplay slice; full original GOAL gameplay remains unfinished.

The current build adds [bounded decoded blocks](DECODED_BLOCK_CACHE.md), reducing
repeated reads/decodes while preserving this window/payload contract. That update
records the revised live-heap gate and measured allocation peak.

## Verified behavior

Stock 16 MiB Flycast passes 26 integrated checks: movement, jump edges, landing,
animation states, following/orbiting camera, boundary respawn, memory, exit,
multiple render-window commits, exact source-window counts/CRCs, a stable resident
texture owner and GPU cleanup. Captured idle, airborne, camera and boundary
frames were visually reviewed. Initial geometry is 3,593 actor triangles plus
3,443 unchanged terrain faces. The exercised replacement windows contain 3,819
and 3,879 faces. Seventy color textures request 392,192 VRAM bytes and remain
resident across replacements. Before decoded-block retention, observed idle live
heap was 3,591,936 bytes, excluding
embedded assets, executable/static storage, KOS and stacks. The TA vertex buffer
is 1 MiB. These are emulator observations, not physical hardware performance.

A separate original-free native oracle now passes 126 checks, including its
earlier 93: wrong source binding,
owned-output refusal, view reuse, invalid-camera readiness refusal, both frame
allocation failures, missing/trailing selected files, preservation of active CPU
and GPU owners, known-empty commits and idempotent cleanup. It compares saved
face/view bytes and returns to the primed free-VRAM baseline. The earlier fixed
terrain target still passes its 20 integrated regression checks. The original
base diagnostic also passes its 42 checks with an identical captured framebuffer.

## Ownership and coverage

`training_render.c` loads the catalog, color textures and CPU PVR headers once.
A replacement builds a separate geometry cache and view frame, prepares its
camera, waits for PVR, then replaces the old pair. Failure releases the candidate
only. A failed reuse-camera preparation clears frame readiness; the caller halts
before drawing. The frame borrows immutable source face bytes from its paired
window. Shutdown waits for PVR before freeing frame, window, bank, GPU and catalog.

The temporary selector requires a 32 m XZ square around the camera target inside
the loaded 36.8 m window. New window centers snap to a 4 m grid. This square is
not full-frustum or distant-world coverage. The existing collision/controller
bound remains +/-6.8 m locally, backed by the validated 18 m collision extent.
Synchronous window loading and preparation can hitch; no frame-rate claim is made.

The private bundle preserves the complete source catalog/index and global file,
plus every complete listed local file needed by reachable anchors. It does not
rewrite faces or silently replace missing listed files with empty cells. Every
reachable anchor is independently assembled from the full source and checked
against the partial bundle. The tested package copies 20 local files, binds 20
anchor oracles and occupies 3,357,214 bytes including actor/collision assets.
Reproduction matches all 28 package files and the private generated header exactly.
Its absolute 1 mm copy margin requires XZ origins within 4,096 m; larger origins
are explicitly refused until the rounding bound is generalized.

## Local build and test

Game files must come from a locally validated extraction. No converted files,
generated private headers, game images or embedding ELF binaries are distributed.
First build the host checks and prepare the earlier original-LOD training assets
and complete render package as described in [ORIGINAL_LODS.md](ORIGINAL_LODS.md)
and [RENDER_SECTORS.md](RENDER_SECTORS.md).

From this directory:

```sh
python prepare_training_render.py /private/training /private/render /private/stream --header build/training_render_fixture.h
make TRAINING_STREAM_ASSET_DIR=/private/stream jak-dc-training-stream.elf
python verify_training.py /path/to/flycast jak-dc-training-stream.elf --nm /path/to/sh-elf-nm --output /private/check --stream-bundle /private/stream/STREAM_BUNDLE.json --heap-budget 6291456
```

Windows SDK wrapper: `build_dreamcast.py --target jak-dc-training-stream.elf
--training-stream-assets /private/stream` with the configured workspace SDK.
Controls: analog stick moves (magnitude selects walk/run), A jumps, L/R orbit,
START exits. The regional boundary deliberately respawns Jak.

Original-free owner checks need no game files:

```sh
python make_training_owner_fixture.py /private/owner --header build/training_owner_fixture.h
make TRAINING_OWNER_ASSET_DIR=/private/owner jak-dc-training-render.elf
python verify_backend.py /path/to/flycast jak-dc-training-render.elf --nm /path/to/sh-elf-nm --output /private/owner-check --suite training-render --tests 126
```

Reflection, time-of-day shading, original normal/prototype tint fidelity, broader
streaming/LOD, original GOAL boot and gameplay, scheduling, GC, failure unwinding,
audio, progression and save systems remain unfinished. This milestone does not
establish full-game playability or physical Dreamcast validation.
