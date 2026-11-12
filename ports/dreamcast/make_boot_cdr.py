"""Build the data track of a self-booting Dreamcast CD-R (AI-assisted), ISC.

A real console boots a CD-R from the first data track of its second session
(MIL-CD): IP.BIN in the track's 16-sector system area and a SCRAMBLED
1ST_READ.BIN, which the boot ROM descrambles in place at 0x8c010000. This
writes that track as 2048-byte sectors with ISO9660 extents absolute from
--lba, the address the burner reports for the second session's first block
(burn_cdr.py), so it is built after session 1 is on the disc. The staged /cd
files go in the same track: KOS reads the last data track of the TOC.
"""
import argparse
import json
from pathlib import Path

import make_boot_gdi as gdi

MAXCHUNK = 2048 * 1024


class _Rand:
    def __init__(self, n):
        self.seed = n & 0xffff

    def __call__(self):
        self.seed = (self.seed * 2109 + 9273) & 0x7fff
        return (self.seed + 0xc000) & 0xffff


def _order(size, rand):
    """Slice order of one chunk, as KOS utils/scramble/scramble.c's save_chunk."""
    n = size // 32
    idx = list(range(n))
    out = []
    for i in range(n - 1, -1, -1):
        x = (rand() * i) >> 16
        idx[i], idx[x] = idx[x], idx[i]
        out.append(idx[i])
    return out


def _walk(data_len):
    """(offset, chunk size) pairs in scramble order; a tail under 32 bytes is plain."""
    rand = _Rand(data_len)
    left, at, chunk = data_len, 0, MAXCHUNK
    while chunk >= 32:
        while left >= chunk:
            yield at, _order(chunk, rand)
            left -= chunk
            at += chunk
        chunk >>= 1


def scramble(data):
    out = bytearray()
    for at, order in _walk(len(data)):
        for s in order:
            out += data[at + 32 * s:at + 32 * s + 32]
    out += data[len(out):]
    return bytes(out)


def descramble(data):
    """The boot ROM's load: read in order, place each slice (scramble.c load_chunk)."""
    out = bytearray(len(data))
    read = 0
    for at, order in _walk(len(data)):
        for s in order:
            out[at + 32 * s:at + 32 * s + 32] = data[read:read + 32]
            read += 32
    out[read:] = data[read:]
    return bytes(out)


def build(elf, template, roots, lba, out, required):
    out = out.resolve()
    if not out.is_relative_to((gdi.PORT / 'build').resolve()):
        raise ValueError('disc output (including private retail data) must stay under build/')
    if not 11400 <= lba < 300000:
        raise ValueError('second-session LBA out of range')
    files = {}
    for root in roots:
        for source in sorted(root.rglob('*')):
            if source.is_file():
                name = source.relative_to(root).as_posix()
                if name in files:
                    raise ValueError('duplicate staged path: ' + name)
                files[name] = source.read_bytes()
    for name in required:
        if name not in files:
            raise ValueError('missing staged ' + name)
    raw = gdi.elf_binary(elf.read_bytes())
    ip = bytearray(gdi.ip_binary(template.read_bytes()))
    ip[0x20:0x30] = b'0000 CD-ROM1/1  '
    ip[0x20:0x24] = ('%04X' % gdi.binascii.crc_hqx(ip[0x40:0x50], 0xffff)).encode()
    ip = bytes(ip)
    boot = scramble(raw)
    if descramble(boot) != raw:
        raise ValueError('scramble does not round-trip')
    files['1ST_READ.BIN'] = boot
    track = gdi.iso_image(files, lba, ip)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'session2.iso').write_bytes(track)
    manifest = dict(ai_assisted=True, lba=lba, sectors=len(track) // gdi.SECTOR, boot_bytes=len(raw),
                    boot_sha256=gdi.sha(raw), scrambled_sha256=gdi.sha(boot), track_sha256=gdi.sha(track),
                    elf_sha256=gdi.sha(elf.read_bytes()), files=len(files))
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--ip-template', type=Path, required=True)
    parser.add_argument('--staged', type=Path, action='append', required=True)
    parser.add_argument('--lba', type=int, required=True, help="the second session's first writable block")
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require', action='append', default=[])
    args = parser.parse_args()
    try:
        print(json.dumps(build(args.elf, args.ip_template, args.staged, args.lba, args.output, args.require), indent=2))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
