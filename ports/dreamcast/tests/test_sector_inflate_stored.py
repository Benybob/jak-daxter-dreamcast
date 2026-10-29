"""sector_inflate_stored.c: stored-block zlib and crc32 (AI-assisted), ISC.

Builds the C file into a host DLL with MinGW/GCC (header from the host zlib) and
checks it against Python's zlib. Synthetic data only.
"""
from pathlib import Path
import ctypes
import os
import shutil
import subprocess
import tempfile
import unittest
import zlib

PORT = Path(__file__).resolve().parents[1]


def build():
    cc = shutil.which('gcc')
    if not cc:
        return None
    out = Path(tempfile.mkdtemp()) / ('stored.dll' if os.name == 'nt' else 'stored.so')
    cmd = [cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
           str(PORT / 'sector_inflate_stored.c'), '-o', str(out)]
    if subprocess.run(cmd, capture_output=True).returncode:
        return None
    return ctypes.CDLL(str(out))


LIB = build()


@unittest.skipIf(LIB is None, 'no host C compiler with zlib.h')
class StoredInflateTest(unittest.TestCase):
    def inflate(self, stream, size, crc):
        out = ctypes.create_string_buffer(max(size, 1))
        f = LIB.dc_sector_inflate
        f.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32]
        f.restype = ctypes.c_char_p
        err = f(stream, len(stream), out, size, crc)
        return (err.decode() if err else None), out.raw[:size]

    def test_crc32_matches_zlib(self):
        f = LIB.crc32
        f.argtypes = [ctypes.c_ulong, ctypes.c_char_p, ctypes.c_uint]
        f.restype = ctypes.c_ulong
        for data in (b'', b'a', bytes(range(256)) * 300):
            self.assertEqual(f(0, data, len(data)), zlib.crc32(data))
        self.assertEqual(f(zlib.crc32(b'ab'), b'cd', 2), zlib.crc32(b'abcd'))

    def test_level_zero_streams_round_trip_across_block_boundaries(self):
        for size in (1, 116, 65535, 65536, 475136):
            data = bytes((i * 7 + size) & 255 for i in range(size))
            err, out = self.inflate(zlib.compress(data, 0), size, zlib.crc32(data))
            self.assertIsNone(err, size)
            self.assertEqual(out, data)

    def test_refusals(self):
        data = bytes(range(200))
        good = zlib.compress(data, 0)
        crc = zlib.crc32(data)
        packed = b'a' * 200  # level 9 stores incompressible data, so compress something that shrinks
        self.assertEqual(self.inflate(zlib.compress(packed, 9), 200, zlib.crc32(packed))[0], 'stored zlib: compressed block')
        self.assertEqual(self.inflate(good, 199, crc)[0], 'stored zlib output size')
        self.assertEqual(self.inflate(good, 201, crc)[0], 'stored zlib output size')
        self.assertEqual(self.inflate(good, 200, crc ^ 1)[0], 'inflate output CRC')
        self.assertEqual(self.inflate(good[:-1], 200, crc)[0], 'stored zlib trailer')
        self.assertEqual(self.inflate(good + b'\0', 200, crc)[0], 'stored zlib trailer')
        bad = bytearray(good)
        bad[-1] ^= 1
        self.assertEqual(self.inflate(bytes(bad), 200, crc)[0], 'stored zlib Adler-32')
        bad = bytearray(good)
        bad[5] ^= 1  # NLEN
        self.assertEqual(self.inflate(bytes(bad), 200, crc)[0], 'stored zlib block length')
        bad = bytearray(good)
        bad[1] ^= 1
        self.assertEqual(self.inflate(bytes(bad), 200, crc)[0], 'stored zlib header')

    def test_empty_stream(self):
        f = LIB.dc_sector_inflate_empty
        f.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
        f.restype = ctypes.c_char_p
        self.assertIsNone(f(zlib.compress(b'', 0), len(zlib.compress(b'', 0))))
        self.assertEqual(f(zlib.compress(b'x', 0), len(zlib.compress(b'x', 0))).decode(), 'stored zlib output size')


if __name__ == '__main__':
    unittest.main()
