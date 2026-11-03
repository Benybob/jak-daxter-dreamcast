"""PS2 SPU ADPCM (VAG) to Dreamcast AICA 4-bit ADPCM.

The SPU decoder follows the integer steps in ``game/sound/common/voice.cpp``:
a 4-bit nibble is placed in the top of a signed 16-bit word, shifted by the
block's shift, then two prediction terms are added with separate arithmetic
shifts. History starts at zero, which is what ``Voice::KeyOn`` stores.

AICA ADPCM is a different codec. Each nibble updates a step size and a
running sample. Encoding tries every nibble and keeps the one whose decoder
output is closest to the source sample, so decoding the bytes reproduces the
encoder's samples exactly. The source PCM and that reconstruction differ by
the quantization error in ``ErrorReport``.

Nothing in this module reads a disc or plays audio. (AI-assisted), ISC.
"""

from __future__ import annotations

from dataclasses import dataclass


# voice.cpp adpcm_coefs. Index is the 3-bit filter field.
_SPU_COEFS = (
    (0, 0),
    (60, 0),
    (115, -52),
    (98, -55),
    (122, -60),
)

# AICA/YMZ step as described by KOS utils/wav2adpcm ymz_step.
# Low nibble first, without adpcm2pcm's optional 254/256 history filter.
_AICA_DIFF = (1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15)
_AICA_SCALE = (
    0x0E6, 0x0E6, 0x0E6, 0x0E6, 0x133, 0x199, 0x200, 0x266,
    0x0E6, 0x0E6, 0x0E6, 0x0E6, 0x133, 0x199, 0x200, 0x266,
)
_AICA_QUANT_MIN = 0x7F
_AICA_QUANT_MAX = 0x6000
_VAG_HEADER_BYTES = 48
_VAG_BLOCK_BYTES = 16
_VAG_SAMPLES_PER_BLOCK = 28
_VAGP_MAGIC = 0x70474156  # bytes 'V','A','G','p'
_PGAV_MAGIC = 0x56414770  # bytes 'p','G','A','V'


@dataclass(frozen=True)
class VagPcm:
    """PCM a Voice would push for this block stream (AI-assisted), ISC."""

    samples: tuple
    loop_start: int | None
    loop_end: int | None
    loop_repeat: bool
    sample_rate: int | None
    block_count: int
    payload_bytes: int
    final_history: tuple


@dataclass(frozen=True)
class ErrorReport:
    """Source PCM against the AICA decoder output (AI-assisted), ISC."""

    sample_count: int
    max_abs_error: int
    mean_abs_error: float
    rms: float
    sum_abs_error: int
    sum_sq_error: int


@dataclass(frozen=True)
class Conversion:
    """One VAG payload converted to AICA nibbles (AI-assisted), ISC."""

    vag: VagPcm
    aica: bytes
    aica_samples: tuple
    report: ErrorReport


class AicaState:
    """Running AICA sample and step (AI-assisted), ISC."""

    def __init__(self, sample=0, quant=_AICA_QUANT_MIN):
        self.sample = sample
        self.quant = quant

    def copy(self):
        """Return a detached copy of this state (AI-assisted), ISC."""
        return AicaState(self.sample, self.quant)


def _div_trunc(numerator, denominator):
    """C99 integer division, toward zero (AI-assisted), ISC."""
    if numerator < 0:
        return -((-numerator) // denominator)
    return numerator // denominator


def _clamp_s16(sample):
    """Saturate to signed 16-bit (AI-assisted), ISC."""
    if sample > 32767:
        return 32767
    if sample < -32768:
        return -32768
    return sample


def _u32_at(data, offset, big):
    """Read one endian-selected 32-bit word (AI-assisted), ISC."""
    return int.from_bytes(data[offset:offset + 4], 'big' if big else 'little')


def _split_vag_container(data):
    """Return payload bytes and an optional sample rate.

    A leading ``VAGp`` or ``pGAV`` header is 48 bytes. ``ProcessVAGData`` reads
    the size at word 3 and the rate at word 4, and byte-swaps both when the
    magic is big-endian ``VAGp``. Raw SPU blocks have no header. (AI-assisted), ISC.
    """
    if len(data) >= _VAG_HEADER_BYTES:
        magic = int.from_bytes(data[0:4], 'little')
        if magic in (_VAGP_MAGIC, _PGAV_MAGIC):
            big = magic == _VAGP_MAGIC
            size = _u32_at(data, 12, big)
            rate = _u32_at(data, 16, big)
            if size < 0 or _VAG_HEADER_BYTES + size > len(data):
                raise ValueError('VAG size field does not fit the buffer')
            if size % _VAG_BLOCK_BYTES != 0:
                raise ValueError('VAG size field is not a whole number of blocks')
            return data[_VAG_HEADER_BYTES:_VAG_HEADER_BYTES + size], rate
    if len(data) % _VAG_BLOCK_BYTES != 0:
        raise ValueError('VAG payload is not a whole number of 16-byte blocks')
    return data, None


def _decode_vag_block(block, hist1, hist2):
    """Decode one 16-byte SPU block (AI-assisted), ISC."""
    if len(block) != _VAG_BLOCK_BYTES:
        raise ValueError('VAG block is not 16 bytes')
    shift = block[0] & 0xF
    filt = (block[0] >> 4) & 0x7  # voice.h stores filter as 3 bits at position 4
    if filt >= len(_SPU_COEFS):
        raise ValueError('VAG filter {} is outside the five playback coefficients'.format(filt))
    coef0, coef1 = _SPU_COEFS[filt]
    flags = block[1]
    samples = []
    for index in range(_VAG_SAMPLES_PER_BLOCK):
        packed = block[2 + index // 2]
        nibble = packed & 0xF if index % 2 == 0 else (packed >> 4) & 0xF
        sample = (nibble << 12) - (0x10000 if nibble & 0x8 else 0)
        sample >>= shift
        # Separate shifts match voice.cpp. A combined (a + b + 32) / 64 does not.
        sample += (coef0 * hist1) >> 6
        sample += (coef1 * hist2) >> 6
        sample = _clamp_s16(sample)
        hist2 = hist1
        hist1 = sample
        samples.append(sample)
    return samples, hist1, hist2, flags


def decode_vag(data, initial_history=(0, 0)):
    """Decode raw SPU blocks, or one VAGp/pGAV clip, to PCM.

    Every stored block is decoded, including a block whose end flag is set.
    ``loop_end`` is the last sample of the first block with that flag. ``loop_start``
    is the first sample of the latest loop-start flag at or before that end, which
    is the loop address ``Voice::UpdateBlockHeader`` would keep. Later bytes are
    still decoded so a tool can see the whole payload. This is the entry pass;
    no loop is unrolled or invented when a start address is absent. Supply the
    returned final_history when decoding a jump without a key-on reset.
    (AI-assisted), ISC.
    """
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError('VAG data must be bytes')
    payload, rate = _split_vag_container(bytes(data))
    if (len(initial_history) != 2 or
            any(not isinstance(x, int) or not -32768 <= x <= 32767 for x in initial_history)):
        raise ValueError("SPU history must contain two signed 16-bit integers")
    hist1, hist2 = initial_history
    samples = []
    loop_start = None
    loop_end = None
    loop_repeat = False
    pending_start = None
    blocks = len(payload) // _VAG_BLOCK_BYTES
    for block_index in range(blocks):
        start = block_index * _VAG_BLOCK_BYTES
        decoded, hist1, hist2, flags = _decode_vag_block(payload[start:start + _VAG_BLOCK_BYTES], hist1, hist2)
        block_start = len(samples)
        samples.extend(decoded)
        if flags & 0x04:
            pending_start = block_start
        if (flags & 0x01) and loop_end is None:
            loop_end = len(samples) - 1
            loop_start = pending_start
            loop_repeat = bool(flags & 0x02)
    if loop_end is None:
        loop_start = pending_start
    return VagPcm(tuple(samples), loop_start, loop_end, loop_repeat, rate, blocks, len(payload), (hist1, hist2))


def aica_step(state, nibble):
    """Decode one AICA nibble into ``state`` and return the sample.

    The unsigned difference magnitude is capped at 32767 before its sign is
    applied and the sample is saturated. Negative division is toward zero. The step is then
    ``(quant * scale) >> 8``, clamped to 127..24576. Nibble 0 is the smallest
    positive step. There is no zero step, so exact digital silence is not
    representable from the reset state. (AI-assisted), ISC.
    """
    if not 0 <= nibble <= 15:
        raise ValueError('AICA nibble is out of range')
    magnitude = min(32767, abs(_AICA_DIFF[nibble]) * state.quant // 8)
    delta = -magnitude if nibble & 8 else magnitude
    state.sample = _clamp_s16(state.sample + delta)
    quant = (state.quant * _AICA_SCALE[nibble]) >> 8
    if quant < _AICA_QUANT_MIN:
        quant = _AICA_QUANT_MIN
    elif quant > _AICA_QUANT_MAX:
        quant = _AICA_QUANT_MAX
    state.quant = quant
    return state.sample


def decode_aica(data, sample_count=None, state=None):
    """Decode AICA bytes, low nibble first (AI-assisted), ISC."""
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError('AICA data must be bytes')
    if sample_count is not None and (not isinstance(sample_count, int) or sample_count < 0):
        raise ValueError('sample_count must be a non-negative int')
    if state is None:
        state = AicaState()
    if sample_count == 0:
        return ()
    samples = []
    for byte in data:
        for nibble in (byte & 0xF, (byte >> 4) & 0xF):
            samples.append(aica_step(state, nibble))
            if sample_count is not None and len(samples) == sample_count:
                return tuple(samples)
    if sample_count is not None and len(samples) != sample_count:
        raise ValueError('AICA data ended before sample_count')
    return tuple(samples)


def encode_aica(samples, state=None):
    """Encode PCM to AICA nibbles, low nibble first.

    Each sample tries all 16 nibbles. The winner is the smallest absolute error,
    and the lowest nibble index breaks a tie. An odd length pads the high nibble
    with 0; pass the original length to ``decode_aica`` to ignore that pad.
    Samples must already be signed 16-bit integers. (AI-assisted), ISC.
    """
    if state is None:
        state = AicaState()
    else:
        state = state.copy()
    out = bytearray()
    low = None
    for target in samples:
        if not isinstance(target, int) or target < -32768 or target > 32767:
            raise ValueError('AICA encoder samples must be signed 16-bit ints')
        best_nibble = None
        best_error = None
        best_state = None
        for nibble in range(16):
            trial = state.copy()
            produced = aica_step(trial, nibble)
            error = produced - target
            if error < 0:
                error = -error
            if best_error is None or error < best_error:
                best_nibble = nibble
                best_error = error
                best_state = trial
        state = best_state
        if low is None:
            low = best_nibble
        else:
            out.append(low | (best_nibble << 4))
            low = None
    if low is not None:
        out.append(low)
    return bytes(out)


def error_report(reference, actual):
    """Compare two equal-length PCM sequences (AI-assisted), ISC."""
    if len(reference) != len(actual):
        raise ValueError('error report inputs differ in length')
    count = len(reference)
    if count == 0:
        return ErrorReport(0, 0, 0.0, 0.0, 0, 0)
    sum_abs = 0
    sum_sq = 0
    max_abs = 0
    for left, right in zip(reference, actual):
        error = left - right
        if error < 0:
            error = -error
        if error > max_abs:
            max_abs = error
        sum_abs += error
        sum_sq += error * error
    return ErrorReport(
        count,
        max_abs,
        sum_abs / count,
        (sum_sq / count) ** 0.5,
        sum_abs,
        sum_sq,
    )


def convert_vag(data, state=None):
    """Convert the stored entry pass; loop metadata is not a sustained loop.

    SPU jump history and AICA loop state differ; callers must choose a later
    playback policy before using these bytes for repeating playback. (AI-assisted), ISC.
    """
    vag = decode_vag(data)
    encoded = encode_aica(vag.samples, state)
    reconstructed = decode_aica(encoded, len(vag.samples), state.copy() if state else None)
    return Conversion(vag, encoded, reconstructed, error_report(vag.samples, reconstructed))
