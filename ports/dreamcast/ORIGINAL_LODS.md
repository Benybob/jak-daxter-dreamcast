# Original background LOD export

2026-10-25. (AI-assisted). A resource experiment for the native prototype;
full render streaming, original GOAL gameplay and the complete game remain open.

`export_background_fr3.cpp` selects the original tfrag and tie LOD arrays, then
calls OpenGOAL's unchanged background exporter. It keeps shrubs and does no new
mesh simplification. Input is the validated decompressed OpenGOAL version-43 FR3.
Requested LODs must exist. The helper refuses existing final/partial outputs and
commits a complete GLB from a temporary file in the same directory.

```text
python ports/dreamcast/build_background_exporter_windows.py --msys LOCAL_MSYS
ports/dreamcast/build/export-background.exe DECOMPRESSED_FR3 NEW_BACKGROUND_GLB 2 3
```

The optional Windows helper uses C++20 and the existing upstream serializer,
exporter, strip fixup, TinyGLTF/STB and geometry sources. Its runtime is linked
statically, so invoking it does not require modifying the shell's MinGW DLL path.
The exporter performs no SH-4 build and does not relink the GOAL frontend.

## Verified original training data

| Measurement | Original highest LOD | Original tfrag 2 / tie 3 |
| --- | --- | --- |
| Complete background triangles | 393,864 | 140,166 |
| Bounded prototype terrain triangles | 2,973 | 1,305 |
| Bounded prototype terrain vertices | 3,231 | 1,903 |

Re-exporting LOD 0/0 reproduces the existing highest-detail GLB byte for byte.
The hardened helper also reproduces the earlier lower-LOD experiment byte for
byte. Ten local CLI/export checks cover invalid LODs, existing output, empty input,
trailing serialized data and both exports. The host suite passes 274 tests,
including an original-free output-preservation check for the optional helper.

The lower-LOD training prototype passes 20 native movement/scene checks and
11 camera-orbit checks in stock 16 MiB Flycast. Idle, airborne and obstructed
camera captures were visually reviewed. Actor geometry stays at 3,593 triangles;
the original 2,032-face collider is unchanged. The combined source count is 4,898.
`verify_training.py --expected-triangles 4898` checks that count explicitly; its
default remains 6,566 for the earlier highest-detail patch.

At readiness this build reports 2,470,144 bytes of live heap and 5,302,696 bytes
of free VRAM. Heap excludes static/embedded assets, executable storage, KOS and
stacks. Polygon and heap reductions do not establish a frame-rate improvement.

This experiment selects one original LOD for the whole exported background.
Distance-based LOD selection and wider traversal are still pending. Sampled
36.8-meter regions in the lower-LOD full background contain at most 7,447 faces
by triangle AABB, which fits the proposed 8,192-face budget. This does not prove
every view/window fits. Long original faces need a separate global/far bucket;
1,366 span over 64 meters. Do not discard them to satisfy local cell limits.

Prepare bounded bundles with the existing `prepare_training_scene.py` and matching
collision report, then build with `--training-assets PRIVATE_BUNDLE`. Actor files,
all GLBs, converted bundles, captures and embedding ELFs stay private. No discs
are prepared and no physical hardware validation is claimed.
