"""Independent literal PAT and re-signed malformed metadata (AI-assisted), ISC."""
from pathlib import Path
import struct,subprocess,sys,unittest,zlib
PORT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(PORT))
import partition_collision as pc
def dcr(pat=0):return struct.pack('<4sIII9fI',b'DCR1',1,43,1,1,0,1,2,0,1,1,0,2,pat)
def resign(data):return data[:-4]+struct.pack('<I',zlib.crc32(data[:-4]))
class SectorContract(unittest.TestCase):
    def test_pat_bit3_matches_existing_native_solver(self):
        for pat in (0,8,16):self.assertEqual(pc.build_package(dcr(pat)).source_count,1)
        for pat in (24,32,40,48,56):
            with self.assertRaises(pc.PartitionError):pc.build_package(dcr(pat))
    def test_resigned_manifest_cannot_raise_caps(self):
        data=pc.build_package(dcr()).manifest
        for offset,value in ((12,0),(36,8192),(44,65536),(92,65536),(96,8192)):
            changed=bytearray(data);struct.pack_into('<I',changed,offset,value)
            with self.subTest(offset=offset),self.assertRaises(pc.PartitionError):pc.parse_manifest(resign(changed))
    def test_resigned_sector_rejects_bad_ids_and_values(self):
        data=next(iter(pc.build_package(dcr()).sectors.values()))
        for offset,value in ((24,65536),(28,0x7fc00001),(64,24)):
            changed=bytearray(data);struct.pack_into('<I',changed,offset,value)
            with self.subTest(offset=offset),self.assertRaises(pc.PartitionError):pc.parse_sector(resign(changed))
    def test_empty_committed_cache_is_air_not_unloaded(self):
        exe=PORT/'build'/('sector-air-check.exe' if sys.platform=='win32' else 'sector-air-check')
        result=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
        self.assertIn('81/81 checks',result.stdout)
