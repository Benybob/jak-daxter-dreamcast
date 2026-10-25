# Resident original-background color textures

2026-10-25. (AI-assisted). The original terrain still uses the fixed-patch
prototype. This texture owner is a dependency for the future streaming renderer.
It does not draw geometry or implement environment reflections.

`render_gpu.c` uploads the validated DRM2 base **and specular color** maps once
to PowerVR VRAM. Its slots retain the CPU catalog's texture indices. Strength
U8 slots remain NULL on the GPU; their bytes stay in the CPU catalog for the
future reflection shader. No palette or CPU material data is changed.

The complete original lower-LOD training catalog has 70 color maps, totaling
**392,192 bytes**. All of them upload and read back exactly in stock 16 MiB
Flycast. All 27 strength maps (84,416 bytes) remain available on the CPU.
The wider source-preservation results are in [RENDER_SECTORS.md](RENDER_SECTORS.md).

## Ownership and verification

Zero-initialize `dc_render_gpu`. `dc_render_gpu_make` requires an initialized,
idle PVR and an immutable validated CPU catalog. It validates dimensions,
formats, sizes and alignment before allocating or touching VRAM. Owned output
is refused unchanged. Successful construction copies texture pixels; CPU
pixels are not consumed or borrowed after return.

Stage a replacement in a **different empty owner**. Allocation failure releases
only that candidate's partial CPU/VRAM allocations, leaving the active owner
and CPU catalog unchanged. After success, wait for the PVR frame that borrowed
the old textures before freeing that old owner. Geometry-window replacement
must never free either texture owner. NULL/repeated frees are safe for valid
zeroed or constructed owners.

The native oracle passes **370 checks** for the original catalog and **30**
for an original-free synthetic catalog. It tests every CPU/VRAM allocation
failure point while the old catalog stays live, compares every old texture
word after each refusal, validates a full replacement alongside the old owner,
then frees the old owner and checks the replacement again. CPU catalog bytes
remain unchanged throughout. Host instrumentation tests the same ownership
cases using synthetic inputs; it is not GPU emulation.

For the original catalog, two complete color copies use **784,384 requested
VRAM bytes** at peak, plus **776 bytes of CPU slot arrays** on SH-4. These
counts exclude PVR allocator overhead, CPU catalogs, test instrumentation,
static data, command buffers, TA/framebuffers, executable, KOS and stacks.
They are not a full-game memory or performance result. Physical Dreamcast
hardware remains untested.

Uploads use KOS `pvr_txr_load`, which uses its SH-4 store-queue path. The pinned
SDK implementation permits aligned CPU words and allocates VRAM on 32-byte
boundaries; every catalog texture has a byte count divisible by 32. The
independent native oracle reads uncached 32-bit VRAM words back and compares
them with the source. No DMA completion or GPU frame is assumed from a host
mock. SH4ZAM continues to provide the existing view/projection path; this
texture-copy layer requires no matrix operations.

## Reproduce

The normal host build includes `render-gpu-check` and its synthetic unit tests.
KOS zlib is needed by the DRM2 reader. A fresh default native build creates
an original-free catalog:

```sh
make jak-dc-render-gpu.elf
```

Explicit private preparation:

```sh
python make_render_gpu_fixture.py /private/new-gpu-fixture --manifest /private/render-package/manifest.drm
make GPU_CATALOG_ASSET_DIR=/private/new-gpu-fixture jak-dc-render-gpu.elf
```

`verify_backend.py --suite gpu-catalog --tests 30` checks the default synthetic
catalog; the measured original catalog uses `--tests 370`. Other catalogs have
`5 * color_count + 20` checks. The fixture's host report records that count.
Windows `build_dreamcast.py` accepts `--gpu-catalog-assets`.

Changing asset-directory arguments may select **older** files. Make now always
refreshes private ROM disks, preventing a timestamp-valid old packet from
silently remaining embedded after a directory change or file removal. Switching
from the original catalog to the older synthetic directory and back is verified
by the native 30/370 counts and exact readback. This rebuilds private ROM-disk
objects on each requested target build; it does not relink the Windows frontend.

Generated manifests and ELFs embedding original textures stay private. The next
gates are the bounded transactional geometry cache, SH4ZAM view/clipping and
PowerVR material submission, followed by reflection rendering and integration
with collision, camera and movement. Full original GOAL gameplay remains open.
