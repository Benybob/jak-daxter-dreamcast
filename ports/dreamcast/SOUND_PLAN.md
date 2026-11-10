(AI-assisted)
# Sound plan: Jak 1 SPU audio on the Dreamcast AICA

Nothing in `ports/dreamcast` plays audio. `rpc-call` and `rpc-busy?` are installed as no-ops (`goal_bridge.c` 1256–1259). This page is how Jak 1 actually asks for sound, what has to be true before one of its own effects can be heard, and the order to build that. Milestone 1 is the sample converter only.

The retail `.SBK`, `.MUS`, and `VAG` files were not in this tree. `iso_data/jak1/` contains `iso_data/.gitignore`, which ignores everything except itself. No bank byte length below was measured from a file.

## How Jak 1 sound is put together

Sound does not run on the EE. GOAL writes command structs and sends them to the IOP driver Overlord (`gsound-h.gc` 6–9, `gsound.gc` 15–16). Two RPC ports carry sound commands, and two more carry the dialogue stream:

| GOAL port | Constant | SIF id, Jak 1 | Who serves it |
| --- | --- | --- | --- |
| 0 | `RPC-SOUND-PLAYER` | `0xdeb1` | `Thread_Player` / `RPC_Player` |
| 1 | `RPC-SOUND-LOADER` | `0xdeb2` | `Thread_Loader` / `RPC_Loader` |
| 4 | `RPC-LOAD-STR` | stream load | ISO thread, VAG pages |
| 5 | `RPC-PLAY-STR` | stream play | ISO thread |

Port numbers: `rpc-h.gc` 8–18. Jak 1 SIF ids: `player_rpc_types.h` 10, `loader_rpc_types.h` 10. The player queue holds 128 commands of `0x50` bytes. The loader queue holds one (`gsound.gc` 17–21, `srpc.cpp` 33–36, `srpc.h` 70). `rpc-call` is the C bridge: channel, function number, async flag, send pointer, send size, receive pointer, receive size (`rpc-h.gc` 140–147, `kdgo.cpp` 58–68). `ksound.cpp` 11–15 registers it.

Each command begins with a reserved `uint16` and a `sound-command` (`gsound-h.gc` 51–77 and 152–155). The Jak 1 numbers in `srpc.h` 14–38 match that enum through `unload-music` (22). `set-mirror` is 201 on both sides. `set-fps` is 23 in GOAL (`gsound-h.gc` 76) and is not a `Jak1SoundCommand` arm. No call of `sound-set-fps` or `sound-set-reverb` turned up under `goal_src/jak1/`; both functions exist (`gsound.gc` 246–253, 378–382). A player command the switch does not list hits the default assert (`srpc.cpp` 341–344).

### Banks

A sound effect bank is an `.sbk` file. The PC loader truncates the requested name to 8 characters and appends `.sbk` (`fake_iso.cpp` 245–247). The file starts with a name table, then a 989snd image on a sector boundary:

- Slot 0 is given `sound_count` `0x3fe` (1022) before the read, and the 989snd image is taken to start at `10 * 2048` = 20480 (`sbank.cpp` 91–92, `fake_iso.cpp` 240–242).
- Any other slot is given `0x65` (101), and the image starts at 2048 (`sbank.cpp` 93–95, `fake_iso.cpp` 241–242).
- The table itself is a 16-byte bank name, a skipped `u32`, a `u32` count at offset `0x14`, then count records of 16 name bytes plus a `u32` falloff (`fake_iso.cpp` 221–233, `streamed_audio.cpp` 301–323). 1022 records occupy 20464 bytes, 16 under the 10-sector skip. 101 records occupy 2044 bytes, 4 under the one-sector skip.

The 989snd image begins with a chunk directory: `u32` type (1 or 3), `u32` chunk count, then count pairs of offset and size (`loader.cpp` 25–33, 410–418). An SFX bank's first chunk is fourcc `SBlk` and its second chunk is the sample bytes (`loader.cpp` 436–440). A music bank's first chunk is `SBv2` and it has a third, MIDI, chunk (`loader.cpp` 425–434). Inside `SBlk`, the reader skips `VagDataSize` and `SRAMAllocSize` (`loader.cpp` 224–227). Those two words are the per-file sizes a budget wants. They are unread here, and no file was present to read them.

There are six bank slots (`sbank.cpp` 11–19). Jak 1 marks all six in use up front (`sbank.cpp` 38–39). `AllocateBank` returns the first slot whose `bank_handle` is still 0 (`sbank.cpp` 75–87). The `spu_loc` / `spu_size` numbers later in that function, including common `0xAFCC0`, are written only for Jak 2 (`sbank.cpp` 48–72). They are not a Jak 1 measurement.

Startup loads `common` or, for SCEI, `commonj`, then `empty1` and `empty2` (`gsound.gc` 122–132). `*sound-bank-1*` and `*sound-bank-2*` start as those two empty names (`gsound-h.gc` 362–366, `gsound.gc` 130–132). `update-sound-banks` keeps at most two level banks besides that always-loaded common bank: it unloads `empty1` / `empty2` and loads the active level names (`level.gc` 1022–1080). A third distinct bank in one frame is an error string (`level.gc` 1036). Music is a separate handle, `gMusic`, not one of the six slots (`srpc.cpp` 397–409).

`level-info.gc` names one SFX bank and one music bank per level. `sunken` and `sunkenb` both name `sunken` (lines 786–787 and 896–897). `darkcave` and `robocave` play the `maincave` music bank (1696–1697, 1748–1749). `training` and `demo` play `village1` music (19–20, 2258–2259). `title` has no SFX bank and plays `village1` (2310–2312). `intro`, `halfpipe`, and `default-level` name neither (2224–2226, 2364–2366, 2418–2420). The other levels name themselves for both, at the lines in the budget table.

### What a "sound" is

Playback is the 989snd engine in `game/sound/989snd/`, not a pointer at a raw clip. An `SBlk` sound is a list of grains (`sfxgrain.h` 54–80): tones, loops, random picks, child sounds, LFO. A tone points at a sample in the sample chunk and carries ADSR, pan, and a pitch note (`vagvoice.h` 19–32, `loader.cpp` 36–56). `snd_PlaySoundVolPanPMPB` starts that program.

The samples in the chunk are SPU ADPCM, the same 16-byte blocks `Voice::DecodeSamples` reads (`voice.cpp` 19–71). One block is a 16-bit header and 28 nibbles, low nibble first inside each little-endian halfword. The header fields (`voice.h` 77–84, `bitfield.h` 10) are:

| Bits | Meaning |
| --- | --- |
| 0–3 | shift |
| 4–6 | filter, five coefficient pairs (`voice.cpp` 11–16) |
| 8 | loop end (flag byte bit 0) |
| 9 | loop repeat (flag byte bit 1) |
| 10 | loop start (flag byte bit 2) |

`KeyOn` clears both history samples (`voice.cpp` 93–94). The filter is `(coef * history) >> 6` for each coefficient, then a clamp to signed 16-bit (`voice.cpp` 35–48). `common/audio/audio_formats.cpp` 129–130 adds the two products and 32 before dividing by 64. That is a different result; the fourth sample of filter 2 in the milestone 1 test is 11803 this way and 11804 the other way. The playback path is `voice.cpp`. Shift values 13–15 and filters 5–7 are not given a defined coefficient path there (`voice.cpp` 38). The converter rejects a filter above 4.

The synth constant is 48000 Hz (`sound_types.h` 9). A streamed VAG file carries its own rate in the header (`iso.cpp` 1206–1213).

Group master volumes default to `0x400` (`vagvoice.cpp` 14). GOAL's full-scale unit is the same 1024: `sound-set-volume` multiplies the 0–100 setting by 10.24 (`gsound.gc` 239–244). `sound-play` does the same for a per-sound volume (`gsound.gc` 273–280).

### Music

`sound-music-load` sends `load-music` (`gsound.gc` 109–114). The loader fades out id 666, unloads `gMusic`, and loads `<name>.mus` from byte 0 (`srpc.cpp` 397–409, `fake_iso.cpp` 204–216, `iso_api.cpp` 34–52). The decompiler instead scans every 2048 bytes for an `SBlk` or `SBv2` directory (`streamed_audio.cpp` 274–298, 350), and it skips `TWEAKVAL.MUS` (`streamed_audio.cpp` 345–347). Whether a retail `.mus` starts at byte 0 was not confirmed.

Once a bank is loaded, the player thread starts sound id 666 itself if it is not already playing (`srpc.cpp` 94–104). `set-flava` stores one byte (`srpc.cpp` 329–330) and each frame writes it as MIDI register 0 on that handle (`srpc.cpp` 111–114). The flavor ids are the `music-flava` enum (`gsound-h.gc` 81–131). Settings load the level's music bank only when both level SFX slots are occupied, the loader is idle, and music volume is above zero (`settings.gc` 186–194).

### Streamed dialogue

A play name that begins with `spool-` is not a bank sound (`srpc.cpp` 125–157). The rest of the name is an 8-character `VAGDIR` key. The directory is `VAGDIR.AYB`: a `u32` count, then records of 8 name bytes and a `u32` sector index. The byte offset is that index times 2048 (`iso.cpp` 229–230, `streamed_audio.cpp` 22–23 and 107–132). The file is `VAGWAD` plus the language (`srpc.cpp` 145–148). Languages, in order: `ENG`, `FRE`, `GER`, `SPA`, `ITA`, `JAP`, `UKE` (`srpc.cpp` 46). `set-language` selects the row (`srpc.cpp` 393–395, `gsound.gc` 140–146).

The clip is a `VAGp` or `pGAV` file. The header is 48 bytes: magic, version, a zero word, size, rate, three zero words, 16 name bytes (`streamed_audio.cpp` 57–64). Big-endian `VAGp` byte-swaps the size and the rate (`iso.cpp` 1200–1211). Playback DMA's into a reserved SPU window and keys one voice at `gStreamSRAM + 0x30`, which skips that header (`iso.cpp` 1220–1237). Further pages alternate between `gStreamSRAM` and `gStreamSRAM + 0x6000` (`iso.cpp` 1251–1295).

Overlord allocates `0xC030` bytes of SPU RAM for this and puts a 0x30-byte silent loop at `+0xC000` (`ssound.cpp` 65–68, `iso.cpp` 126–127). `0xC000` is 49152 and `0x6000` is 24576, so the window is two 24 KiB halves plus the 48-byte trap. The voice is one externally allocated SPU voice (`ssound.cpp` 57–62). `gVoice`'s numbering, core in bit 0 and channel in the rest, was not re-derived past that comment.

Each vblank, if `get-irx-version` has stored an EE address, Overlord DMA's `0x110` bytes of `SoundIopInfo` there (`srpc.cpp` 384–388, 464–508). `nocd` and `dirtycd` are written as 0 (`srpc.cpp` 485–489). GOAL's `*sound-iop-info*` starts with `strpos` -1 (`gsound.gc` 28–46). `swap-sound-buffers` opens the no-disc or bad-disc screen when those words are nonzero (`gsound.gc` 194–200). Leaving them 0 is what keeps the game off that screen.

### The commands that move a playing effect

`sound-play` / `sound-play-by-name` queue `play` with a new id at or above `0x10000` (`gsound-h.gc` 332, `gsound.gc` 67–70 and 282–298). Id 0 is dropped (`srpc.cpp` 122–124). The name is looked up across loaded banks (`srpc.cpp` 160–163, `sbank.cpp` 116–130). A hit allocates one of 64 `Sound` rows (`ssound.h` 55, `srpc.cpp` 186–211) and starts the 989snd program. Falloff, when the play mask does not override it, is unpacked from the name-table word: min in bits 0–13, max in 14–27, curve in 28–31 (`srpc.cpp` 171–179, `soundcommon.cpp` 40–42). Curve 0 means "do not spatialize" (`ssound.cpp` 123–125).

`set-param`, `pause-sound`, `stop-sound`, and `continue-sound` address that id (`srpc.cpp` 213–276). The same id on the VAG voice pauses, stops, or resumes the stream. `set-ear-trans` copies one ear point into both ear arguments (`srpc.cpp` 332–334); the Jak 1 command has one ear (`gsound-h.gc` 236–239). `set-master-volume` walks the group bitfield. Bit 1 (music) is stored in `gMusicVol`, bit 2 (dialog) goes to the stream voice, and any other set bit calls `snd_SetMasterVolume` (`srpc.cpp` 278–290). The GOAL bitfield is `sfx`, `music`, `dialog`, `sog3`, `ambient`, ... (`gsound-h.gc` 13–23), so those are bits 0, 1, 2, 3, 4.

Voice ranges asked of 989snd: groups 1 and 2 get voices 0–15, and the other groups in 0–7 get voices 16–47 (`ssound.cpp` 72–78). That is the 48-voice SPU2, described in the comment at `ssound.cpp` 60–62. `gSounds` can track 64 logical sounds, more rows than voices (`ssound.h` 55).

`InitISOFS` in this tree always selects `fake_iso` (`iso.cpp` 114–120). The retail CD read path is not what these sources run. The file-name rules above are the PC loader's rules.

## What the port does today

`dc_goal_install_pc_services` binds `rpc-call` and `rpc-busy?` to `pc_zero_native` (`goal_bridge.c` 1256–1259, 1288–1289). `rpc-busy?` is therefore 0. `rpc-call` returns 0 and writes nothing.

`check-irx-version` runs at gsound load. It requires major 2 and minor 0, and calls `crash!` otherwise (`gsound.gc` 72–84, `versions.h` 54–56). The command it just allocated has those words at 0 until something writes them. `GAME_CGO.md` 397–399 records the current choice: drop sound and loader RPCs until an AICA driver serves them. `HELP_WANTED.md` names that driver as the next piece behind `rpc-call`.

`rpc-busy?` returning 0 also means `update-sound-banks` and the music load do not wait (`level.gc` 1023, `settings.gc` 188), and `swap-sound-buffers` does not clear `*sound-player-enable*` (`gsound.gc` 187–192). The commands are queued and then discarded. `sound-play` of `"jump"` (`target.gc` 850) has nowhere to go.

Main RAM, VRAM, and sound RAM are separate pools (`README.md` 242–243, `MEMORY_PLAN.md` 3–7). Sound RAM is 2 MiB. KOS clears it in `spu_init` before `main` (`MEMORY_PLAN.md` 32–33). The SH-4 reaches it over the G2 bus, 32-bit, with FIFO waits (`MEMORY_PLAN.md` 69–75). That page's conclusion is the one this plan keeps: sound RAM holds the sample image the audio hardware plays, not GOAL objects the SH-4 walks every frame. Mixing belongs on the AICA side of that bus. Name tables, the 64 `Sound` rows, and the grain programs stay in system RAM. They are small next to a bank.

## What the AICA offers

From this repo: 2 MiB of sound RAM, cleared at init, slow for the SH-4 (`README.md` 242–243, `MEMORY_PLAN.md` 3–7 and 69–75). The port's own word for the backend is "AICA" (`README.md` 43 and 252, `goal_bridge.c` 1258).

Not from this repo, and not re-measured on hardware here: the AICA is a Yamaha chip with 64 channels, each able to play 16-bit PCM or its own 4-bit ADPCM, out of that 2 MiB. Channel count, the ADPCM step table, and the key-on reset state are not in the Jak sources. The converter uses the step table published for that chip (diff `1,3,5,7,9,11,13,15` and the negatives, scale `0x0E6, 0x0E6, 0x0E6, 0x0E6, 0x133, 0x199, 0x200, 0x266` repeated, step clamped to 127..24576). Milestone 6's pass check is a host decode against that table. A listening check on Flycast is later, and it is not proof the table matches the die.

64 AICA channels cover the 48 SPU voices `ssound.cpp` 72–78 assigns, with 16 left over. They do not cover a music player that ignores the 16-voice cap on groups 1 and 2. The cap stays.

## VAG to AICA

SPU ADPCM and AICA ADPCM are both 4-bit and are not the same code. Re-encoding is required. One SPU block is 16 bytes for 28 samples, header included. The same 28 samples are 14 AICA bytes. The sample-chunk image shrinks by 14/16 if every byte of that chunk is SPU blocks, which is what `Voice` consumes. Name tables and the `SBlk` program stay out of sound RAM, so the resident audio image is smaller than the `.sbk` file.

The converter in `vag_adpcm.py` (milestone 1):

1. If the buffer starts with `pGAV` or `VAGp` and is at least 48 bytes, take `size` bytes after the header. Big-endian magic byte-swaps `size` and the rate, as `iso.cpp` 1206–1211 does. A raw buffer is SPU blocks. A size that is not a whole number of blocks, or that runs off the end, is an error.
2. Decode every block with the `voice.cpp` integer steps, history starting at 0. Record loop start, loop end, and the repeat flag. Do not unroll the loop.
3. Encode that PCM to AICA nibbles, low nibble first, two per byte. The encoder calls the same step the decoder uses and keeps the closest nibble. Ties take the lowest index. The reset state is sample 0, step 127.
4. Decode the bytes and fill `ErrorReport`: count, max absolute error, mean absolute error, RMS, and the two sums.

There is no zero AICA step. From step 127 the smallest move is `127/8` = 15, toward zero. A silent VAG block therefore does not come back as zeros. Decoding the encoder's bytes, then encoding those samples again, comes back to the same PCM. That is the exact round trip. The error against the SPU PCM is the report.

Looping on the AICA does not restore the SPU header at the loop point, and this chip's ADPCM state is a step plus one sample, not the SPU's two history samples. Milestone 1 records the loop sample indexes and does not promise a click-free AICA loop. A later player either encodes a loop whose AICA state matches at both ends, or loops in PCM before encoding. That choice is unmade.

`vag_adpcm.py` and `tests/test_vag_adpcm.py` are the milestone 1 files. The tests build blocks in memory. They do not open `iso_data`.

## Memory budget

Sound RAM is 2097152 bytes. The stream window Overlord reserves before any bank is `0xC030` = 49200 bytes (`ssound.cpp` 67). Leaving that window in place leaves 2047952 bytes for samples. The name-table prefix is not part of that image: 20480 bytes for the common slot's file prefix, 2048 for each other SFX slot (`fake_iso.cpp` 240–242). Two level banks plus common is 24576 bytes of prefixes, in system RAM, beside the grain programs.

Resident set the game actually keeps: `common` or `commonj`, up to two level `.sbk` files, one `.mus`, and the 49200-byte stream window (`level.gc` 1022–1080, `srpc.cpp` 94–104 and 397–409, `ssound.cpp` 65–68). `empty1` and `empty2` occupy slots only until the first level bank replaces them.

Per-level sample bytes were not measured. The column is the number milestone 6 reads from a file that is not in the tree: the samples-chunk size in the 989snd directory, and `SRAMAllocSize` inside `SBlk` (`loader.cpp` 224–227, 421–423). Until those exist, a level has no passable budget number.

| Level | SFX bank (`level-info.gc`) | Music bank | Sample bytes |
| --- | --- | --- | --- |
| training | `training` (19) | `village1` (20) | unmeasured |
| village1 | `village1` (139) | `village1` (140) | unmeasured |
| beach | `beach` (283) | `beach` (284) | unmeasured |
| jungle | `jungle` (336) | `jungle` (337) | unmeasured |
| jungleb | `jungleb` (389) | `jungleb` (390) | unmeasured |
| misty | `misty` (440) | `misty` (441) | unmeasured |
| firecanyon | `firecanyon` (624) | `firecanyon` (625) | unmeasured |
| village2 | `village2` (695) | `village2` (696) | unmeasured |
| sunken, sunkenb | `sunken` (786, 896) | `sunken` (787, 897) | unmeasured |
| swamp | `swamp` (967) | `swamp` (968) | unmeasured |
| rolling | `rolling` (1168) | `rolling` (1169) | unmeasured |
| ogre | `ogre` (1220) | `ogre` (1221) | unmeasured |
| village3 | `village3` (1310) | `village3` (1311) | unmeasured |
| snow | `snow` (1401) | `snow` (1402) | unmeasured |
| maincave | `maincave` (1606) | `maincave` (1607) | unmeasured |
| darkcave | `darkcave` (1696) | `maincave` (1697) | unmeasured |
| robocave | `robocave` (1748) | `maincave` (1749) | unmeasured |
| lavatube | `lavatube` (1819) | `lavatube` (1820) | unmeasured |
| citadel | `citadel` (1928) | `citadel` (1929) | unmeasured |
| finalboss | `finalboss` (2152) | `finalboss` (2153) | unmeasured |
| demo | `village1` (2258) | `village1` (2259) | unmeasured |
| title | none (2310) | `village1` (2312) | unmeasured |
| intro, halfpipe, default-level | none | none | none |

The PC loader's 8-character cut (`fake_iso.cpp` 245–246) leaves `firecanyon` as `firecany` and `finalboss` as `finalbos`. The retail directory spelling was not in the tree.

Worst case the code will sit in sound RAM at once: AICA bytes for `common` (or `commonj`), two level sample chunks, one music sample chunk, and 49200 stream bytes. A set whose AICA bytes exceed 2047952 does not load. That comparison is milestone 6, and it cannot pass or fail until the files are measured.

## Calls to service first

The goal of the first playable slice is one bank effect the game itself starts, such as `"jump"` (`target.gc` 850), coming out of an AICA channel. Music, flava, and `spool-` dialogue are not on that path.

1. `get-irx-version` on loader port 1. Write major 2, minor 0, and the EE info address into the command, in the `rpc-call` that carries it (`gsound.gc` 72–77, `srpc.cpp` 384–388). `rpc-busy?` on that port returns 0 once the write is done. If major stays 0, `check-irx-version` takes `crash!` (`gsound.gc` 79–82). Ports other than 0 and 1 stay on the existing no-op so DGO and the ramdisk are unchanged.
2. `load-bank` for `common` (or `commonj`) and for the level name `update-sound-banks` asks for (`gsound.gc` 122–128, `level.gc` 1039–1048). Parse the name table and the `SBlk`, convert each tone's blocks with milestone 1, and keep the grain program in system RAM. A play before this load is a miss (`srpc.cpp` 160–163).
3. `play` on port 0. Ignore id 0 (`srpc.cpp` 122–124). Resolve the 16-byte name (`sbank.cpp` 116–130). Start the tone's AICA channel at step 127, sample 0, with the volume and pitch-mod from the command (`gsound.gc` 288–297). This is the call that makes `"jump"` audible. Group master volume already defaults to full (`vagvoice.cpp` 14), so `set-master-volume` is not required for a first sound. It is required before the settings screen's volume slider does anything (`settings.gc` 160–161).
4. `set-param`, `stop-sound`, `pause-sound`, `continue-sound`, and `set-ear-trans`, still before music. Looping and moving sources update every frame through `set-param` and the ear (`gsound.gc` 319–332 and 255–261, `ssound.cpp` 142–159). `set-master-volume` for the `sfx` bit is in this step so the slider matches.

`swap-sound-buffers` sends the player batch once a frame (`gsound.gc` 186–192). Servicing the batch inside `rpc-call`, before it returns, matches the current stub's timing: `rpc-busy?` is already 0, and GOAL does not wait.

## Milestones

Each one keeps the earlier pass checks. A failure is the first miss. Do not claim a check ran until a reviewer runs it.

### 1. Converter

`vag_adpcm.py` and `tests/test_vag_adpcm.py`. Synthetic blocks only. Standard library only.

Pass: `python ports/dreamcast/tests/test_vag_adpcm.py` exits 0. The VAG cases match the hand values in the test (4096, the filter-2 series 4096/7360/9897/11803, the arithmetic shift to 0, the clamp to 32767). The AICA cases match 15 then 62 for byte `0x10`, and 238 then 276 for byte `0x07`. Encoding a PCM buffer and decoding it with the test's own copy of the step agrees with the library, and encoding that reconstruction returns the same PCM. A silent block's report has `max_abs_error >= 15` and that max equals the sample list.

Fail: any assertion, any import outside the standard library, or a decoder that uses the `+ 32` combined divide from `audio_formats.cpp` 129.

### 2. Bank directory, still silent

Done (2026-11-10): `sbk_bank.py` and `tests/test_sbk_bank.py` (synthetic
fixtures, standard library). Drafted by Grok from this plan, reviewed against
`game/sound/989snd/loader.cpp`: `FileAttributes::Read` (type, count, offset and
size pairs relative to the image) and `SFXBlock::ReadBlock` (magic, then
version +4, flags +8, bank id +12, sound/grain/VAG counts +22/+24/+26,
`VagDataSize` +40, `SRAMAllocSize` +44; 60-byte header, 64 from version 2).
An `SBlk` directory may have more than two chunks, as `BankLoad` accepts.

Measured on the 25 retail `.sbk` files (`iso_data/jak1/SBK`, read only): all
parse; every one is `SBlk` version 1; the sample chunk equals `VagDataSize`; the
name count equals the sound count. Sample bytes (SPU ADPCM; AICA is 14/16 of
that): common 762,224, commonj 763,280, training 114,976, village1 286,256,
beach 347,152, jungle 333,584, misty 321,888, ogre 353,536 (the largest level
bank), village3 165,616, citadel 109,136. Worst resident set, common and the two
largest level banks: 1,462,912 SPU bytes (common, ogre, beach), about 1,280,048 AICA bytes, plus the
49,200-byte stream window: under the 2,047,952 milestone 6 allows.

A host parser for one synthetic `.sbk`: a 2048-byte or 20480-byte name prefix, a chunk directory, an `SBlk`, and a sample chunk of the milestone 1 blocks. No `rpc-call` change.

Pass: the parser returns the 16-byte names, the falloff word, and a sample span that `decode_vag` accepts. The 1022-count slot reads past 20480 and the 101-count slot reads past 2048.

Fail: the sample span includes the name prefix, or a name shorter than 16 bytes is NUL-padded in the comparison (`soundcommon.cpp` 37–39 copies all 16).

### 3. Version reply

`rpc-call` on channel 1, command `get-irx-version`, writes major 2 and minor 0 into the send buffer and records the EE address. `rpc-busy?` on channels 0 and 1 is 0. Channels 2 and up stay the zero stub (`goal_bridge.c` 1256–1259).

Pass: a host test builds the 0x50-byte command, calls the handler, and reads major 2, minor 0 at the GOAL offsets (`gsound-h.gc` 209–212). A channel-3 call still returns 0 and leaves its buffer untouched.

Fail: major/minor stay 0, or a non-sound channel is written.

### 4. One effect plays

Load the milestone 2 fixture as `common`. A `play` command whose name is in the table arms one AICA channel with the converted bytes, the command's volume, and id `>= 0x10000`.

Pass: the channel bytes equal `convert_vag` of that tone, the id is stored, and a second `play` with an unknown name or id 0 arms nothing.

Fail: a voice starts without a loaded bank, or the PCM fed to the encoder is not the `decode_vag` output.

### 5. The effect follows the game

`set-param`, stop, pause, continue, `set-ear-trans`, and `set-master-volume` for the sfx bit.

Pass: curve 0 holds its volume when the ear moves (`ssound.cpp` 123–125). A curve of 1 with `fo_min` / `fo_max` from the name-table word gets quieter as the ear's distance, in the command's 1/16-meter units (`gsound.gc` 215–219), crosses the range. `stop-sound` on the id disarms the channel. Pause leaves it armed and silent. Master volume 0 yields silence and `0x400` restores the play volume.

Fail: a curve-0 sound changes with the ear, or stop leaves the channel keyed.

### 6. The 2 MiB cap, from real files

When `iso_data/jak1/SBK` and `MUS` are present, measure each file's samples-chunk size and `SRAMAllocSize`, convert with milestone 1, and write the numbers into the table above. Refuse a resident set whose AICA bytes plus 49200 exceed 2097152.

Pass: the measured table is filled from those reads, a fixture set over the cap leaves the previous bank loaded, and a set under the cap replaces it. The stream window is not part of the bank image.

Fail: a measurement is copied from the Jak 2 `spu_size` constants (`sbank.cpp` 48–72), or an over-cap load still keys a voice.

### 7. Music and flava

One `.mus` fixture with two flavors. `load-music` installs it. The player loop's id-666 behavior starts it (`srpc.cpp` 94–104). `set-flava` selects the segment.

Pass: flava 0 and flava 1 produce the two different fixture segments, at most 16 music-group channels are keyed (`ssound.cpp` 76–77), and `unload-music` disarms them.

Fail: flava does not change the segment, or a 17th music channel keys.

### 8. Dialogue stream

`spool-` plus a synthetic `VAGDIR` and a two-page `VAGp` in a `VAGWAD.ENG`. Pages land in the two 24576-byte halves. `strpos` advances. `nocd` stays 0.

Pass: the voice starts after the 48-byte header, the second page uses the other half, playback stops at the size field, and `*sound-iop-info*` would still see `nocd == 0` (`srpc.cpp` 488, `gsound.gc` 60–62).

Fail: `nocd` or `dirtycd` becomes nonzero, or a page is written outside the 49200-byte window.

Fixes since the first level ELF stream (`level_sound.c`, from a Grok review, each checked against the code):
- A 16-byte block's 28 samples carry over the 4096-sample buffer edge; before, every full buffer ended with 8 samples of silence.
- The wad is read 2 KiB at a time, not one seek and read per block.
- Pause stops the KOS stream and continue restarts it where the decoder stopped, as `PauseVAG` silences the voice; before, the paused stream queued silence that played on resume.
- A stop sets the stream id back to 1 (`StopVAG`), so `current-str-id` no longer returns a finished line.
- Reading the game clock keeps a fault already on the runtime.

- `strpos` and the line's end follow real time (the TMU), as the IOP's vblank count does, not the game's `base-frame-counter`, which runs ahead of the audio when frames are slow.

## Unconfirmed

- Retail `.sbk`, `.mus`, `VAGDIR.AYB`, and `VAGWAD.*` byte lengths. The directory is empty of those files in this tree.
- On-disc 8.3 spelling of names longer than 8 characters.
- Whether retail `.mus` files begin at byte 0 (`fake_iso.cpp` 213) or behind a sector prefix (`streamed_audio.cpp` 274).
- SPU2 behavior for shift 13–15 and filter 5–7. `voice.cpp` 38 says the invalid case is open.
- AICA channel count, the ADPCM coefficient table, the initial step of 127, and that a key-on clears the running sample. Used by milestone 1 because the chip's manual is not in this repo. Milestone 6 checks the code against itself. It does not check the die.
- A click-free AICA loop. Loop indexes are recorded. The loop method is not chosen.
- That `crash!` from a failed IRX check aborts the Dreamcast boot. The GOAL call is real (`gsound.gc` 79–82). The runtime's reaction was not re-read for this page.
- `ReadBankSoundInfo` asserts (`soundcommon.cpp` 14–18), so `reload-info` has no working body in this tree. Not on the first-play path.
- The retail ISO reader. `InitISOFS` forces `fake_iso` (`iso.cpp` 114–120).
