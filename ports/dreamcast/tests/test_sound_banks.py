"""Synthetic Jak 1 sound banks for the Dreamcast converter.

Every bank in this file is assembled byte by byte. The tests do not open
iso_data. (AI-assisted), ISC.
"""

from __future__ import annotations

import ast
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sound_banks
import vag_adpcm


def spu_block(shift, filt, flags, first_nibble=0):
    """Pack one 16-byte SPU block, low nibble first.

    ``voice.cpp`` reads the shift from the low 4 bits and the filter from the
    next 3 bits. Byte 1 is the flag byte. ``voice.h`` 81 names bit 0 of that
    byte ``LoopEnd``. (AI-assisted), ISC.
    """
    body = bytearray(14)
    body[0] = first_nibble & 0xF
    return bytes((((shift & 0xF) | ((filt & 0xF) << 4)) & 0xFF, flags & 0xFF)) + bytes(body)


def tone24(sample_offset, decoy=0x11111111):
    """Pack one 24-byte ``ReadTone``.

    ``loader.cpp`` 36-55. ``SampleOffset`` is the ``u32`` at byte 16. The word
    at byte 12 is reserved for this fixture so a reader that stops four bytes
    early sees an unaligned pointer. (AI-assisted), ISC.
    """
    tone = bytearray(24)
    struct.pack_into('<I', tone, 12, decoy)
    struct.pack_into('<I', tone, 16, sample_offset & 0xFFFFFFFF)
    return bytes(tone)


def grain_v1(grain_type, sample_offset):
    """Pack one version-1 grain. ``loader.cpp`` 132-134 steps ``0x28`` bytes.

    The tone begins at byte 8 (``loader.cpp`` 107-112). (AI-assisted), ISC.
    """
    grain = bytearray(0x28)
    struct.pack_into('<I', grain, 0, grain_type)
    grain[8:32] = tone24(sample_offset)
    return bytes(grain)


def sblk_v1(sample_offsets, *, version=1, num_vags=0, vag_data_size=0, sram=0, vags_in_sr=0):
    """Build an ``SBlk`` whose tones are version-1 grains.

    Header fields follow ``loader.cpp`` 198-231. One sound is 12 bytes
    (``loader.cpp`` 237-247) and its grain byte offset is ``FirstGrain``.
    (AI-assisted), ISC.
    """
    grains = b''.join(grain_v1(1, offset) for offset in sample_offsets)
    sound_count = 1 if sample_offsets else 0
    first_sound = 60
    first_grain = 60 + (12 if sound_count else 0)
    program = bytearray(first_grain + len(grains))
    program[0:4] = b'SBlk'
    struct.pack_into('<I', program, 4, version)
    struct.pack_into('<h', program, 22, sound_count)
    struct.pack_into('<h', program, 26, num_vags)
    struct.pack_into('<I', program, 28, first_sound)
    struct.pack_into('<I', program, 32, first_grain)
    struct.pack_into('<I', program, 36, vags_in_sr)
    struct.pack_into('<I', program, 40, vag_data_size)
    struct.pack_into('<I', program, 44, sram & 0xFFFFFFFF)
    if sound_count:
        program[first_sound + 4] = len(sample_offsets)
        struct.pack_into('<I', program, first_sound + 8, 0)
    program[first_grain:] = grains
    return bytes(program)


def sblk_v2(sample_offsets, *, opcode_type=1, num_vags=0, vag_data_size=0, sram=0, vags_in_sr=0):
    """Build an ``SBlk`` version 2 with one opcode grain per tone.

    ``loader.cpp`` 151-161. The type is the high byte. The tone is at
    ``GrainData`` plus the low 24 bits, and ``SampleOffset`` is 16 bytes
    later. (AI-assisted), ISC.
    """
    tones = b''.join(tone24(offset) for offset in sample_offsets)
    first_sound = 64
    first_grain = 76
    grain_data = first_grain + 8 * len(sample_offsets)
    program = bytearray(grain_data + len(tones))
    program[0:4] = b'SBlk'
    struct.pack_into('<I', program, 4, 2)
    struct.pack_into('<h', program, 22, 1 if sample_offsets else 0)
    struct.pack_into('<h', program, 26, num_vags)
    struct.pack_into('<I', program, 28, first_sound)
    struct.pack_into('<I', program, 32, first_grain)
    struct.pack_into('<I', program, 36, vags_in_sr)
    struct.pack_into('<I', program, 40, vag_data_size)
    struct.pack_into('<I', program, 44, sram & 0xFFFFFFFF)
    struct.pack_into('<I', program, 52, grain_data)
    if sample_offsets:
        program[first_sound + 4] = len(sample_offsets)
        struct.pack_into('<I', program, first_sound + 8, 0)
        for index in range(len(sample_offsets)):
            opcode = (opcode_type << 24) | (index * 24)
            struct.pack_into('<I', program, first_grain + index * 8, opcode)
    program[grain_data:] = tones
    return bytes(program)


def sbv2(sample_offsets, *, version=1, num_vags=0, vag_data_size=0, vags_in_sr=0, tail=b''):
    """Build an ``SBv2`` program. ``loader.cpp`` 334-387.

    The header ``FirstTone`` at byte 36 is unused (``loader.cpp`` 352). Each
    program is 8 bytes and each tone is 24. There is no SRAM word after
    ``VagDataSize``. (AI-assisted), ISC.
    """
    tones = b''.join(tone24(offset) for offset in sample_offsets)
    program = bytearray(48)
    program[0:4] = b'SBv2'
    struct.pack_into('<I', program, 4, version)
    struct.pack_into('<h', program, 22, 1 if sample_offsets else 0)
    struct.pack_into('<h', program, 26, num_vags)
    struct.pack_into('<I', program, 36, 0xFFFFFFFF)
    struct.pack_into('<I', program, 40, vags_in_sr)
    struct.pack_into('<I', program, 44, vag_data_size)
    if sample_offsets:
        program += bytes(8 + len(tones))
        struct.pack_into('<I', program, 32, 48)
        program[48] = len(sample_offsets)
        struct.pack_into('<I', program, 52, 56)
        program[56:56 + len(tones)] = tones
    program += tail
    return bytes(program)


def midi_chunk(fourcc):
    """Build a nested MIDI directory. ``loader.cpp`` 391-404.

    The fourcc itself may be foreign. A directory that does not fit is a
    different failure. (AI-assisted), ISC.
    """
    blob = bytearray(20)
    struct.pack_into('<I', blob, 0, 1)
    struct.pack_into('<I', blob, 4, 1)
    struct.pack_into('<I', blob, 8, 16)
    struct.pack_into('<I', blob, 12, 4)
    blob[16:20] = fourcc
    return bytes(blob)


def pack_chunks(chunks, kind=1):
    """Build a 989snd chunk directory. ``loader.cpp`` 25-33 and 420-423.

    Offsets are from the start of this image, after the directory.
    (AI-assisted), ISC.
    """
    directory = 8 + 8 * len(chunks)
    blob = bytearray(directory)
    struct.pack_into('<I', blob, 0, kind)
    struct.pack_into('<I', blob, 4, len(chunks))
    cursor = directory
    for index, chunk in enumerate(chunks):
        struct.pack_into('<II', blob, 8 + index * 8, cursor, len(chunk))
        blob.extend(chunk)
        cursor += len(chunk)
    return bytes(blob)


def prefix_file(prefix, image, bank_name=b'bank', records=(), skipped=0x01020304):
    """Place an Overlord name prefix in front of a 989snd image.

    ``fake_iso.cpp`` 222-232. Sixteen name bytes, one skipped ``u32``, the
    count at byte 20, then 20-byte records. (AI-assisted), ISC.
    """
    data = bytearray(prefix)
    data.extend(image)
    data[0:16] = bank_name[:16].ljust(16, b'\x00')
    struct.pack_into('<I', data, 16, skipped)
    struct.pack_into('<I', data, 20, len(records))
    at = 24
    for name, falloff in records:
        data[at:at + 16] = name[:16].ljust(16, b'\x00')
        struct.pack_into('<I', data, at + 16, falloff)
        at += 20
    return bytes(data)


def sfx_file(program, samples, prefix=2048, **kwargs):
    """One SFX file: name prefix plus two chunks (AI-assisted), ISC."""
    return prefix_file(prefix, pack_chunks([program, samples]), **kwargs)


def music_file(program, samples, midi):
    """One music file. ``fake_iso.cpp`` 213 loads it from byte 0 (AI-assisted), ISC."""
    return pack_chunks([program, samples, midi])


def vagp_clip(payload):
    """One ``VAGp`` clip. Size and rate are big-endian.

    ``vag_adpcm.py`` treats magic ``VAGp`` as the big-endian header.
    ``iso.cpp`` 1215 takes the size as the bytes after the 48-byte header.
    (AI-assisted), ISC.
    """
    header = bytearray(48)
    header[0:4] = b'VAGp'
    struct.pack_into('>I', header, 12, len(payload))
    struct.pack_into('>I', header, 16, 22050)
    return bytes(header) + payload


def pgav_clip(payload):
    """One ``pGAV`` clip. Size is little-endian (AI-assisted), ISC."""
    header = bytearray(48)
    header[0:4] = b'pGAV'
    struct.pack_into('<I', header, 12, len(payload))
    struct.pack_into('<I', header, 16, 22050)
    return bytes(header) + payload


def repeat(block, count):
    """Repeat one SPU block (AI-assisted), ISC."""
    return block * count


def unpack_dcsa(blob):
    """Read the ``DCSA`` container written by ``pack_aica`` (AI-assisted), ISC."""
    if blob[:4] != b'DCSA':
        raise AssertionError(blob[:4])
    header = struct.unpack_from('<6I', blob, 4)
    pos = 28
    rows = []
    for _ in range(header[1]):
        fields = struct.unpack_from('<9I', blob, pos)
        pos += 36
        payload = blob[pos:pos + fields[8]]
        pos += fields[8]
        rows.append((fields, payload))
    if pos != len(blob):
        raise AssertionError((pos, len(blob)))
    return header, rows


def level_block(text, name):
    """Return the report lines for one level (AI-assisted), ISC."""
    lines = text.splitlines()
    start = lines.index('level: ' + name)
    end = start + 1
    while end < len(lines) and not lines[end].startswith('level: ') and not lines[end].startswith('worst_'):
        end += 1
    return lines[start:end]


def field(lines, key):
    """Return the value of one ``key: value`` line (AI-assisted), ISC."""
    prefix = key + ': '
    for line in lines:
        if line.startswith(prefix):
            return line[len(prefix):]
    raise AssertionError(key)


def unique_banks():
    """Level SFX and music names from ``level_names`` (AI-assisted), ISC."""
    sfx = []
    music = []
    for _level, sfx_name, music_name, _sfx_line, _music_line in sound_banks.level_names():
        if sfx_name and sfx_name not in sfx:
            sfx.append(sfx_name)
        if music_name and music_name not in music:
            music.append(music_name)
    return sfx, music


def budget_run(iso_data):
    """Measure a tree and count ``convert_vag`` calls (AI-assisted), ISC.

    A report without an output directory sizes each span from its block
    count. ``convert_vag`` stays unused on that path.
    (AI-assisted), ISC.
    """
    calls = []
    original = vag_adpcm.convert_vag

    def counting(data, state=None):
        calls.append(len(data))
        return original(data, state)

    vag_adpcm.convert_vag = counting
    try:
        code, text = sound_banks.run(iso_data, None)
    finally:
        vag_adpcm.convert_vag = original
    return code, text, calls


class SoundBankTests(unittest.TestCase):
    """Byte-built banks, refusals, and a temporary disc tree (AI-assisted), ISC."""

    def test_constants_and_level_rows(self):
        """Sound RAM and the level list (AI-assisted), ISC.

        ``MEMORY_PLAN.md`` 3 and ``SOUND_PLAN.md`` 121 give 2097152 bytes.
        ``ssound.cpp`` 67 reserves ``0xc030``. ``level-info.gc`` 19-20 is
        training, 2224-2226 is intro, 2418-2420 is default-level, and
        2472-2485 is the omitted test-zone. The list order is 2444-2469.
        (AI-assisted), ISC.
        """
        self.assertEqual(sound_banks.SOUND_RAM_BYTES, 2097152)
        self.assertEqual(sound_banks.DRIVER_RESERVE_BYTES, 0xC030)
        self.assertEqual(sound_banks.DRIVER_RESERVE_BYTES, 49200)
        self.assertEqual(sound_banks.BANK_CAP_BYTES, 2097152 - 49200)
        self.assertEqual(sound_banks.level_names()[0], ('training', 'training', 'village1', 19, 20))
        self.assertEqual(sound_banks.level_names()[-1], ('default-level', None, None, 2418, 2420))
        names = [row[0] for row in sound_banks.level_names()]
        self.assertNotIn('test-zone', names)
        self.assertIn('intro', names)
        self.assertEqual(sound_banks.fits_sound_ram(0), 'yes')
        self.assertEqual(sound_banks.fits_sound_ram(sound_banks.BANK_CAP_BYTES + 1), 'no')
        self.assertEqual(sound_banks.fits_sound_ram(None), 'unknown')
        self.assertIsNone(sound_banks.resident_total(None, 0, 0))
        self.assertEqual(sound_banks.resident_total(3, 0, 4), 7)

    def test_two_loop_end_spans_reset_spu_history(self):
        """Each LoopEnd block is its own sample (AI-assisted), ISC.

        ``voice.cpp`` 56-65 stops at the flagged block. ``voice.cpp`` 93-94
        and ``convert_vag`` start history at zero, so the second span is not
        the tail of a decode of both blocks. ``ps2_bytes`` is the sample
        chunk (``loader.cpp`` 423), not the name prefix.
        (AI-assisted), ISC.
        """
        block_a = spu_block(0, 1, 0x01, 1)
        block_b = spu_block(0, 1, 0x01, 1)
        samples = block_a + block_b
        joined = vag_adpcm.decode_vag(samples)
        solo = vag_adpcm.decode_vag(block_b)
        self.assertNotEqual(joined.samples[28:], solo.samples)
        program = sblk_v1([0, 16], num_vags=2, vag_data_size=len(samples), sram=0xABCDE, vags_in_sr=99)
        played = b'AB\x00CDEFGHIJKLMNZ'
        self.assertEqual(len(played), 16)
        data = sfx_file(program, samples, records=((played, 0x90014003),), skipped=0x89ABCDEF)
        report = sound_banks.convert_sbk(data, 2048, source='SBK/training.sbk')
        self.assertEqual(report.samples, 2)
        self.assertEqual(report.converted, 2)
        self.assertEqual(report.ps2_bytes, 32)
        self.assertEqual(report.file_bytes, len(data))
        self.assertGreater(report.file_bytes, report.prefix + report.ps2_bytes)
        self.assertEqual(report.prefix, 2048)
        self.assertEqual(report.tone_offsets, 2)
        self.assertEqual(report.tones_at_span_starts, 2)
        self.assertEqual(report.failures, ())
        self.assertEqual(report.vags_in_sr, 99)
        self.assertEqual(report.sram_alloc_size, 0xABCDE)
        self.assertNotEqual(report.aica_bytes, report.sram_alloc_size)
        self.assertEqual(report.converted_samples[0].aica, vag_adpcm.convert_vag(block_a).aica)
        self.assertEqual(report.converted_samples[1].aica, vag_adpcm.convert_vag(block_b).aica)
        self.assertEqual(report.aica_bytes, len(report.converted_samples[0].aica) + len(report.converted_samples[1].aica))
        self.assertEqual(report.longest[0], 0)
        self.assertEqual(report.longest[1], 28)
        self.assertEqual(report.names[0].name, played)
        self.assertEqual(sound_banks.falloff_parts(report.names[0].falloff), (3, 5, 9))
        text = '\n'.join(sound_banks.format_bank(report))
        self.assertIn('longest_index: 0', text)
        self.assertIn('vags_in_sr: 99', text)

    def test_version2_tone_and_low_byte_opcode(self):
        """Version 2 reads the opcode high byte (AI-assisted), ISC.

        ``sfxgrain.h`` 56-57 defines ``TONE`` as 1 and ``TONE2`` as 9.
        ``loader.cpp`` 151-155 stores that type in the high byte. An opcode
        of 1 keeps the type in the low byte, so it is not a tone. The sample
        is still converted.
        (AI-assisted), ISC.
        """
        block = spu_block(0, 0, 0x01, 1)
        program = sblk_v2([0], opcode_type=9, num_vags=1, vag_data_size=16)
        report = sound_banks.convert_sbk(sfx_file(program, block), 2048)
        self.assertEqual(report.version, 2)
        self.assertEqual(report.tone_offsets, 1)
        self.assertEqual(report.tones_at_span_starts, 1)
        self.assertEqual(report.converted, 1)
        opcode = bytearray(84)
        opcode[0:4] = b'SBlk'
        struct.pack_into('<I', opcode, 4, 2)
        struct.pack_into('<h', opcode, 22, 1)
        struct.pack_into('<h', opcode, 26, 1)
        struct.pack_into('<I', opcode, 28, 64)
        struct.pack_into('<I', opcode, 32, 76)
        struct.pack_into('<I', opcode, 40, 16)
        struct.pack_into('<I', opcode, 52, 0)
        opcode[68] = 1
        struct.pack_into('<I', opcode, 76, 1)
        plain = sound_banks.convert_sbk(sfx_file(bytes(opcode), block), 2048)
        self.assertEqual(plain.tone_offsets, 0)
        self.assertEqual(plain.converted, 1)
        self.assertEqual(plain.failures, ())

    def test_filter_failure_keeps_the_sibling_span(self):
        """Filter 5 fails one span and leaves the bank readable (AI-assisted), ISC.

        ``vag_adpcm.py`` rejects a filter above the five playback coefficients.
        Shift 15 stays legal: ``voice.cpp`` 38 does not reject it.
        (AI-assisted), ISC.
        """
        bad = spu_block(0, 5, 0x01, 1)
        good = spu_block(0, 0, 0x01, 1)
        program = sblk_v1([0, 16], num_vags=2, vag_data_size=32)
        report = sound_banks.convert_sbk(sfx_file(program, bad + good), 2048)
        self.assertEqual(report.samples, 2)
        self.assertEqual(report.converted, 1)
        self.assertEqual(report.converted_samples[0].index, 1)
        self.assertEqual(report.converted_samples[0].aica, vag_adpcm.convert_vag(good).aica)
        self.assertTrue(any('VAG filter 5' in item for item in report.failures))
        self.assertTrue(any('lower bound' in item for item in report.notes))
        wide = spu_block(15, 0, 0x01, 1)
        shifted = sound_banks.convert_sbk(
            sfx_file(sblk_v1([0], num_vags=1, vag_data_size=16), wide), 2048)
        self.assertEqual(shifted.converted, 1)
        self.assertEqual(shifted.failures, ())
        only = sound_banks.convert_sbk(
            sfx_file(sblk_v1([0], num_vags=1, vag_data_size=16), bad), 2048)
        self.assertIsNone(only.longest)
        self.assertEqual(only.aica_bytes, 0)
        self.assertIn('longest_index: none', '\n'.join(sound_banks.format_bank(only)))

    def test_header_words_are_notes_and_music_has_no_sram_field(self):
        """Discarded header words stay out of the sample totals (AI-assisted), ISC.

        ``loader.cpp`` 219 discards ``NumVAGs``. ``loader.cpp`` 225 discards
        ``VagDataSize``. ``loader.cpp`` 354-357 reads music through
        ``VagDataSize`` and does not read an SRAM word. A foreign MIDI fourcc
        is logged and the bank is still returned (``loader.cpp`` 399-404).
        (AI-assisted), ISC.
        """
        samples = repeat(spu_block(0, 0, 0x01, 1), 2)
        program = sblk_v1([0, 16], num_vags=7, vag_data_size=16, sram=0xABCDE, vags_in_sr=99)
        report = sound_banks.convert_sbk(sfx_file(program, samples), 2048)
        self.assertEqual(report.ps2_bytes, 32)
        self.assertEqual(report.samples, 2)
        self.assertEqual(report.header_vag_count, 7)
        self.assertEqual(report.vag_data_size, 16)
        self.assertTrue(any('VagDataSize 16' in note for note in report.notes))
        self.assertTrue(any('NumVAGs 7' in note for note in report.notes))
        negative = sblk_v1([0], num_vags=-1, vag_data_size=16)
        noted = sound_banks.convert_sbk(sfx_file(negative, spu_block(0, 0, 0x01, 0)), 2048)
        self.assertEqual(noted.header_vag_count, -1)
        self.assertTrue(any('NumVAGs -1' in note for note in noted.notes))
        music = sbv2([], version=99, num_vags=1, vag_data_size=16, tail=b'\x44\x33\x22\x11')
        block = spu_block(0, 0, 0x01, 1)
        music_report = sound_banks.convert_music(music_file(music, block, midi_chunk(b'XXXX')), 0)
        self.assertIsNone(music_report.sram_alloc_size)
        self.assertEqual(music_report.vag_data_size, 16)
        self.assertEqual(music_report.version, 99)
        self.assertEqual(music[48:52], b'\x44\x33\x22\x11')
        self.assertTrue(any('not MID' in note or 'MMID' in note for note in music_report.notes))
        self.assertEqual(music_report.converted, 1)
        packed = sound_banks.pack_aica(music_report)
        self.assertEqual(struct.unpack_from('<I', packed, 20)[0], 0xFFFFFFFF)

    def test_interior_and_unaligned_tone_pointers(self):
        """A tone inside a span does not split it (AI-assisted), ISC.

        ``voice.cpp`` 56-65 plays through ``LoopEnd``. An interior pointer is
        an overlap note. An unaligned pointer is one failure line.
        (AI-assisted), ISC.
        """
        span = spu_block(0, 0, 0x00, 1) + spu_block(0, 0, 0x01, 1)
        inside = sound_banks.convert_sbk(
            sfx_file(sblk_v1([16], num_vags=1, vag_data_size=32), span), 2048)
        self.assertEqual(inside.samples, 1)
        self.assertEqual(inside.tones_inside, 1)
        self.assertEqual(inside.tones_at_span_starts, 0)
        self.assertEqual(inside.converted, 1)
        self.assertTrue(any('UNVERIFIED overlap' in note for note in inside.notes))
        askew = sound_banks.convert_sbk(
            sfx_file(sblk_v1([4], num_vags=1, vag_data_size=16), spu_block(0, 0, 0x01, 1)), 2048)
        self.assertEqual(askew.converted, 1)
        self.assertTrue(any('not a multiple of 16' in item for item in askew.failures))
        self.assertFalse(any('lower bound' in note for note in askew.notes))
        naked = sound_banks.convert_sbk(
            sfx_file(sblk_v1([], num_vags=1, vag_data_size=16), spu_block(0, 0, 0x00, 1)), 2048)
        self.assertEqual(naked.samples, 1)
        self.assertTrue(any('no LoopEnd' in note for note in naked.notes))

    def test_name_table_limits_and_prefix_sizes(self):
        """The name count lives at byte 20 and must fit the slot prefix (AI-assisted), ISC.

        ``fake_iso.cpp`` 222-232. A 2048-byte prefix holds 101 records
        (``sbank.cpp`` 93-95, ``sound_count == 0x65``). Ten sectors hold 1022.
        ``convert_sbk`` accepts only those two prefixes unless a sector scan
        produced the offset.
        (AI-assisted), ISC.
        """
        one_sector = bytearray(2048)
        struct.pack_into('<I', one_sector, 16, 0xFFFFFFFF)
        struct.pack_into('<I', one_sector, 20, 101)
        _name, names = sound_banks.parse_name_table(bytes(one_sector), 2048)
        self.assertEqual(len(names), 101)
        ten = bytearray(20480)
        struct.pack_into('<I', ten, 20, 1022)
        _name, wide = sound_banks.parse_name_table(bytes(ten), 20480)
        self.assertEqual(len(wide), 1022)
        crowded = bytearray(2048)
        struct.pack_into('<I', crowded, 20, 102)
        with self.assertRaises(sound_banks.BankFormatError) as crowded_error:
            sound_banks.convert_sbk(bytes(crowded), 2048)
        self.assertIn('name count', str(crowded_error.exception))
        with self.assertRaises(sound_banks.BankFormatError) as short_prefix:
            sound_banks.convert_sbk(b'\x00' * 4096, 1000)
        self.assertIn('one or ten sectors', str(short_prefix.exception))
        with self.assertRaises(sound_banks.BankFormatError):
            sound_banks.convert_sbk(b'\x00' * 8192, 4096)
        with self.assertRaises(TypeError):
            sound_banks.convert_sbk('not-bytes', 2048)
        empty = sblk_v1([], num_vags=0, vag_data_size=0)
        report = sound_banks.convert_sbk(sfx_file(empty, b''), 2048)
        self.assertEqual(report.samples, 0)
        self.assertEqual(report.ps2_bytes, 0)
        self.assertEqual(report.converted, 0)

    def test_malformed_banks_are_refused(self):
        """Structural damage raises ``BankFormatError`` (AI-assisted), ISC.

        ``loader.cpp`` 415-418 refuses a type other than 1 or 3. ``loader.cpp``
        426-428 refuses ``SBv2`` without a third chunk. ``loader.cpp`` 229-231
        is the only ``SBlk`` version branch, so versions other than 1 and 2
        are refused. A sample chunk must be whole 16-byte blocks.
        (AI-assisted), ISC.
        """
        block = spu_block(0, 0, 0x01, 1)
        program = sblk_v1([0], num_vags=1, vag_data_size=16)
        cases = {
            'truncated': (b'\x00' * 16, 2048, 'truncated'),
            'type': (prefix_file(2048, pack_chunks([program, block], kind=2)), 2048, 'bad file type'),
            'three chunks': (prefix_file(2048, pack_chunks([program, block, b'\x00' * 16])), 2048, 'SFX bank needs two chunks'),
            'short sample': (sfx_file(sblk_v1([], vag_data_size=15), b'\x00' * 15), 2048, 'whole SPU blocks'),
        }
        for name, (data, prefix, fragment) in cases.items():
            with self.subTest(case=name):
                with self.assertRaises(sound_banks.BankFormatError) as caught:
                    sound_banks.convert_sbk(data, prefix)
                self.assertIn(fragment, str(caught.exception))
        version = bytearray(60)
        version[0:4] = b'SBlk'
        struct.pack_into('<I', version, 4, 4)
        self._refuse_sfx(prefix_file(2048, pack_chunks([bytes(version), block])), 'unsupported SBlk version')
        negative = bytearray(60)
        negative[0:4] = b'SBlk'
        struct.pack_into('<I', negative, 4, 1)
        struct.pack_into('<h', negative, 22, -1)
        self._refuse_sfx(prefix_file(2048, pack_chunks([bytes(negative), block])), 'negative NumSounds')
        grain = bytearray(72)
        grain[0:4] = b'SBlk'
        struct.pack_into('<I', grain, 4, 1)
        struct.pack_into('<h', grain, 22, 1)
        struct.pack_into('<I', grain, 28, 60)
        struct.pack_into('<I', grain, 32, 72)
        grain[64] = 0x80
        self._refuse_sfx(prefix_file(2048, pack_chunks([bytes(grain), block])), 'negative grain count')
        sounds = bytearray(60)
        sounds[0:4] = b'SBlk'
        struct.pack_into('<I', sounds, 4, 1)
        struct.pack_into('<h', sounds, 22, 3)
        struct.pack_into('<I', sounds, 28, 60)
        self._refuse_sfx(prefix_file(2048, pack_chunks([bytes(sounds), block])), 'sound table exceeds SBlk')
        grains = bytearray(72)
        grains[0:4] = b'SBlk'
        struct.pack_into('<I', grains, 4, 1)
        struct.pack_into('<h', grains, 22, 1)
        struct.pack_into('<I', grains, 28, 60)
        struct.pack_into('<I', grains, 32, 72)
        grains[64] = 1
        self._refuse_sfx(prefix_file(2048, pack_chunks([bytes(grains), block])), 'grain table exceeds SBlk')
        tone = bytearray(84)
        tone[0:4] = b'SBlk'
        struct.pack_into('<I', tone, 4, 2)
        struct.pack_into('<h', tone, 22, 1)
        struct.pack_into('<I', tone, 28, 64)
        struct.pack_into('<I', tone, 32, 76)
        struct.pack_into('<I', tone, 52, 0)
        tone[68] = 1
        struct.pack_into('<I', tone, 76, (1 << 24) | 1000)
        self._refuse_sfx(prefix_file(2048, pack_chunks([bytes(tone), block])), 'tone exceeds SBlk')
        overlap = bytearray(40)
        struct.pack_into('<I', overlap, 0, 1)
        struct.pack_into('<I', overlap, 4, 2)
        struct.pack_into('<II', overlap, 8, 24, 16)
        struct.pack_into('<II', overlap, 16, 24, 16)
        overlap[24:] = block
        self._refuse_sfx(prefix_file(2048, bytes(overlap)), 'overlaps')
        exceeds = bytearray(24)
        struct.pack_into('<I', exceeds, 0, 1)
        struct.pack_into('<I', exceeds, 4, 1)
        struct.pack_into('<II', exceeds, 8, 24, 16)
        self._refuse_sfx(prefix_file(2048, bytes(exceeds)), 'exceeds image')
        alien = bytearray(8)
        alien[0:4] = b'NOPE'
        self._refuse_sfx(prefix_file(2048, pack_chunks([bytes(alien), block])), 'missing or truncated SBlk')
        music = sbv2([0], num_vags=1, vag_data_size=16)
        with self.assertRaises(sound_banks.BankFormatError) as two_chunks:
            sound_banks.convert_music(pack_chunks([music, block]), 0)
        self.assertIn('SBv2 without midi data not supported', str(two_chunks.exception))
        bare = bytearray(48)
        bare[0:4] = b'SBv2'
        struct.pack_into('<h', bare, 22, -1)
        with self.assertRaises(sound_banks.BankFormatError) as programs:
            sound_banks.convert_music(pack_chunks([bytes(bare), block, midi_chunk(b'MID ')]), 0)
        self.assertIn('negative NumProgs', str(programs.exception))
        empty_midi = struct.pack('<II', 1, 0)
        with self.assertRaises(sound_banks.BankFormatError) as midi_error:
            sound_banks.convert_music(pack_chunks([sbv2([]), block, empty_midi]), 0)
        self.assertIn('midi chunk', str(midi_error.exception))

    def _refuse_sfx(self, data, fragment):
        with self.assertRaises(sound_banks.BankFormatError) as caught:
            sound_banks.convert_sbk(data, 2048)
        self.assertIn(fragment, str(caught.exception))

    def test_slot_peek_and_sector_scan(self):
        """The slot offset wins only when it peeks as the right fourcc (AI-assisted), ISC.

        ``fake_iso.cpp`` 240-242. ``common`` is ten sectors. A type word with
        the wrong fourcc is scanned (``streamed_audio.cpp`` 274-298) and
        marked UNVERIFIED. A later full-parse failure stays on that slot.
        (AI-assisted), ISC.
        """
        block = spu_block(0, 0, 0x01, 1)
        real = sblk_v1([0], num_vags=1, vag_data_size=16, vags_in_sr=42)
        decoy = sblk_v1([0], num_vags=1, vag_data_size=16, vags_in_sr=7)
        real_image = pack_chunks([real, block])
        decoy_image = pack_chunks([decoy, block])
        common = bytearray(20480) + bytearray(real_image)
        common[2048:2048 + len(decoy_image)] = decoy_image
        report = sound_banks.load_sfx_bytes(bytes(common), 'COMMON.SBK')
        self.assertEqual(report.prefix, 20480)
        self.assertEqual(report.vags_in_sr, 42)
        self.assertTrue(report.prefix_rule.startswith('slot-0'))
        fake = bytearray(60)
        fake[0:4] = b'XXXX'
        struct.pack_into('<I', fake, 4, 1)
        fake_image = pack_chunks([bytes(fake), block])
        scanned = bytearray(4096) + bytearray(real_image)
        scanned[2048:2048 + len(fake_image)] = fake_image
        found = sound_banks.load_sfx_bytes(bytes(scanned), 'training.sbk')
        self.assertEqual(found.prefix, 4096)
        self.assertEqual(found.vags_in_sr, 42)
        self.assertIn('UNVERIFIED', found.prefix_rule)
        bad = bytearray(60)
        bad[0:4] = b'SBlk'
        struct.pack_into('<I', bad, 4, 1)
        struct.pack_into('<h', bad, 22, -1)
        bad_image = pack_chunks([bytes(bad), block])
        stuck = bytearray(4096) + bytearray(real_image)
        stuck[2048:2048 + len(bad_image)] = bad_image
        with self.assertRaises(sound_banks.BankFormatError) as held:
            sound_banks.load_sfx_bytes(bytes(stuck), 'training.sbk')
        self.assertIn('negative NumSounds', str(held.exception))

    def test_music_starts_at_byte_zero_unless_that_sector_is_not_sbv2(self):
        """``.mus`` prefers byte 0 (AI-assisted), ISC.

        ``fake_iso.cpp`` 213. A sector scan is UNVERIFIED.
        ``streamed_audio.cpp`` 345-347 skips ``TWEAKVAL``.
        (AI-assisted), ISC.
        """
        block = spu_block(0, 0, 0x01, 1)
        early = sbv2([0], num_vags=1, vag_data_size=16, vags_in_sr=3)
        report = sound_banks.load_music_bytes(music_file(early, block, midi_chunk(b'MID ')), 'village1.mus')
        self.assertEqual(report.prefix, 0)
        self.assertIn('byte-0', report.prefix_rule)
        self.assertEqual(report.vags_in_sr, 3)
        later = sbv2([0], num_vags=1, vag_data_size=16, vags_in_sr=9)
        later_image = music_file(later, block, midi_chunk(b'MID '))
        cover = pack_chunks([sblk_v1([], num_vags=0, vag_data_size=0), block])
        data = bytearray(2048) + bytearray(later_image)
        data[:len(cover)] = cover
        scanned = sound_banks.load_music_bytes(bytes(data), 'beach.mus')
        self.assertEqual(scanned.prefix, 2048)
        self.assertEqual(scanned.vags_in_sr, 9)
        self.assertIn('UNVERIFIED', scanned.prefix_rule)
        self.assertIsNone(sound_banks.load_music_bytes(b'garbage', 'TWEAKVAL.MUS'))

    def test_vag_directory_and_clips(self):
        """Jak 1 dialogue is a directory plus sector-addressed clips (AI-assisted), ISC.

        ``iso.h`` 28 allows 868 entries. ``overlord_common.h`` 3 defines a
        2048-byte sector. ``iso.cpp`` 1200-1215 accepts ``VAGp`` and ``pGAV``
        and treats the size as payload bytes. Clip bytes stay out of a level
        total; that assertion is in the disc test.
        (AI-assisted), ISC.
        """
        with self.assertRaises(sound_banks.BankFormatError) as too_many:
            sound_banks.parse_vag_directory(struct.pack('<I', 869))
        self.assertIn('868', str(too_many.exception))
        with self.assertRaises(sound_banks.BankFormatError) as short:
            sound_banks.parse_vag_directory(struct.pack('<I', 1))
        self.assertIn('shorter', str(short.exception))
        exact = struct.pack('<I', 868) + (b'\x00' * 12 * 868)
        self.assertEqual(len(sound_banks.parse_vag_directory(exact)), 868)
        block = spu_block(0, 0, 0x01, 1)
        first = vagp_clip(block)
        second = pgav_clip(block)
        wad = bytearray(4096 + len(second))
        wad[2048:2048 + len(first)] = first
        wad[4096:4096 + len(second)] = second
        entries = ((b'HELLO   ', 1), (b'PGAVCLIP', 2))
        dialogue = sound_banks.convert_dialogue(bytes(wad), entries, 'VAG/VAGWAD.ENG')
        self.assertEqual(dialogue.clips, 2)
        self.assertEqual(dialogue.converted, 2)
        self.assertEqual(dialogue.ps2_bytes, 32)
        self.assertEqual(dialogue.aica_bytes, len(vag_adpcm.convert_vag(first).aica) + len(vag_adpcm.convert_vag(second).aica))
        self.assertEqual(dialogue.longest[0], 'HELLO')
        text = '\n'.join(sound_banks.format_dialogue(dialogue))
        self.assertIn('dialogue_in_level_total: no', text)

    def test_filename_match_prefers_the_exact_stem(self):
        """Eight-character lookup is the ISO cut (AI-assisted), ISC.

        ``fake_iso.cpp`` 245-247 and 207-208. Retail spelling of a longer
        name is UNVERIFIED. ``empty1.sbk`` is not a substitute (``fake_iso.cpp``
        249-254 is the loader fallback this tool does not copy).
        (AI-assisted), ISC.
        """
        exact, how = sound_banks.match_files(
            [Path('cut/firecany.sbk'), Path('full/firecanyon.sbk')], 'firecanyon')
        self.assertEqual(how, 'exact')
        self.assertEqual(exact.name, 'firecanyon.sbk')
        cut, how = sound_banks.match_files([Path('SBK/finalbos.sbk')], 'finalboss')
        self.assertEqual(how, '8-char')
        self.assertEqual(cut.name, 'finalbos.sbk')
        missing, how = sound_banks.match_files([Path('SBK/empty1.sbk')], 'beach')
        self.assertIsNone(missing)
        self.assertEqual(how, 'missing')
        ambiguous, how = sound_banks.match_files(
            [Path('a/firecany.sbk'), Path('b/FIRECANY.sbk')], 'firecanyon')
        self.assertIsNone(ambiguous)
        self.assertEqual(how, 'ambiguous')

    def test_disc_tree_budget_skips_tweakval_and_leaves_dialogue_out(self):
        """A temporary tree is read and the level totals omit dialogue (AI-assisted), ISC.

        Intro has no bank (``level-info.gc`` 2224-2226), so its total is the
        common bank plus the 49200-byte reserve. ``firecanyon`` matches the
        8-character stem. ``commonj`` is reported and is not the default total
        (``gsound.gc`` 123-124). The run sizes AICA bytes from the block count.
        (AI-assisted), ISC.
        """
        block = spu_block(0, 0, 0x01, 1)
        one = len(vag_adpcm.convert_vag(block).aica)
        parent = Path(tempfile.mkdtemp(prefix='sound-banks-iso-'))
        try:
            root = parent / 'jak1'
            (root / 'sbk').mkdir(parents=True)
            (root / 'mus').mkdir()
            (root / 'vag').mkdir()
            common = sfx_file(sblk_v1([0], num_vags=1, vag_data_size=16, vags_in_sr=1), block, prefix=20480)
            commonj = sfx_file(
                sblk_v1([0, 16], num_vags=2, vag_data_size=32, vags_in_sr=2), repeat(block, 2), prefix=20480)
            training = sfx_file(sblk_v1([0, 16], num_vags=2, vag_data_size=32), repeat(block, 2))
            fire = sfx_file(sblk_v1([0], num_vags=1, vag_data_size=16), block)
            music = music_file(sbv2([0], num_vags=1, vag_data_size=16), block, midi_chunk(b'MID '))
            blobs = {
                root / 'sbk' / 'common.SBK': common,
                root / 'sbk' / 'commonj.sbk': commonj,
                root / 'sbk' / 'training.sbk': training,
                root / 'sbk' / 'firecany.sbk': fire,
                root / 'mus' / 'village1.mus': music,
                root / 'mus' / 'TWEAKVAL.MUS': b'not-a-bank',
            }
            clip = vagp_clip(block)
            wad = b'\x00' * 2048 + clip
            directory = struct.pack('<I', 1) + b'HELLO   ' + struct.pack('<I', 1)
            blobs[root / 'vag' / 'vagdir.ayb'] = directory
            blobs[root / 'vag' / 'vagwad.eng'] = wad
            for path, blob in blobs.items():
                path.write_bytes(blob)
            before = {path: path.read_bytes() for path in blobs}
            code, text, calls = budget_run(parent)
            self.assertEqual(calls, [])
            self.assertEqual(code, 0)
            self.assertEqual(before, {path: path.read_bytes() for path in blobs})
            self.assertIn('sound_ram: 2097152', text)
            self.assertIn('driver_reserve: 49200', text)
            self.assertIn('bank_cap: 2047952', text)
            self.assertIn('iso_root: jak1', text)
            self.assertIn('skipped: mus/TWEAKVAL.MUS', text)
            self.assertIn('skip_reason: streamed_audio.cpp 345-347', text)
            self.assertIn('dialogue_in_level_total: no', text)
            self.assertIn('commonj_aica: %d' % (one * 2), text)
            self.assertIn('gsound.gc 124', text)
            intro = level_block(text, 'intro')
            self.assertEqual(field(intro, 'sfx'), 'none')
            self.assertEqual(field(intro, 'sfx_aica'), '0')
            self.assertEqual(field(intro, 'aica_sum'), str(one))
            self.assertEqual(field(intro, 'with_reserve'), str(one + 49200))
            self.assertEqual(field(intro, 'fits'), 'yes')
            title = level_block(text, 'title')
            self.assertEqual(field(title, 'aica_sum'), str(one * 2))
            training_lines = level_block(text, 'training')
            self.assertEqual(field(training_lines, 'aica_sum'), str(one * 4))
            canyon = level_block(text, 'firecanyon')
            self.assertEqual(field(canyon, 'sfx_match'), '8-char')
            self.assertEqual(field(canyon, 'fits'), 'unknown')
            self.assertIn('worst_fits: unknown', text)
            self.assertIn('beach', field(text.splitlines(), 'worst_missing'))
            self.assertIn('VAGWAD.FRE missing', text)
        finally:
            shutil.rmtree(parent)

    def test_worst_pair_ignores_empty1_and_a_refused_file_exits_one(self):
        """The derived pair is two level banks plus one music bank (AI-assisted), ISC.

        ``level.gc`` 1036 allows two level banks. ``empty1`` is not in
        ``*level-load-list*``. A refused sibling still leaves the good bank
        in the report and the process code is 1. A bad chunk type is reported
        as ``bad file type``. The run sizes AICA bytes from the block count.
        (AI-assisted), ISC.
        """
        block = spu_block(0, 0, 0x01, 1)
        one = len(vag_adpcm.convert_vag(block).aica)
        parent = Path(tempfile.mkdtemp(prefix='sound-banks-full-'))
        try:
            root = parent / 'jak1'
            (root / 'SBK').mkdir(parents=True)
            (root / 'MUS').mkdir()
            sfx_names, music_names = unique_banks()
            for name in sfx_names:
                count = 2 if name in ('beach', 'jungle') else 1
                program = sblk_v1(list(range(0, count * 16, 16)), num_vags=count, vag_data_size=count * 16)
                (root / 'SBK' / (name + '.sbk')).write_bytes(sfx_file(program, repeat(block, count)))
            (root / 'SBK' / 'common.sbk').write_bytes(
                sfx_file(sblk_v1([0], num_vags=1, vag_data_size=16), block, prefix=20480))
            (root / 'SBK' / 'empty1.sbk').write_bytes(
                sfx_file(sblk_v1(list(range(0, 64, 16)), num_vags=4, vag_data_size=64), repeat(block, 4)))
            for name in music_names:
                count = 2 if name == 'village1' else 1
                program = sbv2(list(range(0, count * 16, 16)), num_vags=count, vag_data_size=count * 16)
                (root / 'MUS' / (name + '.mus')).write_bytes(
                    music_file(program, repeat(block, count), midi_chunk(b'MID ')))
            code, text, calls = budget_run(root)
            self.assertEqual(calls, [])
            self.assertEqual(code, 0)
            self.assertEqual(field(text.splitlines(), 'worst_sfx'), 'beach+jungle')
            self.assertEqual(field(text.splitlines(), 'worst_sfx_aica'), str(one * 4))
            self.assertEqual(field(text.splitlines(), 'worst_music'), 'village1')
            self.assertEqual(field(text.splitlines(), 'worst_music_aica'), str(one * 2))
            self.assertEqual(field(text.splitlines(), 'worst_aica_sum'), str(one * 7))
            self.assertEqual(field(text.splitlines(), 'worst_with_reserve'), str(one * 7 + 49200))
            self.assertEqual(field(text.splitlines(), 'worst_fits'), 'yes')
            self.assertIn('UNVERIFIED', field(text.splitlines(), 'worst_note'))
            self.assertNotIn('empty1', field(text.splitlines(), 'worst_sfx'))
            self.assertIn('aica_bytes: %d' % (one * 4), text)
        finally:
            shutil.rmtree(parent)

        refused_root = Path(tempfile.mkdtemp(prefix='sound-banks-refused-'))
        try:
            (refused_root / 'SBK').mkdir()
            good = sfx_file(sblk_v1([0], num_vags=1, vag_data_size=16), block)
            bad = prefix_file(2048, pack_chunks([sblk_v1([0]), block], kind=2))
            (refused_root / 'SBK' / 'beach.sbk').write_bytes(good)
            (refused_root / 'SBK' / 'noise.sbk').write_bytes(bad)
            code, text, calls = budget_run(refused_root)
            self.assertEqual(calls, [])
            self.assertEqual(code, 1)
            self.assertIn('bank: SBK/beach.sbk', text)
            self.assertIn('bank: SBK/noise.sbk', text)
            self.assertIn('REFUSED:', text)
            self.assertIn('bad file type', text)
            self.assertEqual((refused_root / 'SBK' / 'beach.sbk').read_bytes(), good)
        finally:
            shutil.rmtree(refused_root)

    def test_output_stays_under_build_and_missing_iso_writes_nothing(self):
        """Converted bytes are written only under the port build directory (AI-assisted), ISC.

        The container starts with ``DCSA`` and the first word is the sample
        count from ``pack_aica``. Dialogue wads are not written.
        (AI-assisted), ISC.
        """
        block = spu_block(0, 0, 0x01, 1)
        root = Path(tempfile.mkdtemp(prefix='sound-banks-write-'))
        build = Path(sound_banks.__file__).resolve().parent / 'build'
        out = build / 'sound-banks-synthetic-test'
        outside = Path(tempfile.mkdtemp(prefix='sound-banks-outside-'))
        build_existed = build.exists()
        try:
            (root / 'SBK').mkdir()
            (root / 'MUS').mkdir()
            blob = sfx_file(sblk_v1([0, 16], num_vags=2, vag_data_size=32, sram=12345), repeat(block, 2))
            (root / 'SBK' / 'training.sbk').write_bytes(blob)
            (root / 'MUS' / 'village1.mus').write_bytes(
                music_file(sbv2([0], num_vags=1, vag_data_size=16), block, midi_chunk(b'MID ')))
            with self.assertRaises(sound_banks.BankFormatError):
                sound_banks.run(root, outside)
            self.assertFalse((outside / 'SOUND_BANK_REPORT.txt').exists())
            if out.exists():
                shutil.rmtree(out)
            code, text = sound_banks.run(root, out)
            self.assertEqual(code, 0)
            written = sorted(path.name for path in out.iterdir())
            self.assertEqual(written, ['SOUND_BANK_REPORT.txt', 'music-village1.aica', 'sfx-training.aica'])
            self.assertEqual((out / 'SOUND_BANK_REPORT.txt').read_text(encoding='utf-8'), text)
            packed = (out / 'sfx-training.aica').read_bytes()
            header, rows = unpack_dcsa(packed)
            self.assertEqual(header[0], 2)
            self.assertEqual(packed[4:8], struct.pack('<I', 2))
            self.assertEqual(header[4], 12345)
            self.assertEqual(rows[0][1], vag_adpcm.convert_vag(block).aica)
            self.assertEqual(rows[0][0][4], 0xFFFFFFFF)
            self.assertEqual(rows[0][0][5], 27)
            self.assertEqual(rows[0][0][7], 1)
            self.assertEqual((root / 'SBK' / 'training.sbk').read_bytes(), blob)
            missing = root / 'missing-iso'
            missing_out = build / 'sound-banks-missing-iso'
            code, text = sound_banks.run(missing, missing_out)
            self.assertEqual(code, 1)
            self.assertIn('not a directory', text)
            self.assertFalse(missing_out.exists())
            bare = Path(tempfile.mkdtemp(prefix='sound-banks-empty-'))
            try:
                code, text = sound_banks.run(bare, None)
                self.assertEqual(code, 1)
                self.assertIn('no SBK or MUS', text)
            finally:
                shutil.rmtree(bare)
        finally:
            shutil.rmtree(root)
            shutil.rmtree(outside)
            if out.exists():
                shutil.rmtree(out)
            missing_out = build / 'sound-banks-missing-iso'
            if missing_out.exists():
                shutil.rmtree(missing_out)
            if not build_existed and build.exists():
                try:
                    build.rmdir()
                except OSError:
                    pass

    def test_main_argument_errors_and_source_limits(self):
        """The CLI and the module imports stay inside the standard library (AI-assisted), ISC.

        A missing ``iso_data`` argument is argparse's own exit. The converter
        opens disc files with ``rb``.
        (AI-assisted), ISC.
        """
        with self.assertRaises(SystemExit) as caught:
            sound_banks.main([])
        self.assertEqual(caught.exception.code, 2)
        banks = Path(sound_banks.__file__).read_text(encoding='utf-8')
        self.assertIn(".open('rb')", banks)
        self.assertNotIn(".open('wb')", banks)
        self.assertNotIn('sbk_bank', banks)
        self.assertNotIn('0xAF' + 'CC0', banks)
        self.assertNotIn('0x42' + '800', banks)
        allowed = {
            sound_banks.__file__: sys.stdlib_module_names | {'vag_adpcm'},
            __file__: sys.stdlib_module_names | {'vag_adpcm', 'sound_banks'},
        }
        for path, names in allowed.items():
            for node in ast.walk(ast.parse(Path(path).read_text(encoding='utf-8'))):
                imported = []
                if isinstance(node, ast.Import):
                    imported = [alias.name for alias in node.names]
                elif isinstance(node, ast.ImportFrom):
                    imported = [node.module or '']
                for name in imported:
                    self.assertIn(name.split('.')[0], names)
        mark = '(AI-assisted), ISC.'
        for path in (sound_banks.__file__, __file__):
            tree = ast.parse(Path(path).read_text(encoding='utf-8'))
            for node in ast.walk(tree):
                if isinstance(node, (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                    doc = ast.get_docstring(node)
                    if doc:
                        self.assertTrue(doc.rstrip().endswith(mark), path)
        notes = Path(sound_banks.__file__).resolve().parent / 'SOUND_BANKS_NOTES.md'
        note_text = notes.read_text(encoding='utf-8')
        self.assertTrue(note_text.startswith('(AI-assisted)\n'))
        self.assertIn('python ports/dreamcast/sound_banks.py iso_data/jak1', note_text)
        self.assertIn('python ports/dreamcast/tests/test_sound_banks.py', note_text)
        self.assertIn('was not run', note_text)

    def test_commonj_alone_fills_the_common_slot(self):
        """SCEI commonj is the common bank when common is absent (AI-assisted), ISC.

        ``gsound.gc`` 123 loads ``commonj`` for SCEI.
        (AI-assisted), ISC.
        """
        block = spu_block(0, 0, 0x01, 1)
        one = len(vag_adpcm.convert_vag(block).aica)
        root = Path(tempfile.mkdtemp(prefix='sound-banks-commonj-'))
        try:
            (root / 'SBK').mkdir()
            (root / 'SBK' / 'commonj.sbk').write_bytes(
                sfx_file(sblk_v1([0], num_vags=1, vag_data_size=16), block, prefix=20480))
            code, text, calls = budget_run(root)
            self.assertEqual(calls, [])
            self.assertEqual(code, 0)
            intro = level_block(text, 'intro')
            self.assertEqual(field(intro, 'common'), 'commonj')
            self.assertEqual(field(intro, 'aica_sum'), str(one))
            self.assertIn('gsound.gc 123', text)
        finally:
            shutil.rmtree(root)


if __name__ == '__main__':
    unittest.main()
