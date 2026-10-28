"""Test DCSH inspection CLI with synthetic modules (AI-assisted).

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

from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
from native_module_format import pack_native_module
from dcsh_tool import read_module

class DcshToolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.module = self.root/'test.dcsh'
        self.module.write_bytes(pack_native_module({
            'a': (b'\x09\x00', {'name':'small', 'relocations':[]}),
            'b': (b'\xaa\xaa\xaa\x0b\x00', {'name':'large', 'relocations':[]})},
            [{'name':'object', 'data':b'123', 'address_offset':0, 'relocations':[]}]))
    def run_tool(self, *args, check=True):
        return subprocess.run([sys.executable, str(PORT/'dcsh_tool.py'), *map(str,args)], capture_output=True, text=True, check=check)
    def test_list_extract_find(self):
        result = self.run_tool('list', self.module).stdout.splitlines()
        self.assertEqual(result[:3], ['function\tlarge\t5', 'data\tobject\t3', 'function\tsmall\t2'])
        self.assertIn('total objects\t3\t10 bytes', result)
        output = self.root/'code.bin'
        self.run_tool('extract', self.module, 'large', output)
        self.assertEqual(output.read_bytes(), b'\xaa\xaa\xaa\x0b\x00')
        second = self.root/'second.dcsh'; second.write_bytes(self.module.read_bytes())
        hits = self.run_tool('find', self.module, second, '--hex', 'aa aa').stdout.splitlines()
        self.assertEqual(hits, [f'{p}\tlarge\t0x{o:x}' for p in (self.module, second) for o in (0,1)])
        self.assertEqual(self.run_tool('find', self.module, '--hex', 'ff').stdout, '')
        self.assertNotEqual(self.run_tool('extract', self.module, 'object', output, check=False).returncode, 0)
        for pattern in ('', 'zz', 'a'):
            self.assertNotEqual(self.run_tool('find', self.module, '--hex', pattern, check=False).returncode, 0)
    def test_invalid_modules(self):
        good = self.module.read_bytes()
        for raw in (b'', b'BAD!'+good[4:], good[:-1], good[:-1]+b'!'):
            self.module.write_bytes(raw)
            with self.assertRaises(ValueError): read_module(self.module)
        raw = bytearray(good)
        struct.pack_into('<I', raw, 68, 0xffffffff)
        struct.pack_into('<I', raw, 44, zlib.crc32(raw[64:]))
        self.module.write_bytes(raw)
        with self.assertRaises(ValueError): read_module(self.module)
    @unittest.skipUnless(shutil.which('sh-elf-objdump'), 'sh-elf-objdump is absent')
    def test_disasm(self):
        result = self.run_tool('disasm', self.module, 'small', '--objdump', shutil.which('sh-elf-objdump'))
        self.assertIn('nop', result.stdout)

if __name__ == '__main__': unittest.main()
