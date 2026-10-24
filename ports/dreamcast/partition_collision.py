"""Offline bounded XZ collision-sector partition for DCR1 interchange.

Corrected host converter (AI-assisted), ISC. Geometry only. Reads a DCR1 byte string or path, assigns
each whole triangle to the closed XZ cells it overlaps, and writes a new
directory. Failure leaves no manifest behind.

This is a native-prototype aid. It does not implement GOAL collision, camera
integration, render streaming, or asset loading.
"""

from __future__ import annotations

import argparse
import math
import shutil
import struct
import sys
import zlib
from pathlib import Path


MAGIC_DCR = b"DCR1"
MAGIC_MANIFEST = b"DCS1"
MAGIC_SECTOR = b"DCSS"
DCR_VERSION = 1
FR3_VERSION = 43
FORMAT_VERSION = 1
FLAG_GEOMETRY_ONLY = 1

DCR_HEADER_BYTES = 16
DCR_RECORD_BYTES = 40
INPUT_RECORD_BYTES = 40
CACHE_RECORD_BYTES = 76
RECORD_SIZE = 44
SECTOR_HEADER_BYTES = 24
MANIFEST_ENTRY_BYTES = 20

CACHE_TRIANGLES = 8192
CANDIDATE_CAP = 1024
MAX_SOURCE_TRIANGLES = 65536
MAX_DCR_BYTES = DCR_HEADER_BYTES + MAX_SOURCE_TRIANGLES * DCR_RECORD_BYTES
MAX_TOTAL_CELLS = 4096
MAX_TOTAL_REPLICAS = 262144
MAX_SECTOR_FACES = 2048
CACHE_GUARD_M = 2.0
MAX_CELLS_PER_TRIANGLE = 36
MAX_TRIANGLE_SPAN_M = 64.0
WORLD_ABS_MAX_M = 1000000.0
LOCAL_ABS_MAX = 4096.0

MIN_CELL_SIZE_M = 4.0
MAX_CELL_SIZE_M = 64.0
DEFAULT_CELL_SIZE_M = 16.0

CAMERA_ARM_M = 10.4
MAX_DISPLACEMENT_M = 8.0
CAPSULE_RADIUS_ALLOWANCE_M = 2.0

# Reach used to size one retarget. Displacement contributes its full length
# on XZ (a conservative bound on the segment AABB) plus both-side padding.
QUERY_WIDTH_M = MAX_DISPLACEMENT_M + 2.0 * (CAPSULE_RADIUS_ALLOWANCE_M + CAMERA_ARM_M)
QUERY_REACH_M = MAX_DISPLACEMENT_M + CAPSULE_RADIUS_ALLOWANCE_M + CAMERA_ARM_M

PAT_NOENTITY_BIT = 1
PAT_MODE_SHIFT = 3
PAT_MODE_MASK = 7
PAT_EVENT_SHIFT = 14
PAT_EVENT_MASK = 63
SUPPORTED_MODES = frozenset((0, 1, 2))  # ground, wall, obstacle

DEDUP_SLOTS = 16384
DEDUP_SLOT_INDEX_BYTES = 2
GEOMETRY_PEAK_CEILING = 2 * 1024 * 1024

# Candidate-cell pad, in meters, so float index rounding cannot drop a touch.
# The SAT test still decides membership. A miss smaller than the separation
# slack is treated as a hit (conservative replica, never a dropped triangle).
RANGE_PAD_M = 1e-4
SEPARATION_SLACK_M = 1e-6

_MANIFEST_TAIL = struct.Struct("<IIIIfIIIIIIIiiiiIIffffIIII")
_MANIFEST_KEYS = (
    "version",
    "fr3_version",
    "flags",
    "source_count",
    "cell_size",
    "cell_count",
    "replica_count",
    "observed_max_faces",
    "per_sector_cap",
    "cells_per_triangle_cap",
    "total_cell_cap",
    "replica_cap",
    "ix_min",
    "ix_max",
    "iz_min",
    "iz_max",
    "halo_cells",
    "source_crc",
    "max_triangle_span",
    "camera_arm",
    "max_displacement",
    "capsule_allowance",
    "cache_cap",
    "candidate_cap",
    "cache_record_bytes",
    "input_record_bytes",
)
_SECTOR_HEADER = struct.Struct("<4sIiiII")
_ENTRY = struct.Struct("<iiIII")

HEADER_SIZE = 4 + _MANIFEST_TAIL.size
assert _MANIFEST_TAIL.size == 104
assert HEADER_SIZE == 108
assert len(_MANIFEST_KEYS) == 26
assert _SECTOR_HEADER.size == SECTOR_HEADER_BYTES
assert _ENTRY.size == MANIFEST_ENTRY_BYTES


class PartitionError(ValueError):
    """The partition was refused. No new manifest is part of this failure."""


class SourceTriangle:
    __slots__ = ("source_id", "raw", "xs", "ys", "zs", "pat")

    def __init__(self, source_id, raw, xs, ys, zs, pat):
        self.source_id = source_id
        self.raw = raw
        self.xs = xs
        self.ys = ys
        self.zs = zs
        self.pat = pat


class Package:
    """In-memory DCS1 package. Bytes are complete before any directory write."""

    def __init__(
        self,
        cell_size,
        source_count,
        replica_count,
        observed_max_faces,
        per_sector_cap,
        cells_per_triangle_cap,
        halo,
        ix_min,
        ix_max,
        iz_min,
        iz_max,
        source_crc,
        sectors,
        manifest,
    ):
        self.cell_size = cell_size
        self.source_count = source_count
        self.cell_count = len(sectors)
        self.replica_count = replica_count
        self.observed_max_faces = observed_max_faces
        self.per_sector_cap = per_sector_cap
        self.cells_per_triangle_cap = cells_per_triangle_cap
        self.halo = halo
        self.ix_min = ix_min
        self.ix_max = ix_max
        self.iz_min = iz_min
        self.iz_max = iz_max
        self.source_crc = source_crc
        self.sectors = sectors
        self.manifest = manifest


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def _f32(value):
    return struct.unpack("<f", struct.pack("<f", float(value)))[0]


def pat_mode(pat):
    return (pat >> PAT_MODE_SHIFT) & PAT_MODE_MASK


def _quantize_cell_size(cell_size):
    if isinstance(cell_size, bool) or not isinstance(cell_size, (int, float)):
        raise PartitionError("cell size out of range")
    if not math.isfinite(cell_size) or not MIN_CELL_SIZE_M <= cell_size <= MAX_CELL_SIZE_M:
        raise PartitionError("cell size out of range")
    quantized = _f32(cell_size)
    if (
        not math.isfinite(quantized)
        or quantized < MIN_CELL_SIZE_M
        or quantized > MAX_CELL_SIZE_M
    ):
        raise PartitionError("cell size out of range")
    return quantized


def neighborhood_limits(cell_size):
    """Bound IO cells incl2m cache guard. Per-sector2048; actual unique gather
    is separately capped8192 after query-box filtering/dedup, never truncated.
    """
    cell = _quantize_cell_size(cell_size)
    axis = int(math.floor((QUERY_WIDTH_M + 2*CACHE_GUARD_M) / cell)) + 2
    cells = axis * axis
    if axis < 1 or cells > CACHE_TRIANGLES or CACHE_TRIANGLES // cells < 1:
        raise PartitionError("cell size out of range")
    return axis, cells, MAX_SECTOR_FACES


def halo_cells(cell_size):
    cell = _quantize_cell_size(cell_size)
    return int(math.floor((QUERY_REACH_M + CACHE_GUARD_M) / cell)) + 1


def memory_budget():
    """Peak bytes for one transactional retarget at the legal maximums.

    Sector IO is released before the replacement cache is allocated.
    """
    cache = CACHE_TRIANGLES * CACHE_RECORD_BYTES
    compact = CACHE_TRIANGLES * (4 + INPUT_RECORD_BYTES)
    dedup = DEDUP_SLOTS * DEDUP_SLOT_INDEX_BYTES
    index = MAX_TOTAL_CELLS * MANIFEST_ENTRY_BYTES
    _axis, neighborhood, per_sector_16 = neighborhood_limits(DEFAULT_CELL_SIZE_M)
    _axis_max, _neigh_max, per_sector_max = neighborhood_limits(MAX_CELL_SIZE_M)
    sector_file = SECTOR_HEADER_BYTES + per_sector_max * RECORD_SIZE + 4
    peak = cache + cache + compact + dedup + index
    peak_with_sector = peak + sector_file
    if peak_with_sector > GEOMETRY_PEAK_CEILING:
        raise PartitionError("geometry budget exceeds ceiling")
    return {
        "cache_bytes": cache,
        "compact_bytes": compact,
        "dedup_bytes": dedup,
        "index_bytes": index,
        "sector_file_bytes": sector_file,
        "peak_bytes": peak,
        "peak_with_sector_held_bytes": peak_with_sector,
        "ceiling_bytes": GEOMETRY_PEAK_CEILING,
        "neighborhood_cells_16m": neighborhood,
        "per_sector_cap_16m": per_sector_16,
    }


def parse_dcr(data):
    """Return source triangles in file order. IDs are the DCR record indices."""
    if not isinstance(data, (bytes, bytearray)):
        raise PartitionError("malformed DCR")
    data = bytes(data)
    if len(data) > MAX_DCR_BYTES or len(data) < DCR_HEADER_BYTES:
        raise PartitionError("malformed DCR" if len(data) < DCR_HEADER_BYTES else "DCR exceeds size limit")
    magic, version, fr3_version, count = struct.unpack_from("<4sIII", data, 0)
    if magic != MAGIC_DCR or version != DCR_VERSION or fr3_version != FR3_VERSION:
        raise PartitionError("unsupported DCR")
    if count < 1:
        raise PartitionError("malformed DCR")
    if count > MAX_SOURCE_TRIANGLES:
        raise PartitionError("triangle count exceeds cap")
    expected = DCR_HEADER_BYTES + count * DCR_RECORD_BYTES
    if len(data) != expected:
        raise PartitionError("malformed DCR")
    triangles = []
    for index in range(count):
        start = DCR_HEADER_BYTES + index * DCR_RECORD_BYTES
        raw = data[start : start + DCR_RECORD_BYTES]
        coords = struct.unpack("<9f", raw[:36])
        (pat,) = struct.unpack("<I", raw[36:])
        if not all(math.isfinite(value) for value in coords):
            raise PartitionError("nonfinite coordinate")
        if any(abs(value) > WORLD_ABS_MAX_M for value in coords):
            raise PartitionError("coordinate exceeds world bound")
        if pat_mode(pat) not in SUPPORTED_MODES:
            raise PartitionError("unsupported PAT mode")
        a,b,c=(coords[i:i+3] for i in (0,3,6))
        u=tuple(b[k]-a[k] for k in range(3));v=tuple(c[k]-a[k] for k in range(3))
        normal=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
        if sum(x*x for x in normal)<1e-14:raise PartitionError('degenerate triangle')
        triangles.append(
            SourceTriangle(
                index,
                raw,
                (coords[0], coords[3], coords[6]),
                (coords[1], coords[4], coords[7]),
                (coords[2], coords[5], coords[8]),
                pat,
            )
        )
    return triangles


def _projections_overlap(axis_x, axis_z, tri, x0, x1, z0, z1):
    length = math.hypot(axis_x, axis_z)
    if length <= 1e-12:
        return True
    ax = axis_x / length
    az = axis_z / length
    projected_tri = [px * ax + pz * az for px, pz in tri]
    projected_rect = (
        x0 * ax + z0 * az,
        x1 * ax + z0 * az,
        x0 * ax + z1 * az,
        x1 * ax + z1 * az,
    )
    if max(projected_tri) < min(projected_rect) - SEPARATION_SLACK_M:
        return False
    if max(projected_rect) < min(projected_tri) - SEPARATION_SLACK_M:
        return False
    return True


def triangle_overlaps_cell(xs, zs, x0, x1, z0, z1):
    """Closed triangle-vs-rectangle test on the XZ projection.

    Y is ignored. A vertical face is the segment or point it projects to.
    A separating gap of SEPARATION_SLACK_M meters or less counts as overlap.
    """
    if max(xs) < x0 - SEPARATION_SLACK_M or min(xs) > x1 + SEPARATION_SLACK_M:
        return False
    if max(zs) < z0 - SEPARATION_SLACK_M or min(zs) > z1 + SEPARATION_SLACK_M:
        return False
    tri = ((xs[0], zs[0]), (xs[1], zs[1]), (xs[2], zs[2]))
    for index in range(3):
        ax, az = tri[index]
        bx, bz = tri[(index + 1) % 3]
        edge_x = bx - ax
        edge_z = bz - az
        if edge_x * edge_x + edge_z * edge_z <= 1e-24:
            continue
        if not _projections_overlap(-edge_z, edge_x, tri, x0, x1, z0, z1):
            return False
    return True


def cell_bounds(ix, iz, cell_size):
    x0 = ix * cell_size
    z0 = iz * cell_size
    return x0, x0 + cell_size, z0, z0 + cell_size


def overlapping_cells(triangle, cell_size):
    """Closed cells whose infinite-Y prism meets the triangle.

    The candidate window is the vertex AABB expanded by RANGE_PAD_M, converted
    with the same ceil/floor rule as query selection. Membership is the SAT
    test. An empty result is a refusal, so a triangle is never dropped.
    """
    xs = triangle.xs
    zs = triangle.zs
    min_x = min(xs) - RANGE_PAD_M
    max_x = max(xs) + RANGE_PAD_M
    min_z = min(zs) - RANGE_PAD_M
    max_z = max(zs) + RANGE_PAD_M
    ix_lo = math.ceil(min_x / cell_size) - 1
    ix_hi = math.floor(max_x / cell_size)
    iz_lo = math.ceil(min_z / cell_size) - 1
    iz_hi = math.floor(max_z / cell_size)
    if ix_hi < ix_lo or iz_hi < iz_lo:
        raise PartitionError("triangle produced no cell")
    if (ix_hi - ix_lo) > 64 or (iz_hi - iz_lo) > 64:
        raise PartitionError("triangle overlaps too many cells")
    hits = []
    for ix in range(ix_lo, ix_hi + 1):
        for iz in range(iz_lo, iz_hi + 1):
            x0, x1, z0, z1 = cell_bounds(ix, iz, cell_size)
            if triangle_overlaps_cell(xs, zs, x0, x1, z0, z1):
                hits.append((ix, iz))
    if not hits:
        raise PartitionError("triangle produced no cell")
    return hits


def _axis_span(values):
    return max(values) - min(values)


def build_sector_bytes(ix, iz, records):
    payload = bytearray()
    previous = -1
    for source_id, raw in records:
        if source_id <= previous or len(raw) != DCR_RECORD_BYTES:
            raise PartitionError("sector order")
        payload += struct.pack("<I", source_id)
        payload += raw
        previous = source_id
    header = _SECTOR_HEADER.pack(
        MAGIC_SECTOR,
        FORMAT_VERSION,
        ix,
        iz,
        len(records),
        len(payload),
    )
    body = header + bytes(payload)
    return body + struct.pack("<I", crc32(body))


def parse_sector(blob):
    blob = bytes(blob)
    if len(blob) < SECTOR_HEADER_BYTES + 4 or crc32(blob[:-4]) != struct.unpack_from("<I", blob, len(blob) - 4)[0]:
        raise PartitionError("sector crc mismatch" if len(blob) >= 4 else "malformed sector")
    magic, version, ix, iz, face_count, payload_bytes = _SECTOR_HEADER.unpack_from(blob, 0)
    if magic != MAGIC_SECTOR or version != FORMAT_VERSION:
        raise PartitionError("malformed sector")
    if not 1 <= face_count <= MAX_SECTOR_FACES or payload_bytes != face_count * RECORD_SIZE:
        raise PartitionError("malformed sector")
    if len(blob) != SECTOR_HEADER_BYTES + payload_bytes + 4:
        raise PartitionError("malformed sector")
    raw_records = []
    offset = SECTOR_HEADER_BYTES
    previous = -1
    for _ in range(face_count):
        (source_id,) = struct.unpack_from("<I", blob, offset)
        raw = blob[offset + 4 : offset + RECORD_SIZE]
        if len(raw) != DCR_RECORD_BYTES or source_id <= previous or source_id >= MAX_SOURCE_TRIANGLES:
            raise PartitionError("sector order")
        coords=struct.unpack('<9f',raw[:36]);pat=struct.unpack_from('<I',raw,36)[0]
        if not all(math.isfinite(v) and abs(v)<=WORLD_ABS_MAX_M for v in coords) or pat_mode(pat) not in SUPPORTED_MODES:
            raise PartitionError('invalid sector record')
        raw_records.append((source_id, raw))
        previous = source_id
        offset += RECORD_SIZE
    return {"ix": ix, "iz": iz, "face_count": face_count, "raw_records": raw_records}


def _manifest_bytes(header_values, entries):
    if len(header_values) != len(_MANIFEST_KEYS):
        raise PartitionError("internal header size")
    body = bytearray(MAGIC_MANIFEST)
    body += _MANIFEST_TAIL.pack(*header_values)
    if len(body) != HEADER_SIZE:
        raise PartitionError("internal header size")
    previous = None
    for entry in entries:
        key = (entry[0], entry[1])
        if previous is not None and key <= previous:
            raise PartitionError("manifest order")
        body += _ENTRY.pack(*entry)
        previous = key
    return bytes(body + struct.pack("<I", crc32(body)))


def parse_manifest(blob):
    blob = bytes(blob)
    if len(blob) < HEADER_SIZE + 4 or blob[:4] != MAGIC_MANIFEST:
        raise PartitionError("malformed manifest")
    stored = struct.unpack_from("<I", blob, len(blob) - 4)[0]
    if crc32(blob[:-4]) != stored:
        raise PartitionError("manifest crc mismatch")
    values = _MANIFEST_TAIL.unpack_from(blob, 4)
    header = dict(zip(_MANIFEST_KEYS, values))
    if header["version"] != FORMAT_VERSION or header["fr3_version"] != FR3_VERSION:
        raise PartitionError("unsupported manifest")
    cell = _quantize_cell_size(header["cell_size"])
    fixed = dict(flags=FLAG_GEOMETRY_ONLY,per_sector_cap=MAX_SECTOR_FACES,
        cells_per_triangle_cap=MAX_CELLS_PER_TRIANGLE,total_cell_cap=MAX_TOTAL_CELLS,
        replica_cap=MAX_TOTAL_REPLICAS,halo_cells=halo_cells(cell),cache_cap=CACHE_TRIANGLES,
        candidate_cap=CANDIDATE_CAP,cache_record_bytes=CACHE_RECORD_BYTES,input_record_bytes=INPUT_RECORD_BYTES,
        max_triangle_span=_f32(MAX_TRIANGLE_SPAN_M),camera_arm=_f32(CAMERA_ARM_M),
        max_displacement=_f32(MAX_DISPLACEMENT_M),capsule_allowance=_f32(CAPSULE_RADIUS_ALLOWANCE_M))
    if any(header[k]!=v for k,v in fixed.items()) or not 1<=header['source_count']<=MAX_SOURCE_TRIANGLES or not 1<=header['replica_count']<=MAX_TOTAL_REPLICAS:
        raise PartitionError("manifest contract mismatch")
    count = header["cell_count"]
    if count < 1 or count > header["total_cell_cap"]:
        raise PartitionError("malformed manifest")
    expected = HEADER_SIZE + count * MANIFEST_ENTRY_BYTES + 4
    if len(blob) != expected:
        raise PartitionError("malformed manifest")
    entries = []
    offset = HEADER_SIZE
    previous = None
    faces = []
    for _ in range(count):
        ix, iz, face_count, sector_crc, sector_bytes = _ENTRY.unpack_from(blob, offset)
        key = (ix, iz)
        if previous is not None and key <= previous:
            raise PartitionError("manifest order")
        if face_count < 1 or face_count > header["per_sector_cap"] or sector_bytes != SECTOR_HEADER_BYTES+face_count*RECORD_SIZE+4:
            raise PartitionError("malformed manifest")
        entries.append(
            {
                "ix": ix,
                "iz": iz,
                "face_count": face_count,
                "crc": sector_crc,
                "bytes": sector_bytes,
            }
        )
        faces.append(face_count)
        previous = key
        offset += MANIFEST_ENTRY_BYTES
    if sum(faces) != header["replica_count"] or max(faces) != header["observed_max_faces"]:
        raise PartitionError("malformed manifest")
    ixs = [entry["ix"] for entry in entries]
    izs = [entry["iz"] for entry in entries]
    if min(ixs) != header["ix_min"] or max(ixs) != header["ix_max"]:
        raise PartitionError("malformed manifest")
    if min(izs) != header["iz_min"] or max(izs) != header["iz_max"]:
        raise PartitionError("malformed manifest")
    header["entries"] = entries
    return header


def build_package(data, cell_size=DEFAULT_CELL_SIZE_M):
    """Validate and pack a DCR1 byte string. Does not touch the filesystem."""
    cell = _quantize_cell_size(cell_size)
    _axis, _neighborhood, per_sector = neighborhood_limits(cell)
    halo = halo_cells(cell)
    if not isinstance(data, (bytes, bytearray)):
        raise PartitionError("malformed DCR")
    data = bytes(data)
    triangles = parse_dcr(data)
    buckets = {}
    for triangle in triangles:
        span = max(
            _axis_span(triangle.xs),
            _axis_span(triangle.ys),
            _axis_span(triangle.zs),
        )
        if span > MAX_TRIANGLE_SPAN_M:
            raise PartitionError(f"triangle span exceeds cap: {triangle.source_id}")
        cells = overlapping_cells(triangle, cell)
        if len(cells) > MAX_CELLS_PER_TRIANGLE:
            raise PartitionError(f"triangle overlaps too many cells: {triangle.source_id}")
        for key in cells:
            buckets.setdefault(key, []).append((triangle.source_id, triangle.raw))
    if len(buckets) > MAX_TOTAL_CELLS:
        raise PartitionError("cell cap exceeded")
    replica_count = sum(len(records) for records in buckets.values())
    if replica_count > MAX_TOTAL_REPLICAS:
        raise PartitionError("replica cap exceeded")
    ordered = sorted(buckets)
    for key in ordered:
        buckets[key].sort(key=lambda item: item[0])
        if len(buckets[key]) > per_sector:
            raise PartitionError("sector face cap exceeded")
    sectors = {}
    entries = []
    for ix, iz in ordered:
        blob = build_sector_bytes(ix, iz, buckets[(ix, iz)])
        parsed = parse_sector(blob)
        if parsed["ix"] != ix or parsed["iz"] != iz or parsed["face_count"] != len(buckets[(ix, iz)]):
            raise PartitionError("sector index mismatch")
        sectors[(ix, iz)] = blob
        entries.append((ix, iz, len(buckets[(ix, iz)]), crc32(blob[:-4]), len(blob)))
    ixs = [ix for ix, _iz in ordered]
    izs = [iz for _ix, iz in ordered]
    observed = max(len(buckets[key]) for key in ordered)
    header_values = (
        FORMAT_VERSION,
        FR3_VERSION,
        FLAG_GEOMETRY_ONLY,
        len(triangles),
        cell,
        len(ordered),
        replica_count,
        observed,
        per_sector,
        MAX_CELLS_PER_TRIANGLE,
        MAX_TOTAL_CELLS,
        MAX_TOTAL_REPLICAS,
        min(ixs),
        max(ixs),
        min(izs),
        max(izs),
        halo,
        crc32(data),
        _f32(MAX_TRIANGLE_SPAN_M),
        _f32(CAMERA_ARM_M),
        _f32(MAX_DISPLACEMENT_M),
        _f32(CAPSULE_RADIUS_ALLOWANCE_M),
        CACHE_TRIANGLES,
        CANDIDATE_CAP,
        CACHE_RECORD_BYTES,
        INPUT_RECORD_BYTES,
    )
    manifest = _manifest_bytes(header_values, entries)
    parsed_manifest = parse_manifest(manifest)
    if parsed_manifest["cell_count"] != len(sectors) or parsed_manifest["source_crc"] != crc32(data):
        raise PartitionError("manifest count mismatch")
    for entry in parsed_manifest["entries"]:
        blob = sectors[(entry["ix"], entry["iz"])]
        if entry["crc"] != crc32(blob[:-4]) or entry["bytes"] != len(blob):
            raise PartitionError("sector crc mismatch")
    return Package(
        cell_size=cell,
        source_count=len(triangles),
        replica_count=replica_count,
        observed_max_faces=observed,
        per_sector_cap=per_sector,
        cells_per_triangle_cap=MAX_CELLS_PER_TRIANGLE,
        halo=halo,
        ix_min=min(ixs),
        ix_max=max(ixs),
        iz_min=min(izs),
        iz_max=max(izs),
        source_crc=crc32(data),
        sectors=sectors,
        manifest=manifest,
    )


def write_package(output_dir, package, fail_after=None):
    """Create output_dir and write sectors, then the manifest.

    output_dir must not already exist. The manifest is the last name installed.
    On any failure the created directory is removed. fail_after is a test hook:
    after that many successful file installs, the write aborts.
    """
    out = Path(output_dir).resolve()
    if out.exists():
        raise PartitionError("output directory already exists")
    files = [
        (f"sectors/{ix}_{iz}.dcs", package.sectors[(ix, iz)])
        for ix, iz in sorted(package.sectors)
    ]
    files.append(("manifest.dcs", package.manifest))
    created = False
    try:
        out.mkdir()
        created = True
        written = 0
        for relative, blob in files:
            if fail_after is not None and written >= fail_after:
                raise OSError("write aborted before complete package")
            target = out / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            partial = target.with_name(target.name + ".partial")
            partial.write_bytes(blob)
            partial.replace(target)
            written += 1
    except Exception:
        if created and out.resolve() == out:
            shutil.rmtree(out, ignore_errors=True)
        raise


def partition_file(dcr_path, output_dir, cell_size=DEFAULT_CELL_SIZE_M):
    path = Path(dcr_path)
    if path.stat().st_size > MAX_DCR_BYTES:
        raise PartitionError("DCR exceeds size limit")
    data = path.read_bytes()
    if len(data) > MAX_DCR_BYTES:
        raise PartitionError("DCR exceeds size limit")
    package = build_package(data, cell_size)
    write_package(output_dir, package)
    return package


def query_window(start, end, capsule_radius, camera_arm=CAMERA_ARM_M):
    """XZ AABB of a sweep expanded by capsule radius and the 10.4 m camera arm."""
    if len(start) != 3 or len(end) != 3:
        raise PartitionError("query")
    values = (start[0], start[1], start[2], end[0], end[1], end[2], capsule_radius, camera_arm)
    if any(isinstance(value, bool) or not isinstance(value, (int, float)) for value in values):
        raise PartitionError("query")
    if not all(math.isfinite(float(value)) for value in values):
        raise PartitionError("query")
    radius = float(capsule_radius)
    arm = float(camera_arm)
    if radius < 0.0 or radius > CAPSULE_RADIUS_ALLOWANCE_M:
        raise PartitionError("capsule radius exceeds allowance")
    if abs(arm - CAMERA_ARM_M) > 1e-6:
        raise PartitionError("camera arm must be 10.4")
    dx = float(end[0]) - float(start[0])
    dy = float(end[1]) - float(start[1])
    dz = float(end[2]) - float(start[2])
    distance = math.sqrt(dx * dx + dy * dy + dz * dz)
    if distance > MAX_DISPLACEMENT_M + 1e-4:
        raise PartitionError("displacement exceeds 8")
    pad = radius + arm
    return (
        min(float(start[0]), float(end[0])) - pad,
        max(float(start[0]), float(end[0])) + pad,
        min(float(start[2]), float(end[2])) - pad,
        max(float(start[2]), float(end[2])) + pad,
    )


def select_cells(min_x, max_x, min_z, max_z, cell_size):
    """Closed grid cells that meet the XZ query interval, ix-major then iz."""
    cell = _quantize_cell_size(cell_size)
    if not all(math.isfinite(value) for value in (min_x, max_x, min_z, max_z)):
        raise PartitionError("query")
    if min_x > max_x or min_z > max_z:
        raise PartitionError("query")
    ix_lo = math.ceil(min_x / cell) - 1
    ix_hi = math.floor(max_x / cell)
    iz_lo = math.ceil(min_z / cell) - 1
    iz_hi = math.floor(max_z / cell)
    if ix_hi < ix_lo or iz_hi < iz_lo:
        raise PartitionError("query")
    span_x = ix_hi - ix_lo + 1
    span_z = iz_hi - iz_lo + 1
    axis, neighborhood, _per_sector = neighborhood_limits(cell)
    if span_x > axis or span_z > axis or span_x * span_z > neighborhood:
        raise PartitionError("query neighborhood exceeds bound")
    return [(ix, iz) for ix in range(ix_lo, ix_hi + 1) for iz in range(iz_lo, iz_hi + 1)]


def cell_is_missing(package, ix, iz):
    """A cell outside the occupied grid expanded by the query halo."""
    return not (
        package.ix_min - package.halo <= ix <= package.ix_max + package.halo
        and package.iz_min - package.halo <= iz <= package.iz_max + package.halo
    )


def unique_records(records):
    """Collapse replicas by source id. Refuse overflow and payload mismatch.

    Output order is ascending source id. The native retarget uses a fixed
    16384-slot index plus a compact 8192-row table; this dict is the oracle
    for membership and refusal, not the device allocator.
    """
    found = {}
    for source_id, raw in records:
        if isinstance(source_id, bool) or not isinstance(source_id, int):
            raise PartitionError("source id out of range")
        if source_id < 0 or source_id > 0xFFFFFFFF:
            raise PartitionError("source id out of range")
        raw = bytes(raw)
        if len(raw) != DCR_RECORD_BYTES:
            raise PartitionError("record size")
        previous = found.get(source_id)
        if previous is None:
            if len(found) >= CACHE_TRIANGLES:
                raise PartitionError("unique faces exceed cache")
            found[source_id] = raw
        elif previous != raw:
            raise PartitionError("replica mismatch")
    return [(source_id, found[source_id]) for source_id in sorted(found)]


def rebase_records(records, origin):
    """Subtract a float32 origin. Refuse non-finite or abs(local) > 4096."""
    if len(origin) != 3:
        raise PartitionError("origin")
    if any(isinstance(value, bool) or not isinstance(value, (int, float)) for value in origin):
        raise PartitionError("origin")
    shifts = (_f32(origin[0]), _f32(origin[1]), _f32(origin[2]))
    if not all(math.isfinite(value) for value in shifts):
        raise PartitionError("origin")
    rebased = []
    for source_id, raw in records:
        coords = struct.unpack("<9f", raw[:36])
        pat = raw[36:40]
        packed = bytearray()
        for index, value in enumerate(coords):
            local = _f32(value - shifts[index % 3])
            if not math.isfinite(local) or abs(local) > LOCAL_ABS_MAX:
                raise PartitionError("local coordinate exceeds 4096")
            packed += struct.pack("<f", local)
        rebased.append((source_id, bytes(packed) + pat))
    return rebased


def assemble_neighborhood(package, cells, bounds=None):
    """Reference gather: missing cells refuse; holes inside the halo are empty."""
    collected = []
    for ix, iz in cells:
        if cell_is_missing(package, ix, iz):
            raise PartitionError("missing region")
        blob = package.sectors.get((ix, iz))
        if blob is None:
            continue
        parsed = parse_sector(blob)
        if parsed["ix"] != ix or parsed["iz"] != iz:
            raise PartitionError("sector index mismatch")
        for source_id,raw in parsed['raw_records']:
            if source_id>=package.source_count:raise PartitionError('source id out of range')
            if bounds is not None:
                xyz=struct.unpack('<9f',raw[:36]);xs=xyz[0::3];zs=xyz[2::3]
                if max(xs)<bounds[0] or min(xs)>bounds[1] or max(zs)<bounds[2] or min(zs)>bounds[3]:continue
            collected.append((source_id,raw))
    return unique_records(collected)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Partition a DCR1 collision interchange into bounded XZ sectors."
    )
    parser.add_argument("dcr", type=Path, help="DCR1 input file")
    parser.add_argument("output", type=Path, help="new output directory; must not exist")
    parser.add_argument("--cell-size", type=float, default=DEFAULT_CELL_SIZE_M)
    args = parser.parse_args(argv)
    try:
        package = partition_file(args.dcr, args.output, args.cell_size)
    except (PartitionError, OSError) as exc:
        print(f"partition: {exc}", file=sys.stderr)
        return 1
    print(
        f"triangles={package.source_count} cells={package.cell_count} "
        f"replicas={package.replica_count} per_sector_cap={package.per_sector_cap} "
        f"cell_size={package.cell_size:.3f}"
    )
    return 0


_BUDGET = memory_budget()


if __name__ == "__main__":
    sys.exit(main())
