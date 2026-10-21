"""Replace only the fixture ROMFS data in the pinned milestone viewer (AI-assisted).

This is an asset-only ELF repack, not a compiler or a fresh SH-4 build. The new
scene must have exactly the fixture's payload length; code and ELF layout stay
unchanged. ROMFS layout: https://www.kernel.org/doc/html/latest/filesystems/romfs.html
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

BASE_SHA256 = 'd6bb58106bc1a0162a835f9dab4c9d0275344351cfd5913a0504f230b856fcf2'

def checksum(data):
    return sum(struct.unpack('>' + 'I' * (len(data)//4), data)) & 0xffffffff

def embed(base, scene):
    if hashlib.sha256(base).hexdigest() != BASE_SHA256:
        raise ValueError('base viewer does not match the milestone SHA-256')
    if scene[:4] != b'DCM1' or len(scene) < 8:
        raise ValueError('expected DCM1 scene')
    vertices = struct.unpack_from('<I', scene, 4)[0]
    if vertices != 1536 or len(scene) != 8 + vertices*16:
        raise ValueError('this asset-only repack requires exactly 512 triangles')
    candidates = []
    offset = 0
    while True:
        offset = base.find(b'-rom1fs-', offset)
        if offset < 0:
            break
        size = struct.unpack_from('>I', base, offset+8)[0]
        if 512 <= size <= len(base)-offset and checksum(base[offset:offset+512]) == 0:
            candidates.append((offset, size))
        offset += 8
    if len(candidates) != 1:
        raise ValueError('expected one valid embedded ROMFS')
    start, size = candidates[0]
    rom = bytearray(base[start:start+size])
    header = rom.find(b'fixture.dcm\0') - 16
    if header < 0 or header % 16 or (struct.unpack_from('>I', rom, header)[0] & 7) != 2:
        raise ValueError('fixture file header not found')
    data_start = header + 32
    if struct.unpack_from('>I', rom, header+8)[0] != len(scene):
        raise ValueError('fixture payload length mismatch')
    if checksum(rom[header:data_start]) != 0:
        raise ValueError('invalid original file checksum')
    rom[data_start:data_start+len(scene)] = scene
    rom[header+16:data_start] = b'scene.dcm\0'.ljust(16, b'\0')
    struct.pack_into('>I', rom, header+12, 0)
    struct.pack_into('>I', rom, header+12, (-checksum(rom[header:data_start])) & 0xffffffff)
    struct.pack_into('>I', rom, 12, 0)
    struct.pack_into('>I', rom, 12, (-checksum(rom[:512])) & 0xffffffff)
    assert checksum(rom[:512]) == checksum(rom[header:data_start]) == 0
    out = base[:start] + rom + base[start+size:]
    assert len(out) == len(base) and out[:start] == base[:start] and out[start+size:] == base[start+size:]
    return out, {'method': 'asset-only ROMFS replacement; no SH-4 code rebuilt',
                 'romfs_offset': start, 'romfs_bytes': size, 'triangles': vertices//3,
                 'base_sha256': BASE_SHA256, 'output_sha256': hashlib.sha256(out).hexdigest(),
                 'scene_sha256': hashlib.sha256(scene).hexdigest()}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('scene', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        data, report = embed(args.base.read_bytes(), args.scene.read_bytes())
        args.output.write_bytes(data)
        print(json.dumps(report, indent=2))
    except (ValueError, OSError) as error:
        parser.error(str(error))
