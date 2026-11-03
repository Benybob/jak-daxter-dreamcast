"""Read back every staged ROMFS file and check placement (AI-assisted), ISC."""
import hashlib
import json
from pathlib import Path
import struct

PORT = Path(__file__).resolve().parents[1]


def main():
    root = PORT / 'build/arena-live-disc/live'
    image = PORT / 'build/arena-live-romdisk.img'
    blob = image.read_bytes()
    align = lambda n: (n+15)//16*16
    size, = struct.unpack_from('>I', blob, 8)
    checksum = lambda b: sum(struct.unpack('>'+str(len(b)//4)+'I', b)) & 0xffffffff
    assert blob[:8] == b'-rom1fs-' and 0 < size <= len(blob) <= 1700000
    assert checksum(blob[:min(size,512)]) == 0
    first = align(blob.index(0,16)+1)
    files, seen = {}, set()

    def walk(offset, parent):
        while offset:
            assert offset % 16 == 0 and 0 < offset < size and offset not in seen
            seen.add(offset)
            nxt, spec, length, _ = struct.unpack_from('>4I',blob,offset)
            end = blob.index(0,offset+16)
            name = blob[offset+16:end].decode()
            data = align(end+1)
            assert checksum(blob[offset:data]) == 0
            if name not in ('.','..'):
                path = parent / name
                if nxt & 7 == 1:
                    walk(spec, path)
                else:
                    assert nxt & 7 == 2 and spec == 0 and data+length <= size
                    files[path.as_posix()] = blob[data:data+length]
            offset = nxt & ~15

    walk(first, Path())
    expected = {p.relative_to(root).as_posix():p.read_bytes() for p in root.rglob('*')
                if p.is_file() and p.suffix != '.json'}
    assert files == expected, 'image contents differ from generated live staging'
    boot = (PORT / 'build/kernel-dgo-romdisk.img').stat().st_size
    start = 0x8cb00000+(boot+31)//32*32
    end = start+(len(blob)+31)//32*32
    assert end <= 0x8d000000
    report = dict(ai_assisted=True, license='ISC', image_bytes=len(blob), image_ceiling=1700000,
                  remaining_bytes=1700000-len(blob), file_count=len(files),
                  payload_bytes=sum(map(len,files.values())), boot_bytes=boot,
                  staged_start=hex(start), staged_end=hex(end),
                  sha256=hashlib.sha256(blob).hexdigest())
    (PORT / 'build/arena-live-validation/image.json').write_text(json.dumps(report,indent=2)+'\n')
    (PORT / 'build/arena-live-section.txt').write_text(hex(start))
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
