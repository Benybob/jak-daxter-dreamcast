# Collision sector foundation

2026-10-24. (AI-assisted).

The offline converter partitions the complete original training collision into
bounded files. Native collision now distinguishes a committed empty region from
missing data. The transactional native sector loader is now verified separately
in [SECTOR_CACHE.md](SECTOR_CACHE.md). This foundation checkpoint does not enable
full-level traversal or render streaming.

## Verified scope

- 249 host tests pass, including malformed packages and independent PAT literals.
- All 61,576 original faces and their exact payloads survive partitioning into
  572 sectors, with 83,200 records including boundary replicas.
- An independent polygon-clipping audit checks coverage at 572 sector-centered
  windows of width 36.8 meters. The largest sampled window contains 3,286 unique
  faces; this is sampled coverage, not a proof for every possible query.
- Repeated conversion produces byte-identical files. The largest sector contains
  1,044 faces, below the 2,048-face file limit.
- 81 native committed-air checks pass in stock 16 MiB Flycast: empty casts and
  movement, camera extension, gravity and rejection of uninitialized geometry.
- Rebuilt regressions pass: 2,063 original-patch collision checks, 1,205 camera
  checks and 20 integrated training movement/scene/camera checks.

The converter began as an isolated Grok proposal. Review corrected the PAT mode
shift to three bits, increased the file cap to cover measured sector density,
strengthened validation and added independent coverage checks. Converted game
files and native binaries embedding assets stay private.

## Bounded format and runtime contract

`partition_collision.py` consumes the DCR1 interchange described by the original
collision exporter. DCS1 version 1 uses a 108-byte manifest header, 20-byte index
entries and CRC32 footer. DCSS sector files have a 24-byte header, 44-byte records
(source ID plus the unchanged 40-byte face), and CRC32 footer. CRC detects damage;
it is not authentication. Parsers enforce exact EOF and fixed format limits.

Limits: 65,536 source faces, 4,096 occupied sectors, 262,144 replicas, 2,048 faces
per sector, 36 cells per triangle and 4–64 meter cells. Surface mode is
`(pat >> 3) & 7`; unsupported modes are rejected. Closed cell boundaries preserve
shared edges, winding and vertical projections.

The planned native cache holds at most 8,192 unique faces and retains the
existing 1,024-candidate query limit. A query spans at most 32.8 meters per axis,
with a two-meter guard on each side. Gathering must filter face bounds against
the requested coverage before deduplication and enforcing the cache cap: loading
all faces from neighboring sectors can exceed that cap. These are contracts for
the pending loader, not verified runtime streaming behavior.

`dc_collision_empty` explicitly commits known air. Its world has `ready == 1`,
zero faces and no triangle allocation. Failed construction and unloaded worlds
remain unready and reject access. Ready air permits falling and clear camera
queries; it cannot provide ground placement. `dc_collision_build` still rejects
an empty input. Every consumer must rebuild after the world-context layout change.

The next loader gate is now verified in SECTOR_CACHE.md, including allocation/IO
rollback and bounded original-data queries in Flycast. Rendering needs its own
sector pipeline and game integration. Full kernel startup and original gameplay
remain unfinished.
