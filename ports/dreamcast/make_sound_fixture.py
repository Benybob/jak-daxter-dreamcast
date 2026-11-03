"""Write two synthetic AICA ADPCM clips for the sound probe (AI-assisted), ISC.

The square clip is ``encode_aica`` of a signed square. The vag clip is
``convert_vag`` of a VAG square: filter-0, shift-2 blocks of all +6144 or all
-6144, alternating every block: a half-period of 28 samples at 48 kHz, 25.7 at
the 44.1 kHz capture, that capture_audio.py can measure.
Both images are an even number of samples, so the encoder adds no nibble pad,
and both lengths are already a multiple of 32 bytes. Nothing here is played
and no disc file is opened. (AI-assisted), ISC.
"""

from __future__ import annotations

import pathlib
import sys

import vag_adpcm


SOUND_RAM_BYTES = 2097152
SAMPLE_BASE = 0x030000
SAMPLE_RAM_BYTES = SOUND_RAM_BYTES - SAMPLE_BASE
ALIGN_BYTES = 32
MAX_SAMPLES = 65534
MAX_RATE = 96000
RATE = 48000
SQUARE_SAMPLES = 8192
SQUARE_AMPLITUDE = 6000
SQUARE_HALF = 32
VAG_BLOCKS = 160
ERR_ARG = 1
ERR_SIZE = 2
ERR_ALIGN = 3
ERR_SAMPLES = 4
ERR_RANGE = 5
ERR_STATE = 10


def square_pcm():
    """8192 samples, half-period 32, amplitude 6000 (AI-assisted), ISC."""
    pcm = []
    for index in range(SQUARE_SAMPLES):
        pcm.append(SQUARE_AMPLITUDE if (index // SQUARE_HALF) % 2 == 0 else -SQUARE_AMPLITUDE)
    return pcm


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


VAG_SHIFT = 2
VAG_LEVEL = 6 << (12 - VAG_SHIFT)  # 6144: nibble 6 shifted into a filter-0 sample


def vag_payload():
    """160 filter-0 blocks alternating all +6 and all -6 nibbles (AI-assisted), ISC."""
    up, down = vag_block(VAG_SHIFT, 0, 0, [6] * 28), vag_block(VAG_SHIFT, 0, 0, [-6] * 28)
    return (up + down) * (VAG_BLOCKS // 2)


def align_adpcm(data):
    """Zero-pad an ADPCM image up to a multiple of 32 bytes (AI-assisted), ISC."""
    if not data:
        raise ValueError('ADPCM image is empty')
    extra = (-len(data)) % ALIGN_BYTES
    return data + bytes(extra)


def byte_sum(data):
    """Wrapping uint32 sum, matching the probe's adder (AI-assisted), ISC."""
    total = 0
    for value in data:
        total = (total + value) & 0xFFFFFFFF
    return total


def classify_load(data, byte_count, sample_count, rate):
    """Return the negative-free error code ``dc_aica_sfx_load`` must use.

    0 means the sizes are acceptable. This does not model ``snd_mem_available``
    or an allocator failure. Order matches ``dc_aica_sfx_load``.
    (AI-assisted), ISC.
    """
    if data is None:
        return ERR_ARG
    if byte_count == 0 or byte_count > SOUND_RAM_BYTES or byte_count > SAMPLE_RAM_BYTES:
        return ERR_SIZE
    if byte_count % ALIGN_BYTES:
        return ERR_ALIGN
    if (sample_count == 0 or sample_count > MAX_SAMPLES or
            sample_count > byte_count * 2):
        return ERR_SAMPLES
    if rate == 0 or rate > MAX_RATE:
        return ERR_RANGE
    return 0


def classify_play(volume, pan, looping, loaded):
    """Return the error code ``dc_aica_sfx_play`` must use (AI-assisted), ISC."""
    if volume < 0 or volume > 255 or pan < 0 or pan > 255 or looping not in (0, 1):
        return ERR_RANGE
    if not loaded:
        return ERR_STATE
    return 0


def build_square():
    """Encode the square and refuse a pad or a zero image (AI-assisted), ISC."""
    pcm = square_pcm()
    if pcm[0] != SQUARE_AMPLITUDE or pcm[SQUARE_HALF] != -SQUARE_AMPLITUDE:
        raise RuntimeError('square fixture lost its amplitude')
    if len(pcm) != SQUARE_SAMPLES:
        raise RuntimeError('square fixture length')
    adpcm = align_adpcm(vag_adpcm.encode_aica(pcm))
    if len(adpcm) != SQUARE_SAMPLES // 2 or byte_sum(adpcm) == 0:
        raise RuntimeError('square ADPCM length or sum')
    if vag_adpcm.decode_aica(adpcm, SQUARE_SAMPLES) != vag_adpcm.decode_aica(
            vag_adpcm.encode_aica(pcm), SQUARE_SAMPLES):
        raise RuntimeError('square ADPCM did not round-trip')
    return adpcm


def build_vag():
    """Convert the VAG square payload (AI-assisted), ISC."""
    converted = vag_adpcm.convert_vag(vag_payload())
    samples = converted.vag.samples
    if samples[0] != VAG_LEVEL or samples[27] != VAG_LEVEL or samples[28] != -VAG_LEVEL:
        raise RuntimeError('vag fixture is not the expected square')
    if len(samples) != VAG_BLOCKS * 28 or len(converted.aica) * 2 != len(samples):
        raise RuntimeError('vag fixture length')
    adpcm = align_adpcm(converted.aica)
    if len(adpcm) != len(converted.aica) or byte_sum(adpcm) == 0:
        raise RuntimeError('vag ADPCM needed padding or summed to zero')
    return adpcm


def _c_array(name, data):
    lines = [f'static const unsigned char {name}[{len(data)}] = {{']
    for start in range(0, len(data), 12):
        chunk = ', '.join(f'0x{value:02x}' for value in data[start:start + 12])
        lines.append(f'  {chunk},')
    lines.append('};')
    return '\n'.join(lines)


def render_header():
    """Return the C header the probe compiles (AI-assisted), ISC."""
    square = build_square()
    vag = build_vag()
    if classify_load(square, len(square), SQUARE_SAMPLES, RATE) != 0:
        raise RuntimeError('square clip is outside the loader bounds')
    if classify_load(vag, len(vag), VAG_BLOCKS * 28, RATE) != 0:
        raise RuntimeError('vag clip is outside the loader bounds')
    if square == vag:
        raise RuntimeError('the two clips are identical')
    text = [
        '/* Generated AICA ADPCM clips. Do not edit (AI-assisted), ISC. */',
        '#ifndef DC_SOUND_FIXTURE_H',
        '#define DC_SOUND_FIXTURE_H',
        f'#define DC_SOUND_PROBE_RATE {RATE}u',
        f'#define DC_SOUND_SQUARE_SAMPLES {SQUARE_SAMPLES}u',
        f'#define DC_SOUND_SQUARE_BYTES {len(square)}u',
        f'#define DC_SOUND_SQUARE_RATE {RATE}u',
        f'#define DC_SOUND_SQUARE_SUM {byte_sum(square)}u',
        f'#define DC_SOUND_VAG_SAMPLES {VAG_BLOCKS * 28}u',
        f'#define DC_SOUND_VAG_BYTES {len(vag)}u',
        f'#define DC_SOUND_VAG_RATE {RATE}u',
        f'#define DC_SOUND_VAG_SUM {byte_sum(vag)}u',
        _c_array('dc_sound_square', square),
        _c_array('dc_sound_vag', vag),
        '#endif',
        '',
    ]
    return '\n'.join(text)


def main(argv):
    """Write the header to argv[1], or build/sound_fixture.h (AI-assisted), ISC."""
    target = pathlib.Path(argv[1] if len(argv) > 1 else 'build/sound_fixture.h')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(render_header(), encoding='utf-8', newline='\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
