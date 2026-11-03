"""Host checks for the synthetic AICA clips. No Dreamcast run (AI-assisted), ISC."""

from pathlib import Path
import re
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import make_sound_fixture
import vag_adpcm


def header_bytes(text, name):
    match = re.search(
        r'static const unsigned char ' + name + r'\[\d+\] = \{([^}]*)\}',
        text)
    if not match:
        raise AssertionError(name + ' array missing')
    return bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', match.group(1)))


class SoundFixtureTest(unittest.TestCase):
    def test_square_is_encode_aica_of_the_documented_wave(self):
        pcm = make_sound_fixture.square_pcm()
        self.assertEqual(len(pcm), 8192)
        self.assertEqual(pcm[0], 6000)
        self.assertEqual(pcm[31], 6000)
        self.assertEqual(pcm[32], -6000)
        self.assertEqual(pcm[63], -6000)
        adpcm = make_sound_fixture.build_square()
        self.assertEqual(adpcm, vag_adpcm.encode_aica(pcm))
        self.assertEqual(len(adpcm), 4096)
        self.assertEqual(len(adpcm) % 32, 0)
        self.assertEqual(vag_adpcm.decode_aica(adpcm, 8192),
                         vag_adpcm.decode_aica(vag_adpcm.encode_aica(pcm), 8192))
        self.assertNotEqual(make_sound_fixture.byte_sum(adpcm), 0)
        self.assertEqual(make_sound_fixture.classify_load(adpcm, len(adpcm), 8192, 48000), 0)

    def test_vag_clip_is_a_square_and_convert_vag_bytes(self):
        payload = make_sound_fixture.vag_payload()
        self.assertEqual(len(payload), 160 * 16)
        decoded = vag_adpcm.decode_vag(payload)
        self.assertEqual(list(decoded.samples[:28]), [6144] * 28)
        self.assertEqual(list(decoded.samples[28:56]), [-6144] * 28)
        self.assertEqual(decoded.samples[56], 6144)
        self.assertEqual(len(decoded.samples), 4480)
        self.assertFalse(decoded.loop_repeat)
        converted = vag_adpcm.convert_vag(payload)
        adpcm = make_sound_fixture.build_vag()
        self.assertEqual(adpcm, converted.aica)
        self.assertEqual(len(adpcm), 2240)
        self.assertEqual(len(adpcm) % 32, 0)
        self.assertEqual(len(adpcm) * 2, len(converted.vag.samples))
        self.assertNotEqual(adpcm, make_sound_fixture.build_square())
        self.assertEqual(make_sound_fixture.classify_load(adpcm, len(adpcm), 4480, 48000), 0)

    def test_align_pads_with_zeros_and_leaves_a_multiple_of_32_alone(self):
        padded = make_sound_fixture.align_adpcm(b'\x0a')
        self.assertEqual(len(padded), 32)
        self.assertEqual(padded[0], 0x0A)
        self.assertEqual(padded[1:], bytes(31))
        square = make_sound_fixture.build_square()
        self.assertEqual(make_sound_fixture.align_adpcm(square), square)
        with self.assertRaises(ValueError):
            make_sound_fixture.align_adpcm(b'')

    def test_load_and_play_classification_matches_the_c_order(self):
        square = make_sound_fixture.build_square()
        self.assertEqual(make_sound_fixture.SAMPLE_RAM_BYTES, 1900544)
        self.assertEqual(make_sound_fixture.SAMPLE_BASE, 0x030000)
        self.assertEqual(make_sound_fixture.classify_load(None, 32, 1, 48000), 1)
        self.assertEqual(make_sound_fixture.classify_load(square, 0, 1, 48000), 2)
        self.assertEqual(make_sound_fixture.classify_load(square, 2097184, 1, 48000), 2)
        self.assertEqual(make_sound_fixture.classify_load(square, 1900576, 1, 48000), 2)
        self.assertEqual(make_sound_fixture.classify_load(square, 16, 1, 48000), 3)
        self.assertEqual(make_sound_fixture.classify_load(square, 32, 0, 48000), 4)
        self.assertEqual(make_sound_fixture.classify_load(square, 32, 65535, 48000), 4)
        self.assertEqual(make_sound_fixture.classify_load(square, 32, 65, 48000), 4)
        self.assertEqual(make_sound_fixture.classify_load(square, 32, 64, 48000), 0)
        self.assertEqual(make_sound_fixture.classify_load(square, 32, 1, 0), 5)
        self.assertEqual(make_sound_fixture.classify_load(square, 32, 1, 96000), 0)
        self.assertEqual(make_sound_fixture.classify_load(square, 32, 1, 96001), 5)
        self.assertEqual(make_sound_fixture.classify_play(256, 128, 1, True), 5)
        self.assertEqual(make_sound_fixture.classify_play(255, -1, 1, True), 5)
        self.assertEqual(make_sound_fixture.classify_play(255, 128, 2, True), 5)
        self.assertEqual(make_sound_fixture.classify_play(256, 128, 1, False), 5)
        self.assertEqual(make_sound_fixture.classify_play(255, 128, 1, False), 10)
        self.assertEqual(make_sound_fixture.classify_play(255, 128, 0, True), 0)
        self.assertEqual(make_sound_fixture.classify_play(0, 0, 1, True), 0)

    def test_header_bytes_macros_and_writer(self):
        text = make_sound_fixture.render_header()
        square = header_bytes(text, 'dc_sound_square')
        vag = header_bytes(text, 'dc_sound_vag')
        self.assertEqual(square, make_sound_fixture.build_square())
        self.assertEqual(vag, make_sound_fixture.build_vag())
        self.assertIn('#define DC_SOUND_SQUARE_BYTES 4096u', text)
        self.assertIn('#define DC_SOUND_SQUARE_SAMPLES 8192u', text)
        self.assertIn('#define DC_SOUND_VAG_BYTES 2240u', text)
        self.assertIn('#define DC_SOUND_VAG_SAMPLES 4480u', text)
        self.assertIn('#define DC_SOUND_PROBE_RATE 48000u', text)
        self.assertIn(f'#define DC_SOUND_SQUARE_SUM {make_sound_fixture.byte_sum(square)}u', text)
        self.assertIn(f'#define DC_SOUND_VAG_SUM {make_sound_fixture.byte_sum(vag)}u', text)
        self.assertTrue(text.startswith('/* Generated AICA ADPCM clips.'))
        self.assertTrue(text.strip().endswith('(AI-assisted), ISC. */') or
                        '(AI-assisted), ISC.' in text.splitlines()[0])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'sound_fixture.h'
            self.assertEqual(make_sound_fixture.main(['make_sound_fixture.py', str(path)]), 0)
            self.assertEqual(path.read_text(encoding='utf-8'), text)

    def test_c_headers_repeat_the_same_bounds_and_check_count(self):
        header = (PORT / 'aica_sfx.h').read_text(encoding='utf-8')
        probe = (PORT / 'sound_probe_main.c').read_text(encoding='utf-8')
        for name, value in (
            ('DC_AICA_SOUND_RAM_BYTES', '2097152u'),
            ('DC_AICA_SAMPLE_BASE', '0x030000u'),
            ('DC_AICA_SAMPLE_RAM_BYTES', '1900544u'),
            ('DC_AICA_ALIGN_BYTES', '32u'),
            ('DC_AICA_MAX_SAMPLES', '65534u'),
            ('DC_AICA_MAX_RATE', '96000u'),
            ('DC_AICA_ERR_ARG', '1'),
            ('DC_AICA_ERR_SIZE', '2'),
            ('DC_AICA_ERR_ALIGN', '3'),
            ('DC_AICA_ERR_SAMPLES', '4'),
            ('DC_AICA_ERR_RANGE', '5'),
            ('DC_AICA_ERR_STATE', '10'),
        ):
            self.assertRegex(header, r'#define ' + name + r' ' + value + r'\b|' +
                             name + r' = ' + value + r'\b')
        defined = int(re.search(r'#define DC_SOUND_PROBE_CHECKS (\d+)', probe).group(1))
        calls = len(re.findall(r'^\s+check_(?:u32|i32)\(', probe, re.M))
        self.assertEqual(calls, defined)
        self.assertNotEqual(defined, 17)
        self.assertIn('dc_backend', probe)
        self.assertIn('0x34485347u', probe)
        self.assertIn('dc_aica_sfx_play', probe)
        self.assertIn('dc_sound_square', probe)
        self.assertIn('dc_sound_vag', probe)


if __name__ == '__main__':
    unittest.main()
