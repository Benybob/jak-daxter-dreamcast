"""Pack DCSH native modules into the original DGO container layout (AI-assisted).

Header: uint32 object count, 60-byte NUL-filled name. Each object: uint32 size,
60-byte NUL-filled name, payload, zero padding to 16 bytes. This is the layout
dgo_stream.c validates and goal_dgo.c loads; payloads are DCSH, not PS2 objects.
"""
import struct
from pathlib import Path


def _name(text):
    raw = text.encode('ascii')
    if not raw or len(raw) >= 60 or b'\0' in raw:
        raise ValueError(f'invalid DGO name {text!r}')
    return raw.ljust(60, b'\0')


def pack_dgo(name, objects):
    """objects: ordered (name, DCSH bytes) pairs. Returns the DGO bytes."""
    out = bytearray(struct.pack('<I', len(objects)) + _name(name))
    for object_name, payload in objects:
        if payload[:4] != b'DCSH':
            raise ValueError(f'{object_name} is not a DCSH module')
        out += struct.pack('<I', len(payload)) + _name(object_name) + payload
        out += b'\0' * ((16 - len(payload) % 16) % 16)
    return bytes(out)


def write_dgo(path, name, objects):
    data = pack_dgo(name, [(n, Path(p).read_bytes()) for n, p in objects])
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(data)
    return data
