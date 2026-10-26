# Bounded decoded terrain blocks

2026-10-26. (AI-assisted). The playable native training target now retains
validated, immutable whole terrain blocks under a **2 MiB raw-payload cap**.
This reduces repeated loading without changing the selected geometry. The
full game and original GOAL gameplay remain unfinished.

## Verified result

The same scripted stock 16 MiB Flycast workload performs **10 complete block
loads/decodes instead of 200** across five render-window commits. Captured window
anchors, face counts and exact source-payload CRCs match the earlier run. The
normal playable build passes all 26 movement/streaming checks. Idle, airborne,
camera and boundary captures were visually reviewed; the idle framebuffer is
byte-identical to the previous checkpoint. This is a reduction in measured load
work, not a claim about physical Dreamcast speed or full-game frame rate.

The owner suite passes **126 native checks**, retaining its earlier 93 and adding
explicit small-budget/bypass/eviction checks, invalid-source refusal, exact payload
comparison and missing-file rollback. All **379 host tests** pass. New host cases
include 140-cell pressure against 128 slots, both cache passes, adjacent windows,
all existing replica/role/filter/multibatch fixtures, open/close failures,
metadata allocation failure, malformed files and nested-borrow refusal. The older
fixed-terrain target still passes 20 checks with its default 4 MiB live-heap gate.

## Explicit ownership

`dc_render_window_retarget` retains its direct-file behavior. The new
`dc_render_window_retarget_source` takes an explicit validated-block source.
It still compares complete replicas and source roles before filtering faces,
including records outside the query. It still commits geometry transactionally.

`render_block_cache.c` implements that source for an immutable catalog/root/file
set. The first miss uses the existing full reader: exact catalog entry, framing,
compressed/raw CRCs, every vertex/record check, EOF and successful close. A hit
borrows the same validated bytes. A single load/release pair may be outstanding;
empty global blocks also participate in that rule. Release clears the borrowed
descriptor. A failed load is never cached as a known-empty block.

The owner has 128 LRU slots and a configurable retained raw-byte budget between
one byte and 2 MiB. A block larger than that budget bypasses retention and is
fully validated normally. Rank updates are bounded by the slot count. Eviction
frees whole blocks. Active geometry windows have separate private face copies,
so eviction cannot invalidate a frame or GPU texture. Cache warming and eviction
may survive a failed geometry transaction; the active frame/window remains intact.

The catalog descriptor/storage identity, source SHA and root are bound at make.
They and the files must remain unchanged for the owner lifetime. A cached file
is not reread to detect external edits. Mutable filesystem clients must continue
using the direct reader or create a fresh owner. Shutdown frees blocks before
the catalog they reference. No GPU ownership is added.

## RAM accounting

The raw-byte cap excludes fixed slot metadata, allocator overhead, the transient
reader's raw/compressed/codec workspace, old/candidate geometry and view frames,
and other game resources. It is not a total-RAM budget. No 32 MiB emulator mode
is used.

Per-frame sampled live heap ranges from 4,621,408 to 4,704,240 bytes in the checked
playing phases. The cached target now uses an explicit **6 MiB sampled live-heap
gate**; the previous 4 MiB gate remains the verifier default for earlier targets.
A separate native allocation probe observes malloc/calloc/realloc/memalign call
sites while running the same 26 checks. It reports:

| Observation | Bytes |
| --- | ---: |
| Highest observed live allocation total, including staging | 6,477,168 |
| Allocator arena high-water | 7,306,536 |
| Space above highest observed program break to 16 MiB RAM top | 5,591,040 |

No nonzero allocation request failed. The linked executable/ROMdisk end lies
below the observed heap base, so program-break accounting includes their address
range. Address headroom is not all free RAM: stacks and system reservations still
apply. The probe enforces an 8 MiB allocation/arena peak gate and at least 4 MiB
of address headroom. Physical Dreamcast validation remains pending.

## Build and verification

Use the private preparation and build steps in [TRAINING_STREAM.md](TRAINING_STREAM.md).
The cached playable verifier explicitly requests its revised steady-state gate:

```sh
python verify_training.py /path/to/flycast jak-dc-training-stream.elf --nm /path/to/sh-elf-nm --output /private/check --stream-bundle /private/stream/STREAM_BUNDLE.json --heap-budget 6291456
```

The original-free owner target now requires `--suite training-render --tests 126`.
Host tests build through the normal host target/Windows helper. No game files or
embedding ELF binaries are distributed with source.

Window changes remain synchronous and can hitch. Incremental preparation and
broader streaming/LOD remain future work. This cache does not supply reflection,
time-of-day shading, original normals/prototype tint, original GOAL startup and
gameplay, scheduling/GC/unwinding, audio, progression or saves.
