"""Independent disc-byte and launch-contract checks (AI-assisted), ISC.

All backend subprocesses below are mocks. This module never runs Flycast.
"""
import contextlib
import io
import json
from pathlib import Path
import runpy
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import make_boot_gdi as writer


def read_iso(image, base=0):
    """Independent ISO reader: validate both endian copies and walk directories."""
    def number(data, at, width):
        a = int.from_bytes(data[at:at + width], 'little')
        b = int.from_bytes(data[at + width:at + 2 * width], 'big')
        assert a == b, 'endian copies differ'
        return a

    def extent(lba, size):
        at = (lba - base) * 2048
        assert at >= 0 and at + size <= len(image), 'extent outside track'
        return image[at:at + size]

    pvd = image[32768:34816]
    assert pvd[:7] == b'\x01CD001\x01'
    assert image[34816:34823] == b'\xffCD001\x01'
    assert number(pvd, 80, 4) == base + len(image) // 2048
    assert number(pvd, 128, 2) == 2048
    table_bytes = number(pvd, 132, 4)
    tables = []
    for at, endian in ((140, 'little'), (148, 'big')):
        table = extent(int.from_bytes(pvd[at:at + 4], endian), table_bytes)
        pos, rows = 0, []
        while pos < len(table):
            n = table[pos]
            rows.append((table[pos + 8:pos + 8 + n],
                         int.from_bytes(table[pos + 2:pos + 6], endian),
                         int.from_bytes(table[pos + 6:pos + 8], endian)))
            pos += 8 + n + n % 2
        assert pos == len(table)
        tables.append(rows)
    assert tables[0] == tables[1], 'path tables disagree'
    files, seen_dirs = {}, {}

    def walk(row, path, parent_lba):
        lba, size = number(row, 2, 4), number(row, 10, 4)
        assert lba not in seen_dirs, 'directory cycle'
        seen_dirs[lba] = path
        data, pos, entries = extent(lba, size), 0, []
        while pos < len(data):
            length = data[pos]
            if not length:
                pos = (pos // 2048 + 1) * 2048
                continue
            assert length >= 34 and pos % 2048 + length <= 2048
            child = data[pos:pos + length]
            n = child[32]
            name = child[33:33 + n]
            target, count = number(child, 2, 4), number(child, 10, 4)
            assert number(child, 28, 2) == 1
            entries.append(name)
            if name in (b'\0', b'\1'):
                assert target == (lba if name == b'\0' else parent_lba)
            else:
                key = path + name.decode().removesuffix(';1')
                if child[25] & 2:
                    walk(child, key + '/', lba)
                else:
                    assert key not in files
                    files[key] = extent(target, count)
            pos += length
        assert entries[:2] == [b'\0', b'\1']

    root = pvd[156:190]
    walk(root, '', number(root, 2, 4))
    for name, lba, parent in tables[0]:
        assert lba in seen_dirs and 1 <= parent <= len(tables[0])
        assert name == (seen_dirs[lba].rstrip('/').rsplit('/', 1)[-1].encode() if seen_dirs[lba] else b'\0')
        assert seen_dirs[tables[0][parent - 1][1]] == seen_dirs[lba].rstrip('/').rpartition('/')[0] + ('/' if '/' in seen_dirs[lba].rstrip('/') else '')
    return files


def small_elf():
    elf = bytearray(256)
    elf[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HHIII', elf, 16, 2, 42, 1, 0x8c010000, 52)
    struct.pack_into('<HH', elf, 42, 32, 2)
    struct.pack_into('<8I', elf, 52, 1, 128, 0x8c010000, 0x8c010000, 4, 16, 5, 32)
    struct.pack_into('<8I', elf, 84, 1, 160, 0x8c010040, 0x8c010040, 5, 8, 4, 32)
    elf[128:132], elf[160:165] = b'CODE', b'STAGE'
    return bytes(elf)


class DiscTests(unittest.TestCase):
    def test_iso_roundtrip_both_sessions_and_large_directory(self):
        files = {f'live/cells/-{i}_10.dci': bytes([i]) * (i * 39) for i in range(180)}
        files.update({'EICHAR.DGO': b'art' * 1001, 'boot/GAME.CGO.zst': b'compressed'})
        expected = {p.upper(): d for p, d in files.items()}
        for base in (0, 45000):
            self.assertEqual(read_iso(writer.iso_image(files, base), base), expected)

    def test_name_refusals(self):
        for files in ({'a': b'', 'A': b''}, {'../art': b''}, {'a/b': b'', 'a': b''}, {'a b': b''}):
            with self.assertRaises(ValueError):
                writer.iso_image(files)

    def test_elf_vmas_gaps_and_refusals(self):
        elf = small_elf()
        self.assertEqual(writer.elf_binary(elf), b'CODE' + bytes(60) + b'STAGE')
        for offset, fmt, value in ((18, '<H', 3), (24, '<I', 0), (104, '<I', 4),
                                   (60, '<I', 0x8c000000), (68, '<I', 1000), (92, '<I', 0x8d000000)):
            bad = bytearray(elf)
            struct.pack_into(fmt, bad, offset, value)
            with self.assertRaises(ValueError):
                writer.elf_binary(bad)
        with self.assertRaises(ValueError):
            writer.elf_binary(elf[:100])

    def test_generated_real_disc_every_file_and_boot(self):
        # This is a host artifact audit, never evidence of SH-4 execution.
        out = PORT / 'build/arena-jak-gdi'
        if not (out / 'manifest.json').is_file():
            self.skipTest('generate the private GDI first')
        manifest = json.loads((out / 'manifest.json').read_text())
        low = read_iso((out / 'track01.iso').read_bytes())
        self.assertEqual(set(low), {n.upper() for n in manifest['files']})
        for name, row in manifest['files'].items():
            self.assertEqual(len(low[name.upper()]), row['bytes'])
            self.assertEqual(writer.sha(low[name.upper()]), row['sha256'])
        high = read_iso((out / 'track03.iso').read_bytes(), 45000)
        self.assertEqual(set(high), {'1ST_READ.BIN', 'IP.BIN'})
        elf = Path(manifest['elf']).read_bytes()
        self.assertEqual(writer.sha(elf), manifest['elf_sha256'])
        self.assertEqual(high['1ST_READ.BIN'], writer.elf_binary(elf))
        self.assertEqual(high['IP.BIN'], (out / 'track03.iso').read_bytes()[:32768])
        ip = high['IP.BIN']
        self.assertEqual(ip[0x60:0x70], b'1ST_READ.BIN    ')
        self.assertEqual(ip[0x25:0x2b], b'GD-ROM')
        self.assertEqual(ip[0x20:0x24], ('%04X' % writer.binascii.crc_hqx(ip[0x40:0x50], 0xffff)).encode())
        template = Path(manifest['ip_template']).read_bytes()
        self.assertEqual(ip[256:], template[256:])
        identity = writer.disc_identity(out / 'jak-arena.gdi')
        self.assertEqual(identity, manifest['disc'])
        rows = (out / 'jak-arena.gdi').read_text().splitlines()
        audio_lba = int(rows[2].split()[1])
        self.assertGreaterEqual(audio_lba, len((out / 'track01.iso').read_bytes()) // 2048 + 150)
        self.assertLess(audio_lba + 300 + 150, 45000)
        print(f'Host GDI audit: {len(low)} staged files match; boot {len(high["1ST_READ.BIN"])} bytes match ELF')


class BackendTests(unittest.TestCase):
    def run_backend(self, root, disc=False, failure=False):
        elf = root / 'symbols.elf'
        elf.write_bytes(small_elf())
        image = root / ('image.gdi' if disc == 'gdi' else 'image.cdi')
        image.write_bytes(b'host mock only')
        if disc == 'gdi':
            (root / 'data.iso').write_bytes(bytes(2048))
            (root / 'audio.raw').write_bytes(bytes(2352))
            image.write_text('3\n1 0 4 2048 data.iso 0\n2 200 0 2352 audio.raw 0\n3 45000 4 2048 data.iso 0\n')
            identity = writer.disc_identity(image)
            identity['path'] = 'previous/location/image.gdi'
            (root / 'manifest.json').write_text(json.dumps(dict(
                boot_sha256='host fixture', elf_sha256=writer.sha(elf.read_bytes()), disc=identity)))
        out = root / 'result'
        argv = ['verify_backend.py', 'NEVER_LAUNCHED_FLYCAST', str(elf), '--nm', 'MOCK_NM', '--output', str(out)]
        if disc:
            argv += ['--disc', str(image)]
        class Process:
            def poll(self):
                return 0
        def fake_launch(command, **kwargs):
            report = struct.pack('<12I', 0x34485347, 1, 3 if failure else 2, 17, 16 if failure else 17, 9 if failure else 0, 16777216, 0, 0, 0, 0, 0)
            (out / 'target.bin').write_bytes(report + bytes(128 - len(report)))
            return Process()
        # Keep the actual platform's startup branch; the copy is mocked too.
        with patch.object(sys, 'argv', argv), patch('subprocess.check_output', return_value='8c010040 T _dc_backend\n') as nm, \
                patch('subprocess.Popen', side_effect=fake_launch) as launch, patch('shutil.copy2'), \
                contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit) as ended:
                runpy.run_path(str(PORT / 'verify_backend.py'), run_name='__main__')
        self.assertEqual(ended.exception.code, 1 if failure else 0)
        self.assertEqual(nm.call_args.args[0], ['MOCK_NM', str(elf)])
        command = launch.call_args.args[0]
        self.assertEqual(command[-1], str((image if disc else elf).resolve()))
        self.assertNotIn(str(elf.resolve()), command[:-1])
        self.assertIn('config:UseReios=yes', command)
        self.assertIn('config:Dreamcast.RamMod32MB=no', command)
        result = json.loads((out / 'result.json').read_text())
        self.assertEqual(result['result'], 'FAIL' if failure else 'PASS')
        self.assertEqual(result['elf_sha256'], writer.sha(elf.read_bytes()))
        self.assertEqual('disc' in result, bool(disc))
        if disc:
            self.assertEqual(result['disc']['sha256'], writer.sha(image.read_bytes()))
        if disc == 'gdi':
            self.assertEqual(len(result['disc']['tracks']), 3)

    def test_bare_elf_default_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.run_backend(Path(tmp))

    def test_disc_single_content_and_failure_stays_failure(self):
        for kind in ('gdi', 'cdi'):
            for failure in (False, True):
                with tempfile.TemporaryDirectory() as tmp:
                    self.run_backend(Path(tmp), disc=kind, failure=failure)

    def test_stale_elf_or_modified_disc_refused_before_launch(self):
        for mismatch in ('elf', 'disc', 'track'):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                elf, disc = root / 'symbols.elf', root / ('image.gdi' if mismatch == 'track' else 'image.cdi')
                elf.write_bytes(small_elf())
                disc.write_bytes(b'host mock only')
                if mismatch == 'track':
                    (root / 'data.iso').write_bytes(bytes(2048))
                    (root / 'audio.raw').write_bytes(bytes(2352))
                    disc.write_text('3\n1 0 4 2048 data.iso 0\n2 200 0 2352 audio.raw 0\n3 45000 4 2048 data.iso 0\n')
                manifest = dict(boot_sha256='marker', elf_sha256=writer.sha(elf.read_bytes()),
                                disc=writer.disc_identity(disc))
                if mismatch == 'elf':
                    elf.write_bytes(elf.read_bytes() + b'changed')
                elif mismatch == 'track':
                    (root / 'data.iso').write_bytes(b'changed'.ljust(2048, b'\0'))
                else:
                    disc.write_bytes(b'modified')
                (root / 'manifest.json').write_text(json.dumps(manifest))
                argv = ['verify_backend.py', 'NEVER_LAUNCHED_FLYCAST', str(elf), '--disc', str(disc),
                        '--nm', 'MOCK_NM', '--output', str(root / 'out')]
                with patch.object(sys, 'argv', argv), patch('subprocess.Popen') as launch, \
                        patch('subprocess.check_output') as nm, contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as ended:
                        runpy.run_path(str(PORT / 'verify_backend.py'), run_name='__main__')
                self.assertEqual(ended.exception.code, 2)
                launch.assert_not_called()
                nm.assert_not_called()


if __name__ == '__main__':
    unittest.main()
