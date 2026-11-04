(AI-assisted)

This tool was not run. No retail sample counts or byte lengths are filled in here.

From the repository root:

```
python ports/dreamcast/sound_banks.py iso_data/jak1
python ports/dreamcast/sound_banks.py iso_data/jak1 --progress
python ports/dreamcast/sound_banks.py iso_data/jak1 --limit 4 --progress
python ports/dreamcast/sound_banks.py iso_data/jak1 --output ports/dreamcast/build/sound-banks
python ports/dreamcast/tests/test_sound_banks.py
```

`iso_data` or `iso_data/jak1` both work when `SBK`, `MUS`, or `VAG` is present. Disc files are opened with `rb` only. `--output` is accepted only when the path resolves to `ports/dreamcast/build` or a directory inside it. Omitting `--output` prints the report and writes nothing.

The default command does not call `convert_vag`. Each kept SPU block is 28 samples and AICA stores two samples per byte, so `aica_bytes` is 14 times the block count. That is the length `convert_vag` would write. A block whose filter `vag_adpcm` rejects is left out of the total. Dialogue clips use the same size. The tool reads each clip's 48-byte header and its filter bytes. It does not copy the rest of the wad for every clip, and it does not encode dialogue.

`--output` is the path that calls `convert_vag` and writes `DCSA` for SBK and MUS. That encode tries every AICA nibble in pure Python, so a full disc is slow. `--limit N` reads at most N of those files, SBK first, then MUS. `TWEAKVAL.MUS` stays skipped and does not count. `--progress` writes each bank path to stderr before it is read. The text report still arrives on stdout when the run finishes.

Sound RAM is 2097152 bytes (`MEMORY_PLAN.md` 3, `SOUND_PLAN.md` 121). The driver reserve is 49200 bytes, `snd_SRAMMalloc(0xc030)` (`ssound.cpp` 67). The bank cap is 2047952. A level total is `common` (or `commonj` when `common` is absent) plus that level's SFX AICA bytes plus that level's music AICA bytes. The reserve is added for the fit check. Dialogue stays in its own section. Level totals leave it out.

Sample spans stop on the LoopEnd flag (`voice.cpp` 56-65). Back-to-back packing with no pad is UNVERIFIED. `.mus` at byte 0 follows `fake_iso.cpp` 213; a later sector hit is UNVERIFIED. The 8-character stem cut follows `fake_iso.cpp` 207-208 and 245-247. Retail spelling of names longer than eight characters is UNVERIFIED. `DCSA` is this tool's container. A player for it is UNVERIFIED. `SBlk` versions other than 1 and 2 are refused (`loader.cpp` 229-231). Music has no SRAM word after `VagDataSize` (`loader.cpp` 354-357). Jak 1 dialogue entries have no stereo word (`streamed_audio.cpp` 111-132); stereo interleave is UNVERIFIED and is not applied. Loose `*.VAG` files outside `VAGWAD.<language>` are not walked. Jak 2 SRAM sizes at `sbank.cpp` 48-72 are unused.
