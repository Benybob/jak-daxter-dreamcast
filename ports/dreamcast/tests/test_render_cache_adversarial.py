"""Adversarial geometry-window invariants (AI-assisted).

Grok proposal independently reviewed and executed by Codex. Failed fopen calls
count as attempts and require no fclose; live FILE counts must still be zero.
Open/close counters are CacheReview words[1] and words[2]: dc_rc_arm(af, of, cf, rf)
and the existing injection loop use indices 0..3 for alloc, open, close, read.
Query tuples are (x0, x1, z0, z1), matching CacheReview.load and assemble_window.
"""
import math
import os
import struct
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_render_cache  # noqa: E402  (sets the port import path)
from test_render_cache import CacheReview
import prepare_render_sectors as r
from test_render_sectors import _one, _package


LIMIT = 36.8 + 0.001
COORD = 1000000.0


class AdversarialCache(CacheReview):
    def manifest(self, root):
        data = (Path(root) / 'manifest.drm').read_bytes()
        self.assertGreaterEqual(len(data), 256)
        return data

    def cell_size(self, root):
        return struct.unpack_from('<f', self.manifest(root), 48)[0]

    def assert_sector_layout(self, root):
        root = Path(root)
        self.assertTrue((root / 'global.drs').is_file())
        sector_dir = root / 'sectors'
        found = []
        if sector_dir.is_dir():
            for path in sorted(sector_dir.glob('*.drs')):
                # Signed decimal ix_iz. A c_ prefix or a '+' pad fails this.
                self.assertRegex(path.name, r'^-?\d+_-?\d+\.drs$')
                found.append(path)
        return found

    def touching_sectors(self, root, query):
        cell = self.cell_size(root)
        hits = []
        qx0, qx1, qz0, qz1 = query
        for path in self.assert_sector_layout(root):
            ix_s, iz_s = path.stem.split('_', 1)
            ix, iz = int(ix_s), int(iz_s)
            x0, z0 = ix * cell, iz * cell
            if x0 <= qx1 and x0 + cell >= qx0 and z0 <= qz1 and z0 + cell >= qz0:
                hits.append(path)
        return hits

    def oracle(self, package, query):
        return b''.join(face['raw'] for face in r.assemble_window(package, query))

    def bounds(self, payload):
        self.assertEqual(len(payload) % 116, 0)
        self.assertTrue(payload)
        min_x = min_z = math.inf
        max_x = max_z = -math.inf
        for offset in range(0, len(payload), 116):
            for vertex in range(3):
                base = offset + 8 + vertex * 36
                x = struct.unpack_from('<f', payload, base)[0]
                z = struct.unpack_from('<f', payload, base + 8)[0]
                min_x, max_x = min(min_x, x), max(max_x, x)
                min_z, max_z = min(min_z, z), max(max_z, z)
        return min_x, max_x, min_z, max_z

    def assert_window(self, ctx, words, expected):
        payload = self.payload(ctx)
        self.assertEqual(payload, expected)
        self.assertEqual(words[8] * 116, len(payload))
        self.assertEqual(words[1], words[2])
        ids = [struct.unpack_from('<I', payload, i)[0] for i in range(0, len(payload), 116)]
        self.assertEqual(ids, sorted(ids))
        self.assertEqual(len(ids), len(set(ids)))
        return payload

    def assert_closed_package(self, positions, name):
        package = _package(_one(positions).to_bytes(), cell_size=4)
        root = self.packet(package, name)
        self.assert_sector_layout(root)
        x0 = min(p[0] for p in positions) - 1.0
        x1 = max(p[0] for p in positions) + 1.0
        z0 = min(p[2] for p in positions) - 1.0
        z1 = max(p[2] for p in positions) + 1.0
        self.assertLessEqual(x1 - x0, LIMIT)
        self.assertLessEqual(z1 - z0, LIMIT)
        wide = (x0, x1, z0, z1)
        ctx = self.context(root)
        error, words = self.load(ctx, root, wide)
        self.assertIsNone(error, error)
        payload = self.assert_window(ctx, words, self.oracle(package, wide))
        min_x, max_x, min_z, max_z = self.bounds(payload)

        fresh = self.context(root)
        closed = (max_x, max_x, min_z, max_z)
        error, words = self.load(fresh, root, closed)
        self.assertIsNone(error, error)
        self.assert_window(fresh, words, self.oracle(package, closed))
        self.assertTrue(self.payload(fresh))

        # 0.5 mm is inside the query-width allowance and outside a closed bbox.
        # One ulp catches any bbox epsilon larger than a single double.
        for edge in (max_x + 0.0005, math.nextafter(max_x, math.inf)):
            outside = (edge, edge + 1.0, min_z, max_z)
            ctx_out = self.context(root)
            error, words = self.load(ctx_out, root, outside)
            self.assertIsNone(error, error)
            self.assert_window(ctx_out, words, self.oracle(package, outside))
            self.assertEqual(self.payload(ctx_out), b'')
        below = (min_x - 1.0, min_x - 0.0005, min_z, max_z)
        ctx_below = self.context(root)
        error, words = self.load(ctx_below, root, below)
        self.assertIsNone(error, error)
        self.assertEqual(self.payload(ctx_below), b'')
        return root

    def test_closed_aabb_rejects_half_millimetre_and_one_ulp(self):
        self.assert_closed_package([(2, 0, 2), (2, 1, 2), (2, 0, 3)], 'edge')

    def test_large_and_negative_coordinates_have_no_scaled_bbox_slack(self):
        self.assert_closed_package(
            [(999900, 0, 0), (999900, 1, 0), (999900, 0, 1)], 'far-x')
        self.assert_closed_package(
            [(-2, 0, -2), (-1, 0, -2), (-2, 1, -2)], 'neg')

    def test_width_threshold_and_coordinate_caps_preserve_the_window(self):
        package = _package(_one([(1, 0, 1), (2, 0, 1), (1, 1, 1)]).to_bytes(), cell_size=4)
        root = self.packet(package, 'width')
        ctx = self.context(root)
        error, words = self.load(ctx, root, (0.0, LIMIT, 0.0, 1.0))
        self.assertIsNone(error, error)
        before = self.assert_window(ctx, words, self.oracle(package, (0.0, LIMIT, 0.0, 1.0)))

        over = math.nextafter(LIMIT, math.inf)
        error, _ = self.load(ctx, root, (0.0, over, 0.0, 1.0))
        self.assertIsNotNone(error)
        self.assertEqual(self.payload(ctx), before)

        error, words = self.load(ctx, root, (0.0, LIMIT, 0.0, LIMIT))
        self.assertIsNone(error, error)
        self.assertEqual(words[1], words[2])
        before = self.payload(ctx)
        error, _ = self.load(ctx, root, (0.0, over, 0.0, LIMIT))
        self.assertIsNotNone(error)
        self.assertEqual(self.payload(ctx), before)

        # Unlisted cells around x=999900 must not be opened as missing paths.
        x0 = 999900.0
        x1 = x0 + 36.8
        while x1 - x0 > LIMIT:
            x1 = math.nextafter(x1, -math.inf)
        self.assertGreater(x1 - x0, 36.0)
        self.assertLessEqual(x1, COORD)
        error, words = self.load(ctx, root, (x0, x1, 0.0, 1.0))
        self.assertIsNone(error, error)
        self.assertEqual(words[1], words[2])
        before = self.payload(ctx)
        x_over = x0 + LIMIT
        while x_over - x0 <= LIMIT:
            x_over = math.nextafter(x_over, math.inf)
        self.assertLessEqual(x_over, COORD)
        error, _ = self.load(ctx, root, (x0, x_over, 0.0, 1.0))
        self.assertIsNotNone(error)
        self.assertEqual(self.payload(ctx), before)

        error, words = self.load(ctx, root, (-COORD, -COORD + 1.0, 0.0, 1.0))
        self.assertIsNone(error, error)
        before = self.payload(ctx)
        low = math.nextafter(-COORD, -math.inf)
        error, _ = self.load(ctx, root, (low, low + 1.0, 0.0, 1.0))
        self.assertIsNotNone(error)
        self.assertEqual(self.payload(ctx), before)
        error, words = self.load(ctx, root, (COORD - 1.0, COORD, 0.0, 1.0))
        self.assertIsNone(error, error)
        before = self.payload(ctx)
        high = math.nextafter(COORD, math.inf)
        error, _ = self.load(ctx, root, (high - 1.0, high, 0.0, 1.0))
        self.assertIsNotNone(error)
        self.assertEqual(self.payload(ctx), before)
        error, _ = self.load(ctx, root, (0.0, math.inf, 0.0, 1.0))
        self.assertIsNotNone(error)
        self.assertEqual(self.payload(ctx), before)

    def test_relative_spaced_root_filtered_query_commits(self):
        package = _package(_one([(1, 0, 1), (2, 0, 1), (1, 1, 1)]).to_bytes(), cell_size=4)
        miss = (3.0, 3.5, 3.0, 3.5)
        hit = (0.0, 3.0, 0.0, 3.0)
        self.assertEqual(self.oracle(package, miss), b'')
        previous = Path.cwd()
        try:
            os.chdir(self.parent)
            rel = Path('rel root')
            r.publish_package(rel, package)
            self.assert_sector_layout(rel)
            ctx = self.context(rel)
            error, words = self.load(ctx, Path(os.path.join('.', 'rel root')), miss)
            self.assertIsNone(error, error)
            self.assert_window(ctx, words, b'')
            self.assertGreaterEqual(words[1], 1)
            error, words = self.load(ctx, str(rel) + '/', hit)
            self.assertIsNone(error, error)
            self.assert_window(ctx, words, self.oracle(package, hit))
            self.assertTrue(self.payload(ctx))
            if os.name == 'nt':
                error, words = self.load(ctx, str(rel) + '\\', hit)
                self.assertIsNone(error, error)
                self.assertEqual(self.payload(ctx), self.oracle(package, hit))
        finally:
            os.chdir(previous)

    def test_sha_is_manifest_bytes_164_through_195(self):
        package = _package(_one([(1, 0, 1), (2, 0, 1), (1, 1, 1)]).to_bytes(), cell_size=4)
        root = self.packet(package, 'sha')
        digest = self.manifest(root)[164:196]
        self.assertEqual(len(digest), 32)
        ctx = self.context(root)
        query = (0.0, 3.0, 0.0, 3.0)
        error, words = self.load(ctx, root, query, sha=digest.hex().upper())
        self.assertIsNone(error, error)
        before = self.assert_window(ctx, words, self.oracle(package, query))
        # Lowercase is the same digest. The face still touches, so the bytes stay.
        error, words = self.load(ctx, root, (1.0, 2.0, 1.0, 2.0), sha=digest.hex())
        self.assertIsNone(error, error)
        self.assertEqual(words[1], words[2])
        self.assertEqual(self.payload(ctx), before)

        flipped = bytearray(digest)
        flipped[31] ^= 0xff
        error, _ = self.load(ctx, root, (0.0, 3.0, 4.0, 6.0), sha=flipped.hex())
        self.assertIsNotNone(error)
        self.assertEqual(self.payload(ctx), before)
        error, _ = self.load(ctx, root, query, sha=('g' + digest.hex()[1:]))
        self.assertIsNotNone(error)
        self.assertEqual(self.payload(ctx), before)
        error, _ = self.load(ctx, root, query, sha=digest.hex()[:-1])
        self.assertIsNotNone(error)
        self.assertEqual(self.payload(ctx), before)

        corrupt = self.parent / 'corrupt-manifest'
        corrupt.mkdir()
        (corrupt / 'manifest.drm').write_bytes(b'not a catalog')
        error, _ = self.load(ctx, root, (0.0, 3.0, 4.0, 6.0), new=corrupt / 'manifest.drm')
        self.assertIsNotNone(error)
        self.assertEqual(self.payload(ctx), before)

    def test_known_empty_differs_from_missing_listed_files(self):
        package = _package(_one([(1, 0, 1), (2, 0, 1), (1, 1, 1)]).to_bytes(), cell_size=4)
        root = self.packet(package, 'listed')
        hit = (0.0, 3.0, 0.0, 3.0)
        far = (-30.0, -20.0, -30.0, -20.0)
        ctx = self.context(root)
        error, far_words = self.load(ctx, root, far)
        self.assertIsNone(error, error)
        self.assert_window(ctx, far_words, self.oracle(package, far))
        hit_ctx = self.context(root)
        error, hit_words = self.load(hit_ctx, root, hit)
        self.assertIsNone(error, error)
        hit_payload = self.assert_window(hit_ctx, hit_words, self.oracle(package, hit))
        self.assertTrue(hit_payload)
        selected = self.touching_sectors(root, hit)
        self.assertTrue(selected)
        if not self.touching_sectors(root, far):
            self.assertGreater(hit_words[1], far_words[1])
        saved = [(path, path.read_bytes()) for path in selected]
        try:
            for path, _data in saved:
                path.unlink()
            far_again = self.context(root)
            error, words = self.load(far_again, root, far)
            self.assertIsNone(error, error)
            self.assertEqual(words[1], words[2])
            self.assertEqual(self.payload(far_again), self.oracle(package, far))
            # Extends past the committed coverage, so reuse cannot hide the missing file.
            error, words = self.load(hit_ctx, root, (0.0, 3.0, 0.0, 5.0))
            self.assertIsNotNone(error)
            self.assertEqual(words[1], words[2] + 1)
            self.assertEqual(self.payload(hit_ctx), hit_payload)
            missing = self.context(root)
            error, words = self.load(missing, root, hit)
            self.assertIsNotNone(error)
            self.assertEqual(words[1], words[2] + 1)
        finally:
            for path, data in saved:
                path.write_bytes(data)

        (root / 'global.drs').unlink()
        bare = self.context(root)
        error, words = self.load(bare, root, far)
        self.assertIsNotNone(error)
        self.assertEqual(words[1], words[2] + 1)

    def test_truncated_and_trailing_global_rolls_back_and_closes(self):
        package = _package(_one([(1, 0, 1), (2, 0, 1), (1, 1, 1)]).to_bytes(), cell_size=4)
        root = self.packet(package, 'eof')
        hit = (0.0, 3.0, 0.0, 3.0)
        outside = (10.0, 12.0, 10.0, 12.0)
        ctx = self.context(root)
        error, words = self.load(ctx, root, hit)
        self.assertIsNone(error, error)
        before = self.assert_window(ctx, words, self.oracle(package, hit))
        target = root / 'global.drs'
        original = target.read_bytes()
        self.assertGreater(len(original), 32)
        try:
            target.write_bytes(original[:8])
            error, words = self.load(ctx, root, outside)
            self.assertIsNotNone(error)
            self.assertEqual(words[1], words[2])
            self.assertEqual(self.payload(ctx), before)
            fresh = self.context(root)
            error, words = self.load(fresh, root, hit)
            self.assertIsNotNone(error)
            self.assertEqual(words[1], words[2])
        finally:
            target.write_bytes(original)

        target.write_bytes(original + b'\x00')
        try:
            fresh = self.context(root)
            error, words = self.load(fresh, root, hit)
            self.assertIsNotNone(error)
            self.assertEqual(words[1], words[2])
            self.assertEqual(self.payload(ctx), before)
        finally:
            target.write_bytes(original)
        restored = self.context(root)
        error, words = self.load(restored, root, hit)
        self.assertIsNone(error, error)
        self.assert_window(restored, words, before)

    def test_cell_boundary_span_matches_oracle_once(self):
        package = _package(_one([(3.5, 0, 1), (4.5, 0, 1), (4.0, 1, 1)]).to_bytes(), cell_size=4)
        root = self.packet(package, 'span')
        self.assert_sector_layout(root)
        query = (3.0, 5.0, 0.0, 2.0)
        ctx = self.context(root)
        error, words = self.load(ctx, root, query)
        self.assertIsNone(error, error)
        payload = self.assert_window(ctx, words, self.oracle(package, query))
        self.assertTrue(payload)
        min_x, max_x, min_z, max_z = self.bounds(payload)
        self.assertLessEqual(min_x, 4.0)
        self.assertGreaterEqual(max_x, 4.0)
        self.assertLessEqual(min_z, query[3])
        self.assertGreaterEqual(max_z, query[2])


def load_tests(loader, tests, pattern):
    # Only new methods; baseline cases run in their own module.
    return unittest.TestSuite(AdversarialCache(name) for name in vars(AdversarialCache)
                              if name.startswith('test_'))


if __name__ == '__main__':
    unittest.main()
