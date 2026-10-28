"""Inspect, extract, search and disassemble DCSH v1 modules (AI-assisted).

ISC License
Copyright (c) 2026
Permission to use, copy, modify, and/or distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright notice
and this permission notice appear in all copies.
THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
"""

import argparse
from dataclasses import dataclass
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

@dataclass(frozen=True)
class Entry:
    kind: str
    name: str
    code: bytes


def read_module(path):
    raw = Path(path).read_bytes()
    if len(raw) < 64:
        raise ValueError('truncated DCSH header')
    magic, version, abi, total, nf, nd, nr, ns, np, init, ik, crc, *reserved = struct.unpack_from('<4s15I', raw)
    if magic != b'DCSH' or version != 1 or abi != 1:
        raise ValueError('unsupported DCSH magic, version or ABI')
    strings_at = 64 + (nf + nd)*24 + nr*8
    payload_at = strings_at + ns
    if total != len(raw) or payload_at + np != total:
        raise ValueError('invalid DCSH section lengths')
    if zlib.crc32(raw[64:]) != crc:
        raise ValueError('DCSH checksum mismatch')
    strings = raw[strings_at:payload_at]
    def name(offset):
        if offset >= ns or (offset and strings[offset-1] != 0):
            raise ValueError('invalid DCSH string offset')
        end = strings.find(b'\0', offset)
        if end < 0: raise ValueError('unterminated DCSH string')
        return strings[offset:end].decode('utf-8')
    entries = []
    for i in range(nf+nd):
        no, offset, length, *extra = struct.unpack_from('<6I', raw, 64+i*24)
        if offset + length > np: raise ValueError('DCSH object exceeds payload')
        entries.append(Entry('function' if i < nf else 'data', name(no),
                             raw[payload_at+offset:payload_at+offset+length]))
    return entries


def function(entries, name):
    matches = [e for e in entries if e.kind == 'function' and e.name == name]
    if len(matches) != 1: raise ValueError(f'expected one function named {name!r}, found {len(matches)}')
    return matches[0].code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    listing = commands.add_parser('list'); listing.add_argument('file', type=Path)
    extract = commands.add_parser('extract'); extract.add_argument('file', type=Path)
    extract.add_argument('name'); extract.add_argument('out', type=Path)
    find = commands.add_parser('find'); find.add_argument('files', type=Path, nargs='+')
    find.add_argument('--hex', required=True, dest='hex_bytes')
    disasm = commands.add_parser('disasm'); disasm.add_argument('file', type=Path)
    disasm.add_argument('name'); disasm.add_argument('--objdump', default='sh-elf-objdump')
    args = parser.parse_args(argv)
    try:
        if args.command == 'find':
            pattern = bytes.fromhex(args.hex_bytes)
            if not pattern: raise ValueError('hex pattern must not be empty')
            for path in args.files:
                for entry in read_module(path):
                    if entry.kind != 'function': continue
                    start = 0
                    while (offset := entry.code.find(pattern, start)) >= 0:
                        print(f'{path}\t{entry.name}\t0x{offset:x}')
                        start = offset+1
        else:
            entries = read_module(args.file)
            if args.command == 'list':
                for entry in sorted(entries, key=lambda e: (-len(e.code), e.kind, e.name)):
                    print(f'{entry.kind}\t{entry.name}\t{len(entry.code)}')
                for kind in ('function', 'data'):
                    selected = [e for e in entries if e.kind == kind]
                    print(f'total {kind}\t{len(selected)}\t{sum(len(e.code) for e in selected)} bytes')
                print(f'total objects\t{len(entries)}\t{sum(len(e.code) for e in entries)} bytes')
            elif args.command == 'extract':
                args.out.write_bytes(function(entries, args.name))
            else:
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / 'function.bin'
                    path.write_bytes(function(entries, args.name))
                    subprocess.run([args.objdump, '-D', '-b', 'binary', '-m', 'sh4', '-EL', str(path)], check=True)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{parser.prog}: {error}\n')

if __name__ == '__main__':
    main()
