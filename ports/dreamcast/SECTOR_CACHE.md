# Native collision sector cache

2026-10-25. (AI-assisted). Full gameplay remains unfinished.

`sector_cache.c` loads bounded collision regions from DCS1 packages on the
Dreamcast. It validates selected files and their complete records, deduplicates
boundary replicas, sorts by source ID and rebases into the capsule world.
The current training game target still uses its earlier fixed patch. Render
streaming and controller integration are the next gates.

The initial C proposal came from authorized Grok Build collaboration. Independent
review and tests corrected ignored close failures and non-finite committed
coverage checks. No original game assets are present in this source.

## Verification

- 269 host tests pass, including 20 cache tests with literal fixtures, re-signed
  malformed records, every byte truncation of small files, duplicate payloads,
  boundary inclusion, negative cells and rebase limits.
- Forced failures at all four retarget allocations, interrupted reads and close
  failures leave the active cache unchanged, with no tracked allocation/file leaks.
- A full 8,192-face cache succeeds; an 8,193rd unique face refuses the replacement.
- Stock 16 MiB Flycast passes 4,020 native checks across 572 original training
  windows, including reuse and failure injection. Rebased geometry payload CRCs
  match independent expectations. Together the windows exercise all 61,576 source
  faces. The largest sampled cache holds 3,286 faces.
- Peak tracked geometry payload is 889,764 bytes in that run. This excludes
  allocator overhead, stdio, stacks, executable/static storage, embedded ROM disk,
  rendering and other game systems. It is not a full-game memory measurement.
- The normal production object also compiles with installed SH-4 GCC. Native
  failure tests use a separate object from the same source with allocator/stdio
  calls redirected to test wrappers. Production objects contain no test wrappers.

CRCs detect damage and compare payloads; they are not authentication. These
results establish collision loading, not gameplay, rendering performance or
physical hardware validation.

## Ownership and access

Zero the index before loading; free an existing index first. The caller owns and
closes the manifest `FILE`. A successful index owns its entry array. Keep the
validated index and package immutable while a cache uses them.

Zero the cache before its first retarget. Successful replacement installs the
new world, origin and coverage together, then frees the old world. Failure leaves
all active fields unchanged. Serialize queries and retargets; retain no pointers
into replaced worlds. This is collision storage, not a GOAL execution context.

Call `dc_sector_covers` before using a query region. Bounds are world meters,
at most 32.8 meters per axis, expanded by two meters per side. Closed cell ranges
handle negative coordinates and boundaries. An absent entry inside the manifest
halo is known air. Missing listed files, invalid files or regions beyond the halo
fail. Known air commits explicitly through `dc_collision_empty`.

Reuse requires matching package CRC and a query inside committed coverage. It
does no IO/allocation and **keeps the committed origin**, even if a different one
was requested. Translate collision positions using `cache.origin`. The existing
1,024-candidate query limit still applies; overflow fails rather than dropping faces.

The cache holds at most 8,192 unique faces; each file has at most 2,048 records.
Gathering filters face bounds against coverage before enforcing the unique cap.
Maximum old/new caches, staging rows, IDs, hash table and index total 1,720,320
bytes (about 1.64 MiB) of geometry payload, plus stack/stdio/allocator overhead.
Retarget uses synchronous IO and allocation at controlled loading points, never
inside a collision query.

## Reproduce

Build host tools using [BUILDING.md](BUILDING.md). Host tests prepare an
original-free fixture and run the shared C oracle. For native synthetic testing:

```text
python ports/dreamcast/make_sector_fixture.py PRIVATE_FIXTURE
python ports/dreamcast/build_dreamcast.py --msys LOCAL_MSYS --target jak-dc-sector-cache.elf --sector-assets PRIVATE_FIXTURE
python ports/dreamcast/verify_backend.py FLYCAST ports/dreamcast/jak-dc-sector-cache.elf --nm SH_ELF_NM --suite sectors --tests 44 --output LOCAL_RESULTS
```

For original local data, produce DCR1 with the collision exporter, then:

```text
python ports/dreamcast/partition_collision.py TRAINING_DCR PRIVATE_SECTORS
python ports/dreamcast/make_sector_oracle.py TRAINING_DCR PRIVATE_SECTORS PRIVATE_ORACLE
python ports/dreamcast/build_dreamcast.py --msys LOCAL_MSYS --target jak-dc-sector-cache.elf --sector-assets PRIVATE_ORACLE
```

The preparer validates every sector record against the source and prints the
expected check count (`7 * windows + 16`). Pass it to `verify_backend.py --suite
sectors --timeout 180`. This is a payload-membership/rebase oracle. Independent
offline spatial-coverage checks are described in
[COLLISION_SECTORS.md](COLLISION_SECTORS.md).

Original-data directories and embedding ELFs remain private. No discs are
prepared. Full render streaming, game traversal, original GOAL gameplay, complete
kernel startup, audio, UI, progression and physical hardware testing remain open.
