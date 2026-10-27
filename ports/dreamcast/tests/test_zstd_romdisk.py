"""RFC 8878 framing/window checks, without decompression (AI-assisted).

Reference: https://www.rfc-editor.org/rfc/rfc8878.html sections 3.1.1.1-2.
Only frame and block boundaries are parsed: compressed block contents and the
optional checksum are not decoded/validated. No zstd executable is required.
"""
from pathlib import Path
import unittest

PORT = Path(__file__).resolve().parents[1]
COMPRESSED = PORT / 'build/kernel-dgo-rd/GAME.CGO.zst'
RAW = PORT / 'build/kernel-dgo/GAME.CGO'


def frame_info(data):
    offset = 0

    def take(n):
        nonlocal offset
        if offset + n > len(data):
            raise ValueError('truncated zstd frame')
        part = data[offset:offset + n]
        offset += n
        return int.from_bytes(part, 'little')

    if take(4) != 0xfd2fb528:
        raise ValueError('expected one ordinary zstd frame')
    descriptor = take(1)
    if descriptor & 8:
        raise ValueError('reserved zstd descriptor bit')
    flag, single = descriptor >> 6, bool(descriptor & 32)
    window = None
    if not single:
        wd = take(1)
        base = 1 << (10 + (wd >> 3))
        window = base + (base >> 3) * (wd & 7)
    take((0, 1, 2, 4)[descriptor & 3])  # dictionary ID
    fcs_bytes = (1 if single else 0) if flag == 0 else (2, 4, 8)[flag - 1]
    content_size = take(fcs_bytes) if fcs_bytes else None
    if fcs_bytes == 2:
        content_size += 256
    if single:
        window = content_size
    blocks = 0
    while True:
        header = take(3)
        last, kind, size = header & 1, (header >> 1) & 3, header >> 3
        if kind == 3 or size > min(window, 128 * 1024):
            raise ValueError('invalid zstd block header')
        take(1 if kind == 1 else size)
        blocks += 1
        if last:
            break
    if descriptor & 4:
        take(4)
    if offset != len(data):
        raise ValueError('trailing data or multiple zstd frames')
    return dict(window_size=window, content_size=content_size, blocks=blocks)


def block(payload, kind=0, last=True, size=None):
    size = len(payload) if size is None else size
    return ((size << 3) | (kind << 1) | int(last)).to_bytes(3, 'little') + payload


class ZstdFramingTest(unittest.TestCase):
    def test_single_segment_and_magic_inside_raw_payload(self):
        payload = b'\x28\xb5\x2f\xfd'
        frame = b'\x28\xb5\x2f\xfd\x20\x04' + block(payload)
        self.assertEqual(frame_info(frame), dict(window_size=4, content_size=4, blocks=1))

    def test_window_unknown_size_multiblock_rle_and_checksum(self):
        frame = b'\x28\xb5\x2f\xfd\x04\x38'
        frame += block(b'abc', last=False) + block(b'x', kind=1, size=10) + bytes(4)
        self.assertEqual(frame_info(frame), dict(window_size=131072, content_size=None, blocks=2))

    def test_content_size_widths_offset_and_dictionary_field(self):
        for flag, width, size in [(1, 2, 256), (2, 4, 70000), (3, 8, 131072)]:
            with self.subTest(flag=flag):
                frame = b'\x28\xb5\x2f\xfd' + bytes([(flag << 6) | 3, 0x38]) + bytes(4)
                frame += (size - (256 if width == 2 else 0)).to_bytes(width, 'little')
                frame += block(b'x', kind=1, size=size)
                self.assertEqual(frame_info(frame)['content_size'], size)

    def test_truncation_multiple_frames_reserved_fields_and_oversized_blocks(self):
        valid = b'\x28\xb5\x2f\xfd\x00\x38' + block(b'x')
        for bad in (valid[:-1], valid + valid, valid + b'junk', b'',
                    b'\x28\xb5\x2f\xfd\x08',
                    b'\x28\xb5\x2f\xfd\x00\x38' + block(b'', kind=3),
                    b'\x28\xb5\x2f\xfd\x00\x00' + block(b'x', kind=1, size=1025)):
            with self.subTest(frame=bad), self.assertRaises(ValueError):
                frame_info(bad)


class ZstdRomdiskTest(unittest.TestCase):
    @unittest.skipUnless(COMPRESSED.is_file(), 'build compressed GAME.CGO romdisk first')
    def test_one_frame_and_bounded_window(self):
        self.assertLessEqual(frame_info(COMPRESSED.read_bytes())['window_size'], 128 * 1024)

    @unittest.skipUnless(COMPRESSED.is_file() and RAW.is_file(), 'build compressed and raw GAME.CGO first')
    def test_declared_content_size_matches_raw_archive(self):
        size = frame_info(COMPRESSED.read_bytes())['content_size']
        if size is not None:
            self.assertEqual(size, RAW.stat().st_size)
