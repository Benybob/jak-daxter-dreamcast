"""Write a VAGDIR.AYB / VAGWAD.<lang> pair holding only the named dialogue lines (AI-assisted), ISC.

The retail VAGWAD.ENG is 174 MB; a test disc needs a level's handful of lines.
VAGDIR.AYB is a u32 count, then 12-byte records: an 8-byte name (upper case,
space padded) and a u32 start sector in the VAGWAD file (2048-byte sectors).
Each line is a big-endian VAGp file: 48-byte header (size at +12, rate at +16),
then size bytes of SPU ADPCM. The subset keeps that format, each line on its
own sector boundary, so level_sound.c reads it as it would the retail pair.

Output is PRIVATE game-derived data: write it under build/ (git-ignored).
"""
import argparse
import struct
from pathlib import Path

SECTOR = 2048


def directory(path):
    data = path.read_bytes()
    count = struct.unpack_from('<I', data, 0)[0]
    if 4 + 12 * count > len(data):
        raise ValueError('VAGDIR shorter than its count')
    return {data[4 + 12 * i:12 + 12 * i].decode('ascii'): struct.unpack_from('<I', data, 12 + 12 * i)[0]
            for i in range(count)}


def iso_name(name):
    if not 0 < len(name) <= 8 or not name.isascii():
        raise ValueError(f'{name!r}: a VAGDIR name is 1 to 8 ASCII characters')
    return name.upper().ljust(8)


def subset(vagdir, vagwad, names):
    entries = directory(vagdir)
    wad = vagwad.read_bytes()
    out_dir = bytearray(struct.pack('<I', len(names)))
    out_wad = bytearray()
    report = {}
    for name in names:
        key = iso_name(name)
        if key not in entries:
            raise ValueError(f'{name}: not in {vagdir.name}')
        start = entries[key] * SECTOR
        header = wad[start:start + 48]
        if len(header) < 48 or header[:4] != b'VAGp':
            raise ValueError(f'{name}: no big-endian VAGp header at sector {entries[key]}')
        size, rate = struct.unpack_from('>II', header, 12)
        if not size or size % 16 or start + 48 + size > len(wad) or not 8000 <= rate <= 48000:
            raise ValueError(f'{name}: header size {size} rate {rate}')
        sector = len(out_wad) // SECTOR
        out_dir += key.encode('ascii') + struct.pack('<I', sector)
        out_wad += wad[start:start + 48 + size]
        out_wad += b'\0' * (-len(out_wad) % SECTOR)
        report[key.strip()] = dict(sector=sector, bytes=size, rate=rate, seconds=round(size // 16 * 28 / rate, 3))
    return bytes(out_dir), bytes(out_wad), report


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('vag', type=Path, help='the extracted VAG directory (VAGDIR.AYB, VAGWAD.ENG)')
    parser.add_argument('out', type=Path, help='output directory (under build/)')
    parser.add_argument('names', nargs='+')
    parser.add_argument('--language', default='ENG')
    args = parser.parse_args()
    out_dir, out_wad, report = subset(args.vag / 'VAGDIR.AYB', args.vag / f'VAGWAD.{args.language}', args.names)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'VAGDIR.AYB').write_bytes(out_dir)
    (args.out / f'VAGWAD.{args.language}').write_bytes(out_wad)
    for key, info in report.items():
        print(key, info)


if __name__ == '__main__':
    main()
