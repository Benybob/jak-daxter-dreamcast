"""Split a DCI3 package's instance records into XZ cell files. (AI-assisted), ISC.

    python instance_cells.py PACKAGE.dci3 OUT_DIR [--cell 16]

The package keeps its geometry and has zero instances; OUT_DIR/cells/IX_IZ.dci
holds each cell's 52-byte records in original order, with the original instance
index in the record's pad, so instance_cells.c can restore that order.
"""
import argparse
import math
from pathlib import Path
import struct

INSTANCE = struct.Struct('<2H12f')
CELL = 16.0
MARGIN = 0.125  # DC_INSTANCE_CELLS_MARGIN


def read(blob):
    """(geometry prefix, [(index, record bytes)]) of a DCI2 or DCI3 package."""
    if blob[:4] not in (b'DCI2', b'DCI3'):
        raise ValueError('needs a DCI2 or DCI3 package')
    count = struct.unpack_from('<I', blob, 12)[0]
    if count > 65536:
        raise ValueError('too many instances for a 16-bit index')
    start = len(blob) - count * INSTANCE.size
    if start < 16:
        raise ValueError('truncated package')
    return blob[:start], [(i, blob[start + i * INSTANCE.size:start + (i + 1) * INSTANCE.size]) for i in range(count)]


def cell_of(record, cell=CELL):
    fields = INSTANCE.unpack(record)
    m = fields[2:]
    return math.floor(m[9] / cell), math.floor(m[11] / cell)


def split(blob, out, cell=CELL):
    """Write the cell files and return the package with no instances."""
    prefix, records = read(blob)
    cells = {}
    for index, record in records:
        cells.setdefault(cell_of(record, cell), []).append(
            record[:2] + struct.pack('<H', index) + record[4:])
    folder = Path(out) / 'cells'
    folder.mkdir(parents=True, exist_ok=True)
    for old in folder.glob('*.dci'):  # a stale cell would draw beside the new ones
        old.unlink()
    for (ix, iz), rows in cells.items():
        (folder / f'{ix}_{iz}.dci').write_bytes(b''.join(rows))
    return prefix[:12] + struct.pack('<I', 0) + prefix[16:]


def touches(ix, iz, x, z, draw, cell=CELL):
    x0, z0 = ix * cell, iz * cell
    x1, z1 = x0 + cell, z0 + cell
    dx = x0 - x if x < x0 else (x - x1 if x > x1 else 0.0)
    dz = z0 - z if z < z0 else (z - z1 if z > z1 else 0.0)
    reach = draw + MARGIN
    return dx * dx + dz * dz <= reach * reach


def resident(blob, x, z, draw, cell=CELL):
    """The original indices instance_cells.c holds for this position, in order."""
    return [i for i, record in read(blob)[1] if touches(*cell_of(record, cell), x, z, draw, cell)]


def peak(blob, targets, draw, cell=CELL):
    """Records a path needs at once: cells that leave go before new ones arrive."""
    _, records = read(blob)
    placed = [cell_of(record, cell) for _, record in records]
    return max(sum(1 for c in placed if touches(*c, t[0], t[-1], draw, cell)) for t in targets)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('package', type=Path)
    ap.add_argument('out', type=Path)
    ap.add_argument('--cell', type=float, default=CELL)
    a = ap.parse_args()
    trimmed = split(a.package.read_bytes(), a.out, a.cell)
    (a.out / 'package.dci').write_bytes(trimmed)
    print(len(trimmed))


if __name__ == '__main__':
    main()
