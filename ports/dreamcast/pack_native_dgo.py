"""Pack DCSH native modules into the original DGO container layout (AI-assisted).

Header: uint32 object count, 60-byte NUL-filled name. Each object: uint32 size,
60-byte NUL-filled name, payload, zero padding to 16 bytes. This is the layout
dgo_stream.c validates and goal_dgo.c loads; payloads are DCSH, not PS2 objects.
"""
import argparse
import struct
from pathlib import Path

HEADER = struct.Struct('<I60s')


def _padding_size(size):
    return (-size) % 16


def _name(text):
    raw = text.encode('ascii')
    if not raw or len(raw) >= 60 or b'\0' in raw:
        raise ValueError(f'invalid DGO name {text!r}')
    return raw.ljust(60, b'\0')


def pack_dgo(name, objects):
    """objects: ordered (name, DCSH bytes) pairs. Returns the DGO bytes."""
    out = bytearray(HEADER.pack(len(objects), _name(name)))
    for object_name, payload in objects:
        if payload[:4] != b'DCSH':
            raise ValueError(f'{object_name} is not a DCSH module')
        out += HEADER.pack(len(payload), _name(object_name)) + payload
        out += b'\0' * _padding_size(len(payload))
    return bytes(out)


def write_dgo(path, name, objects):
    data = pack_dgo(name, [(n, Path(p).read_bytes()) for n, p in objects])
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(data)
    return data


def list_dgo(data):
    """Describe a pack_dgo archive; sizes exclude headers and alignment padding."""
    def header_at(offset):
        if offset + HEADER.size > len(data):
            raise ValueError('short DGO header')
        size, raw = HEADER.unpack_from(data, offset)
        try:
            name = raw.split(b'\0', 1)[0].decode('ascii')
            if _name(name) != raw:
                raise ValueError('invalid DGO name padding')
        except UnicodeError as error:
            raise ValueError('invalid DGO name') from error
        return size, name

    count, name = header_at(0)
    offset, objects = HEADER.size, []
    for _ in range(count):
        size, object_name = header_at(offset)
        offset += HEADER.size
        end = offset + size
        padded_end = end + _padding_size(size)
        if padded_end > len(data):
            raise ValueError('short DGO payload or padding')
        if size < 4 or data[offset:offset + 4] != b'DCSH':
            raise ValueError(f'{object_name} is not a DCSH module')
        if any(data[end:padded_end]):
            raise ValueError('nonzero DGO padding')
        objects.append(dict(name=object_name, size=size))
        offset = padded_end
    if offset != len(data):
        raise ValueError('unexpected trailing DGO data')
    return dict(name=name, objects=objects, total=sum(o['size'] for o in objects))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', required=True, type=Path, metavar='DGO',
                        help='list native object names and payload sizes without loading code')
    args = parser.parse_args(argv)
    try:
        report = list_dgo(args.list.read_bytes())
    except (OSError, ValueError) as error:
        parser.error(str(error))
    for item in report['objects']:
        print(f"{item['name']}\t{item['size']}")
    print(f"total: {len(report['objects'])} objects, {report['total']} payload bytes")


if __name__ == '__main__':
    main()
