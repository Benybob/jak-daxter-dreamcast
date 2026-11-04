"""Convert Jak 1 sound banks from PS2 SPU ADPCM to Dreamcast AICA ADPCM.

The disc is opened read-only. Converted banks are written only under this
port's ``build`` directory, and only when the caller asks. Nothing here is a
retail measurement: a run on ``iso_data`` prints the numbers, and this module
does not record that it ran.

Words in the 989snd image are little-endian. ``BinaryReader::read`` copies
them with the host endian (``common/util/BinaryReader.h`` 20-26), and
``iso.cpp`` 1200 compares the VAG magic as a little-endian ``u32``.

SFX name prefix, before the 989snd image (``fake_iso.cpp`` 221-233 and
240-242, ``sbank.cpp`` 91-95):

- 16-byte bank name, one skipped ``u32``, a ``u32`` count at offset 20
- ``count`` records of 16 name bytes and a ``u32`` falloff
- slot 0 is loaded first (``gsound.gc`` 122-124) and the image starts at
  ``10 * 2048``. Every later slot starts at ``2048``

989snd directory (``loader.cpp`` 19-33 and 410-440): ``u32`` type 1 or 3,
``u32`` chunk count, then count pairs of offset and size. Offsets are from
the start of this image. Chunk 0 is the program, chunk 1 is the samples,
chunk 2 is MIDI for ``SBv2`` only.

``SBlk`` program (``loader.cpp`` 198-257), version 1 header through 60 bytes
and version 2 through 64: ``NumSounds`` at 22, ``NumVAGs`` at 26,
``FirstSound`` at 28, ``FirstGrain`` at 32, discarded ``VagsInSR`` at 36,
discarded ``VagDataSize`` at 40, discarded ``SRAMAllocSize`` at 44, and
``GrainData`` at 52 when the version is at least 2. Each sound is 12 bytes.
A version-1 grain is ``0x28`` bytes (``loader.cpp`` 134). A version-2 grain
is an opcode plus a delay, 8 bytes, and the tone sits at ``GrainData`` plus
the low 24 bits (``loader.cpp`` 151-161). Tone types are 1 and 9
(``sfxgrain.h`` 56-57). ``SampleOffset`` is byte 16 of ``ReadTone``
(``loader.cpp`` 51-52).

``SBv2`` program (``loader.cpp`` 334-387): ``NumSounds`` at 20, ``NumProgs``
at 22, discarded ``NumVAGs`` at 26, ``FirstProg`` at 32, the header
``FirstTone`` is discarded (352), discarded ``VagsInSR`` at 40, discarded
``VagDataSize`` at 44. The header is 48 bytes. Each sound is 28 bytes
(``musicbank.h`` 40, ``loader.cpp`` 361-371). Each program is 8 bytes and
its tones are 24-byte ``ReadTone`` records. The MIDI chunk is itself a chunk
directory (``loader.cpp`` 391-404). This tool does not convert MIDI.

A sample span is walked from a 16-byte block boundary through the first
block whose flag byte has bit 0 set. That bit is ``LoopEnd``, bit 8 of the
little-endian header word (``voice.h`` 81, ``voice.cpp`` 56-65). The next
span starts on the next block. Packing those spans back to back, with no
pad after ``LoopEnd``, is UNVERIFIED: the voice stops or jumps there, and
the file order is not given a length field. ``convert_vag`` resets SPU
history, which is what ``Voice::KeyOn`` stores (``voice.cpp`` 93-94).
A span whose filter ``vag_adpcm`` rejects is listed on that bank. The other
spans are still counted. ``aica_bytes`` then counts only those spans, so
the total is a lower bound.

Each kept block is 28 PCM samples (``vag_adpcm``) and AICA stores two
samples per byte, so the AICA length is 14 bytes times the block count.
That is the length ``convert_vag`` would write. The disc report uses this
size and does not call ``convert_vag``. ``convert_vag`` runs when a DCSA
file is written, and when ``convert_sbk`` or ``convert_music`` is asked
to encode. Dialogue clips use the same size and are not encoded.

Sound RAM is 2097152 bytes (``MEMORY_PLAN.md`` 3, ``SOUND_PLAN.md`` 121).
The driver reserve kept here is the 49200-byte stream window
``snd_SRAMMalloc(0xc030)`` (``ssound.cpp`` 67). Bank AICA bytes must fit in
what remains. Dialogue clips use that window and are not added to a level.
Jak 2 ``spu_size`` constants (``sbank.cpp`` 48-72) are not read.

(AI-assisted), ISC.
"""

from __future__ import annotations

import argparse
import bisect
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import vag_adpcm


# MEMORY_PLAN.md 3. SOUND_PLAN.md 121. Two mebibytes of sound RAM.
SOUND_RAM_BYTES = 2097152
# ssound.cpp 67. The stream window Overlord reserves before any bank.
DRIVER_RESERVE_BYTES = 0xC030
# SOUND_PLAN.md 122. What remains for bank samples.
BANK_CAP_BYTES = SOUND_RAM_BYTES - DRIVER_RESERVE_BYTES

# overlord_common.h 3. fake_iso.cpp 240-242 uses the same 2048-byte sector.
_SECTOR = 0x800
_SLOT0_PREFIX = 10 * _SECTOR
_SLOTN_PREFIX = _SECTOR
# fake_iso.cpp 222-232. 16-byte name, skipped u32, count, then 16+4 records.
_NAME_HEADER = 24
_NAME_RECORD = 20
# iso.h 28. The IOP directory holds this many Jak 1 entries.
_VAG_DIR_LIMIT = 868
# iso.h 24-27. Eight name bytes and a sector index.
_VAG_ENTRY = 12
# voice.h 81. LoopEnd is bit 8 of the header word, byte 1 bit 0.
_LOOP_END = 0x01
# sfxgrain.h 56-57.
_TONE_TYPES = (1, 9)
# srpc.cpp 46. ISO extension order, not a guess.
_LANGUAGES = ('ENG', 'FRE', 'GER', 'SPA', 'ITA', 'JAP', 'UKE')

_SLOT0_RULE = 'slot-0 10*2048 (sbank.cpp 91-92, fake_iso.cpp 240-242)'
_SLOTN_RULE = 'slot-n 2048 (sbank.cpp 93-95, fake_iso.cpp 241-242)'
_BYTE0_RULE = 'byte-0 (fake_iso.cpp 213)'


class BankFormatError(ValueError):
    """The bytes are not a bank or directory this reader will convert (AI-assisted), ISC."""


@dataclass(frozen=True)
class SoundName:
    """One Overlord play name and its falloff word (AI-assisted), ISC."""

    name: bytes
    falloff: int


@dataclass(frozen=True)
class SampleImage:
    """One converted span inside a sample chunk (AI-assisted), ISC."""

    index: int
    ps2_offset: int
    ps2_bytes: int
    pcm_count: int
    aica: bytes
    loop_start: int | None
    loop_end: int | None
    loop_repeat: bool
    ended_on_loop_end: bool
    referenced: bool


@dataclass(frozen=True)
class BankReport:
    """One SFX or music file after a successful read (AI-assisted), ISC."""

    source: str
    kind: str
    prefix: int
    prefix_rule: str
    version: int
    file_bytes: int
    samples: int
    converted: int
    ps2_bytes: int
    aica_bytes: int
    vag_data_size: int | None
    sram_alloc_size: int | None
    header_vag_count: int
    vags_in_sr: int
    tone_offsets: int
    tones_at_span_starts: int
    tones_inside: int
    longest: tuple | None
    failures: tuple
    notes: tuple
    names: tuple
    table_name: bytes
    converted_samples: tuple


@dataclass(frozen=True)
class Refusal:
    """One file the reader refused (AI-assisted), ISC."""

    source: str
    reason: str


@dataclass(frozen=True)
class DialogueReport:
    """One language wad of streamed clips (AI-assisted), ISC."""

    source: str
    clips: int
    converted: int
    ps2_bytes: int
    aica_bytes: int
    longest: tuple | None
    failures: tuple


def falloff_parts(word):
    """Split a name-table falloff word into min, max, and curve.

    ``srpc.cpp`` 172-178. Min is bits 0-13, max is bits 14-27, curve is bits
    28-31. ``soundcommon.cpp`` 40-42 prints the same split.
    (AI-assisted), ISC.
    """
    return word & 0x3FFF, (word >> 14) & 0x3FFF, word >> 28


def _u32(data, offset):
    """Read one little-endian ``u32`` (AI-assisted), ISC."""
    if offset < 0 or offset + 4 > len(data):
        raise BankFormatError('truncated field')
    return struct.unpack_from('<I', data, offset)[0]


def _s16(data, offset):
    """Read one little-endian ``s16`` (AI-assisted), ISC."""
    if offset < 0 or offset + 2 > len(data):
        raise BankFormatError('truncated field')
    return struct.unpack_from('<h', data, offset)[0]


def _require_bytes(data):
    """Reject a bank that is not a byte string (AI-assisted), ISC."""
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError('bank data must be bytes')
    return bytes(data)


def read_chunks(image):
    """Read a 989snd chunk directory and return its type and slices.

    ``loader.cpp`` 25-33 reads the type, the count, and each offset/size.
    ``loader.cpp`` 420-423 uses those offsets from the start of the image.
    A chunk that covers the directory, or another chunk, is refused here.
    The loader does not check that overlap. A resident sample span has to
    be one range. (AI-assisted), ISC.
    """
    if len(image) < 8:
        raise BankFormatError('truncated chunk directory')
    kind = _u32(image, 0)
    count = _u32(image, 4)
    if kind not in (1, 3):
        raise BankFormatError('bad file type')
    if count > (len(image) - 8) // 8:
        raise BankFormatError('chunk directory exceeds image')
    directory_end = 8 + count * 8
    spans = []
    ranges = []
    for index in range(count):
        base = 8 + index * 8
        offset = _u32(image, base)
        size = _u32(image, base + 4)
        if offset > len(image) or size > len(image) - offset:
            raise BankFormatError('chunk exceeds image')
        if size and offset < directory_end:
            raise BankFormatError('chunk overlaps directory or another chunk')
        end = offset + size
        for start, previous in ranges:
            if size and previous > start and offset < previous and start < end:
                raise BankFormatError('chunk overlaps directory or another chunk')
        ranges.append((offset, end))
        spans.append(image[offset:end])
    return kind, tuple(spans)


def _peek_fourcc(data, offset, fourcc, count):
    """Return whether a sector holds a directory of this chunk kind.

    The check reads the type, the count, and the first chunk's fourcc.
    It does not copy chunk bodies. A full ``read_chunks`` still runs once
    the offset is chosen. (AI-assisted), ISC.
    """
    if offset < 0 or offset + 8 > len(data):
        return False
    kind = struct.unpack_from('<I', data, offset)[0]
    chunk_count = struct.unpack_from('<I', data, offset + 4)[0]
    if kind not in (1, 3) or chunk_count != count:
        return False
    table = 8 + chunk_count * 8
    if offset + table > len(data):
        return False
    first_off = struct.unpack_from('<I', data, offset + 8)[0]
    first_size = struct.unpack_from('<I', data, offset + 12)[0]
    tail = len(data) - offset
    if first_size < 4 or first_off > tail or first_size > tail - first_off:
        return False
    return data[offset + first_off:offset + first_off + 4] == fourcc


def _refuse_bad_type(data, offset):
    """Raise when the loader offset is a directory whose type is not 1 or 3.

    ``loader.cpp`` 415-418 prints ``bad file type`` after the chunk table
    has been read. A zero count is not that table, so the caller can scan.
    (AI-assisted), ISC.
    """
    if offset < 0 or offset + 8 > len(data):
        return
    kind = struct.unpack_from('<I', data, offset)[0]
    count = struct.unpack_from('<I', data, offset + 4)[0]
    if kind in (1, 3) or count < 1:
        return
    if offset + 8 + count * 8 > len(data):
        return
    raise BankFormatError('bad file type')


def _scan_fourcc(data, fourcc, count):
    """Find the first sector-aligned directory of this kind.

    ``streamed_audio.cpp`` 274-298 steps 2048 bytes. The hit is UNVERIFIED
    when the loader's own offset did not hold the directory. (AI-assisted), ISC.
    """
    offset = 0
    while offset + 16 <= len(data):
        if _peek_fourcc(data, offset, fourcc, count):
            return offset
        offset += _SECTOR
    return None


def parse_name_table(data, prefix):
    """Read the Overlord name prefix in front of an SFX image.

    ``fake_iso.cpp`` 222-232. The count is the file's count. A count that
    walks out of the prefix is the read ``parseSoundBank`` would assert on.
    (AI-assisted), ISC.
    """
    if prefix == 0:
        return b'', ()
    if len(data) < prefix or len(data) < _NAME_HEADER:
        raise BankFormatError('truncated name prefix or chunk directory')
    count = _u32(data, 20)
    max_count = (prefix - _NAME_HEADER) // _NAME_RECORD
    if count > max_count:
        raise BankFormatError('name count crosses fixed sector prefix')
    names = []
    for index in range(count):
        at = _NAME_HEADER + index * _NAME_RECORD
        names.append(SoundName(data[at:at + 16], _u32(data, at + 16)))
    return data[:16], tuple(names)


def _sfx_tone_offsets(program, version):
    """Collect ``SampleOffset`` values from ``SBlk`` tone grains (AI-assisted), ISC."""
    num_sounds = _s16(program, 22)
    if num_sounds < 0:
        raise BankFormatError('negative NumSounds')
    first_sound = _u32(program, 28)
    first_grain = _u32(program, 32)
    # loader.cpp 229-231. Version 2 inserts GrainData before the name offset.
    grain_data = _u32(program, 52) if version >= 2 else 0
    if num_sounds:
        if first_sound > len(program) or num_sounds > (len(program) - first_sound) // 12:
            raise BankFormatError('sound table exceeds SBlk')
    offsets = []
    for index in range(num_sounds):
        base = first_sound + index * 12
        # loader.cpp 241. NumGrains is signed. A negative resize is refused.
        grains = program[base + 4]
        if grains >= 128:
            raise BankFormatError('negative grain count')
        if grains == 0:
            continue
        origin = first_grain + _u32(program, base + 8)
        if version < 2:
            if origin > len(program) or grains > (len(program) - origin) // 0x28:
                raise BankFormatError('grain table exceeds SBlk')
            for grain_index in range(grains):
                gbase = origin + grain_index * 0x28
                if _u32(program, gbase) in _TONE_TYPES:
                    # Tone starts at grain+8. SampleOffset is tone+16.
                    offsets.append(_u32(program, gbase + 24))
        else:
            if origin > len(program) or grains > (len(program) - origin) // 8:
                raise BankFormatError('grain table exceeds SBlk')
            for grain_index in range(grains):
                gbase = origin + grain_index * 8
                opcode = _u32(program, gbase)
                if ((opcode >> 24) & 0xFF) in _TONE_TYPES:
                    tone_at = grain_data + (opcode & 0xFFFFFF)
                    if tone_at > len(program) or tone_at + 24 > len(program):
                        raise BankFormatError('tone exceeds SBlk')
                    offsets.append(_u32(program, tone_at + 16))
    return tuple(offsets)


def _parse_sfx_program(program):
    """Read the ``SBlk`` fields this budget uses (AI-assisted), ISC."""
    if len(program) < 4 or program[:4] != b'SBlk':
        raise BankFormatError('missing or truncated SBlk')
    if len(program) < 8:
        raise BankFormatError('missing or truncated SBlk')
    version = _u32(program, 4)
    # loader.cpp 229-231 is the only version branch in this tree.
    if version not in (1, 2):
        raise BankFormatError('unsupported SBlk version')
    if len(program) < (64 if version == 2 else 60):
        raise BankFormatError('missing or truncated SBlk')
    return {
        'version': version,
        'header_vag_count': _s16(program, 26),
        'vags_in_sr': _u32(program, 36),
        'vag_data_size': _u32(program, 40),
        'sram_alloc_size': _u32(program, 44),
        'tone_offsets': _sfx_tone_offsets(program, version),
        'midi_note': None,
        'vag_cite': '219',
        'size_cite': '225',
    }


def _music_facts(program):
    """Read the ``SBv2`` fields this budget uses (AI-assisted), ISC."""
    if len(program) < 4 or program[:4] != b'SBv2':
        raise BankFormatError('missing or truncated SBv2')
    if len(program) < 48:
        raise BankFormatError('truncated SBv2 header')
    num_sounds = _s16(program, 20)
    num_progs = _s16(program, 22)
    if num_sounds < 0:
        raise BankFormatError('negative NumSounds')
    if num_progs < 0:
        raise BankFormatError('negative NumProgs')
    first_sound = _u32(program, 28)
    first_prog = _u32(program, 32)
    # musicbank.h 40. loader.cpp 361-371 reads 28 bytes and then seeks away.
    if num_sounds:
        if first_sound > len(program) or num_sounds > (len(program) - first_sound) // 28:
            raise BankFormatError('sound table exceeds SBv2')
    offsets = []
    if num_progs:
        if first_prog > len(program) or num_progs > (len(program) - first_prog) // 8:
            raise BankFormatError('program table exceeds SBv2')
        for index in range(num_progs):
            base = first_prog + index * 8
            tones = program[base]
            if tones >= 128:
                raise BankFormatError('negative tone count')
            if tones == 0:
                continue
            # loader.cpp 382-387. The per-program FirstTone, not the header one.
            first_tone = _u32(program, base + 4)
            if first_tone > len(program) or tones > (len(program) - first_tone) // 24:
                raise BankFormatError('tone table exceeds SBv2')
            for tone_index in range(tones):
                offsets.append(_u32(program, first_tone + tone_index * 24 + 16))
    return {
        'version': _u32(program, 4),
        'header_vag_count': _s16(program, 26),
        'vags_in_sr': _u32(program, 40),
        'vag_data_size': _u32(program, 44),
        'sram_alloc_size': None,
        'tone_offsets': tuple(offsets),
        'midi_note': None,
        'vag_cite': '348',
        'size_cite': '357',
    }


def _midi_note(midi):
    """Check the nested MIDI directory and describe a foreign fourcc.

    ``loader.cpp`` 391-404 reads a chunk directory inside the MIDI chunk and
    still returns the bank when the fourcc is neither ``MID `` nor ``MMID``.
    A directory that does not fit is the ``BinaryReader`` assert.
    (AI-assisted), ISC.
    """
    if len(midi) < 8:
        raise BankFormatError('midi chunk directory exceeds chunk')
    count = _u32(midi, 4)
    if count < 1 or 8 + count * 8 > len(midi):
        raise BankFormatError('midi chunk directory exceeds chunk')
    offset = _u32(midi, 8)
    size = _u32(midi, 12)
    if offset > len(midi) or size > len(midi) - offset:
        raise BankFormatError('midi sub-chunk exceeds chunk')
    if size < 4:
        return 'midi sub-chunk has no fourcc (loader.cpp 399-404)'
    fourcc = midi[offset:offset + 4]
    if fourcc not in (b'MID ', b'MMID'):
        return 'midi fourcc is not MID or MMID (loader.cpp 403 logs this and still returns the bank)'
    return None


def _span_size(blob, start=0, size=None):
    """Return PCM samples and AICA bytes for one raw SPU span.

    ``decode_vag`` emits 28 samples per 16-byte block. ``encode_aica`` stores
    two samples per byte, and 28 is even, so the AICA length is 14 bytes
    times the block count. Filter values 5, 6, and 7 raise the same
    ``ValueError`` as ``vag_adpcm``. ``start`` and ``size`` select a range
    inside ``blob`` so a dialogue clip is not copied out of its wad.
    (AI-assisted), ISC.
    """
    if size is None:
        size = len(blob) - start
    if size % 16:
        raise ValueError('VAG payload is not a whole number of 16-byte blocks')
    end = start + size
    for pos in range(start, end, 16):
        filt = (blob[pos] >> 4) & 7
        if filt >= 5:
            raise ValueError(
                'VAG filter %d is outside the five playback coefficients' % filt)
    blocks = size // 16
    return blocks * 28, blocks * 14


def _measure_span(blob, encode_samples):
    """Return PCM count, AICA bytes, and loop fields for one span.

    ``encode_samples`` calls ``convert_vag`` and keeps the encoded bytes.
    The report path keeps the length from ``_span_size`` and an empty
    buffer. (AI-assisted), ISC.
    """
    if encode_samples:
        result = vag_adpcm.convert_vag(blob)
        vag = result.vag
        return (
            len(vag.samples), result.aica, len(result.aica),
            vag.loop_start, vag.loop_end, vag.loop_repeat)
    pcm, aica_len = _span_size(blob)
    return pcm, b'', aica_len, None, None, False


def _vag_payload_at(data, start):
    """Return the payload offset and size of a ``VAGp`` or ``pGAV`` clip.

    ``VAGp`` sizes are big-endian and ``pGAV`` sizes are little-endian, which
    is the split ``vag_adpcm`` uses for a 48-byte header. (AI-assisted), ISC.
    """
    if start < 0 or start + 48 > len(data):
        raise ValueError('VAG size field does not fit the buffer')
    magic = int.from_bytes(data[start:start + 4], 'little')
    big = magic == 0x70474156
    size = int.from_bytes(data[start + 12:start + 16], 'big' if big else 'little')
    if start + 48 + size > len(data):
        raise ValueError('VAG size field does not fit the buffer')
    if size % 16 != 0:
        raise ValueError('VAG size field is not a whole number of blocks')
    return start + 48, size


def _split_spans(chunk):
    """Split a sample chunk on ``LoopEnd`` block boundaries.

    Back-to-back packing is UNVERIFIED. ``voice.cpp`` 56-65 stops the
    sequential walk at the end of the flagged block. (AI-assisted), ISC.
    """
    spans = []
    pos = 0
    while pos < len(chunk):
        end = pos
        ended = False
        while end < len(chunk):
            flags = chunk[end + 1]
            end += 16
            if flags & _LOOP_END:
                ended = True
                break
        spans.append((pos, end, ended))
        pos = end
    return spans


def _convert_chunk(chunk, tone_offsets, facts, encode_samples=True):
    """Convert each span and collect tone pointers that miss a start.

    Tone pointers are matched with a binary search over the sorted span
    starts. ``encode_samples`` is false for a size report. (AI-assisted), ISC.
    """
    if len(chunk) % 16:
        raise BankFormatError('sample chunk is not whole SPU blocks')
    spans = _split_spans(chunk)
    failures = []
    good = []
    for offset in tone_offsets:
        if offset % 16 != 0:
            failures.append('tone SampleOffset %d is not a multiple of 16' % offset)
        elif offset >= len(chunk):
            failures.append(
                'tone SampleOffset %d is past the %d-byte sample chunk' % (offset, len(chunk)))
        else:
            good.append(offset)
    start_list = [start for start, _end, _ended in spans]
    end_list = [end for _start, end, _ended in spans]
    starts = set(start_list)
    inside = []
    hits = 0
    referenced = set()
    for offset in good:
        if offset in starts:
            hits += 1
            referenced.add(offset)
            continue
        index = bisect.bisect_right(start_list, offset) - 1
        if index >= 0 and start_list[index] < offset < end_list[index]:
            inside.append(offset)
            referenced.add(start_list[index])
    notes = []
    if facts['header_vag_count'] != len(spans):
        notes.append(
            'NumVAGs %d != %d LoopEnd spans (loader.cpp %s discards NumVAGs)'
            % (facts['header_vag_count'], len(spans), facts['vag_cite']))
    if facts['vag_data_size'] != len(chunk):
        notes.append(
            'VagDataSize %d != sample chunk %d (loader.cpp %s discards VagDataSize; '
            'ps2_bytes is the sample chunk, loader.cpp 423)'
            % (facts['vag_data_size'], len(chunk), facts['size_cite']))
    if facts['midi_note']:
        notes.append(facts['midi_note'])
    if inside:
        shown = ', '.join(str(offset) for offset in inside[:8])
        extra = ''
        if len(inside) > 8:
            extra = ' and %d more' % (len(inside) - 8)
        notes.append(
            'tone SampleOffset %s falls inside a LoopEnd span%s. Counted once. UNVERIFIED overlap'
            % (shown, extra))
    if not hits and not inside and spans:
        notes.append(
            'no tone SampleOffset lands on a span start. UNVERIFIED packing; '
            'the end of a span is LoopEnd (voice.cpp 56-65)')
    converted = []
    aica_bytes = 0
    longest = None
    for index, (start, end, ended) in enumerate(spans):
        blob = chunk[start:end]
        if not ended:
            notes.append(
                'sample %d has no LoopEnd flag (voice.cpp 56-65). '
                'UNVERIFIED extent through the following bytes' % index)
        try:
            pcm, aica, aica_len, loop_start, loop_end, loop_repeat = _measure_span(
                blob, encode_samples)
        except (ValueError, TypeError) as exc:
            failures.append(
                'sample %d at ps2 offset %d (%d bytes): %s' % (index, start, len(blob), exc))
            continue
        image = SampleImage(
            index, start, len(blob), pcm, aica,
            loop_start, loop_end, loop_repeat,
            ended, start in referenced)
        converted.append(image)
        aica_bytes += aica_len
        if longest is None or pcm > longest[1]:
            longest = (index, pcm, len(blob), aica_len, start)
    if any(item.startswith('sample ') for item in failures):
        notes.append(
            'aica_bytes counts converted spans only. A failed sample makes that total a lower bound')
    return {
        'samples': len(spans),
        'converted': len(converted),
        'ps2_bytes': len(chunk),
        'aica_bytes': aica_bytes,
        'hits': hits,
        'inside': len(inside),
        'longest': longest,
        'failures': tuple(failures),
        'notes': tuple(notes),
        'converted_samples': tuple(converted),
    }


def _allow_prefix(prefix, rule):
    """Accept a slot prefix, or a sector hit from the scanner (AI-assisted), ISC."""
    if rule.startswith('sector-scan'):
        if prefix % _SECTOR or prefix < _SECTOR:
            raise BankFormatError('name prefix must be one or ten sectors')
        return
    if prefix not in (_SLOTN_PREFIX, _SLOT0_PREFIX):
        raise BankFormatError('name prefix must be one or ten sectors')


def convert_sbk(data, prefix_bytes, source='bank.sbk', prefix_rule='explicit', encode_samples=True):
    """Convert one SFX file whose 989snd image starts at ``prefix_bytes``.

    The name count is checked before the chunk directory. A count that does
    not fit the slot prefix raises ``name count`` even when the image is
    missing. ``encode_samples`` is false when the caller only wants sizes.
    (AI-assisted), ISC.
    """
    data = _require_bytes(data)
    _allow_prefix(prefix_bytes, prefix_rule)
    table_name, names = parse_name_table(data, prefix_bytes)
    if len(data) < prefix_bytes + 8:
        raise BankFormatError('truncated name prefix or chunk directory')
    _kind, spans = read_chunks(data[prefix_bytes:])
    if len(spans) != 2:
        raise BankFormatError('SFX bank needs two chunks')
    facts = _parse_sfx_program(spans[0])
    body = _convert_chunk(spans[1], facts['tone_offsets'], facts, encode_samples)
    return _report('sfx', source, prefix_bytes, prefix_rule, data, table_name, names, facts, body)


def convert_music(data, offset, source='bank.mus', prefix_rule=_BYTE0_RULE, encode_samples=True):
    """Convert one music file whose ``SBv2`` image starts at ``offset``.

    ``encode_samples`` is false when the caller only wants sizes.
    (AI-assisted), ISC.
    """
    data = _require_bytes(data)
    if offset < 0 or offset > len(data):
        raise BankFormatError('truncated chunk directory')
    _kind, spans = read_chunks(data[offset:])
    if len(spans) < 1 or len(spans[0]) < 4 or spans[0][:4] != b'SBv2':
        raise BankFormatError('missing or truncated SBv2')
    # loader.cpp 426-428.
    if len(spans) != 3:
        raise BankFormatError('SBv2 without midi data not supported')
    facts = _music_facts(spans[0])
    facts['midi_note'] = _midi_note(spans[2])
    body = _convert_chunk(spans[1], facts['tone_offsets'], facts, encode_samples)
    return _report('music', source, offset, prefix_rule, data, b'', (), facts, body)


def _report(kind, source, prefix, rule, data, table_name, names, facts, body):
    """Assemble one bank report from a parsed image (AI-assisted), ISC."""
    return BankReport(
        source, kind, prefix, rule, facts['version'], len(data),
        body['samples'], body['converted'], body['ps2_bytes'], body['aica_bytes'],
        facts['vag_data_size'], facts['sram_alloc_size'], facts['header_vag_count'],
        facts['vags_in_sr'], len(facts['tone_offsets']), body['hits'], body['inside'],
        body['longest'], body['failures'], body['notes'], tuple(names), table_name,
        body['converted_samples'])


def locate_sfx(data, file_name):
    """Pick the SFX image offset for this file name.

    ``common`` and ``commonj`` are the first ``sound-bank-load`` (``gsound.gc``
    122-124), so they are slot 0. The slot offset is kept when it peeks as
    ``SBlk`` with two chunks (``loader.cpp`` 420-423 and 436). A directory
    there whose type is not 1 or 3 is ``bad file type`` (``loader.cpp``
    415-418). A different fourcc is not that image, so the file is scanned
    by 2048-byte steps (``streamed_audio.cpp`` 274-298) and the hit is
    marked UNVERIFIED.
    (AI-assisted), ISC.
    """
    stem = Path(file_name).stem.lower()
    preferred = _SLOT0_PREFIX if stem in ('common', 'commonj') else _SLOTN_PREFIX
    rule = _SLOT0_RULE if preferred == _SLOT0_PREFIX else _SLOTN_RULE
    _refuse_bad_type(data, preferred)
    if _peek_fourcc(data, preferred, b'SBlk', 2):
        return preferred, rule
    found = _scan_fourcc(data, b'SBlk', 2)
    if found is None:
        raise BankFormatError('no SBlk directory on a sector boundary')
    return found, (
        'sector-scan offset %d (streamed_audio.cpp 274-298). UNVERIFIED retail prefix' % found)


def locate_music(data):
    """Pick the music image offset.

    ``fake_iso.cpp`` 213 loads ``.mus`` from byte 0. That offset is kept when
    it peeks as ``SBv2`` with three chunks (``loader.cpp`` 425-428). A
    directory there whose type is not 1 or 3 is ``bad file type``
    (``loader.cpp`` 415-418). A later sector hit is the decompiler scan and
    is marked UNVERIFIED.
    (AI-assisted), ISC.
    """
    _refuse_bad_type(data, 0)
    if _peek_fourcc(data, 0, b'SBv2', 3):
        return 0, _BYTE0_RULE
    found = _scan_fourcc(data, b'SBv2', 3)
    if found is None:
        raise BankFormatError('no SBv2 directory on a sector boundary')
    return found, (
        'sector-scan offset %d (streamed_audio.cpp 274-298). '
        'UNVERIFIED: fake_iso.cpp 213 loads .mus at byte 0' % found)


def load_sfx_bytes(data, file_name, source=None, encode_samples=True):
    """Locate and convert one SFX byte string.

    ``encode_samples`` is false for a size report. (AI-assisted), ISC.
    """
    data = _require_bytes(data)
    prefix, rule = locate_sfx(data, file_name)
    return convert_sbk(data, prefix, source or file_name, rule, encode_samples)


def load_music_bytes(data, file_name, source=None, encode_samples=True):
    """Locate and convert one music byte string.

    ``TWEAKVAL.MUS`` is skipped by ``streamed_audio.cpp`` 345-347. The caller
    gets ``None`` for that stem. ``encode_samples`` is false for a size
    report. (AI-assisted), ISC.
    """
    if Path(file_name).stem.lower() == 'tweakval':
        return None
    data = _require_bytes(data)
    offset, rule = locate_music(data)
    return convert_music(data, offset, source or file_name, rule, encode_samples)


def parse_vag_directory(data):
    """Read a Jak 1 ``VAGDIR.AYB``.

    ``iso.h`` 24-31 and ``streamed_audio.cpp`` 107-132. The ``u32`` after each
    8-byte name is a sector index. ``fake_iso.cpp`` 139 multiplies the open
    location by ``SECTOR_SIZE`` (``overlord_common.h`` 3). (AI-assisted), ISC.
    """
    data = _require_bytes(data)
    if len(data) < 4:
        raise BankFormatError('VAG directory is shorter than its count')
    count = _u32(data, 0)
    if count > _VAG_DIR_LIMIT:
        raise BankFormatError('VAG directory count exceeds 868 entries')
    need = 4 + count * _VAG_ENTRY
    if need > len(data):
        raise BankFormatError('VAG directory is shorter than its count')
    entries = []
    for index in range(count):
        at = 4 + index * _VAG_ENTRY
        entries.append((data[at:at + 8], _u32(data, at + 8)))
    return tuple(entries)


def convert_dialogue(wad, entries, source):
    """Convert ``VAGp`` or ``pGAV`` clips in one wad.

    ``iso.cpp`` 1200-1203 stops a stream whose first word is neither magic.
    Those bytes are not decoded as a raw bank. Clip payload bytes are the
    size field, which ``iso.cpp`` 1215 treats as the bytes after the 48-byte
    header. The AICA length is the block-count size from ``_span_size``.
    Each clip is measured in place. The bytes after that clip stay unread
    by this call. Jak 1 directory entries have no stereo word
    (``streamed_audio.cpp`` 111-132). Stereo interleave is UNVERIFIED and
    is not applied. Dialogue is not written, so these clips are not encoded.
    (AI-assisted), ISC.
    """
    wad = _require_bytes(wad)
    failures = []
    converted = []
    ps2_bytes = 0
    aica_bytes = 0
    for index, (name, sector) in enumerate(entries):
        start = sector * _SECTOR
        label = _bytes_label(name).strip() or index
        if start >= len(wad) or start + 48 > len(wad):
            failures.append('clip %s sector %d is past the wad' % (label, sector))
            continue
        magic = wad[start:start + 4]
        if magic not in (b'VAGp', b'pGAV'):
            failures.append(
                'clip %s does not start with VAGp or pGAV (iso.cpp 1200-1203)' % label)
            continue
        try:
            payload_at, size = _vag_payload_at(wad, start)
            pcm, aica_len = _span_size(wad, payload_at, size)
        except (ValueError, TypeError) as exc:
            failures.append('clip %s sector %d: %s' % (label, sector, exc))
            continue
        converted.append((index, label, size, pcm, aica_len))
        ps2_bytes += size
        aica_bytes += aica_len
    longest = None
    for item in converted:
        if longest is None or item[3] > longest[1]:
            longest = (item[1], item[3], item[2], item[4])
    return DialogueReport(
        source, len(entries), len(converted), ps2_bytes, aica_bytes, longest, tuple(failures))


def match_files(paths, logical_name):
    """Find a bank file for a level name.

    ``fake_iso.cpp`` 245-246 stores a NUL at byte 8 before appending ``.sbk``
    or ``.mus`` (207-208). The retail 8.3 spelling of a longer name is
    UNVERIFIED. An exact stem wins over that 8-character cut.
    (AI-assisted), ISC.
    """
    key = logical_name.lower()
    short = key[:8]
    exact = []
    cut = []
    for path in paths:
        stem = Path(path).stem.lower()
        if stem == key:
            exact.append(path)
        elif short != key and stem == short:
            cut.append(path)
    if len(exact) == 1:
        return exact[0], 'exact'
    if len(exact) > 1:
        return None, 'ambiguous'
    if len(cut) == 1:
        return cut[0], '8-char'
    if len(cut) > 1:
        return None, 'ambiguous'
    return None, 'missing'


def resident_total(common_aica, sfx_aica, music_aica):
    """Add one common bank, one level SFX bank, and one music bank.

    A missing piece is ``None``. A level that names no bank passes 0 for that
    piece. ``level.gc`` 1022-1080 keeps common plus at most two level banks;
    one call of this function is one level's own pair plus common.
    (AI-assisted), ISC.
    """
    if common_aica is None or sfx_aica is None or music_aica is None:
        return None
    return common_aica + sfx_aica + music_aica


def fits_sound_ram(aica_sum):
    """Compare bank AICA bytes plus the stream reserve with sound RAM (AI-assisted), ISC."""
    if aica_sum is None:
        return 'unknown'
    if aica_sum + DRIVER_RESERVE_BYTES <= SOUND_RAM_BYTES:
        return 'yes'
    return 'no'


def output_directory(path):
    """Resolve an output directory under ``ports/dreamcast/build`` (AI-assisted), ISC."""
    root = (Path(__file__).resolve().parent / 'build').resolve()
    chosen = Path(path).expanduser().resolve()
    if chosen != root and root not in chosen.parents:
        raise BankFormatError('output must stay under ports/dreamcast/build')
    if chosen.is_file():
        raise BankFormatError('output must stay under ports/dreamcast/build')
    return chosen


def pack_aica(report):
    """Pack converted spans into this tool's ``DCSA`` container.

    The container is not a ``.sbk``. A player for it is UNVERIFIED. Failed
    spans are omitted. ``sram_alloc_size`` is ``0xffffffff`` when the source
    bank has no such field. (AI-assisted), ISC.
    """
    sram = 0xFFFFFFFF if report.sram_alloc_size is None else report.sram_alloc_size
    out = bytearray()
    out += b'DCSA'
    out += struct.pack(
        '<6I', report.samples, report.converted, report.ps2_bytes, report.aica_bytes,
        sram, len(report.failures))
    for image in report.converted_samples:
        loop_start = 0xFFFFFFFF if image.loop_start is None else image.loop_start
        loop_end = 0xFFFFFFFF if image.loop_end is None else image.loop_end
        out += struct.pack(
            '<9I', image.index, image.ps2_offset, image.ps2_bytes, image.pcm_count,
            loop_start, loop_end, 1 if image.loop_repeat else 0,
            1 if image.ended_on_loop_end else 0, len(image.aica))
        out += image.aica
    return bytes(out)


def _bytes_label(raw):
    """Show name-table bytes without treating an interior NUL as the end (AI-assisted), ISC."""
    text = []
    for value in raw:
        text.append(chr(value) if 32 <= value < 127 else '.')
    return ''.join(text)


def _fmt_optional(value):
    """Render a missing header word (AI-assisted), ISC."""
    if value is None:
        return 'none'
    return str(value)


def _fmt_longest(longest, labeled=False):
    """Render the longest converted span (AI-assisted), ISC."""
    if longest is None:
        return [
            'longest_index: none',
            'longest_pcm: none',
            'longest_ps2_bytes: none',
            'longest_aica_bytes: none',
            'longest_ps2_offset: none',
        ]
    if labeled:
        return [
            'longest_name: %s' % longest[0],
            'longest_pcm: %d' % longest[1],
            'longest_ps2_bytes: %d' % longest[2],
            'longest_aica_bytes: %d' % longest[3],
        ]
    return [
        'longest_index: %d' % longest[0],
        'longest_pcm: %d' % longest[1],
        'longest_ps2_bytes: %d' % longest[2],
        'longest_aica_bytes: %d' % longest[3],
        'longest_ps2_offset: %d' % longest[4],
    ]


def format_bank(report):
    """Format one converted bank (AI-assisted), ISC."""
    lines = [
        'bank: %s' % report.source,
        'kind: %s' % report.kind,
        'prefix: %d' % report.prefix,
        'prefix_rule: %s' % report.prefix_rule,
        'version: %d' % report.version,
        'file_bytes: %d' % report.file_bytes,
        'samples: %d' % report.samples,
        'converted: %d' % report.converted,
        'ps2_bytes: %d' % report.ps2_bytes,
        'aica_bytes: %d' % report.aica_bytes,
        'vag_data_size: %d' % report.vag_data_size,
        'sram_alloc_size: %s' % _fmt_optional(report.sram_alloc_size),
        'header_vag_count: %d' % report.header_vag_count,
        'vags_in_sr: %d' % report.vags_in_sr,
        'tone_offsets: %d' % report.tone_offsets,
        'tones_at_span_starts: %d' % report.tones_at_span_starts,
        'tones_inside: %d' % report.tones_inside,
        'name_table_sounds: %d' % len(report.names),
        'table_name: %s' % _bytes_label(report.table_name),
        'failures: %d' % len(report.failures),
    ]
    lines.extend(_fmt_longest(report.longest))
    for failure in report.failures:
        lines.append('failure: %s' % failure)
    for note in report.notes:
        lines.append('note: %s' % note)
    return lines


def format_dialogue(report):
    """Format one dialogue wad (AI-assisted), ISC."""
    lines = [
        'dialogue: %s' % report.source,
        'clips: %d' % report.clips,
        'converted: %d' % report.converted,
        'ps2_bytes: %d' % report.ps2_bytes,
        'aica_bytes: %d' % report.aica_bytes,
        'failures: %d' % len(report.failures),
        'dialogue_in_level_total: no',
    ]
    lines.extend(_fmt_longest(report.longest, labeled=True))
    for failure in report.failures:
        lines.append('failure: %s' % failure)
    return lines


# goal_src/jak1/engine/level/level-info.gc. The order is *level-load-list*
# at 2444-2469. test-zone at 2472 is a custom level and is omitted.
# Each row is level, sfx bank, music bank, sound-banks line, music-bank line.
_LEVELS = (
    ('training', 'training', 'village1', 19, 20),
    ('village1', 'village1', 'village1', 139, 140),
    ('beach', 'beach', 'beach', 283, 284),
    ('jungle', 'jungle', 'jungle', 336, 337),
    ('jungleb', 'jungleb', 'jungleb', 389, 390),
    ('misty', 'misty', 'misty', 440, 441),
    ('firecanyon', 'firecanyon', 'firecanyon', 624, 625),
    ('village2', 'village2', 'village2', 695, 696),
    ('sunken', 'sunken', 'sunken', 786, 787),
    ('sunkenb', 'sunken', 'sunken', 896, 897),
    ('swamp', 'swamp', 'swamp', 967, 968),
    ('rolling', 'rolling', 'rolling', 1168, 1169),
    ('ogre', 'ogre', 'ogre', 1220, 1221),
    ('village3', 'village3', 'village3', 1310, 1311),
    ('snow', 'snow', 'snow', 1401, 1402),
    ('maincave', 'maincave', 'maincave', 1606, 1607),
    ('darkcave', 'darkcave', 'maincave', 1696, 1697),
    ('robocave', 'robocave', 'maincave', 1748, 1749),
    ('lavatube', 'lavatube', 'lavatube', 1819, 1820),
    ('citadel', 'citadel', 'citadel', 1928, 1929),
    ('finalboss', 'finalboss', 'finalboss', 2152, 2153),
    ('intro', None, None, 2224, 2226),
    ('demo', 'village1', 'village1', 2258, 2259),
    ('title', None, 'village1', 2310, 2312),
    ('halfpipe', None, None, 2364, 2366),
    ('default-level', None, None, 2418, 2420),
)


def level_names():
    """Return the Jak 1 level rows this budget scores (AI-assisted), ISC."""
    return _LEVELS


def _piece(match):
    """Return AICA and PS2 bytes for a matched bank, or None when it is absent (AI-assisted), ISC."""
    if match is None or match[0] is None:
        return None
    report = match[0]
    return report.aica_bytes, report.ps2_bytes


def format_levels(sfx_matches, music_matches, common_match, commonj_match):
    """Format one resident total per level and one derived two-bank total (AI-assisted), ISC."""
    lines = []
    chosen = common_match
    chosen_name = 'common'
    if chosen is None or chosen[0] is None:
        if commonj_match is not None and commonj_match[0] is not None:
            chosen = commonj_match
            chosen_name = 'commonj'
            lines.append(
                'common_note: common.sbk is absent. gsound.gc 123 loads commonj for SCEI')
        else:
            lines.append('common_note: common and commonj are absent (gsound.gc 122-124)')
    elif commonj_match is not None and commonj_match[0] is not None:
        lines.append(
            'common_note: level totals use common (gsound.gc 124). '
            'commonj is the SCEI bank (gsound.gc 123)')
        lines.append('commonj_aica: %d' % commonj_match[0].aica_bytes)
        lines.append('commonj_ps2: %d' % commonj_match[0].ps2_bytes)
    common_parts = _piece(chosen)
    required_missing = []
    measured_sfx = {}
    measured_music = {}
    for level, sfx_name, music_name, sfx_line, music_line in _LEVELS:
        lines.append('level: %s' % level)
        lines.append('sfx_line: level-info.gc %d' % sfx_line)
        lines.append('music_line: level-info.gc %d' % music_line)
        sfx_parts = _level_piece(lines, 'sfx', sfx_name, sfx_matches, required_missing, measured_sfx)
        music_parts = _level_piece(
            lines, 'music', music_name, music_matches, required_missing, measured_music)
        lines.append('common: %s' % (chosen_name if common_parts else 'missing'))
        if common_parts is None:
            lines.append('common_aica: missing')
            lines.append('common_ps2: missing')
            if 'common' not in required_missing:
                required_missing.append('common')
        else:
            lines.append('common_file: %s' % chosen[1])
            lines.append('common_aica: %d' % common_parts[0])
            lines.append('common_ps2: %d' % common_parts[1])
        sfx_aica = None if sfx_parts is None else sfx_parts[0]
        music_aica = None if music_parts is None else music_parts[0]
        if sfx_name is None:
            sfx_aica = 0
        if music_name is None:
            music_aica = 0
        total = resident_total(
            None if common_parts is None else common_parts[0], sfx_aica, music_aica)
        _append_fit(lines, total)
    if required_missing:
        lines.append('worst_fits: unknown')
        lines.append('worst_missing: %s' % ' '.join(required_missing))
        return lines
    sfx_reports = list(measured_sfx.values())
    sfx_reports.sort(key=lambda item: item.aica_bytes, reverse=True)
    top = sfx_reports[:2]
    music_reports = list(measured_music.values())
    music_pick = None
    if music_reports:
        music_reports.sort(key=lambda item: item.aica_bytes, reverse=True)
        music_pick = music_reports[0]
    music_aica = 0 if music_pick is None else music_pick.aica_bytes
    worst = common_parts[0] + sum(item.aica_bytes for item in top) + music_aica
    names = '+'.join(Path(item.source).stem for item in top) if top else 'none'
    lines.append('worst_sfx: %s' % names)
    lines.append('worst_sfx_aica: %d' % sum(item.aica_bytes for item in top))
    lines.append('worst_music: %s' % ('none' if music_pick is None else Path(music_pick.source).stem))
    lines.append('worst_music_aica: %d' % music_aica)
    lines.append('worst_aica_sum: %d' % worst)
    lines.append('worst_with_reserve: %d' % (worst + DRIVER_RESERVE_BYTES))
    lines.append('worst_fits: %s' % fits_sound_ram(worst))
    lines.append(
        'worst_note: common plus the largest measured level SFX banks (at most two) '
        'plus the largest measured music bank. level.gc 1036 allows two level banks. '
        'UNVERIFIED as a pair the game loads together')
    return lines


def _level_piece(lines, label, logical, matches, missing, measured):
    """Print one level's SFX or music piece and return its byte counts (AI-assisted), ISC."""
    if logical is None:
        lines.append('%s: none' % label)
        lines.append('%s_aica: 0' % label)
        lines.append('%s_ps2: 0' % label)
        return (0, 0)
    lines.append('%s: %s' % (label, logical))
    match = matches.get(logical)
    if match is None or match[0] is None:
        lines.append('%s_file: missing' % label)
        lines.append('%s_match: %s' % (label, 'missing' if match is None else match[2]))
        lines.append('%s_aica: missing' % label)
        lines.append('%s_ps2: missing' % label)
        if logical not in missing:
            missing.append(logical)
        return None
    report, source, how = match
    measured[logical] = report
    lines.append('%s_file: %s' % (label, source))
    lines.append('%s_match: %s' % (label, how))
    lines.append('%s_aica: %d' % (label, report.aica_bytes))
    lines.append('%s_ps2: %d' % (label, report.ps2_bytes))
    return report.aica_bytes, report.ps2_bytes


def _append_fit(lines, total):
    """Print the sound-RAM comparison for one sum (AI-assisted), ISC."""
    if total is None:
        lines.append('aica_sum: unknown')
        lines.append('with_reserve: unknown')
        lines.append('fits: unknown')
        lines.append('headroom: unknown')
        return
    lines.append('aica_sum: %d' % total)
    lines.append('with_reserve: %d' % (total + DRIVER_RESERVE_BYTES))
    lines.append('fits: %s' % fits_sound_ram(total))
    lines.append('headroom: %d' % (BANK_CAP_BYTES - total))


def _folder(root, name):
    """Return a direct child directory with this name, ignoring case (AI-assisted), ISC."""
    if not root.is_dir():
        return None
    for child in root.iterdir():
        if child.is_dir() and child.name.lower() == name.lower():
            return child
    return None


def resolve_root(path):
    """Find the extracted Jak 1 folder under an ``iso_data`` path.

    ``docs/project-overview.md`` 48 puts the disc in ``iso_data/jak1``. A path
    that already contains ``SBK``, ``MUS``, or ``VAG`` is used as given.
    (AI-assisted), ISC.
    """
    path = Path(path).expanduser()
    if _folder(path, 'SBK') or _folder(path, 'MUS') or _folder(path, 'VAG'):
        return path
    child = _folder(path, 'jak1')
    if child and (_folder(child, 'SBK') or _folder(child, 'MUS') or _folder(child, 'VAG')):
        return child
    return path


def _read_only(path):
    """Read one disc file without opening it for write (AI-assisted), ISC."""
    with path.open('rb') as handle:
        return handle.read()


def _rel(root, path):
    """Show a path relative to the disc root (AI-assisted), ISC."""
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.name


def _files(folder, suffix):
    """List files with one suffix, in a stable order (AI-assisted), ISC."""
    if folder is None:
        return []
    found = [child for child in folder.iterdir() if child.is_file() and child.suffix.lower() == suffix]
    found.sort(key=lambda item: item.name.lower())
    return found


def _index_matches(paths, reports_by_source, logical_names):
    """Match level names to loaded banks (AI-assisted), ISC."""
    matches = {}
    for logical in logical_names:
        path, how = match_files(paths, logical)
        if path is None:
            matches[logical] = (None, '', how)
            continue
        source = reports_by_source[path]
        if isinstance(source, Refusal):
            matches[logical] = (None, source.source, 'refused')
        else:
            matches[logical] = (source, source.source, how)
    return matches


def run(iso_data, output=None, limit=None, progress=False):
    """Read a disc tree, measure its banks, and return ``(exit_code, text)``.

    Exit 1 means a file was refused or the disc folders are absent. A sample
    whose filter is rejected stays in the report and still exits 0. Sizes are
    the block-count AICA length. ``convert_vag`` runs only when ``output`` is
    set, and only for SBK and MUS files. ``limit`` stops after that many of
    those files, SBK first. ``progress`` writes each path to stderr. The disc
    is read with ``rb`` only. ``output`` is created after that read.
    (AI-assisted), ISC.
    """
    if limit is not None and (not isinstance(limit, int) or isinstance(limit, bool) or limit < 0):
        raise BankFormatError('limit must be zero or a positive integer')
    if output is not None:
        output = output_directory(output)
    encode_samples = output is not None
    root = resolve_root(iso_data)
    lines = [
        'sound_ram: %d' % SOUND_RAM_BYTES,
        'driver_reserve: %d' % DRIVER_RESERVE_BYTES,
        'bank_cap: %d' % BANK_CAP_BYTES,
        'reserve_note: ssound.cpp 67 snd_SRAMMalloc(0xc030). '
        'Bank AICA bytes must fit in sound_ram minus this reserve',
        'split_note: LoopEnd ends a voice (voice.cpp 56-65). '
        'Spans are packed back to back from offset 0. UNVERIFIED padding',
        'vags_in_sr_note: discarded by loader.cpp 224 for SBlk and 354-355 for SBv2. '
        'It is not the sample count',
        'ps2_bytes_note: the sample chunk copied by loader.cpp 423. '
        'The name prefix is file_bytes minus that image, and it stays out of sound RAM',
        'size_note: aica_bytes is 14 bytes per 16-byte SPU block '
        '(28 samples, two per AICA byte). convert_vag runs only when --output writes DCSA',
    ]
    if limit is not None:
        lines.append('limit: %d' % limit)
        lines.append('limit_note: SBK is read first, then MUS, and this many files is the cap')
    if not root.is_dir():
        lines.append('REFUSED: iso_data path is not a directory')
        return 1, '\n'.join(lines) + '\n'
    lines.append('iso_root: %s' % root.name)
    sfx_dir = _folder(root, 'SBK')
    music_dir = _folder(root, 'MUS')
    vag_dir = _folder(root, 'VAG')
    lines.append('folder: SBK %s' % ('present' if sfx_dir else 'missing'))
    lines.append('folder: MUS %s' % ('present' if music_dir else 'missing'))
    lines.append('folder: VAG %s' % ('present' if vag_dir else 'missing'))
    if sfx_dir is None and music_dir is None:
        lines.append('REFUSED: no SBK or MUS directory under the iso_data path')
        return 1, '\n'.join(lines) + '\n'

    refused = False
    banks = []
    skipped = []
    by_path = {}
    loaded = 0
    for folder, suffix, loader in (
            (sfx_dir, '.sbk', load_sfx_bytes),
            (music_dir, '.mus', load_music_bytes)):
        if limit is not None and loaded >= limit:
            break
        for path in _files(folder, suffix):
            source = _rel(root, path)
            if suffix == '.mus' and path.stem.lower() == 'tweakval':
                skipped.append(source)
                continue
            if limit is not None and loaded >= limit:
                break
            if progress:
                sys.stderr.write('reading %s\n' % source)
                sys.stderr.flush()
            try:
                report = loader(_read_only(path), path.name, source, encode_samples)
            except BankFormatError as exc:
                banks.append(Refusal(source, str(exc)))
                by_path[path] = Refusal(source, str(exc))
                refused = True
                loaded += 1
                continue
            except OSError as exc:
                banks.append(Refusal(source, 'unreadable: %s' % exc))
                by_path[path] = banks[-1]
                refused = True
                loaded += 1
                continue
            banks.append(report)
            by_path[path] = report
            loaded += 1

    for item in banks:
        if isinstance(item, Refusal):
            lines.append('bank: %s' % item.source)
            lines.append('REFUSED: %s' % item.reason)
        else:
            lines.extend(format_bank(item))
        lines.append('')
    for source in skipped:
        lines.append('skipped: %s' % source)
        lines.append('skip_reason: streamed_audio.cpp 345-347')
        lines.append('')

    dialogue_refused = _append_dialogue(lines, root, vag_dir)
    refused = refused or dialogue_refused

    sfx_names = []
    music_names = []
    for _level, sfx_name, music_name, _sfx_line, _music_line in _LEVELS:
        if sfx_name and sfx_name not in sfx_names:
            sfx_names.append(sfx_name)
        if music_name and music_name not in music_names:
            music_names.append(music_name)
    sfx_paths = _files(sfx_dir, '.sbk')
    music_paths = [path for path in _files(music_dir, '.mus') if path.stem.lower() != 'tweakval']
    sfx_matches = _index_matches(sfx_paths, by_path, sfx_names)
    music_matches = _index_matches(music_paths, by_path, music_names)
    common_match = _index_matches(sfx_paths, by_path, ('common',)).get('common')
    commonj_match = _index_matches(sfx_paths, by_path, ('commonj',)).get('commonj')
    lines.extend(format_levels(sfx_matches, music_matches, common_match, commonj_match))
    text = '\n'.join(lines) + '\n'
    if output is not None:
        _write_outputs(output, text, banks)
    return (1 if refused else 0), text


def _append_dialogue(lines, root, vag_dir):
    """Read ``VAGDIR.AYB`` and any language wads that are present (AI-assisted), ISC."""
    if vag_dir is None:
        lines.append('dialogue_note: VAG directory is absent. Stream clips were not measured')
        lines.append('')
        return False
    directory_path = None
    for child in vag_dir.iterdir():
        if child.is_file() and child.name.lower() == 'vagdir.ayb':
            directory_path = child
            break
    if directory_path is None:
        lines.append('dialogue_note: VAG/VAGDIR.AYB is absent (streamed_audio.cpp 463)')
        lines.append('')
        return False
    source = _rel(root, directory_path)
    try:
        entries = parse_vag_directory(_read_only(directory_path))
    except BankFormatError as exc:
        lines.append('bank: %s' % source)
        lines.append('REFUSED: %s' % exc)
        lines.append('')
        return True
    except OSError as exc:
        lines.append('bank: %s' % source)
        lines.append('REFUSED: unreadable: %s' % exc)
        lines.append('')
        return True
    lines.append('vag_directory: %s' % source)
    lines.append('vag_entries: %d' % len(entries))
    lines.append('')
    refused = False
    for language in _LANGUAGES:
        wad = None
        for child in vag_dir.iterdir():
            if child.is_file() and child.name.lower() == 'vagwad.' + language.lower():
                wad = child
                break
        if wad is None:
            lines.append('dialogue: VAGWAD.%s missing' % language)
            lines.append('')
            continue
        wad_source = _rel(root, wad)
        try:
            report = convert_dialogue(_read_only(wad), entries, wad_source)
        except OSError as exc:
            lines.append('bank: %s' % wad_source)
            lines.append('REFUSED: unreadable: %s' % exc)
            lines.append('')
            refused = True
            continue
        lines.extend(format_dialogue(report))
        lines.append('')
    return refused


def _safe_stem(text):
    """Keep a file stem inside the output directory (AI-assisted), ISC."""
    cleaned = []
    for char in text:
        if char.isalnum() or char in '._-':
            cleaned.append(char)
        else:
            cleaned.append('_')
    stem = ''.join(cleaned).strip('._')
    if not stem or stem in ('.', '..'):
        raise BankFormatError('bank file name is empty after sanitizing')
    return stem


def _write_outputs(directory, text, banks):
    """Write the text report and one ``DCSA`` file per converted bank (AI-assisted), ISC."""
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'SOUND_BANK_REPORT.txt').write_bytes(text.encode('utf-8'))
    for bank in banks:
        if not isinstance(bank, BankReport):
            continue
        stem = _safe_stem(bank.kind + '-' + Path(bank.source).stem)
        (directory / (stem + '.aica')).write_bytes(pack_aica(bank))


def main(argv=None):
    """Parse arguments and print the bank report (AI-assisted), ISC."""
    parser = argparse.ArgumentParser(
        description='Measure Jak 1 SBK, MUS, and VAGWAD samples against sound RAM.')
    parser.add_argument(
        'iso_data', type=Path,
        help='Extracted disc folder, iso_data or iso_data/jak1. Read only.')
    parser.add_argument(
        '--output', type=Path, default=None,
        help='Write DCSA banks under ports/dreamcast/build/. Omit to print sizes only.')
    parser.add_argument(
        '--limit', type=int, default=None,
        help='Read at most this many SBK and MUS files, SBK first. Omit to read all.')
    parser.add_argument(
        '--progress', action='store_true',
        help='Write each bank path to stderr as it is read.')
    args = parser.parse_args(argv)
    try:
        code, text = run(args.iso_data, args.output, args.limit, args.progress)
    except BankFormatError as exc:
        sys.stdout.write('REFUSED: %s\n' % exc)
        return 1
    sys.stdout.write(text)
    return code


if __name__ == '__main__':
    sys.exit(main())
