"""A level's render header for the disc: LEVEL.DRH (AI-assisted), ISC.

make_level_render_assets.py describes a level's render data in a C header
(package size and CRC, window caps, instance records, material map, source
hash). Compiled in, that header ties an ELF to one level. This writes the same
numbers as a file beside the level's render data, so level_draw.c reads them
when it opens a level:

  0   'DRH2'
  4   uint32 package_bytes, package_crc, instance_records, material_slots,
      window_faces, window_vertices, window_remap, cache_budget, swept
  40  char source_sha[68]  (64 hex digits, NUL filled)
  108 uint16 material_map[material_slots], zero padded to 4 bytes
  then uint32 window_half: the terrain window's half width in meters, which the
      caps were measured for (version 2; version 1 files mean 14)

  python make_level_drh.py build/level_render_fixture.h build/level-disc/GEYSER/LEVEL.DRH
"""
import argparse
from pathlib import Path
import re
import struct


def drh(text, window_half=14):
    def number(name):
        m = re.search(rf'#define {name} (\d+)u', text)
        if not m:
            raise SystemExit(f'fixture header has no {name}')
        return int(m.group(1))

    sha = re.search(r'fly_source_sha\[\]="([0-9a-f]{64})"', text)
    slots = number('FLY_MATERIAL_SLOTS')
    table = re.search(r'fly_material_map\[FLY_MATERIAL_SLOTS\]=\{([^}]*)\}', text)
    if not sha or not table:
        raise SystemExit('fixture header has no source hash or material map')
    material = [int(v.strip().rstrip('u')) for v in table.group(1).split(',') if v.strip()]
    if len(material) != slots:
        raise SystemExit('material map length differs from FLY_MATERIAL_SLOTS')
    out = b'DRH2' + struct.pack('<9I', number('FLY_PACKAGE_BYTES'), number('FLY_PACKAGE_CRC'),
                                number('FLY_INSTANCE_RECORDS'), slots, number('FLY_WINDOW_FACES'),
                                number('FLY_WINDOW_VERTICES'), number('FLY_WINDOW_REMAP'),
                                number('FLY_CACHE_BUDGET'), 1 if 'FLY_SWEPT' in text else 0)
    out += sha.group(1).encode('ascii').ljust(68, b'\0') + struct.pack(f'<{slots}H', *material)
    out += bytes((-len(out)) % 4)
    return out + struct.pack('<I', int(window_half))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('header', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--half', type=float, default=14.0, help='the terrain window half width the caps are for')
    args = parser.parse_args()
    data = drh(args.header.read_text(), args.half)
    args.output.write_bytes(data)
    print(f'{args.output}: {len(data)} bytes')


if __name__ == '__main__':
    main()
