# Native VQ texture checkpoint

2026-10-21. AI-assisted implementation. The full game remains unfinished.

The existing DCT1 format now accepts texture descriptor format 2: twiddled
ARGB1555 full-codebook VQ. Format 1 remains uncompressed ARGB1555. The shared
host/SH-4 loader validates the exact bounded payload, dimensions and format.
PowerVR receives `PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_VQ_ENABLE`; no runtime
decompression or extra CPU pixel copy is needed.

The payload is a 2,048-byte codebook (256 vectors of four little-endian 16-bit
pixels) followed by one byte per 2x2 block in twiddled block order. Codebook
pixel order is (0,0), (0,1), (1,0), (1,1). The entire payload is padded to 32
bytes for the existing store-queue upload. This first profile accepts square
power-of-two sizes 8 through 512, no mipmaps. At 8x8 the full codebook costs
more than uncompressed pixels; VQ is an explicit offline option, not a universal
size improvement.

`host_vq.c` performs deterministic farthest-point seeding and eight integer
k-means updates. Alpha pattern mismatch has a penalty larger than all possible
RGB error, preserving all 16 alpha patterns. `vq_texture.py` verifies alpha
and records RGB5 error after independently decoding the produced payload.
This is lossy color compression. It adds no third-party Python dependency.

The complete training mesh 5 (`tra-grass`) has 7,784 triangles, 6,934 vertices,
four batches and 213,120 bytes of CPU geometry. Its one 128x128 texture shrinks
from 32,768 to 6,144 bytes (81.25%). RGB5 MSE is 0.536865 and PSNR 32.5286 dB;
no alpha bits change. These quality metrics compare the encoded texture with
ARGB1555 input, not the original higher-precision image.

Flycast 2.7, stock 16 MiB, reports free VRAM 4,335,272 bytes for the rebuilt
ARGB reference and 4,361,896 for VQ: exactly 26,624 bytes saved. Live CPU heap
after upload remains 390,584 bytes. Actual framebuffer captures were inspected;
the texture layout is correct at the viewer's scale. Both builds pass real
scene, orbit, zoom and cleanup checks. The VQ target rejects a payload shortened
by one byte with `short texture pixels`. Physical hardware is untested.

From the repository, in the documented surrounding workspace layout:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/convert_textured.py ../opengoal-release/data/decompiler_out/jak1/levels/training/training-background.glb ports/dreamcast/romdisk/scene.dct --mesh-index 5 --texture-size 128 --texture-format vq
python ports/dreamcast/build_dreamcast.py --target jak-dc-textured.elf
python ports/dreamcast/verify_target.py ../flycast/flycast.exe ports/dreamcast/jak-dc-textured.elf --nm ../dc-sdk/sh-elf/bin/sh-elf-nm.exe --output ../flycast-vq --triangles 7784
```

Use the system Python with Pillow for conversion. The PowerShell host wrapper
sets its own MinGW PATH for cc1 and resolves Python before doing so; use a fresh
shell or an explicit system Python path for commands after that wrapper.

Primary format reference: [KallistiOS texture flags](https://kos-docs.dreamcast.wiki/group__pvr__txr__fmts.html).
The pinned Flycast v2.7 source's `core/rend/texconv.cpp` supplies an independent
hardware-format cross-check. Palette conversion, texture streaming, mipmaps,
whole-level rendering and combined game/runtime memory measurements remain open.
