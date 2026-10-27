"""Bounded native module-file decoding through the real C linker (AI-assisted)."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
ROOT=Path(__file__).resolve().parents[1]; sys.path.insert(0,str(ROOT))
from native_module_format import pack_native_module
CHECK=ROOT/'build'/('native-file-check.exe' if sys.platform=='win32' else 'native-file-check')
def sample(relocations=None):
    return pack_native_module({'entry':(b'\x0b\0\x09\0'+b'\0'*4,
        {'name':'entry','relocations':relocations or []})},[],'entry',2)
def changed(data,offset,value):
    data=bytearray(data); struct.pack_into('<I',data,offset,value)
    struct.pack_into('<I',data,44,zlib.crc32(data[64:])); return data
class NativeFileTest(unittest.TestCase):
    def scan(self,data):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'module.dcn'; path.write_bytes(data)
            p=subprocess.run([str(CHECK),str(path)],capture_output=True,text=True)
        return p,json.loads(p.stdout)
    def test_native_file_links_without_host_execution(self):
        p,r=self.scan(sample()); self.assertEqual(p.returncode,0,p.stderr)
        self.assertEqual((r['functions'],r['code_bytes'],r['data_objects'],r['initializer_kind']),(1,8,0,2))
        self.assertEqual(sample(),sample())
    def test_corrupt_native_headers_and_records_leave_runtime_unchanged(self):
        data=sample()
        mutations=[(4,2),(8,0),(12,len(data)+1),(16,4097),(20,8193),(24,262145),(28,1048577),
                   (32,4*1048576+1),(36,1),(40,3),(48,1),(64,1),(68,1),(72,1),(76,1),(80,1),(84,1)]
        for offset,value in mutations:
            with self.subTest(offset=offset):
                p,r=self.scan(changed(data,offset,value)); self.assertNotEqual(p.returncode,0)
                self.assertEqual((r['used'],r['symbols'],r['functions']),(263104,13,0))
        for end in (0,1,63,64,len(data)-1):
            p,r=self.scan(data[:end]); self.assertNotEqual(p.returncode,0)
            self.assertEqual(r['used'],263104)
        bad=bytearray(data); bad[-1]^=1
        self.assertNotEqual(self.scan(bad)[0].returncode,0)
    def test_late_missing_native_import_rolls_back(self):
        p,r=self.scan(sample([{'offset':4,'symbol':'function:missing'}]))
        self.assertNotEqual(p.returncode,0)
        self.assertEqual((r['used'],r['symbols']),(263104,13))
    def test_private_data_relocations_and_import_names_are_checked(self):
        obj={'entry':(b'\x0b\0\x09\0',{'name':'entry','relocations':[]})}
        data=[dict(name='text',data=b'\0\0\0\0\x02\0\0\0ok\0',address_offset=4,
                   relocations=[{'offset':0,'symbol':'type:string'}])]
        binary=pack_native_module(obj,data)
        p,r=self.scan(binary); self.assertEqual(p.returncode,0,p.stderr)
        self.assertEqual(r['data_objects'],1)
        p,r=self.scan(changed(binary,64+24+12,11)); self.assertNotEqual(p.returncode,0)
    def test_writer_rejects_unknown_startup_abi_and_oversized_metadata(self):
        obj={'entry':(b'\x0b\0\x09\0',{'name':'entry','relocations':[]})}
        for kind in (0,3):
            with self.assertRaises(ValueError): pack_native_module(obj,[],'entry',kind)
        with self.assertRaises(ValueError): pack_native_module(obj,[],'missing',2)
