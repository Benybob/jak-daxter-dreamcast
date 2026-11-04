"""Build a two-session boot GDI without external writers (AI-assisted), ISC.

Track 1 holds the staged /cd files for KOS's low-density TOC reader. Track 3
holds IP.BIN and an UNSCRAMBLED flat SH-4 image for GD-ROM boot at LBA 45000.
This is an emulator GDI, not a burnable CD-R or a GD-ROM streaming benchmark.
"""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct

SECTOR = 2048
BOOT_ADDRESS = 0x8c010000
HIGH_LBA = 45000
PORT = Path(__file__).resolve().parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def elf_binary(data):
    """Flatten file-backed PT_LOADs, preserving VMAs and zeroing the gaps.

    An ELF header cannot be used as 1ST_READ.BIN. In particular .dc_stage and
    .dc_fly must stay at their linked high addresses, not follow .data densely.
    """
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('expected little-endian ELF32')
    kind, machine, version, entry, offset = struct.unpack_from('<HHIII', data, 16)
    entsize, count = struct.unpack_from('<HH', data, 42)
    if (kind, machine, version, entry, entsize) != (2, 42, 1, BOOT_ADDRESS, 32):
        raise ValueError('expected executable SH-4 ELF with entry 0x8c010000')
    if not count or offset + count * entsize > len(data):
        raise ValueError('invalid program table')
    segments = []
    for i in range(count):
        tag, at, vma, pma, size, memory, flags, align = struct.unpack_from('<8I', data, offset + i * entsize)
        if tag != 1:
            continue
        if (pma != vma or size > memory or at + size > len(data) or
                vma < BOOT_ADDRESS or vma + memory > 0x8d000000):
            raise ValueError('PT_LOAD outside bootable 16 MiB memory map')
        segments.append((vma, at, size, memory))
    segments.sort()
    if not segments or segments[0][0] != BOOT_ADDRESS:
        raise ValueError('boot entry must begin the first PT_LOAD')
    for a, b in zip(segments, segments[1:]):
        if a[0] + a[3] > b[0]:
            raise ValueError('overlapping PT_LOADs')
    end = max(vma + size for vma, at, size, memory in segments)
    image = bytearray(end - BOOT_ADDRESS)
    for vma, at, size, memory in segments:
        image[vma - BOOT_ADDRESS:vma - BOOT_ADDRESS + size] = data[at:at + size]
    return bytes(image)


def ip_binary(template):
    """Keep the KOS bootstrap code; fill the makeip metadata and CRC fields."""
    if len(template) != 32768 or not any(template[0x300:0x400]):
        raise ValueError('expected KOS makeip 32768-byte ip.tmpl')
    ip = bytearray(template)
    for at, length, value in (
            (0, 16, 'SEGA SEGAKATANA'), (0x10, 16, 'SEGA ENTERPRISES'), (0x20, 16, '0000 GD-ROM1/1'),
            (0x30, 8, 'JUE'), (0x38, 8, 'E000F10'), (0x40, 10, 'T-00000'),
            (0x4a, 6, 'V1.000'), (0x50, 16, '20261103'),
            (0x60, 16, '1ST_READ.BIN'), (0x70, 16, 'OpenGOAL'),
            (0x80, 128, 'Jak arena (AI-assisted)')):
        ip[at:at + length] = value.encode().ljust(length, b' ')
    ip[0x20:0x24] = ('%04X' % binascii.crc_hqx(ip[0x40:0x50], 0xffff)).encode()
    return bytes(ip)


def both(value, width):
    return value.to_bytes(width, 'little') + value.to_bytes(width, 'big')


def record(name, extent, size, directory=False):
    n = 33 + len(name) + (len(name) % 2 == 0)
    return (bytes([n, 0]) + both(extent, 4) + both(size, 4) +
            bytes([126, 11, 3, 0, 0, 0, 0, 2 if directory else 0, 0, 0]) +
            both(1, 2) + bytes([len(name)]) + name + bytes(n - 33 - len(name)))


def directory_data(records):
    out = bytearray()
    for row in records:
        if len(out) % SECTOR + len(row) > SECTOR:
            out += bytes((-len(out)) % SECTOR)
        out += row
    return bytes(out) + bytes((-len(out)) % SECTOR)


def iso_image(files, base=0, bootstrap=bytes(32768)):
    """ISO9660 with both path tables, sector-safe directories, absolute LBAs.

    Relaxed ASCII names retain '-' and multiple dots in the existing fixtures.
    KOS fncompare is case-insensitive and removes the ISO ;1 version suffix.
    No Joliet/Rock Ridge, name truncation, compression, or retail transformations.
    """
    if len(bootstrap) != 32768:
        raise ValueError('bootstrap must fill the 16-sector system area')
    normalized, dirs = {}, {''}
    for path, data in files.items():
        parts = path.split('/')
        if any(not re.fullmatch(r'[A-Za-z0-9_.-]{1,29}', p) or p in ('.', '..') for p in parts):
            raise ValueError('unsupported ISO path: ' + path)
        name = '/'.join(parts).upper()
        if name in normalized:
            raise ValueError('case-insensitive ISO name collision: ' + path)
        normalized[name] = data
        for n in range(1, len(parts)):
            dirs.add('/'.join(parts[:n]).upper())
    if dirs.intersection(normalized):
        raise ValueError('ISO file/directory collision')
    ordered = sorted(dirs, key=lambda p: (p.count('/') + bool(p), p))
    children = {d: [] for d in dirs}
    for name in sorted((dirs - {''}) | normalized.keys()):
        parent, _, leaf = name.rpartition('/')
        children[parent].append((name, leaf.encode() + (b'' if name in dirs else b';1')))
    sizes = {d: len(directory_data([record(b'\0', 0, 0, True), record(b'\1', 0, 0, True)] +
                                 [record(leaf, 0, 0, name in dirs) for name, leaf in children[d]]))
             for d in dirs}
    path_size = sum(8 + len(d.rsplit('/', 1)[-1].encode() if d else b'\0') +
                    len(d.rsplit('/', 1)[-1].encode() if d else b'\0') % 2 for d in ordered)
    path_sectors = (path_size + SECTOR - 1) // SECTOR
    little, big = 18, 18 + path_sectors
    cursor = big + path_sectors
    extents = {}
    for d in ordered:
        extents[d] = base + cursor
        cursor += sizes[d] // SECTOR
    for name, data in sorted(normalized.items()):
        extents[name] = base + cursor
        cursor += max(1, (len(data) + SECTOR - 1) // SECTOR)
    image = bytearray(cursor * SECTOR)
    image[:32768] = bootstrap
    pvd = bytearray(SECTOR)
    pvd[:8] = b'\x01CD001\x01\0'
    pvd[8:40], pvd[40:72] = b'KALLISTIOS'.ljust(32), b'JAK_ARENA'.ljust(32)
    pvd[80:88] = both(base + cursor, 4)
    pvd[120:128], pvd[128:132] = both(1, 2) * 2, both(SECTOR, 2)
    pvd[132:140] = both(path_size, 4)
    struct.pack_into('<I', pvd, 140, base + little)
    struct.pack_into('>I', pvd, 148, base + big)
    pvd[156:190] = record(b'\0', extents[''], sizes[''], True)
    for at in (813, 830, 847):
        pvd[at:at + 17] = b'2026110300000000\0'
    pvd[864:881], pvd[881] = b'0' * 16 + b'\0', 1
    image[16 * SECTOR:17 * SECTOR] = pvd
    image[17 * SECTOR:17 * SECTOR + 7] = b'\xffCD001\x01'
    for sector, endian in ((little, 'little'), (big, 'big')):
        table = bytearray()
        for d in ordered:
            leaf = d.rsplit('/', 1)[-1].encode() if d else b'\0'
            parent = d.rpartition('/')[0]
            table += (bytes([len(leaf), 0]) + extents[d].to_bytes(4, endian) +
                      (ordered.index(parent) + 1).to_bytes(2, endian) + leaf + bytes(len(leaf) % 2))
        image[sector * SECTOR:sector * SECTOR + len(table)] = table
    for d in ordered:
        parent = d.rpartition('/')[0]
        rows = [record(b'\0', extents[d], sizes[d], True), record(b'\1', extents[parent], sizes[parent], True)]
        rows += [record(leaf, extents[name], sizes[name] if name in dirs else len(normalized[name]), name in dirs)
                 for name, leaf in children[d]]
        at = (extents[d] - base) * SECTOR
        image[at:at + sizes[d]] = directory_data(rows)
    for name, data in normalized.items():
        at = (extents[name] - base) * SECTOR
        image[at:at + len(data)] = data
    return bytes(image)


def disc_identity(path):
    """Hash the actual content argument AND every GDI track, before launch."""
    path = path.resolve()
    result = {'path': str(path), 'sha256': sha(path.read_bytes())}
    if path.suffix.lower() == '.gdi':
        lines = path.read_text().splitlines()
        if not lines or not 3 <= int(lines[0]) <= 99 or len(lines) != int(lines[0]) + 1:
            raise ValueError('GDI track count mismatch')
        tracks = []
        for line in lines[1:]:
            fields = shlex.split(line)
            if len(fields) != 6:
                raise ValueError('invalid GDI track row')
            track = path.parent / fields[4]
            if not track.resolve().is_relative_to(path.parent):
                raise ValueError('GDI track must stay in image directory')
            size = int(fields[3])
            data = track.read_bytes()
            if size not in (2048, 2352) or not data or len(data) % size:
                raise ValueError('invalid GDI track sector size/length')
            tracks.append({'file': fields[4], 'bytes': len(data), 'sha256': sha(data)})
        result['tracks'] = tracks
    elif path.suffix.lower() != '.cdi':
        raise ValueError('--disc requires .gdi or .cdi')
    return result


def build(elf, template, roots, out, required=('EICHAR.DGO', 'JAKART.DGO')):
    out = out.resolve()
    if not out.is_relative_to((PORT / 'build').resolve()):
        raise ValueError('disc output (including private retail data) must stay under build/')
    files = {}
    for root in roots:
        if not root.is_dir():
            raise ValueError('staged root missing: ' + str(root))
        for source in sorted(root.rglob('*')):
            if source.is_file():
                name = source.relative_to(root).as_posix()
                if name in files:
                    raise ValueError('duplicate staged path: ' + name)
                files[name] = source.read_bytes()
    for name in required:
        if name not in files:
            raise ValueError('missing staged ' + name)
    elf_data = elf.read_bytes()
    raw, ip = elf_binary(elf_data), ip_binary(template.read_bytes())
    low = iso_image(files)
    # Minimum Red Book audio track: 300 sectors, preceded by a 150-sector gap.
    audio_lba = len(low) // SECTOR + 150
    if audio_lba + 300 + 150 > HIGH_LBA:
        raise ValueError('staged files exceed low-density area')
    high = iso_image({'1ST_READ.BIN': raw, 'IP.BIN': ip}, HIGH_LBA, ip)
    out.mkdir(parents=True, exist_ok=True)
    for name, data in (('track01.iso', low), ('track02.raw', bytes(300 * 2352)), ('track03.iso', high)):
        (out / name).write_bytes(data)
    gdi = out / 'jak-arena.gdi'
    gdi.write_text(f'3\n1 0 4 2048 track01.iso 0\n2 {audio_lba} 0 2352 track02.raw 0\n3 45000 4 2048 track03.iso 0\n')
    manifest = dict(ai_assisted=True, license='ISC (tools only; retail data is user owned)',
                    elf=str(elf.resolve()), elf_sha256=sha(elf_data), boot_sha256=sha(raw),
                    boot_bytes=len(raw), boot_address=BOOT_ADDRESS, scrambled=False,
                    ip_template=str(template.resolve()), ip_template_sha256=sha(template.read_bytes()),
                    files={n: dict(bytes=len(d), sha256=sha(d)) for n, d in sorted(files.items())},
                    disc=disc_identity(gdi))
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--ip-template', type=Path, required=True, help='KOS utils/makeip/rsrc/templates/ip.tmpl')
    parser.add_argument('--staged', type=Path, action='append', required=True, help='merge this staged directory at /cd (repeatable)')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require', action='append',
                        help='staged file the disc must contain (repeatable; default EICHAR.DGO and JAKART.DGO)')
    args = parser.parse_args()
    try:
        result = build(args.elf, args.ip_template, args.staged, args.output,
                       tuple(args.require) if args.require else ('EICHAR.DGO', 'JAKART.DGO'))
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps({k: result[k] for k in ('elf_sha256', 'boot_bytes', 'boot_sha256', 'disc')}, indent=2))


if __name__ == '__main__':
    main()
