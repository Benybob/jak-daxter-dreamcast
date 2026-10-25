# Transactional background geometry windows

2026-10-25. (AI-assisted). This CPU cache is a dependency for the streaming
renderer. The playable prototype still renders a bounded fixed patch.

`render_cache.h/.c` loads a closed XZ query from a validated immutable DRM2
catalog and local DRS2 files. The source SHA must match the catalog. A window
owns exact 116-byte face copies, its root and source identity; catalog pixels
and resident GPU textures have independent owners. Zero-initialize the window.
One serial owner may retarget/free it; no concurrent file/catalog mutation.

Every listed cell touching the query is selected, plus the global bucket.
Queries span at most 36.8 m per axis, with 1 mm absolute endpoint rounding
tolerance; inclusion itself uses exact closed binary64 comparisons. Each file
is loaded through the bounded reader and closed before opening the next one.
Missing selected files fail, while an empty covered query commits valid air.

The cache validates **all selected replica payloads before filtering**, including
replicas both outside the query. Equal IDs in global and local buckets fail.
Source-ID batches of 8,192 permit overfetch larger than the result cap without
allocating arrays proportional to the complete source. The returned faces are
source-ID sorted, unrebased and unchanged, capped at 8,192 after filtering.
No face splitting, material dropping or lower hidden cap is used.

Retarget stages a replacement while the old window remains usable. Allocation,
open/read/close, CRC, replica and cap failures preserve the old struct and owned
bytes. Same identity plus a query inside loaded coverage reuses the larger
window unchanged. Files changed under an unchanged identity are intentionally
not re-read on that path. Catalog/root/files must remain immutable during calls.

## Verification and limits

354 host tests pass. All **572 original training windows** match independently
assembled face counts and payload CRCs on SH-4 in stock 16 MiB Flycast.
Five disjoint native runs pass **2,813 checks**; the six synthetic fixtures
and fault checks repeat at each boot. The largest original result has 7,447
faces. Peak instrumented cache/reader/catalog allocations are **3,935,850 bytes**,
including old and staged geometry; this excludes the zlib private pool,
allocator overhead, bridge context/static data, executable, KOS/stacks and GPU.
It is not a full application memory budget.

Synthetic coverage includes 20,480 selected unique IDs with only one included,
over-cap refusal, both-filtered mismatches, whole 1,897 m global faces, known
air and every measured allocation/open/read/close failure. Host tests also
cover negative closed corners, identity changes and coverage reuse.

The worst structural bound is 122 selected files, 61 nonempty ID batches and
up to 15,006 opens (including a possible final empty selection). This prioritizes
bounded validation, **not demonstrated frame-time streaming performance**.
The initial single full native sweep exceeded the 300-second verifier limit;
splitting the same unchanged 572 oracles into bounded runs completed verification.
Scheduling/prefetch and real renderer integration still need measured work.
No physical Dreamcast testing or full-game playability is claimed.

## Reproduce without original assets

Run the normal host test suite. For the native synthetic target, from this folder:

```sh
python3 make_render_cache_fixture.py build/private-render-cache-assets --header build/render_cache_fixture.h
make jak-dc-render-cache.elf
python3 verify_backend.py /path/to/flycast jak-dc-render-cache.elf --nm /path/to/sh-elf-nm --suite render-cache --tests 103 --timeout 300 --output build/cache-verification
```

Use a fresh fixture directory/header or remove only your generated fixture first.
`--real` plus `--windows` optionally embeds privately prepared data and independent
window oracles. `--reuse --windows ... --first-window N --window-count C`
regenerates only a chosen oracle header for an existing private bundle. Never
publish original data, generated private headers, ROM disks or their embedding ELFs.
See [RENDER_SECTORS.md](RENDER_SECTORS.md) and [RESIDENT_TEXTURES.md](RESIDENT_TEXTURES.md).
