"""Synthetic VAG and AICA checks. No disc data (AI-assisted), ISC."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import vag_adpcm


def vag_block(shift, filt, flags, nibbles):
    """Pack one 16-byte SPU block, low nibble first (AI-assisted), ISC."""
    if len(nibbles) != 28:
        raise ValueError('a VAG block holds 28 nibbles')
    body = bytearray(14)
    for index, nibble in enumerate(nibbles):
        if index % 2 == 0:
            body[index // 2] = nibble & 0xF
        else:
            body[index // 2] |= (nibble & 0xF) << 4
    return bytes(((shift & 0xF) | ((filt & 0xF) << 4), flags & 0xFF)) + bytes(body)


def independent_spu(block, hist1=0, hist2=0):
    """Format oracle: signed residual plus two separately floored rational taps.

    Written from SOUND_REVIEW's block description using bit-string extraction
    and exact fractions, without production helpers or shift arithmetic.
    (AI-assisted), ISC.
    """
    from fractions import Fraction
    from math import floor
    taps = {0: (0, 0), 1: (60, 0), 2: (115, -52),
            3: (98, -55), 4: (122, -60)}
    first, second = taps[(block[0] // 16) % 8]
    codes = ''.join(f'{byte:08b}'[4:] + f'{byte:08b}'[:4] for byte in block[2:])
    history = [hist2, hist1]
    out = []
    for position in range(0, len(codes), 4):
        signed = int(codes[position:position + 4], 2)
        if signed >= 8:
            signed -= 16
        residual = floor(Fraction(signed * 4096, 2 ** (block[0] % 16)))
        prediction = sum(floor(Fraction(tap * old, 64))
                         for tap, old in zip((second, first), history))
        out.append(min(32767, max(-32768, residual + prediction)))
        history = [history[-1], out[-1]]
    return out, history[-1], history[-2]


def independent_aica(data, count, quant=127, sample=0):
    """Format oracle: sign bit, 3-bit magnitude, adaptive unsigned step.

    Decimal factors describe magnitudes 0..7; the sign does not affect step
    adaptation. No signed-product division or production difference table.
    Matches ymz_step, without the optional history filter. (AI-assisted), ISC.
    """
    from itertools import islice
    factors = (230, 230, 230, 230, 307, 409, 512, 614)
    codes = (int(bits, 2) for byte in data
             for bits in (f'{byte:08b}'[4:], f'{byte:08b}'[:4]))
    out = []
    for code in islice(codes, count):
        magnitude = code % 8
        distance = min(32767, (2 * magnitude + 1) * quant // 8)
        sample = min(32767, max(-32768, sample + (distance if code < 8 else -distance)))
        quant = min(24576, max(127, quant * factors[magnitude] // 256))
        out.append(sample)
    return out, quant


class VagDecodeTest(unittest.TestCase):
    def test_zero_block_is_silence(self):
        decoded = vag_adpcm.decode_vag(vag_block(0, 0, 0, [0] * 28))
        self.assertEqual(decoded.samples, tuple([0] * 28))
        self.assertEqual(decoded.block_count, 1)
        self.assertIsNone(decoded.sample_rate)
        self.assertFalse(decoded.loop_repeat)

    def test_filter0_shift_and_sign_match_hand_values(self):
        positive = vag_block(0, 0, 0, [1] + [0] * 27)
        negative = vag_block(1, 0, 0, [0xF] + [0] * 27)
        self.assertEqual(vag_adpcm.decode_vag(positive).samples[0], 4096)
        self.assertEqual(vag_adpcm.decode_vag(positive).samples[1], 0)
        self.assertEqual(vag_adpcm.decode_vag(negative).samples[0], -2048)

    def test_filter2_fourth_sample_uses_separate_shifts(self):
        # (115*hist1)>>6 + (-52*hist2)>>6. The combined (a+b+32)/64 form gives 11804 here.
        block = vag_block(0, 2, 0, [1, 0, 0, 0] + [0] * 24)
        samples = vag_adpcm.decode_vag(block).samples
        self.assertEqual(samples[:4], (4096, 7360, 9897, 11803))

    def test_negative_coefficient_shifts_arithmetically(self):
        # -52 >> 6 is -1. Toward-zero division would leave the third sample at 1.
        block = vag_block(12, 2, 0, [1, 0, 0] + [0] * 25)
        self.assertEqual(vag_adpcm.decode_vag(block).samples[:3], (1, 1, 0))

    def test_prediction_clamps_to_int16(self):
        block = vag_block(0, 1, 0, [7, 7] + [0] * 26)
        self.assertEqual(vag_adpcm.decode_vag(block).samples[:2], (28672, 32767))

    def test_second_block_continues_history(self):
        first = vag_block(0, 1, 0, [1] + [0] * 27)
        second = vag_block(0, 1, 0, [0] * 28)
        samples, hist1, hist2 = independent_spu(first)
        more, _, _ = independent_spu(second, hist1, hist2)
        decoded = vag_adpcm.decode_vag(first + second)
        self.assertEqual(decoded.samples, tuple(samples + more))
        self.assertEqual(decoded.block_count, 2)
        self.assertEqual(decoded.payload_bytes, 32)

    def test_loop_flags_mark_samples_without_dropping_them(self):
        start = vag_block(0, 0, 0x04, [1] + [0] * 27)
        end = vag_block(0, 0, 0x03, [0] * 28)
        decoded = vag_adpcm.decode_vag(start + end)
        self.assertEqual(decoded.loop_start, 0)
        self.assertEqual(decoded.loop_end, 55)
        self.assertTrue(decoded.loop_repeat)
        self.assertEqual(len(decoded.samples), 56)

    def test_one_block_loop_uses_that_block(self):
        decoded = vag_adpcm.decode_vag(vag_block(0, 0, 0x07, [0] * 28))
        self.assertEqual(decoded.loop_start, 0)
        self.assertEqual(decoded.loop_end, 27)
        self.assertTrue(decoded.loop_repeat)

    def test_pgav_and_vagp_headers(self):
        body = vag_block(0, 0, 0x01, [1] + [0] * 27)
        little = b'pGAV' + (1).to_bytes(4, 'little') + (0).to_bytes(4, 'little')
        little += (16).to_bytes(4, 'little') + (22050).to_bytes(4, 'little')
        little += bytes(28) + body
        big = b'VAGp' + (1).to_bytes(4, 'big') + (0).to_bytes(4, 'big')
        big += (16).to_bytes(4, 'big') + (48000).to_bytes(4, 'big')
        big += bytes(28) + body
        self.assertEqual(vag_adpcm.decode_vag(little).sample_rate, 22050)
        self.assertEqual(vag_adpcm.decode_vag(big).sample_rate, 48000)
        self.assertEqual(vag_adpcm.decode_vag(little).samples, vag_adpcm.decode_vag(body).samples)
        self.assertEqual(vag_adpcm.decode_vag(little).loop_end, 27)

    def test_rejects_bad_shapes(self):
        with self.assertRaises(ValueError):
            vag_adpcm.decode_vag(b'\x00' * 15)
        with self.assertRaises(ValueError):
            vag_adpcm.decode_vag(vag_block(0, 5, 0, [0] * 28))
        header = b'pGAV' + bytes(8) + (32).to_bytes(4, 'little') + bytes(32)
        with self.assertRaises(ValueError):
            vag_adpcm.decode_vag(header)
        with self.assertRaises(TypeError):
            vag_adpcm.decode_vag('pGAV')


class AicaCodecTest(unittest.TestCase):
    def test_low_nibble_is_first_and_matches_hand_values(self):
        # Byte 0x10: nibble 0 (+1) then nibble 1 (+3), quant staying at 127.
        samples, quant = independent_aica(bytes([0x10]), 2)
        self.assertEqual(samples, [15, 62])
        self.assertEqual(quant, 0x7F)
        self.assertEqual(vag_adpcm.decode_aica(bytes([0x10])), (15, 62))

    def test_large_positive_step_and_quant_growth(self):
        # Low nibble 7 (+15) then nibble 0 (+1): samples 238, 276 and quant 273.
        self.assertEqual(vag_adpcm.decode_aica(bytes([0x07])), (238, 276))
        state = vag_adpcm.AicaState()
        vag_adpcm.aica_step(state, 7)
        vag_adpcm.aica_step(state, 0)
        self.assertEqual(state.sample, 276)
        self.assertEqual(state.quant, 273)

    def test_quant_floor_and_ceiling(self):
        state = vag_adpcm.AicaState()
        vag_adpcm.aica_step(state, 0)
        self.assertEqual(state.quant, 0x7F)
        state = vag_adpcm.AicaState(sample=20000, quant=0x6000)
        vag_adpcm.aica_step(state, 4)
        self.assertEqual(state.quant, 0x6000)
        self.assertEqual(state.sample, 32767)

    def test_encode_then_decode_matches_a_second_decoder(self):
        pcm = []
        value = 0
        for index in range(180):
            value = ((value * 17) + index * 97 - 4000) & 0xFFFF
            if value >= 0x8000:
                value -= 0x10000
            pcm.append(value)
        pcm.extend([32767] * 8 + [-32768] * 8 + [0] * 7)  # odd length, plus clamps
        encoded = vag_adpcm.encode_aica(pcm)
        decoded = vag_adpcm.decode_aica(encoded, len(pcm))
        oracle, _ = independent_aica(encoded, len(pcm))
        self.assertEqual(list(decoded), oracle)
        again = vag_adpcm.decode_aica(vag_adpcm.encode_aica(decoded), len(decoded))
        self.assertEqual(again, decoded)
        self.assertEqual(len(encoded), (len(pcm) + 1) // 2)

    def test_pad_nibble_is_outside_the_requested_count(self):
        encoded = vag_adpcm.encode_aica([1000])
        self.assertEqual(encoded, bytes([0x07]))
        self.assertEqual(vag_adpcm.decode_aica(encoded, 1), vag_adpcm.decode_aica(encoded)[:1])
        self.assertEqual(len(vag_adpcm.decode_aica(encoded)), 2)

    def test_rejects_samples_outside_int16(self):
        with self.assertRaises(ValueError):
            vag_adpcm.encode_aica([32768])
        with self.assertRaises(ValueError):
            vag_adpcm.aica_step(vag_adpcm.AicaState(), 16)


class ConversionReportTest(unittest.TestCase):
    def test_known_error_numbers(self):
        report = vag_adpcm.error_report((0, 0), (3, -4))
        self.assertEqual(report.sample_count, 2)
        self.assertEqual(report.max_abs_error, 4)
        self.assertEqual(report.sum_abs_error, 7)
        self.assertEqual(report.mean_abs_error, 3.5)
        self.assertEqual(report.sum_sq_error, 25)
        self.assertAlmostEqual(report.rms, (12.5) ** 0.5)

    def test_empty_report_is_zero(self):
        report = vag_adpcm.error_report((), ())
        self.assertEqual(report, vag_adpcm.ErrorReport(0, 0, 0.0, 0.0, 0, 0))

    def test_silence_is_not_exact_and_reconstruction_round_trips(self):
        converted = vag_adpcm.convert_vag(vag_block(0, 0, 0x01, [0] * 28))
        self.assertEqual(converted.vag.samples, tuple([0] * 28))
        self.assertEqual(converted.report.max_abs_error, 15)
        self.assertEqual(converted.aica_samples, (15, 0) * 14)
        self.assertEqual(
            converted.report.max_abs_error,
            max(abs(a - b) for a, b in zip(converted.vag.samples, converted.aica_samples)),
        )
        self.assertEqual(converted.report.sample_count, 28)
        self.assertEqual(converted.aica_samples, vag_adpcm.decode_aica(converted.aica, 28))
        requantized = vag_adpcm.decode_aica(
            vag_adpcm.encode_aica(converted.aica_samples), len(converted.aica_samples))
        self.assertEqual(requantized, converted.aica_samples)
        exact = vag_adpcm.error_report(converted.aica_samples, requantized)
        self.assertEqual(exact.max_abs_error, 0)
        self.assertEqual(exact.rms, 0.0)

    def test_pitched_block_report_matches_the_samples(self):
        block = vag_block(0, 4, 0, [3, 1, 0, 15, 8, 2] + [0] * 22)
        converted = vag_adpcm.convert_vag(block)
        self.assertEqual(converted.vag.samples, tuple(independent_spu(block)[0]))
        self.assertEqual(len(converted.aica), 14)
        self.assertEqual(converted.report.sum_abs_error,
                         sum(abs(a - b) for a, b in zip(converted.vag.samples, converted.aica_samples)))


class ReviewRegressionTest(unittest.TestCase):
    def test_difference_cap_and_negative_rails(self):
        state = vag_adpcm.AicaState(-20000, 24576)
        self.assertEqual(vag_adpcm.aica_step(state, 7), 12767)
        self.assertEqual(state.quant, 24576)
        state = vag_adpcm.AicaState(-32768, 127)
        self.assertEqual(vag_adpcm.aica_step(state, 8), -32768)
        state = vag_adpcm.AicaState()
        self.assertEqual(vag_adpcm.decode_aica(b'\x08', state=state), (-15, 0))
        self.assertEqual(state.quant, 127)

    def test_repeat_is_only_read_at_first_end(self):
        first = vag_block(0, 0, 2, [0] * 28)
        last = vag_block(0, 0, 1, [0] * 28)
        decoded = vag_adpcm.decode_vag(first + last)
        self.assertEqual((decoded.loop_repeat, decoded.loop_end, decoded.loop_start),
                         (False, 55, None))
        decoded = vag_adpcm.decode_vag(vag_block(0, 0, 3, [0] * 28))
        self.assertEqual((decoded.loop_repeat, decoded.loop_end, decoded.loop_start),
                         (True, 27, None))
        decoded = vag_adpcm.decode_vag(last + first)
        self.assertFalse(decoded.loop_repeat)

    def test_jump_keeps_history_but_conversion_is_entry_only(self):
        block = vag_block(0, 1, 7, [1] + [0] * 27)
        entry = vag_adpcm.decode_vag(block)
        self.assertEqual(entry.samples[0], 4096)
        self.assertEqual(entry.final_history, (712, 760))
        jump = vag_adpcm.decode_vag(block, entry.final_history)
        self.assertEqual(jump.samples[0], 4763)
        self.assertEqual(vag_adpcm.convert_vag(block).vag.samples, entry.samples)

    def test_all_aica_codes_against_format_oracle(self):
        for quant in (127, 273, 17475, 24110, 24576):
            for sample in (-32768, -20000, 0, 20000, 32767):
                for byte in range(256):
                    state = vag_adpcm.AicaState(sample, quant)
                    expected, step = independent_aica(bytes([byte]), 2, quant, sample)
                    self.assertEqual(vag_adpcm.decode_aica(bytes([byte]), state=state), tuple(expected))
                    self.assertEqual(state.quant, step)

    def test_spu_filters_shifts_and_history_against_format_oracle(self):
        for filt in range(5):
            for shift in range(16):
                block = vag_block(shift, filt, 0, list(range(16)) + list(range(12)))
                for history in ((0, 0), (-32768, 32767), (712, 760)):
                    expected, h1, h2 = independent_spu(block, *history)
                    actual = vag_adpcm.decode_vag(block, history)
                    self.assertEqual(actual.samples, tuple(expected))
                    self.assertEqual(actual.final_history, (h1, h2))

    def test_lossy_round_trip_compares_original_pcm_with_independent_decode(self):
        # Slowly moving source exercises both signs, reversals and odd padding.
        # This bound compares the original signal, never a re-encoded reconstruction.
        pcm = [(40 - abs(((i + 40) % 160) - 80)) * 24 for i in range(401)]
        encoded = vag_adpcm.encode_aica(pcm)
        actual, _ = independent_aica(encoded, len(pcm))
        self.assertEqual(len(actual), len(pcm))
        self.assertLessEqual(max(abs(a-b) for a,b in zip(pcm, actual)), 32)
        self.assertLessEqual(sum(abs(a-b) for a,b in zip(pcm, actual)), 6000)
        # A fully fixed short encode/decode vector also locks sign, tie and packing.
        source = [15, 62, -17, -2]
        self.assertEqual(vag_adpcm.encode_aica(source), b'\x10\x0a')
        self.assertEqual(independent_aica(b'\x10\x0a', 4)[0], source)

    def test_conversion_uses_supplied_aica_state_and_zero_count_is_empty(self):
        state = vag_adpcm.AicaState(-20000, 24576)
        block = vag_block(12, 0, 1, [0] * 28)
        result = vag_adpcm.convert_vag(block, state)
        expected, _ = independent_aica(result.aica, 28, 24576, -20000)
        self.assertEqual(result.aica_samples, tuple(expected))
        self.assertEqual((state.sample, state.quant), (-20000, 24576))
        self.assertEqual(vag_adpcm.decode_aica(b'\xff', 0, state), ())
        self.assertEqual((state.sample, state.quant), (-20000, 24576))

    def test_imports_are_standard_library_or_this_module(self):
        import ast
        for path in (Path(vag_adpcm.__file__), Path(__file__)):
            for node in ast.walk(ast.parse(path.read_text())):
                names = ([alias.name for alias in node.names] if isinstance(node, ast.Import)
                         else [node.module] if isinstance(node, ast.ImportFrom) else [])
                for name in names:
                    self.assertIn(name.split('.')[0], sys.stdlib_module_names | {'vag_adpcm'})


if __name__ == '__main__':
    unittest.main()
