# Original background render package and native CPU reader

2026-10-25. (AI-assisted). Grok supplied an isolated converter proposal; Codex
reviewed it, corrected it and independently verified the original data. The
format remains experimental. Full render streaming and gameplay are unfinished.

`prepare_render_sectors.py` converts a static GLB into a DRM2 catalog and
compressed DRS2 geometry files. `render_package.c` reads the catalog and one
bounded geometry file at a time on the host and SH-4. It does not upload GPU
textures, assemble a native render window or replace the game's fixed patch.
The existing movement prototype continues to use SH4ZAM projection separately.

## Verified source preservation

The complete original training background at original tfrag LOD2 / tie LOD3 has
140,166 triangles. No new mesh simplification, face clipping or triangle dropping
is used. Active scene traversal, world transforms, index order, UVs, vertex
color/factor baking and degenerate faces are preserved. Supplied normals use the
inverse transpose; missing normals use the wound world-space face normal after
float32 position conversion. Zero-area faces retain zero normals.

The private GLB has 117 material definitions, 116 used by the active scene.
Equivalent complete material states deduplicate to 107 records. The package
contains 97 textures (70 color and 27 strength), totaling 476,608 pixel bytes.
Every original base/specular/strength reference, scalar factor, double-sided
state, sampler wrap and supported filter is checked against its packaged state.
The independent host oracle also compares all geometry, UVs, colors and normals
and validates all replicas. No game files are included in this repository.

The full package has 3,158 local sectors, 180,757 local placements and 1,366
global faces. Storage is 7,899,260 bytes, including a 586,324-byte catalog.
The old 6 MiB storage limit correctly refused this input before publication;
the measured storage cap is now 8 MiB. **Storage size is not resident RAM.**
The 4,096-face/file, 8,192-face/window and 2 MiB pixel limits were not raised.

At all 572 training collision grid centers, independent triangle/box SAT checks
find no missing intersecting faces for a 36.8 m XZ query. The host window oracle
selects at most 7,447 complete faces (863,852 bytes of package records). These
are sampled all-Y windows, not proof that every location, camera frustum or
combination of old/new CPU windows fits. No frame rate or hardware claim follows.

## Material behavior

- Base color is twiddled ARGB1555. The original PS2 compensation factor
  `[2,2,2,2]` is retained in the round/clamp vertex bake. MASK uses the original
  double cutoff for pixel identity, even when two cutoffs have equal float32
  metadata. OPAQUE ignores alpha before resampling, preventing premultiplied
  image resizing from destroying RGB that must remain visible.
- `KHR_materials_specular.specularColorTexture` is retained as opaque color,
  including its factors and sampler. Its source alpha is removed before resize.
- The OpenGOAL metallic-roughness image stores environment strength in B.
  Every **source** pixel must have R=255, G=0 and A=255 before resampling; an
  isolated unsupported pixel cannot disappear in downsampling. Twiddled U8
  retains all eight strength bits. This is CPU data, not a claimed PVR format.
- Double-sided is base flag 64; nearest is flag 128, independently per texture
  role. Matched LINEAR or NEAREST min/mag filters are supported; mixed or
  mipmapped filters fail. Base MASK/unlit bits are 1/2; clamp S/T bits 4/8;
  mirror S/T bits 16/32. Clamp and mirror conflict on an axis. Auxiliary roles
  accept only sampler flags. Missing auxiliary maps are `0xffffffff`, flags 0.
- Unknown meaningful material extensions, material normal/emission/occlusion
  maps, BLEND, skins, morphs, non-triangle modes, nonzero UV sets and texture
  transforms fail. Tangents are unused because this path has no normal map.
  Unknown required extensions fail. glTF material factors/defaults are stored.

## Partition and framing

World positions are absolute float32 meters, bounded to +/-1,000,000; UVs to
+/-65,536. Cells are closed XZ rectangles (default 16 m; permitted 4..64 m).
Triangle/cell membership uses closed SAT, retaining whole triangles. Faces with
any axis span over 64 m, more than 36 replicas or a pathological/empty local
membership go into the global bucket once. They are not discarded or split.
Global and local source-id sets are disjoint and their union is the source.

The host window oracle compares every selected replica's full payload before
filtering local/global face AABBs. The 8,192 cap is applied after filtering.
A filtered-first disagreement still fails. This oracle uses host dictionaries;
a bounded native cache must implement that contract separately.

All files are little-endian. Manifest/header/table sizes are checked, including
compiled caps, strides, reserved words, exact EOF, CRC, table coverage, sector
order and package totals. The manifest has a 256-byte header, 16-byte texture
directory entries, contiguous padded pixels, 64-byte materials and sorted
32-byte local entries. A final u32 CRC covers every preceding byte.

Key header offsets (all u32 unless noted):

| Offset | Field |
| --- | --- |
| 0 / 4 / 8 / 12 | `DRM2` / version 2 / header 256 / absolute-position flag 1 |
| 16 / 20 / 24 / 28 | source / local sector / local replica / global counts |
| 32 / 36 / 40 / 44 | material / texture counts / pixel bytes / storage bytes |
| 48 / 52 | f32 cell size / f32 local-span threshold 64 |
| 56..96 | exact caps: 4096,4096,8192,36,1000000,8192,2000000,256,256,2097152,8388608 |
| 100 / 104 | face 116 / vertex 36 |
| 108..120 | signed ix/iz min/max; zero for no local sectors |
| 124..144 | f32 min xyz, max xyz |
| 148..160 | global raw bytes / file bytes / raw CRC / compressed CRC |
| 164..195 | source GLB SHA-256 |
| 196 / 200 / 204 | padded pixel bytes / table bytes / observed largest local count |
| 208 / 212 / 216..255 | material stride 64 / normal encoding 1 / zero reserved |

Texture entries are `<IIHHI>`: pixel offset/bytes, width/height, format (1
ARGB1555, 2 U8 strength). Power-of-two edges 8..64. Material offsets:

| Offset | Field |
| --- | --- |
| 0 / 4 / 8 / 12 | base texture / flags / f32 cutoff / alpha mode 0 or 1 |
| 16 / 20 / 24 / 28 | strength texture / flags / f32 metallic / roughness |
| 32 / 36 / 40 / 44..52 | specular color texture / flags / f32 specular / RGB factors |
| 56..63 | zero reserved |

Local entries are `<iiIIIIII>`: ix/iz, count, raw bytes, file bytes, raw CRC,
compressed CRC, zero reserved. A sector has a 32-byte `<4sIIiiIII>` header:
`DRS2`, version 2, global flag (0 local, 1 global), ix/iz (0 for global),
count, raw bytes, compressed bytes. The exact zlib member follows, then raw
and compressed CRCs (8 bytes). Exact file length is compressed bytes + 40.

One face is source ID + material ID + three `<8fI>` vertices: xyz, uv, normal
xyz, ARGB. A full file is at most 475,136 raw bytes and 476,160 compressed
bytes. `dc_sector_inflate` checks exact input/output, end-of-stream and CRC
inside a 64 KiB workspace cap; the separate exact-empty helper handles a zero
global bucket. A local file cannot be empty. See [SECTOR_INFLATE.md](SECTOR_INFLATE.md).

## Native reader ownership

Zero-initialize `dc_render_catalog` and `dc_render_block`. Owned output is
refused unchanged. Failure frees temporary reader allocations and leaves an
unowned output zero. Free functions are idempotent. Texture pixels returned
by getters borrow the catalog's storage; free the catalog only after all
borrowers have finished. The reader never closes the caller's FILE. Check
`fclose` before committing a replacement. There are no callbacks or yields.

Block entries must exactly match the loaded catalog and explicit local/global
role. The reader checks IDs/order, finite bounded positions/UVs and unit-or-
exact-zero normals. Normal zero detection uses float bits so an SH-4 denormal
flush mode cannot disguise a malformed nonzero normal. Missing/corrupt data
must never be interpreted as a successfully loaded geometry window.

The native oracle reads every original sector, checks an independently verified
host ledger and accounts for all source IDs. Its source-sized CRC/seen tables
are **diagnostic only**, not the future bounded cache. Reader peak allocation
is measured separately from those tables, decoder workspace, embedded ROM disk,
static executable, KOS, stacks, GPU state and allocator overhead.

Stock 16 MiB Flycast passes **9,578 native checks**, including all 3,158 original
local files plus the global bucket and 44 synthetic acceptance/refusal/failure
cases. Peak reader-owned allocation is **960,738 bytes**. The diagnostic source
tables add 578,185 bytes; these do not belong in a gameplay cache. The existing
decoder has a separately measured 7,132-byte normal workspace peak. These
figures do not prove a complete game memory budget or GPU performance.

## Reproduce

Host: run the normal `make test` or `build_host_windows.ps1`; Python needs Pillow
and C needs zlib. Tests synthesize assets and cover framing, corrupt roles,
resigned caps/material/normal fields, allocation/read/EOF errors and cleanup.
Newlib inlines `ferror`, so the instrumented native reader selects explicit test
IO wrappers after its headers; production builds use ordinary stdio.

To prepare a **private**, new package:

```sh
python prepare_render_sectors.py /private/original-lod-background.glb /private/new-render-package
python make_render_fixture.py /private/new-render-test-assets build/render_fixture.h --real-package /private/new-render-package
make RENDER_ASSET_DIR=/private/new-render-test-assets jak-dc-render-io.elf
```

Without private data, `make jak-dc-render-io.elf` creates synthetic fixtures in a
new `build/private-render-assets` directory. The fixture writer refuses to
overwrite an existing directory. Generated files and embedding ELFs stay local.
`verify_backend.py --suite render-io` explicitly uses stock 16 MiB Flycast.
Pass `--tests 88` for the synthetic-only target, or `--tests 9578` for the
measured private original fixture described above. Other inputs change counts.

Next: a transactional bounded geometry cache with replica validation before
filtering, a resident texture catalog and GPU ownership independent of CPU
window swaps. Then integrate SH4ZAM view/clipping, reflection rendering, camera
and collision streaming. The fixed-patch movement prototype, original GOAL
runtime, full gameplay, streaming I/O and physical hardware validation remain
separate unfinished gates. No CD images or burns are needed at this stage.
