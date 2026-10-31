"""DCI4 C loader/cell/colour oracle, synthetic data only (AI-assisted), ISC."""
import ctypes as ct
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import instance_cells as cells
import instance_palette as palette
import tie_lods
from test_instance_cells import Owner, Package


class Palette(ct.Structure):
    _fields_ = [('data', ct.c_void_p)] + [(n, ct.c_uint32) for n in
                ('size', 'resident', 'count', 'proto_count', 'geometry_count')] + [
                (n, ct.c_void_p) for n in ('protos', 'geometries', 'tables')]


def synthetic(wide=False, enabled=True):
    pts = ((0., 0., 0.), (1., 0., 0.), (0., 1., 0.))
    uv = ((0., 0.), (1., 0.), (0., 1.))
    c = (0.25, 0.5, 0.75, 0.5)
    # Two corners at the same position/UV/ARGB diverge in another placement.
    base = [(pts, uv, (c, c, c), 3), (pts, uv, (c, c, c), 7)]
    n = 257 if wide else 4
    rows = [bytes(x for j in range(6) for x in ((i + j) % 256, i // 256, j)) for i in range(n)]
    factors = {3: (2., .5, 1., .75), 7: (.5, 2., 1., 1.)}
    tris, colours, tables, slots = palette.prototype(base, rows, [(99, 98, 97, 255)], factors)
    result = {'name': 'rock', 'geometry': tris if enabled else base, 'palette': colours,
              'slots': slots, 'tables': dict(enumerate(tables))}
    origin = [4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 0]
    instances = [{'proto': 0, 'origin': origin, 'x': float(i % 4) * 20, 'y': 0., 'z': float(i // 4) * 20}
                 for i in range(n)]
    lower = ([pts], [uv])
    lods = {'rock': [([pts, pts], [uv, uv]), lower, lower, None]}
    blob, _ = tie_lods.build_textured([result], instances, lods, {'rock': (10., 20., 30.)}, factors,
                                     True, enabled)
    return blob, rows, base, factors


class EncodingTests(unittest.TestCase):
    def test_disabled_bytes_match_legacy_pack(self):
        blob, _, base, factors = synthetic(enabled=False)
        # Legacy layout assembled independently, including shared lower LODs.
        expected = struct.pack('<4s3I', b'DCI3', 1, 3, 4)
        for i, (mat, name) in enumerate(((3, b'rock#3'), (7, b'rock#7'), (3, b'rock:1#3'))):
            expected += struct.pack('<5I32s', 3, 3, 3 * i, 3 * i, mat, name)
        expected += struct.pack('<4H3f4B', 0, 2, 2, 65535, 10., 20., 30., 1, 0, 0, 0)
        expected += struct.pack('<4f', .5, .5, 0., .5 / 32767) * 3
        for argb in (0x608040bf, 0x8020ffbf, 0x608040bf):
            for xyzuv in ((-32767, -32767, 0, 0, 0), (32767, -32767, 0, 2048, 0),
                          (-32767, 32767, 0, 0, 2048)):
                expected += struct.pack('<5hHI', *xyzuv, 0, argb)
        expected += struct.pack('<9H', *([0, 1, 2] * 3)) + bytes(2)
        for i in range(4):
            expected += struct.pack('<2H12f', 0, 0, 1., 0., 0., 0., 1., 0., 0., 0., 1., i * 20., 0., 0.)
        self.assertEqual(blob, expected)
        geoms = tie_lods.material_geometries(base, 'rock', factors)
        a, _ = tie_lods.build_textured([{'name': 'rock', 'geometry': base}], [], {}, {}, factors, True)
        b, _ = tie_lods.build_textured([{'name': 'rock', 'geometry': base}], [], {}, {}, factors, True, False)
        self.assertEqual(a, b)
        self.assertTrue(all(len(v) == 6 for g in geoms for v in g['verts']))

    def test_flatten_uses_the_selected_placement_table(self):
        import make_instance_fixture as mf
        blob, _, _, _ = synthetic()
        flat = tie_lods.flatten(blob, 0., 0.)
        geoms, instances = mf.read(flat)
        at = 12 + 52 * len(geoms)
        original_geoms, rows, _, _, original = tie_lods.read_dci2(blob)
        cursor = 0
        for i, inst in enumerate(original):
            lod = tie_lods.lod_index(rows[0][:4], rows[0][4:7], inst[-3] ** 2 + inst[-1] ** 2)
            for g in range(rows[0][lod], rows[0][lod] + 1 + rows[0][7 + lod]):
                proto = instances[cursor][0]
                vc, _, fv, _, _ = geoms[proto]
                for v in range(vc):
                    actual, = struct.unpack_from('<I', flat, at + 24 * (fv + v) + 20)
                    self.assertEqual(actual, palette.colour(blob, i, g, v))
                cursor += 1

    def test_shared_slots_split_otherwise_identical_corners(self):
        pts = ((0., 0., 0.),) * 3
        base = [(pts, ((0., 0.),) * 3, ((.5, .5, .5, 1.),) * 3, 0)]
        rows = [bytes([128] * 9), bytes([10, 20, 30, 40, 50, 60, 10, 20, 30])]
        tris, _, tables, slots = palette.prototype(base, rows, [], {})
        g = tie_lods.material_geometries(tris, 'test', {})[0]
        self.assertEqual(slots, 2)
        self.assertEqual(len(g['verts']), 2)
        self.assertEqual(g['indices'], [0, 1, 0])
        self.assertEqual(len(tables[0]), 2)


@unittest.skipUnless(shutil.which('gcc'), 'gcc is required for the C oracle')
class NativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        path = Path(cls.temp.name) / ('palette.dll' if os.name == 'nt' else 'palette.so')
        subprocess.run([shutil.which('gcc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-O2', '-shared',
                        '-static', str(PORT / 'instance_pass.c'), str(PORT / 'instance_cells.c'),
                        '-o', str(path), '-lm'], check=True, capture_output=True, text=True)
        cls.lib = ct.CDLL(str(path))
        if os.name == 'nt':
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        cls.lib.dc_instance_palette_open.argtypes = [ct.c_void_p, ct.c_size_t, ct.POINTER(Package), ct.POINTER(Palette)]
        cls.lib.dc_instance_palette_open.restype = ct.c_char_p
        cls.lib.dc_instance_palette_colour.argtypes = [ct.POINTER(Package), ct.POINTER(Palette), ct.c_void_p,
                                                       ct.c_uint32, ct.c_uint32]
        cls.lib.dc_instance_palette_colour.restype = ct.c_uint32
        cls.lib.dc_instance_cells_palette_make.argtypes = [ct.POINTER(Owner), ct.c_char_p, ct.c_double,
                                                           ct.c_uint32, ct.POINTER(Palette), ct.c_uint32]
        cls.lib.dc_instance_cells_palette_make.restype = ct.c_char_p
        cls.lib.dc_instance_cells_update.argtypes = [ct.POINTER(Owner), ct.POINTER(Package)] + [ct.c_double] * 3
        cls.lib.dc_instance_cells_update.restype = ct.c_char_p
        cls.lib.dc_instance_cells_free.argtypes = [ct.POINTER(Owner)]

    def load(self, blob):
        data = ct.create_string_buffer(blob)
        pkg, pal = Package(), Palette()
        error = self.lib.dc_instance_palette_open(data, len(blob), ct.byref(pkg), ct.byref(pal))
        return error, pkg, pal, data

    def check_colours(self, blob, pkg, pal, table, placement):
        geoms, rows, _, _, _ = tie_lods.read_dci2(blob)
        for g, geom in enumerate(geoms):
            for v in range(geom[0]):
                self.assertEqual(self.lib.dc_instance_palette_colour(ct.byref(pkg), ct.byref(pal), table, g, v),
                                 palette.colour(blob, placement, g, v))

    def test_u8_and_u16_c_colours_match_python_and_source(self):
        for wide in (False, True):
            blob, raw, base, factors = synthetic(wide)
            error, pkg, pal, data = self.load(blob)
            self.assertIsNone(error)
            at, ext, geoms, _, inst = palette.locate(blob)
            for i in range(len(inst)):
                offset, = struct.unpack_from('<I', ext, pal.resident + 4 * i)
                self.check_colours(blob, pkg, pal, ct.addressof(data) + at + offset, i)
                for g in (0, 1):
                    factor = factors[geoms[g][4]]
                    for v in range(3):
                        c = raw[i][3 * (3 * g + v):3 * (3 * g + v + 1)]
                        c = [struct.unpack('<f', struct.pack('<f', x / 255))[0] for x in c]
                        truth = tie_lods.colour((*c, .5), factor)
                        self.assertEqual(palette.colour(blob, i, g, v), truth)

    def test_malformed_packages_leave_outputs_unchanged(self):
        blob, _, _, _ = synthetic()
        at, ext, geoms, table, _ = palette.locate(blob)
        _, _, vstart, _, _ = tie_lods.read_dci2(blob)
        resident = struct.unpack_from('<I', ext, 8)[0]
        tests = [('truncated', blob[:-1])]
        def spoil(name, pos, value, fmt='<I'):
            data = bytearray(blob)
            struct.pack_into(fmt, data, pos, value)
            tests.append((name, data))
        spoil('extension size', at + 4, 0xffffffff)
        spoil('unaligned extension', at + 4, len(ext) - 1)
        spoil('resident', at + 8, 0xffffffff)
        spoil('count', at + 12, 999999)
        spoil('zero slots', at + 16, 0)
        spoil('entries', at + 20, 65537)
        desc = at + 16 + 8 * len(table)
        for field in range(4):
            spoil('geometry' + str(field), desc + 4 * field, 0xffffffff)
        spoil('slot', vstart + 10, 65535, '<H')
        spoil('offset', at + resident, 0xffffffff)
        spoil('offset inside header', at + resident, 16)
        first = struct.unpack_from('<I', ext, resident)[0]
        spoil('index', at + first, 255, '<B')
        for name, bad in tests:
            with self.subTest(name=name):
                data = ct.create_string_buffer(bytes(bad))
                pkg, pal = Package(), Palette()
                ct.memset(ct.byref(pkg), 0x5a, ct.sizeof(pkg))
                ct.memset(ct.byref(pal), 0x5a, ct.sizeof(pal))
                before = bytes(pkg), bytes(pal)
                self.assertIsNotNone(self.lib.dc_instance_palette_open(data, len(bad), ct.byref(pkg), ct.byref(pal)))
                self.assertEqual((bytes(pkg), bytes(pal)), before)

    def test_u16_last_slot_and_truncated_table_are_refused(self):
        blob, _, _, _ = synthetic(True)
        at, ext, _, _, _ = palette.locate(blob)
        resident, count = struct.unpack_from('<2I', ext, 8)
        offset, = struct.unpack_from('<I', ext, resident + 4 * (count - 1))
        slots, _ = struct.unpack_from('<2I', ext, 16)
        bad = bytearray(blob)
        struct.pack_into('<H', bad, at + offset + 2 * (slots - 1), 65535)
        self.assertIsNotNone(self.load(bytes(bad))[0])
        bad = bytearray(blob)
        struct.pack_into('<I', bad, at + resident, len(ext) - 4)
        self.assertIsNotNone(self.load(bytes(bad))[0])

    def stream(self, blob, folder, path, capacity=None):
        trimmed = cells.split(blob, folder)
        error, pkg, pal, data = self.load(trimmed)
        self.assertIsNone(error)
        owner = Owner()
        cap = cells.palette_peak(folder, path, 8.) if capacity is None else capacity
        self.assertIsNone(self.lib.dc_instance_cells_palette_make(ct.byref(owner), str(folder).encode(), 16.,
                                                                 cells.peak(blob, path, 8.), ct.byref(pal), cap))
        return owner, pkg, pal, data

    def test_cell_path_evicts_and_reloads_exact_colours_at_exact_capacity(self):
        blob, _, _, _ = synthetic(True)
        targets = [(0., 0.), (20., 20.), (60., 120.), (0., 0.), (60., 1280.)]
        with tempfile.TemporaryDirectory() as folder:
            owner, pkg, pal, data = self.stream(blob, folder, targets)
            peak = 0
            try:
                for x, z in targets:
                    self.assertIsNone(self.lib.dc_instance_cells_update(ct.byref(owner), ct.byref(pkg), x, z, 8.))
                    records = ct.string_at(pkg.instances, 52 * pkg.instance_count)
                    ids = [struct.unpack_from('<H', records, 52 * i + 2)[0] for i in range(pkg.instance_count)]
                    self.assertEqual(ids, cells.resident(blob, x, z, 8.))
                    tables = ct.cast(pal.tables, ct.POINTER(ct.c_void_p))
                    for i, original in enumerate(ids):
                        self.check_colours(blob, pkg, pal, tables[i], original)
                    peak = max(peak, owner.lighting_used)
                self.assertEqual(peak, owner.lighting_capacity)
            finally:
                self.lib.dc_instance_cells_free(ct.byref(owner))

    def test_bad_cell_is_atomic_and_retried(self):
        blob, _, _, _ = synthetic()
        for name in ('magic', 'size', 'count', 'reserved', 'offset', 'index', 'truncate', 'proto', 'nan'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as folder:
                owner, pkg, pal, data = self.stream(blob, folder, [(0., 0.)])
                file = Path(folder) / 'cells/0_0.dci'
                original = file.read_bytes()
                bad = bytearray(original)
                if name == 'truncate':
                    bad = bad[:-1]
                elif name == 'magic':
                    bad[:4] = b'NOPE'
                elif name == 'index':
                    bad[struct.unpack_from('<I', bad, 68)[0]] = 255
                else:
                    pos = {'size': 4, 'count': 8, 'reserved': 12, 'offset': 68, 'proto': 16, 'nan': 20}[name]
                    struct.pack_into('<H' if name == 'proto' else '<I', bad, pos, 65535 if name == 'proto' else 0xffffffff)
                file.write_bytes(bad)
                try:
                    self.assertIsNotNone(self.lib.dc_instance_cells_update(ct.byref(owner), ct.byref(pkg), 0., 0., 8.))
                    self.assertEqual(pkg.instance_count, 0)
                    self.assertEqual(owner.lighting_used, 0)
                    file.write_bytes(original)
                    self.assertIsNone(self.lib.dc_instance_cells_update(ct.byref(owner), ct.byref(pkg), 0., 0., 8.))
                    self.assertEqual(pkg.instance_count, 1)
                finally:
                    self.lib.dc_instance_cells_free(ct.byref(owner))

    def test_fixed_byte_capacity_refuses_cell(self):
        blob, _, _, _ = synthetic()
        with tempfile.TemporaryDirectory() as folder:
            owner, pkg, pal, data = self.stream(blob, folder, [(0., 0.)], 1)
            try:
                self.assertEqual(self.lib.dc_instance_cells_update(ct.byref(owner), ct.byref(pkg), 0., 0., 8.),
                                 b'instance palette capacity')
                self.assertEqual(pkg.instance_count, 0)
            finally:
                self.lib.dc_instance_cells_free(ct.byref(owner))


if __name__ == '__main__':
    unittest.main()
