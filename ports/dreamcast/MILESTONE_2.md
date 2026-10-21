# Dreamcast milestone 2: executable diagnostics and geometry viewer

2026-10-20. AI-assisted implementation. This is port-development work; no Jak
gameplay or original assets are included.

## Delivered

- `jak-dc-probe.elf`: sequential DGO diagnostic, synthetic ROM-disk fallback.
- `jak-dc-scene.elf`: PowerVR triangle renderer, orbit/zoom controls and a
  synthetic terrain fixture, with an external scene loading path.
- `convert_scene.py`: static OpenGOAL background GLB to DCM1 geometry converter.
- `scene_io.c`: shared host/SH-4 validation and bounded scene loading.
- `verify_flycast.py`: repeatable Linux offscreen boot, framebuffer and serial
  checks using the emulator's documented Lua functions and controller state.
- `setup_toolchain.sh`: checksum-pinned Linux x86_64/WSL toolchain installer.

The release package has stripped ELFs in `bin/`, actual emulator framebuffer
captures in `evidence/`, sources and a cumulative Git patch. ELFs can be loaded
directly in Flycast. They are not self-booting CD images.

## Verified results

Both ELFs were compiled with `-std=c99 -Wall -Wextra -Werror` using GCC 15.1.0
and KallistiOS 2.2.1. Boot tests used Flycast 2.7, HLE BIOS, explicitly disabled
32 MiB RAM modification, SDL offscreen and Mesa llvmpipe software rendering.
No external console BIOS was needed.

| Check | Result |
| --- | --- |
| Host tests | 16 passed: archive streaming, CRC, malformed input, GLB transforms/accessors, selection, truncation and native scene loader |
| Asset probe | 2 objects, 240 container bytes; `crc-vector` CRC32 `cbf43926`; START exits with code 0 |
| PowerVR fixture | 512 triangles, 1,536 vertices, 24,576 geometry allocation bytes |
| Emulated system RAM | 16,777,216 bytes |
| Scene allocator at startup | 118,552 bytes live in an allocator arena of 185,544 bytes |
| Scene free VRAM after initialization | 4,675,272 bytes |
| Frame rate in emulator | Approximately 59.9 fps for this small synthetic scene; not a hardware performance result |
| External ROM-disk scene | Loaded and rendered using `/rd/scene.dcm` |
| Malformed external scene | Truncated payload rejected with `SCENE_FAIL short scene payload`, exit code 1 |
| Controller | START exits; orbit and trigger zoom are exercised by the verification script |

The allocator figures exclude the executable, static data, stacks and other
memory not owned by that allocator. The geometry numbers do not establish
whether Jak gameplay, collision or animated characters fit. The synthetic
scene is only a small graphics/loader test. Retail hardware remains untested.

## Build locally

On Linux x86_64 or x86_64 WSL with Python 3, Make, a host C compiler, curl and tar:

```sh
cd ports/dreamcast
bash setup_toolchain.sh /your/path/dc-sdk
source /your/path/dc-sdk/kos/environ.sh
make test fixture
make CFLAGS='-std=c99 -Wall -Wextra -Werror'
```

The installer fetches a roughly 472 MB third-party toolchain release. It
verifies SHA-256 before extraction. It does not change your shell profile.
If you already have KOS installed, source its environment instead.

Pinned release:
`drpaneas/dreamcast-toolchain-builds`, tag `gcc15.1.0-kos2.2.1`, asset
`dreamcast-toolchain-gcc15.1.0-kos2.2.1-linux-x86_64-ubuntu22.tar.gz`.
SHA-256: `36ee8b174331f53996990327de8df77ff662041989bbafab9e68572383d03b41`.

## Reproduce the emulator check

Install [Flycast 2.7](https://github.com/flyinghead/flycast/releases/tag/v2.7).
On Linux, the AppImage can be extracted using `--appimage-extract`; pass the
resulting `squashfs-root/usr/bin/flycast` executable below.

```sh
python3 verify_flycast.py /path/to/flycast jak-dc-probe.elf --kind probe --output build/check-probe
python3 verify_flycast.py /path/to/flycast jak-dc-scene.elf --kind scene --output build/check-scene
```

Each check creates an isolated emulator config, captures the actual emulated
RGB565 framebuffer to `frame.ppm`, injects START and checks the serial log for
clean target shutdown. The script then closes the emulator frontend. It does
not require an X server. Keep the log if a check fails.

The tested AppImage SHA-256 is
`5b9f8a636a6acb8446bc78fe96650d8cb8233ac9f8ce69665dab3f39a1cbaae1`.
Flycast source tag v2.7 resolves to
`5aa091fde632fb332c8d8c34e280d62dc951954c`.

## Load geometry from your own Jak copy

The port workbench has no game assets. In your existing local OpenGOAL setup,
enable `"rip_levels": true` in the Jak 1 decompiler configuration and follow
OpenGOAL's normal extraction workflow for your own PS2 copy. Background geometry
exports appear under:

`decompiler_out/jak1/levels/<level>/<level>-background.glb`

This follows the checked upstream `decompiler/level_extractor/extract_level.cpp`.
The converter accepts a single embedded-buffer GLB 2 file, static TRIANGLES,
indexed or non-indexed positions, vertex colors and base color factors. It
applies node transforms/instances and normalizes the result to a unit sphere.
It ignores textures, transparency, animation, skinning and collision. A textured
level can therefore look very different in this geometry-only viewer.

```sh
python3 convert_scene.py /path/to/level-background.glb --list-meshes
python3 convert_scene.py /path/to/level-background.glb romdisk/scene.dcm --mesh mesh-name
build/scene-check romdisk/scene.dcm
make
```

Select/crop a small mesh first: the renderer caps the triangle list at 8,192
triangles (24,576 vertices, 384 KiB). Oversized selections fail by default.
`--truncate --max-triangles N` explicitly keeps the first N triangles for an
I/O/geometry experiment; it is not mesh simplification or a complete level.

Scene loading order: `/cd/SCENE.DCM`, `/rd/scene.dcm`, `/rd/fixture.dcm`.
An opened but malformed file causes failure without falling back to a different
scene. Remove `romdisk/scene.dcm` and rebuild to return to the synthetic fixture.
The build detects a removed file, preventing a stale embedded external scene.

Controls: analog stick orbits, left trigger zooms out, right trigger zooms in,
START exits. The camera stays outside the normalized scene's unit sphere; this
is a constrained inspection camera, without movement/collision gameplay.

## Remaining work

The next content test needs one small background GLB exported from your own
Jak 1 copy. That file is absent here, so no real Jak scene has been rendered or
measured. After geometry validation: add native texture conversion/rendering,
measure on a retail Dreamcast, then implement the GOAL SH-4 compiler/runtime
slice and gameplay systems described in `README.md`.

## Attribution

OpenGOAL source uses the repository ISC license. The compiled diagnostics link
KallistiOS and its C/compiler runtimes. See the release's `licenses/` directory
for KOS attribution, contributor list, newlib notices and GCC runtime exception.
The SDK and emulator themselves are not redistributed in this package.
