"""Offline VQ payload and shared native loader checks (AI-assisted)."""
import struct
import unittest
import test_textured
from vq_texture import encode, decode, payload_size

class VQTest(unittest.TestCase):
    setUp=test_textured.TexturedTest.setUp
    tearDown=test_textured.TexturedTest.tearDown
    native=test_textured.TexturedTest.native
    add_texture=test_textured.TexturedTest.add_texture
    def test_vq_exact_blocks_padding_and_native_roundtrip(self):
        self.add_texture()
        raw, raw_report = __import__('convert_textured').convert(self.doc,self.binary,0)
        packed, report = __import__('convert_textured').convert(self.doc,self.binary,0,texture_format='vq')
        self.assertEqual(report['texture_bytes'],2080)
        self.assertTrue(report['vq_quality'][0]['lossless'])
        self.assertEqual(self.native(packed).returncode,0)
        pixels=32+report['geometry_bytes']+16+12
        self.assertEqual(decode(packed[pixels:],8,8),raw[pixels:])
        self.assertEqual(packed[-16:],b'\0'*16)
        self.assertNotEqual(self.native(packed[:-1]).returncode,0)
        for offset,value in [(pixels-8,struct.pack('<I',3)),(pixels-10,struct.pack('<H',16)),
                             (pixels-4,struct.pack('<I',2064))]:
            bad=packed[:offset]+value+packed[offset+len(value):]
            self.assertNotEqual(self.native(bad).returncode,0)

    def test_vq_known_hardware_block_order_and_determinism(self):
        # Four distinct pixels per codebook entry expose 2x2 ordering errors.
        words=[0x8000,0x801f,0x83e0,0xfc00]*16
        raw=struct.pack('<64H',*words)
        data,quality=encode(raw,8,8)
        self.assertEqual(decode(data,8,8),raw)
        self.assertEqual(data[:8],struct.pack('<4H',*words[:4]))
        self.assertEqual(quality['alpha_mismatches'],0)
        self.assertEqual(encode(raw,8,8)[0],data)

    def test_vq_all_alpha_patterns_survive_lossy_quantization(self):
        words=[]
        for block in range(1024):
            for pixel in range(4):
                words.append((((block%16)>>pixel)&1)<<15 | ((block*71+pixel*373)&0x7fff))
        raw=struct.pack('<4096H',*words)
        data,quality=encode(raw,64,64)
        self.assertEqual(len(data),3072)
        self.assertEqual(quality['alpha_mismatches'],0)
        self.assertGreater(quality['rgb5_mse'],0)

    def test_vq_bounded_dimensions_and_payload(self):
        self.assertEqual(payload_size(128,128),6144)
        for width,height in [(0,0),(4,4),(8,16),(7,7),(1024,1024)]:
            with self.assertRaises(ValueError): payload_size(width,height)
        with self.assertRaises(ValueError): decode(b'\0'*2048,8,8)
        with self.assertRaises(ValueError): encode(b'',8,8)
