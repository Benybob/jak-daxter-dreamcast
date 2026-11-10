"""Silent Jak 1 .sbk directory reader (SOUND_PLAN.md milestone 2).

The PC loader keeps a fixed name-table prefix, then a 989snd image on the
next sector. Slot 0 is preset to 1022 names and the image starts at
10 * 2048. Every other slot is preset to 101 names and the image starts at
2048. The file's own count replaces that preset after the prefix is chosen,
so the caller passes the prefix explicitly.

Inside the prefix: 16-byte bank name, one skipped u32, a u32 count at
offset 0x14, then count records of 16 name bytes and a u32 falloff.
1022 records end at byte 20464. 101 records end at byte 2044.

The image is a chunk directory (u32 type, u32 count, then offset/size
pairs). An SFX bank has type 1 or 3 and two chunks. Chunk 0 is fourcc
SBlk. Chunk 1 is SPU ADPCM blocks. Offsets are relative to the image,
not the start of the file. VagDataSize and SRAMAllocSize are the two
words the 989 reader skips; they are returned and not applied.

Names are 16 raw bytes. Bytes after an embedded NUL stay part of the key.
This module does not walk grains, convert samples, or open a disc.
(AI-assisted), ISC.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct


_PREFIX_BYTES = (2048, 20480)
_NAME_HEADER_BYTES = 24
_NAME_RECORD_BYTES = 20
_NAME_BYTES = 16
_DIRECTORY_ENTRY_BYTES = 8
_SBLK_V1_BYTES = 60
_SBLK_V2_BYTES = 64
_SPU_BLOCK_BYTES = 16


@dataclass(frozen=True)
class SoundName:
    """One name-table row (AI-assisted), ISC."""

    name: bytes
    falloff: int

    @property
    def falloff_min(self):
        """Bits 0–13 of the falloff word (AI-assisted), ISC."""
        return self.falloff & 0x3FFF

    @property
    def falloff_max(self):
        """Bits 14–27 of the falloff word (AI-assisted), ISC."""
        return (self.falloff >> 14) & 0x3FFF

    @property
    def falloff_curve(self):
        """Bits 28–31 of the falloff word (AI-assisted), ISC."""
        return self.falloff >> 28


@dataclass(frozen=True)
class FileSpan:
    """One chunk, addressed from the start of the .sbk (AI-assisted), ISC."""

    offset: int
    data: bytes


@dataclass(frozen=True)
class SfxBlock:
    """SBlk header fields. The grain bytes stay in the program span (AI-assisted), ISC."""

    version: int
    flags: int
    bank_id: int
    sound_count: int
    grain_count: int
    vag_count: int
    vag_data_size: int
    sram_alloc_size: int


@dataclass(frozen=True)
class SoundBank:
    """A parsed SFX bank. ``samples`` begins at or after the name prefix (AI-assisted), ISC."""

    name: bytes
    sounds: tuple
    prefix_bytes: int
    image_type: int
    sblk: SfxBlock
    program: FileSpan
    samples: FileSpan

    def find_sound(self, name):
        """Find a row by all 16 bytes.

        A shorter key is rejected. It is not padded with NULs, so a key that
        differs after an embedded NUL does not match. (AI-assisted), ISC.
        """
        if not isinstance(name, (bytes, bytearray)) or len(name) != _NAME_BYTES:
            raise ValueError('sound lookup requires exactly 16 bytes')
        key = bytes(name)
        for sound in self.sounds:
            if sound.name == key:
                return sound
        return None


def _u32(data, offset):
    """Read one little-endian u32 (AI-assisted), ISC."""
    return struct.unpack_from('<I', data, offset)[0]


def parse_sbk(data, prefix_bytes=2048):
    """Parse one synthetic SFX .sbk.

    ``prefix_bytes`` is 20480 for the common slot and 2048 for every other
    slot. The count stored in the file does not select the prefix. Music
    (``SBv2``, three chunks) is rejected. (AI-assisted), ISC.
    """
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError('bank data must be bytes')
    if prefix_bytes not in _PREFIX_BYTES:
        raise ValueError('name prefix must be one or ten sectors')
    data = bytes(data)
    if len(data) < prefix_bytes + _NAME_HEADER_BYTES:
        raise ValueError('truncated name prefix or chunk directory')

    count = _u32(data, 0x14)
    room = (prefix_bytes - _NAME_HEADER_BYTES) // _NAME_RECORD_BYTES
    if count > room:
        raise ValueError('name count crosses fixed sector prefix')

    sounds = []
    for index in range(count):
        base = _NAME_HEADER_BYTES + index * _NAME_RECORD_BYTES
        sounds.append(SoundName(data[base:base + _NAME_BYTES], _u32(data, base + _NAME_BYTES)))

    image = data[prefix_bytes:]
    kind = _u32(image, 0)
    chunks = _u32(image, 4)
    if kind not in (1, 3) or not 2 <= chunks <= 16:
        raise ValueError('expected a type 1/3 SFX directory of at least two chunks')

    directory_bytes = 8 + chunks * _DIRECTORY_ENTRY_BYTES
    spans = []
    ranges = []
    for index in range(chunks):
        entry = 8 + index * _DIRECTORY_ENTRY_BYTES
        offset = _u32(image, entry)
        size = _u32(image, entry + 4)
        end = offset + size
        if offset < directory_bytes or offset > len(image) or size > len(image) - offset:
            raise ValueError('chunk overlaps directory or exceeds image')
        for start, previous_end in ranges:
            if offset < previous_end and start < end:
                raise ValueError('overlapping chunks')
        ranges.append((offset, end))
        spans.append(FileSpan(prefix_bytes + offset, image[offset:end]))

    program, samples = spans[0], spans[1]
    block = program.data
    if len(block) < _SBLK_V1_BYTES or block[:4] != b'SBlk':
        raise ValueError('missing or truncated SBlk')
    version = _u32(block, 4)
    flags = _u32(block, 8)
    bank_id = _u32(block, 12)
    header_bytes = _SBLK_V2_BYTES if version >= 2 else _SBLK_V1_BYTES
    if version not in (1, 2) or len(block) < header_bytes:
        raise ValueError('unsupported or truncated SBlk version')
    sound_count, grain_count, vag_count = struct.unpack_from('<hhh', block, 22)
    if min(sound_count, grain_count, vag_count) < 0:
        raise ValueError('negative SBlk count')
    vag_data_size = _u32(block, 40)
    sram_alloc_size = _u32(block, 44)
    if len(samples.data) % _SPU_BLOCK_BYTES:
        raise ValueError('sample chunk is not whole SPU blocks')
    if samples.offset < prefix_bytes:
        raise ValueError('sample span includes the name prefix')

    return SoundBank(
        data[:_NAME_BYTES],
        tuple(sounds),
        prefix_bytes,
        kind,
        SfxBlock(version, flags, bank_id, sound_count, grain_count, vag_count,
                 vag_data_size, sram_alloc_size),
        program,
        samples,
    )
