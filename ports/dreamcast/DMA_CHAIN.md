<!-- (AI-assisted), ISC license; see the repository LICENSE. -->
# Bounded DMA source-chain walker

This is a C99, caller-owned, allocation-free reader. It has no global variables,
platform dependencies, C++ dependencies, or renderer integration. It exposes
bytes and does not execute DMA, VIF, VU, GIF, GS, or PowerVR commands. Host tests
are synthetic; they establish no behavior on real Dreamcast hardware.

## Desktop sources and mapping

The reference follower is `common/dma/dma_chain_read.h` (not the older suggested
`game/graphics/dma/dma_chain_read.h`). Tag definitions are in `common/dma/dma.h`;
GIF/GS definitions are in `common/dma/gs.h`. `display-sync` in
`goal_src/jak1/engine/draw/drawable.gc` passes its calculated DMA buffer to
`__send-gfx-dma-chain`. These files are unchanged.

`dc_dma_view {base, addr, size}` maps the GOAL interval `[addr, addr + size)` to
`base[0..size)`. The caller must actually supply that many readable bytes and
keep them valid and unchanged for the iteration. Address arithmetic uses 64-bit
intermediates; it never wraps a 32-bit GOAL address. A host buffer need not be
aligned: little-endian words are assembled byte by byte. GOAL tag/data addresses
must be 16-byte aligned. The view itself need not start at an aligned address.
For native addresses, supply the corresponding C pointer and its numeric GOAL
address; do not use an unbounded address-space cast in place of a real view.

Initialize a caller-owned `dc_dma_follower` with `dc_dma_follower_init`: zero
means success. Each `dc_dma_follower_next` returns one transfer (1), end (0), or a
negative error. The terminal tag itself yields a transfer before end is returned.
Errors are sticky until reinitialization; an error leaves the output unchanged.
The follower copies the view descriptor, not its memory. Do not edit private
state. The one configurable field is `limit`, set after initialization and before
iteration; it defaults to 65536 transfers. Zero permits no transfers.

## Tag layout and transfer semantics

Each source-chain tag occupies 16 bytes:

- Bytes 0..7: raw 64-bit little-endian tag. QWC is bits 0..15, ID is bits 28..30,
  IRQ is bit 31, ADDR is bits 32..62, and SPR is bit 63. Bits 26..27 are PCE.
  IRQ, PCE and other unused bits remain in `raw_tag` without interpretation.
- Bytes 8..11 and 12..15: VIF words `vif0` and `vif1`, returned separately.
- The transfer payload contains `QWC * 16` bytes, excluding the tag and VIF
  words. `data_addr` is its GOAL address; `data` points into the supplied view.

Let T be the tag address, N = T + 16, L = QWC * 16, and A = ADDR:

| ID | Name | Data address | Next tag / action |
| --- | --- | --- | --- |
| 0 | REFE | A | T + 16; finish after this transfer |
| 1 | CNT | N | N + L; require A = 0 |
| 2 | NEXT | N | A |
| 3 | REF | A | T + 16 |
| 4 | REFS | A | T + 16 |
| 5 | CALL | N | Push N + L; jump to A |
| 6 | RET | N | Pop next tag address |
| 7 | END | N | Finish; current tag remains T |

This matches `DmaFollower::read_and_advance`, including inline data on CALL,
RET and END, REFE termination, the CNT address assertion, and rejection of SPR.
It is the desktop follower's traversal model, not a full PS2 DMAC emulator:
IRQ/PCE do not interrupt or change execution, and REFS does not model stalling.

CALL/RET use exactly two return-address slots. CALL stores the address after its
inline data; RET transfers its own inline data before returning. A third active
CALL fails, and RET without a saved return address fails. END and REFE terminate
even inside a called chain, as in the desktop follower.

## Refusals and bounds

| Result | Meaning |
| --- | --- |
| `DC_DMA_ADDRESS` (-1) | Tag or payload outside the view, truncated tag/payload, overflowing view interval or unrepresentable address arithmetic |
| `DC_DMA_ALIGNMENT` (-2) | Tag or payload address is not a multiple of 16 |
| `DC_DMA_STACK_OVERFLOW` (-3) | Third active CALL |
| `DC_DMA_STACK_EMPTY` (-4) | RET with no active CALL |
| `DC_DMA_UNKNOWN_ID` (-5) | Decoded ID outside 0..7 |
| `DC_DMA_LIMIT` (-6) | Transfer count reaches the configured limit; breaks cycles |
| `DC_DMA_SCRATCHPAD` (-7) | SPR bit set; unsupported by the desktop follower too |
| `DC_DMA_CNT_ADDRESS` (-8) | CNT has nonzero ADDR |
| `DC_DMA_ARGUMENT` (-9) | Null required argument or null/empty view |
| `DC_DMA_BUCKET_LAYOUT` (-10) | Unexpected Jak 1 default-register prologue or termination before the expected next bucket boundary |

All eight three-bit IDs are defined: an unknown ID cannot be encoded in a raw
PS2 tag. `dc_dma_tag_id_check` accepts external decoded IDs and returns -5 for an
out-of-range value. Tests cover that guard and all eight accepted IDs; the
follower's defensive unknown-ID branch is unreachable for an intact raw tag.
Bit 31 is IRQ, not a fourth ID bit; rejecting it as an unknown ID would be wrong.

Bounds are checked before reading bytes or forming a payload pointer. A
zero-byte payload can point one-past the view, provided its GOAL address fits in
32 bits. Its pointer must not be dereferenced. Successor tag addresses are checked
on the next call, before being read, so a valid transfer preceding a bad jump is
still returned. Alignment is checked before range when both are invalid.
This API cannot verify that the caller's pointer actually owns the stated size.

## Jak 1 bucket iteration

`OpenGLRenderer::dispatch_buckets_jak1` establishes the following convention:

1. At frame start T, a CALL with both tag VIF words zero jumps to default GS regs.
2. That target starts with CNT of QWC 10 (160 bytes), then RET of QWC 0.
   The renderer reads fog color at payload byte 144.
3. Returning must land at T + 16, the first bucket slot. Each following bucket
   boundary is another 16 bytes further in this top-level slot table.
4. A bucket renderer follows transfers until `current_tag_offset()` equals the
   next slot address. `DirectRenderer::render` uses that exact equality.

`dc_dma_bucket_iter_init` validates and consumes those three prologue transfers.
`dc_dma_bucket_iter_next` then returns transfers annotated with a zero-based
bucket ID until the requested number of slot boundaries is reached. Pass 70 for
the current Jak 1 `BucketId::MAX_BUCKETS` in
`game/graphics/opengl_renderer/buckets.h`. The count is explicit rather than a
second hardcoded bucket-name table. Prologue transfers count toward the follower
limit. Structural CALL/RET and zero-size transfers remain visible. The helper
uses address equality, not a presumed END tag or a scan of payload contents.
It does not validate bucket-specific VIF/GIF layouts, classify draws, copy fog,
perform visibility callbacks, or reproduce renderer-specific handling of calls
back into the default-register buffer. Jak 2/3 use different entry conventions
and are not supported by this helper. The final trailing frame data is left
unconsumed, matching the desktop dispatcher's ending-data TODO.

## Future PowerVR consumer

A renderer could use the bucket IDs to choose handlers and feed each transfer's
two VIF words plus payload into a VIF interpreter. That layer would maintain VIF
state, unpack VU inputs, reproduce relevant VU programs, and parse DIRECT/GIF
packets using the layouts in `gs.h`. It would translate resulting primitives,
textures and GS render state to PowerVR submissions. DMA payload bytes are not
PowerVR commands and cannot be submitted directly. This reader implements none
of that translation and is not wired to `__send-gfx-dma-chain`.

## Checks

From the repository root:

```sh
python -m unittest discover -s ports/dreamcast/tests -p 'test_dma_chain.py'
```

The host test builds `dma_chain.c` and `host_dma_chain.c` with GCC, C99,
`-pedantic -Wall -Wextra -Werror -O2`, then runs synthetic chains and checks
fields, pointers, nesting, errors, termination and bucket boundaries. It skips
when GCC is absent. The separate SH-4 syntax check uses
`sh-elf-gcc -std=c99 -O2 -fsyntax-only -m4-single -ml dma_chain.c`, resolving the
compiler from PATH or `C:/dcw/dc-sdk/sh-elf/bin/sh-elf-gcc.exe`. It skips if absent.
A syntax check does not establish linking, runtime operation or hardware results.
