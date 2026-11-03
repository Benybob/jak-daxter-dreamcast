(AI-assisted)

# Sound review

Read-only review of `vag_adpcm.py` and `tests/test_vag_adpcm.py` against `SOUND_PLAN.md`, `game/sound/common/voice.cpp`, `game/sound/common/voice.h`, `game/overlord/jak1/iso.cpp`, and the AICA step in `kos-source/utils/wav2adpcm/wav2adpcm.c`. No other file was edited. `python ports/dreamcast/tests/test_vag_adpcm.py` was not re-run. The numbers below are the integer results of the cited formulas.

## Findings

### 1. High — the AICA difference is applied before it is capped at 32767

`vag_adpcm.py` 211–212. `aica_step` adds `trunc(diff * quant / 8)` to the running sample, then clamps the sum to signed 16-bit.

`ymz_step` does the opposite order. The magnitude is `((1 + (delta << 1)) * step) >> 3`, that magnitude is clamped to 0..32767, and only then is the sign applied (`wav2adpcm.c` 72–82). The same function clamps the step to 127..24576 (`wav2adpcm.c` 84). The comment on lines 76–77 says this cap is in the official AICA encoder. Both `pcm2adpcm` and `adpcm2pcm` call `ymz_step`, so the bytes that tool writes and the bytes it reads back both use the cap.

Concrete case: sample −20000, quant `0x6000` (24576), nibble 7.

- Magnitude is `15 * 24576 >> 3` = 46080.
- `ymz_step` clamps that to 32767, then adds it: −20000 + 32767 = 12767.
- `aica_step` adds 46080 first: −20000 + 46080 = 26080. The sample clamp never fires, because 26080 is inside signed 16-bit.

The encoder searches this step (`vag_adpcm.py` 261–270). Once the quantizer is above 17475, nibbles 5, 6, and 7 overshoot on the chip relative to the nibble the search keeps. Six nibble-7 updates from step 127 land on quant 24110; the next nibble 7 is already in this region.

`test_quant_floor_and_ceiling` (`tests/test_vag_adpcm.py` 177–180) does not reach it. Nibble 4 at quant `0x6000` has magnitude `9 * 24576 >> 3` = 27648, which is under 32767. From sample 20000 both formulas saturate to 32767. The assertion stays true with the cap missing.

### 2. Medium — loop repeat is taken from any block, and the stored loop is the entry pass

`vag_adpcm.py` 189–195.

`Voice::DecodeSamples` reads `LoopRepeat` only after the block has finished, and only on the header whose `LoopEnd` bit is set (`voice.cpp` 56–65). `LoopStart` updates `mLSA` when that header is read (`voice.cpp` 74–77). `KeyOn` clears the two history samples (`voice.cpp` 93–94). The jump at line 57 does not.

`decode_vag` sets `loop_repeat` when any block has flag bit 1 (`0x02`). Bit positions themselves match `voice.h` 79–81 and `bitfield.h` 10: flag byte bit 0 is end, bit 1 is repeat, bit 2 is start. `VAG_MarkLoopEnd` writes the value 3 at the flag byte of the last block (`iso.cpp` 1467–1468), which is end+repeat on that block.

Concrete case that `Voice` stops and `decode_vag` marks as a loop: block 0 flags `0x02`, block 1 flags `0x01`. No end bit is set on the block that carries repeat, and the end block's repeat bit is clear, so `voice.cpp` 60–64 stops the ADSR. `decode_vag` still returns `loop_repeat` true, `loop_end` 55, `loop_start` None.

The tested pattern is the other one. `test_loop_flags_mark_samples_without_dropping_them` (`tests/test_vag_adpcm.py` 116–123) uses `0x04` then `0x03`. The end block has the repeat bit, so both sides loop. That assertion does not fail on the `0x02`/`0x01` pair.

A second concrete case, one block, filter 1, shift 0, flags `0x07`, nibble 1 and then 27 zeros. `KeyOn` history is (0, 0), so sample 0 is 4096 and the block ends at 712. The jump keeps that history. The next sample 0 is `4096 + (60 * 712 >> 6)` = 4763. `convert_vag` encodes the entry pass, whose sample 0 is 4096 (`vag_adpcm.py` 310–315). `test_one_block_loop_uses_that_block` (`tests/test_vag_adpcm.py` 125–129) uses filter 0 and zero nibbles, so the second pass is the same waveform and the assertion cannot see 4763.

`SOUND_PLAN.md` 116 says an AICA loop is a different predictor and is not promised click-free. The 4763 gap is the SPU history across the jump, before any AICA state is involved.

A related hole: flags `0x03` with no start bit set `loop_repeat` and leave `loop_start` None. `mLSA` is still 0 (`voice.h` 103). `UpdateBlockHeader` does not write it unless the start bit is set. None is the honest "no start flag" answer for a clip whose SPU address is not 0. A player that substitutes sample 0 has invented an address `Voice` did not store.

### 3. High — the AICA round trip cannot fail, and the second decoder is a copy

`tests/test_vag_adpcm.py` 182–196 and 236–241. `independent_aica` (lines 48–73) is the same difference table, the same scale table, the same toward-zero divide, and the same clamps as `aica_step`. `independent_spu` (lines 24–45) is the same copy of `voice.cpp` 35–49. `test_second_block_continues_history` (lines 106–114) and `test_pitched_block_report_matches_the_samples` (line 246) compare the library to that copy.

`encode_aica` keeps the nibble whose `aica_step` output is closest (`vag_adpcm.py` 261–270). `decode_aica` runs `aica_step` on those nibbles. The reconstructed PCM is the encoder's chosen outputs, so encoding it again finds a nibble of error 0 and decoding that returns the same PCM. Lines 195–196, and the requantize checks at lines 236–241, stay true for any step function that encode and decode share.

Failing case that still passes those assertions: clamp AICA samples to 0..32767 inside `aica_step`, and make the same edit in `independent_aica`. Every current literal is positive (15, 62, 238, 276, 32767). The PCM buffer at lines 190–194 does contain −32768, but the assertions compare the two copies to each other. A negative rail of −32783, from sample −32768 and nibble 8 at quant 127, is never read. `test_quant_floor_and_ceiling` only steps nibble 0 and nibble 4.

`SOUND_PLAN.md` 177–178 makes this copy, and the re-encode, the milestone 1 pass condition. A shared wrong table satisfies it. The plan's own line 243 says milestone 6 checks the code against itself.

The literals that do stand on their own are the SPU series 4096, 7360, 9897, 11803 and the AICA bytes `0x10` and `0x07`. They do not cover finding 1, finding 2, a negative AICA nibble, or the pad byte.

### 4. Low — the pad check never reads the pad

`tests/test_vag_adpcm.py` 199–203. `encode_aica` of an odd length appends the leftover nibble in the low half and leaves the high half 0 (`vag_adpcm.py` 277–278).

From the reset state the closest nibble to 1000 is 7 (produced sample 238). The padded byte is `0x07`. `decode_aica` of that byte with no count is the pair the other test already locks, (238, 276), because nibble 0 is the pad.

The test checks the length and that a one-sample decode equals the first sample of the two-sample decode. Failing case that still passes: store the pad as `0xF` so the byte is `0xF7`. Length is still 1, the counted decode is still the first sample, and the uncounted decode is still two samples.

### 5. Medium — silence is only required to be at least 15 counts loud

`tests/test_vag_adpcm.py` 226–233, which is the check `SOUND_PLAN.md` 177 describes.

A zero VAG block, reset AICA state, quant 127. Nibble 0 and nibble 8 are both 15 counts from zero (`127 >> 3` = 15). The tie takes the lowest index, nibble 0, sample 15. The next sample uses nibble 8 and lands on 0. The 28 reconstructed samples are `(15, 0)` fourteen times, and `max_abs_error` is 15.

The assertion is `max_abs_error >= 15`, then that this maximum equals `max(abs(vag - aica))`. A constant 15, or a constant 100, satisfies both. So does finding 1's step, because quant stays 127 on nibbles 0 and 8 and the 32767 cap never moves. The re-encode underneath (lines 236–241) is finding 3.

## What is clean

SPU coefficients. `_SPU_COEFS` (`vag_adpcm.py` 23–29) is `adpcm_coefs` (`voice.cpp` 11–17): (0, 0), (60, 0), (115, −52), (98, −55), (122, −60). Each product is shifted by 6 on its own, then the sum is clamped (`voice.cpp` 39–43, `vag_adpcm.py` 154–157). The combined `(f1*h1 + f2*h2 + 32) / 64` form in `common/audio/audio_formats.cpp` 129 is a different result. Filter 2, shift 0, nibble 1 then zeros, is 4096, 7360, 9897, 11803 this way. The test literal at `tests/test_vag_adpcm.py` 95 matches `voice.cpp`. The arithmetic shift of −52 is the case at line 100, and the sample clamp to 32767 is the case at line 104.

SPU nibble order. `DecodeSamples` takes the low nibble of the `u16` and then shifts the word down by 4 (`voice.cpp` 33–49). Seven data words follow the header word, 28 nibbles. Walking bytes low nibble then high nibble (`vag_adpcm.py` 150–151) is that little-endian layout. Flag `0x03` and shift/filter in byte 0 match the `u16` bitfield (`voice.h` 77–84).

VAG container. Magic `0x70474156` is the little-endian load of the bytes `VAGp`, and `0x56414770` is `pGAV` (`iso.cpp` 1200–1211, `vag_adpcm.py` 43–44 and 123–127). Size is word 3 and rate is word 4. `VAGp` byte-swaps both. `iso.cpp` 1215 adds 48 to the size, so the field is the payload after the 48-byte header. `decode_vag` slices that many bytes. One SPU block is 16 bytes and 28 samples. The same 28 samples are 14 AICA bytes. Filters 5–7 raise, which is the plan's choice where `voice.cpp` 38 indexes off the end of a 5-entry table.

AICA tables and the unclamped positive steps. Diff `1,3,5,...,15` and the negated copies, scale `0x0E6, 0x0E6, 0x0E6, 0x0E6, 0x133, 0x199, 0x200, 0x266` repeated, step clamp 127..24576 (`vag_adpcm.py` 33–39) match `wav2adpcm.c` 67–68 and 84 (`230,230,230,230,307,409,512,614`). Reset state is sample 0, step 127. Byte `0x10`, low nibble first, is 15 then 62 with the step left at 127. Byte `0x07` is 238 then 276 with the step at 273. Those two literals match `ymz_step` while the magnitude stays under 32767. Toward-zero division of a negative product matches `ymz_step`, which builds a positive magnitude and then subtracts. Byte `0x08` is (−15, 0) either way. A signed `>> 3` of −127 is −16, and no current assertion reads that byte.

## What I could not confirm

The AICA die. `SOUND_PLAN.md` 99 and 243 say the step table was not measured here. `ymz_step` is the codec in the tree's `wav2adpcm`. That is the reference for finding 1. It is not a capture off the chip.

Nibble order on that chip. `wav2adpcm.c` 8–9 says the only difference between YMZ280B and AICA ADPCM is that the nibbles are swapped. `adpcm2pcm` still takes the low nibble first: `nibble` starts at 4, and `(int8)byte << 4 >> 4` is the low nibble (`wav2adpcm.c` 93–101). `pcm2adpcm` writes the first sample with `<< 4` (`wav2adpcm.c` 122–123), which is the high nibble, and it writes that byte before a low nibble exists, so the encoder drops the even sample. Byte `0x10` is (15, 62) low-nibble-first and (47, 62) high-nibble-first, same `ymz_step`, step still 127. `decode_aica` and the test at `tests/test_vag_adpcm.py` 157–162 implement the low-nibble walk. They match `adpcm2pcm`. They do not match `pcm2adpcm`, and the test cannot fail for the high-nibble walk.

The `254/256` filter. `adpcm2pcm` does `history = history * 254 / 256` before every step (`wav2adpcm.c` 102). `ymz_step` does not, and `pcm2adpcm` calls `ymz_step` directly (`wav2adpcm.c` 127). With the filter, byte `0x10` is (15, 61): `15 * 254 / 256` = 14, then 14 + 47 = 61. Without it, the second sample stays 62. The converter matches `ymz_step`. I did not treat the filter as a defect in `aica_step`.

Retail `.sbk`, `.mus`, and `VAG` bytes. `iso_data/jak1/` has no bank payload, which `SOUND_PLAN.md` already says. Shift 13–15 and filters 5–7 stay on the open `voice.cpp` 38 note. `voice.cpp` 42 asks whether the int16 clamp is what the SPU actually does; the converter follows the clamp that is written there.

`streamed_audio.cpp` 240 takes `header.size` bytes from the start of the file and then skips a 48-byte header. `iso.cpp` treats the size field as payload after the header. The converter follows `iso.cpp`. No retail header was available to compare the two.

The unit test command was not executed in this review.

## Next milestone

Repair the codec checks before `SOUND_PLAN.md` milestone 2 (the silent `.sbk` parser). Milestone 2 stays blocked until the command below exits 0. One file pair changes: `vag_adpcm.py` and `tests/test_vag_adpcm.py`. Synthetic blocks only. Standard library only.

Pass command, from the repo root:

```
python ports/dreamcast/tests/test_vag_adpcm.py
```

Exit 0. Every older literal stays: SPU 4096; filter 2 gives 4096, 7360, 9897, 11803; the shift-12 filter-2 triple is (1, 1, 0); the filter-1 clamp is (28672, 32767); byte `0x10` is (15, 62) and the step stays `0x7F`; byte `0x07` is (238, 276) and the step is 273. A decoder that uses the `+ 32` combined divide from `audio_formats.cpp` 129 fails the 11803 literal.

New checks, all integer literals:

1. Difference cap. `AicaState(sample=-20000, quant=0x6000)`, nibble 7, returns sample 12767 and leaves quant at `0x6000`. The existing nibble-4 case from sample 20000 at the same quant still returns 32767.
2. Negative nibble. `decode_aica(bytes([0x08]))` is (−15, 0) and the step stays `0x7F`. An arithmetic shift of the signed product (`-127 >> 3` = −16) fails this.
3. Repeat bit. `decode_vag` of flags `0x02` then flags `0x01` returns `loop_repeat` false, `loop_end` 55, `loop_start` None. Flags `0x04` then `0x03` still return `loop_repeat` true, `loop_start` 0, `loop_end` 55. One block of flags `0x03` returns `loop_repeat` true, `loop_start` None, `loop_end` 27.
4. Entry history versus the jump. `decode_vag` of one filter-1, shift-0, flags-`0x07` block whose nibbles are `[1] + [0]*27` returns sample 0 equal to 4096 and final history `(712, 760)`. Decoding that same block again with that history returns sample 0 equal to 4763. Expose the final history on the decode result, or take an initial `(hist1, hist2)`, so the test calls the library twice and compares both times to those literals. `independent_spu` is not the expected value.
5. Silence. `convert_vag` of a 28-zero, flags-`0x01` block returns `aica_samples` of `(15, 0)` repeated 14 times and `max_abs_error` of 15. The `>= 15` form fails this step.
6. Pad. `encode_aica([1000])` is `bytes([0x07])`.

The re-encode assertion (`decode(encode(decoded)) == decoded`) can stay in the file. It is not one of the six checks. `independent_aica` can stay as a drift detector. A case in this list compares against a literal, so editing the copy and the library together cannot keep the case green.

Leave the `254/256` filter out. Byte `0x10` stays (15, 62), the `ymz_step` result. Keep the low nibble as the first sample, matching `adpcm2pcm`. Do not also match `pcm2adpcm`'s high-nibble store until that encoder writes both nibbles of a byte.

Fail the command on any of the six literals, on any older literal above, or on an import outside the standard library.
