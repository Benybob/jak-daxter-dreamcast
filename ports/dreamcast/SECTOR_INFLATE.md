# Bounded compressed sector blocks

2026-10-25. (AI-assisted). Render streaming and full gameplay remain unfinished.

`dc_sector_inflate` decodes exactly one zlib stream into caller-owned staging
memory. It requires the exact compressed length, expected output length and
output CRC. It rejects trailing bytes, concatenated streams, truncation, incorrect
output size/CRC, gzip/raw-deflate framing and streams requiring a dictionary.
This is a codec utility; container validation and committing a world or GPU layer
remain the caller's responsibility.

Input is limited to 513 KiB and output to 512 KiB. The general decoder refuses
empty or overlapping buffers. `dc_sector_inflate_empty` separately validates one
complete zero-output stream without touching caller output memory. This supports
an empty global render bucket while local sectors still contain geometry.
Both entry points use the same framing checks and allocator. The private
allocator caps zlib workspace, including its own block
headers, at 64 KiB; multiplication is checked before allocating. Every per-call
allocation is released on initialization, decoding and cleanup failures. There is
no persistent decoder state. Output may be partly written on error, so it must
never point at an active world, layer or other committed data.

## Verification

274 host tests pass. The shared C oracle passes 158 checks on the host and in
stock 16 MiB Flycast. Synthetic fixtures exercise stored, fixed and dynamic
Huffman streams, incompressible 512 KiB output, highly compressed output,
truncated streams, allocation failures, framing/checksum errors, canaries,
overlap and size refusal. Empty streams cover stored/compressed framing,
truncation, trailing/concatenated data, nonempty input refusal and allocation
failure. Allocator tests exercise the exact workspace cap and
overflowing allocation requests. No game data is used.

Native decoding used at most 7,132 bytes of tracked workspace for the fixtures;
the host used 7,184 bytes. These exclude caller input/output buffers, stack and
system allocator overhead. These figures are not whole-game memory or frame-rate
measurements. The normal production object is compiled separately; the oracle
redirects allocations through a test-only wrapper of the same source.

The tested host zlib is 1.3.1 and the installed KOS zlib is 1.3.2, from the pinned
[KOS ports tree](https://github.com/KallistiOS/kos-ports/tree/f4faacc42faaf552625777b7709e871a827e1055/zlib).
Its upstream archive SHA256 is
`bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`.
The library retains the zlib license; it is not vendored into this repository.

## Build and use

Host builds need zlib development headers and a library. The MinGW wrapper uses
`include/zlib.h` and `lib/libz.a` from the selected compiler's installation. Native
builds need KOS ports zlib headers and `libz.a` in the configured `KOS_PORTS` prefix.
Use the same SDK and precision mode as the rest of the port (`-m4-single` here).
The standard KOS ports installation builds the dependency; this repository's SDK
bootstrap does not install it automatically.

```text
python ports/dreamcast/build_dreamcast.py --msys LOCAL_MSYS --target jak-dc-inflate.elf
python ports/dreamcast/verify_backend.py FLYCAST ports/dreamcast/jak-dc-inflate.elf --nm SH_ELF_NM --suite compression --tests 158 --output LOCAL_RESULTS
```

The fixture generator is `make_inflate_fixture.py`; its generated header stays in
the ignored build directory. CRC is a damage check, not authentication. A future
render loader must validate its own manifest, counts, material/texture references,
geometry and GPU lifetime before installing decoded data. Collision loading,
rendering and original GOAL runtime are still separate programs at this checkpoint.
