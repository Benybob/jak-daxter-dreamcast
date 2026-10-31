"""Sector-aligned level extent packer and verifier (AI-assisted), ISC.

Experimental DCLV v1: 2048-byte header, index extension, global, textures in
catalog order, Morton-ordered cell pairs, original catalog and resident package.
All offsets are file-relative, never FADs. See DISC_READER_NOTES.md for the ABI.
"""
import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import struct
import zlib

import prepare_render_sectors as catalog

SECTOR = 2048
TILE = 8
MAX_BYTES = 0x7FFFF800  # sector padded, representable by KOS signed off_t
MAX_INDEX = 4 * 1024 * 1024
HEADER = struct.Struct("<4sIIIfiiIIIIIII")
EXTENT = struct.Struct("<III")  # offset, exact length, crc32; zero triple = absent
TILE_ENTRY = struct.Struct("<iiI")  # absolute tile x,z, index-relative dense-table offset
NAME = re.compile(r"(-?\d+)_(-?\d+)$")


def align(n):
    return (n + SECTOR - 1) // SECTOR * SECTOR


def morton(x, z):
    if not 0 <= x <= 0xFFFFFFFF or not 0 <= z <= 0xFFFFFFFF:
        raise ValueError("Morton coordinate out of range")
    return sum(((x >> b) & 1) << (2 * b) | ((z >> b) & 1) << (2 * b + 1)
               for b in range(32))


def cells(root):
    found = {}
    for directory, suffix, kind in (("sectors", ".drx", 0), ("cells", ".dci", 1)):
        for path in sorted((root / directory).glob("*" + suffix)):
            match = NAME.fullmatch(path.stem)
            if not match:
                raise ValueError(f"bad cell name: {path.name}")
            key = tuple(map(int, match.groups()))
            if any(not -0x80000000 <= v <= 0x7FFFFFFF for v in key):
                raise ValueError("cell coordinate out of range")
            if path.stem != f"{key[0]}_{key[1]}":
                raise ValueError("noncanonical/duplicate cell name")
            found.setdefault(key, [None, None])[kind] = path
    return found


@dataclass(frozen=True)
class Range:
    offset: int
    length: int
    crc: int


def source_payloads(root):
    """Use the existing binary catalog validator; report.json is not trusted."""
    root = Path(root)
    manifest = (root / "manifest.drm").read_bytes()
    parsed = catalog.parse_manifest(manifest)
    return parsed, manifest, cells(root)


def pack(root, output):
    root, output = Path(root), Path(output)
    parsed, manifest, grid = source_payloads(root)
    coords = list(grid)
    ox = min((x for x, _ in coords), default=0)
    oz = min((z for _, z in coords), default=0)
    width = max((x - ox + 1 for x, _ in coords), default=0)
    height = max((z - oz + 1 for _, z in coords), default=0)
    if width > 0xFFFFFFFF or height > 0xFFFFFFFF:
        raise ValueError("grid dimensions out of range")
    tiles = sorted({(x // TILE, z // TILE) for x, z in coords})
    textures = parsed["textures"]
    index_bytes = len(textures) * EXTENT.size + len(tiles) * (TILE_ENTRY.size + TILE*TILE*2*EXTENT.size)
    if index_bytes > MAX_INDEX:
        raise ValueError("index budget exceeded")
    index = bytearray(index_bytes)
    payload_start = align(SECTOR + index_bytes)
    cursor = payload_start
    ranges = {}
    payloads = []

    def add(name, blob):
        nonlocal cursor
        if not blob or align(len(blob)) > MAX_BYTES - cursor:
            raise ValueError("empty payload or file budget exceeded")
        entry = Range(cursor, len(blob), zlib.crc32(blob))
        ranges[name] = entry
        payloads.append((entry, blob))
        cursor += align(len(blob))
        return EXTENT.pack(entry.offset, entry.length, entry.crc)

    global_entry = add("global.drx", (root / "global.drx").read_bytes())
    for i, texture in enumerate(textures):
        index[i*EXTENT.size:(i+1)*EXTENT.size] = add(f"texture/{i}", texture["pixels"])
    tile_tables = {}
    directory = len(textures) * EXTENT.size
    tables = directory + len(tiles) * TILE_ENTRY.size
    for i, (tx, tz) in enumerate(tiles):
        table = tables + i * TILE*TILE*2*EXTENT.size
        TILE_ENTRY.pack_into(index, directory + i*TILE_ENTRY.size, tx, tz, table)
        tile_tables[tx, tz] = table
    for x, z in sorted(coords, key=lambda p: morton(p[0]-ox, p[1]-oz)):
        table = tile_tables[x // TILE, z // TILE]
        at = table + ((z % TILE)*TILE + x % TILE)*2*EXTENT.size
        for kind, path in enumerate(grid[x, z]):
            if path:
                index[at + kind*EXTENT.size:at + (kind+1)*EXTENT.size] = add(
                    path.relative_to(root).as_posix(), path.read_bytes())
    manifest_entry = add("manifest.drm", manifest)
    package_entry = add("package.dci", (root / "package.dci").read_bytes())
    header = bytearray(SECTOR)
    HEADER.pack_into(header, 0, b"DCLV", 1, SECTOR, cursor, parsed["cell_size"],
                     ox, oz, width, height, len(textures), len(tiles), index_bytes,
                     zlib.crc32(index), 0)
    header[HEADER.size:HEADER.size + 3*EXTENT.size] = global_entry + manifest_entry + package_entry
    struct.pack_into("<I", header, SECTOR-4, zlib.crc32(header[:-4]))
    # Do not overwrite source inputs, including via symlinks.
    if output.resolve().is_relative_to(root.resolve()):
        raise ValueError("output must be outside source tree")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as out:
        out.write(header)
        out.write(index)
        out.write(bytes(payload_start - SECTOR - len(index)))
        for entry, blob in payloads:
            out.write(blob)
            out.write(bytes(align(entry.length)-entry.length))
    return verify(root, output)


def read_index(blob):
    """Strict canonical decoder, also rejects overlaps, gaps and bad padding."""
    if len(blob) < SECTOR:
        raise ValueError("short header")
    (magic, version, sector, total, cell, ox, oz, width, height, ntex, ntile,
     ibytes, icrc, reserved) = HEADER.unpack_from(blob)
    if (magic != b"DCLV" or version != 1 or sector != SECTOR or total != len(blob)
            or total > MAX_BYTES or total % SECTOR or reserved
            or not 4 <= cell <= 64 or ntex > 256 or ibytes > MAX_INDEX
            or ox + width > 0x80000000 or oz + height > 0x80000000
            or ibytes != ntex*EXTENT.size + ntile*(TILE_ENTRY.size + TILE*TILE*2*EXTENT.size)):
        raise ValueError("header contract")
    used = HEADER.size + 3*EXTENT.size
    if any(blob[used:SECTOR-4]) or zlib.crc32(blob[:SECTOR-4]) != struct.unpack_from("<I", blob, SECTOR-4)[0]:
        raise ValueError("header padding/CRC")
    start = align(SECTOR + ibytes)
    if start > total:
        raise ValueError("truncated index")
    index = blob[SECTOR:SECTOR+ibytes]
    if zlib.crc32(index) != icrc or any(blob[SECTOR+ibytes:start]):
        raise ValueError("index padding/CRC")
    entries = {}

    def take(name, data, pos, absent=False):
        off, size, crc = EXTENT.unpack_from(data, pos)
        if absent and (off, size, crc) == (0, 0, 0):
            return
        if not size or off < start or off % SECTOR or off + align(size) > total:
            raise ValueError("extent bounds/alignment")
        if zlib.crc32(blob[off:off+size]) != crc or any(blob[off+size:off+align(size)]):
            raise ValueError("payload padding/CRC")
        entries[name] = Range(off, size, crc)

    for i, name in enumerate(("global.drx", "manifest.drm", "package.dci")):
        take(name, blob, HEADER.size + i*EXTENT.size)
    for i in range(ntex):
        take(f"texture/{i}", index, i*EXTENT.size)
    directory = ntex*EXTENT.size
    tables = directory + ntile*TILE_ENTRY.size
    previous = None
    coords = set()
    for i in range(ntile):
        tx, tz, at = TILE_ENTRY.unpack_from(index, directory + i*TILE_ENTRY.size)
        if previous is not None and (tx, tz) <= previous:
            raise ValueError("tile directory order")
        previous = (tx, tz)
        if at != tables + i*TILE*TILE*2*EXTENT.size:
            raise ValueError("dense tile offset")
        count = 0
        for z in range(TILE):
            for x in range(TILE):
                ix, iz = tx*TILE + x, tz*TILE + z
                for kind, (directory_name, suffix) in enumerate((("sectors", "drx"), ("cells", "dci"))):
                    name = f"{directory_name}/{ix}_{iz}.{suffix}"
                    take(name, index, at + ((z*TILE+x)*2+kind)*EXTENT.size, absent=True)
                    if name in entries:
                        if not ox <= ix < ox + width or not oz <= iz < oz + height:
                            raise ValueError("cell outside grid")
                        coords.add((ix, iz)); count += 1
        if not count:
            raise ValueError("empty tile")
    if coords:
        if (min(x for x, z in coords), min(z for x, z in coords),
            max(x for x, z in coords)-ox+1, max(z for x, z in coords)-oz+1) != (ox, oz, width, height):
            raise ValueError("noncanonical grid bounds")
    elif (ox, oz, width, height) != (0, 0, 0, 0):
        raise ValueError("empty grid bounds")
    names = ["global.drx"] + [f"texture/{i}" for i in range(ntex)]
    for ix, iz in sorted(coords, key=lambda p: morton(p[0]-ox, p[1]-oz)):
        names.extend(n for n in (f"sectors/{ix}_{iz}.drx", f"cells/{ix}_{iz}.dci") if n in entries)
    names += ["manifest.drm", "package.dci"]
    cursor = start
    for name in names:
        entry = entries[name]
        if entry.offset != cursor:
            raise ValueError("payload order/gap/overlap")
        cursor += align(entry.length)
    if cursor != total:
        raise ValueError("trailing payload")
    return {"cell_size": cell, "origin": (ox, oz), "entries": entries, "coords": coords,
            "index_bytes": ibytes, "tile_count": ntile}


def window_runs(index, ix, iz, radius=1):
    """Exact coalesced runs for available DRX/DCI payloads in a cell square.

    Include sector padding. A run joins ONLY physically adjacent extents;
    absent cells do not fabricate IO, and a disjoint Morton window stays split.
    """
    extents = []
    for (x, z) in index["coords"]:
        if abs(x-ix) <= radius and abs(z-iz) <= radius:
            for name in (f"sectors/{x}_{z}.drx", f"cells/{x}_{z}.dci"):
                if name in index["entries"]:
                    entry = index["entries"][name]
                    extents.append((entry.offset, align(entry.length)))
    runs = []
    for off, size in sorted(extents):
        if runs and sum(runs[-1]) == off:
            runs[-1] = (runs[-1][0], runs[-1][1] + size)
        else:
            runs.append((off, size))
    return runs


def verify(root, packed):
    root = Path(root)
    blob = Path(packed).read_bytes()
    index = read_index(blob)
    parsed, manifest, grid = source_payloads(root)
    expected = {"global.drx": (root / "global.drx").read_bytes(), "manifest.drm": manifest,
                "package.dci": (root / "package.dci").read_bytes()}
    expected.update((f"texture/{i}", t["pixels"]) for i, t in enumerate(parsed["textures"]))
    for pair in grid.values():
        expected.update((p.relative_to(root).as_posix(), p.read_bytes()) for p in pair if p)
    if index["cell_size"] != parsed["cell_size"] or set(expected) != set(index["entries"]):
        raise ValueError("source/index mismatch")
    for name, source in expected.items():
        entry = index["entries"][name]
        if blob[entry.offset:entry.offset+entry.length] != source:
            raise ValueError(f"source byte mismatch: {name}")
    return {"bytes": len(blob), "payloads": len(expected), "cells": len(grid),
            "tiles": index["tile_count"], "index_bytes": index["index_bytes"],
            "texture_bytes": sum(len(t["pixels"]) for t in parsed["textures"]),
            "global_crc32": index["entries"]["global.drx"].crc}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.source, args.output) if args.verify_only else pack(args.source, args.output), sort_keys=True))
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
