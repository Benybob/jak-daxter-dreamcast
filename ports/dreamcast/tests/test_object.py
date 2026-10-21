"""Original data table edge cases through the shared native C parser (AI-assisted)."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from make_object_fixture import fixture,object_file
CHECK=ROOT/'build'/('object-check.exe' if sys.platform=='win32' else 'object-check')

class ObjectTest(unittest.TestCase):
    def test_exact_one_past_is_valid_but_beyond_end_is_rejected(self):
        data=bytearray(fixture()); struct.pack_into('<I',data,28,64)
        self.assertEqual(self.scan(data).returncode,0)
        struct.pack_into('<I',data,28,65)
        self.assertIn('target bounds',self.scan(data).stderr)
    def test_relocation_inventory_preserves_kinds_names_and_offsets(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'asset.bin'; path.write_bytes(fixture())
            p=subprocess.run([str(CHECK),'--relocations',str(path)],check=True,capture_output=True,text=True)
        rows=[json.loads(line) for line in p.stdout.splitlines()]
        self.assertEqual([(r['kind'],r['offset'],r['name']) for r in rows[:-1]],
                         [(0,12,None),(2,0,'basic'),(1,4,'global'),(3,8,'_empty_')])
        self.assertEqual(rows[0]['target'],20)
    def scan(self,data):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'asset.bin'; path.write_bytes(data)
            return subprocess.run([str(CHECK),str(path)],capture_output=True,text=True)

    def test_original_v2_v4_pointer_symbol_type_and_empty_tables(self):
        for version,offset in [(2,64),(4,16)]:
            with self.subTest(version=version):
                result=self.scan(fixture(version)); self.assertEqual(result.returncode,0,result.stderr)
                layout=json.loads(result.stdout)
                self.assertEqual(layout,dict(version=version,data_offset=offset,data_bytes=64,
                    link_offset=0 if version==2 else 80,link_bytes=64,bitmap_bytes=2,
                    internal=1,symbols=1,types=1,empty=1,imports=3))

    def test_all_short_headers_tables_and_padding_fail_closed(self):
        data=fixture()
        for end in range(len(data)):
            with self.subTest(end=end): self.assertNotEqual(self.scan(data[:end]).returncode,0)
        for offset,replacement in [(0,b'BAD!'),(4,struct.pack('<I',65)),(8,struct.pack('<I',3)),
                (12,struct.pack('<I',65)),(84,struct.pack('<I',128)),(88,struct.pack('<I',4)),
                (28,struct.pack('<I',65)),(16,struct.pack('<I',0)),(143,b'\x01')]:
            with self.subTest(offset=offset):
                changed=data[:offset]+replacement+data[offset+len(replacement):]
                self.assertNotEqual(self.scan(changed).returncode,0)
        self.assertNotEqual(self.scan(data+b'\0').returncode,0)

    def test_pointer_255_continuations_switch_seek_and_fix_modes(self):
        for table,count in [(bytes([255,255,0,10,0]),10),(bytes([1,255,0,1,0]),255)]:
            result=self.scan(object_file(b'\0'*(520*4),table))
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(json.loads(result.stdout)['internal'],count)
        result=self.scan(object_file(b'\0'*64,bytes([255,1,0])))
        self.assertIn('run bounds',result.stderr)

    def test_import_variable_width_deltas_include_zero_first_offset(self):
        data=bytearray(66000)
        for offset in (0,4,260,65796,65800): struct.pack_into('<I',data,offset,0xffffffff)
        deltas=bytes([0,4,1,1,2,0,1,7,0,0,0,0])
        result=self.scan(object_file(data,imports=b'\x80basic\0'+deltas+b'\0'))
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)['types'],5)

    def test_duplicate_patches_and_bad_imports_rejected(self):
        data=bytearray(64); struct.pack_into('<I',data,0,0xffffffff)
        for imports,error in [(b'foo\0\0\0bar\0\0\0\0','duplicate'),
                              (b'\x80\0\0\0\0','name'),(b'\x01foo\0\0\0\0','encoding'),
                              (b'foo\0\x44\0\0','delta bounds')]:
            with self.subTest(error=error):
                self.assertIn(error,self.scan(object_file(data,imports=imports)).stderr)
        self.assertIn('placeholder',self.scan(object_file(b'\0'*64,imports=b'foo\0\0\0\0')).stderr)
