"""Silent Jak 1 SFX bank directory reader. (AI-assisted), ISC.

Layout sources: game/overlord/jak1/fake_iso.cpp parseSoundBank/FS_LoadSoundBank,
game/sound/989snd/loader.cpp FileAttributes::Read/SFXBlock::ReadBlock.
Offsets in the chunk directory are relative to the image AFTER the fixed name
prefix. Names are opaque 16-byte keys, including bytes after an embedded NUL.
This module does not interpret grains, relocate tones, convert or play audio.
"""
from dataclasses import dataclass
import struct


@dataclass(frozen=True)
class SoundName:
    name: bytes
    falloff: int

    @property
    def falloff_min(self):
        return self.falloff & 0x3fff

    @property
    def falloff_max(self):
        return (self.falloff >> 14) & 0x3fff

    @property
    def falloff_curve(self):
        return self.falloff >> 28


@dataclass(frozen=True)
class FileSpan:
    """Absolute offset in the original .sbk and its bounded contents."""
    offset: int
    data: bytes


@dataclass(frozen=True)
class SfxBlock:
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
    name: bytes
    sounds: tuple[SoundName, ...]
    prefix_bytes: int
    image_type: int
    sblk: SfxBlock
    program: FileSpan
    samples: FileSpan

    def find_sound(self, name):
        """Compare all sixteen bytes, with no text conversion or padding."""
        if not isinstance(name, bytes) or len(name) != 16:
            raise ValueError('sound lookup requires exactly 16 bytes')
        return next((sound for sound in self.sounds if sound.name == name), None)


def parse_sbk(data, *, prefix_bytes=2048):
    """Parse a bounded SFX image; slot 0 explicitly selects prefix_bytes=20480.

    Only the SBlk/two-chunk SFX format is supported. SBv2 music is a later
    milestone. Program bytes remain opaque; their count fields are metadata,
    not a claim that grain/voice playback has been validated.
    """
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError('bank data must be bytes')
    if prefix_bytes not in (2048, 20480):
        raise ValueError('name prefix must be one or ten sectors')
    data = bytes(data)
    if len(data) < prefix_bytes + 24:
        raise ValueError('truncated name prefix or chunk directory')
    count, = struct.unpack_from('<I', data, 20)
    if count > (prefix_bytes - 24) // 20:
        raise ValueError('name count crosses fixed sector prefix')
    names = tuple(SoundName(data[24 + i*20:40 + i*20],
                            struct.unpack_from('<I', data, 40 + i*20)[0])
                  for i in range(count))
    image = data[prefix_bytes:]
    kind, chunks = struct.unpack_from('<II', image)
    if kind not in (1, 3) or chunks != 2:
        raise ValueError('expected type 1/3 two-chunk SFX directory')
    spans = []
    ranges = []
    for index in range(chunks):
        offset, size = struct.unpack_from('<II', image, 8 + index*8)
        if offset < 24 or offset > len(image) or size > len(image) - offset:
            raise ValueError('chunk overlaps directory or exceeds image')
        for start, end in ranges:
            if offset < end and start < offset + size:
                raise ValueError('overlapping chunks')
        ranges.append((offset, offset + size))
        spans.append(FileSpan(prefix_bytes + offset, image[offset:offset + size]))
    program, samples = spans
    block = program.data
    if len(block) < 60 or block[:4] != b'SBlk':
        raise ValueError('missing or truncated SBlk')
    version, flags, bank_id = struct.unpack_from('<III', block, 4)
    if version not in (1, 2) or len(block) < (64 if version == 2 else 60):
        raise ValueError('unsupported or truncated SBlk version')
    sounds, grains, vags = struct.unpack_from('<hhh', block, 22)
    if min(sounds, grains, vags) < 0:
        raise ValueError('negative SBlk count')
    vag_size, sram_size = struct.unpack_from('<II', block, 40)
    if len(samples.data) % 16:
        raise ValueError('sample chunk is not whole SPU blocks')
    return SoundBank(data[:16], names, prefix_bytes, kind,
                     SfxBlock(version, flags, bank_id, sounds, grains, vags,
                              vag_size, sram_size), program, samples)
