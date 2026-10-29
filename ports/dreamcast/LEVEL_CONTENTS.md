# What a Jak 1 level holds (AI-assisted)

`level_contents.py` reads the 24 level DGOs from the user's own disc and splits
them by kind. Nothing from the disc is committed; these are file sizes, in KiB,
measured 2026-10-29 from the USA Rev 1 disc. They are not resident sizes:
linking drops the headers and link tables, and nothing is decompressed or
converted. They bound what a level-loading design has to place.

On the PS2 a level loads into a 10,416 KiB level heap, and two levels are
resident at once. The Dreamcast has 16 MiB of main RAM in all, and the port's
GOAL arena already holds 10.7 MB of engine (GAME.CGO) in its 13 MiB.

## By kind

Code is v3 objects; the rest are v4 data. `tpage-*` are texture pages, the
`*-vis` object is the level's BSP, and the other data objects are art groups
(skinned models, joints and compressed animations).

| Level | Objects | Total | Code | Textures | Level (BSP) | Art groups | Largest object |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| VI1 | 55 | 11,063 | 254 | 1,936 | 7,304 | 1,569 | village1-vis (level, 7,304) |
| ROL | 32 | 11,046 | 202 | 1,666 | 7,971 | 1,207 | rolling-vis (level, 7,971) |
| MIS | 59 | 11,044 | 360 | 2,040 | 6,749 | 1,894 | misty-vis (level, 6,749) |
| LAV | 37 | 10,962 | 180 | 1,489 | 8,165 | 1,129 | lavatube-vis (level, 8,165) |
| SNO | 51 | 10,939 | 287 | 1,766 | 6,715 | 2,172 | snow-vis (level, 6,715) |
| OGR | 35 | 10,686 | 236 | 1,810 | 6,937 | 1,703 | ogre-vis (level, 6,937) |
| VI3 | 35 | 10,677 | 117 | 2,013 | 7,272 | 1,275 | village3-vis (level, 7,272) |
| VI2 | 52 | 10,613 | 259 | 2,259 | 6,049 | 2,047 | village2-vis (level, 6,049) |
| CIT | 51 | 10,306 | 222 | 1,791 | 5,712 | 2,582 | citadel-vis (level, 5,712) |
| ROB | 22 | 10,268 | 16 | 1,666 | 7,731 | 855 | robocave-vis (level, 7,731) |
| BEA | 50 | 10,253 | 232 | 2,141 | 6,229 | 1,651 | beach-vis (level, 6,229) |
| MAI | 33 | 10,112 | 187 | 1,457 | 7,599 | 869 | maincave-vis (level, 7,599) |
| JUN | 50 | 10,070 | 190 | 2,122 | 6,309 | 1,449 | jungle-vis (level, 6,309) |
| SUN | 61 | 9,869 | 480 | 1,410 | 6,446 | 1,533 | sunken-vis (level, 6,446) |
| SWA | 36 | 9,222 | 242 | 1,716 | 5,915 | 1,349 | swamp-vis (level, 5,915) |
| FIN | 37 | 9,065 | 250 | 845 | 5,939 | 2,030 | finalboss-vis (level, 5,939) |
| FIC | 25 | 7,541 | 175 | 1,437 | 4,959 | 969 | firecanyon-vis (level, 4,959) |
| TRA | 19 | 6,611 | 47 | 1,566 | 4,441 | 557 | training-vis (level, 4,441) |
| DAR | 16 | 6,516 | 6 | 1,196 | 4,966 | 348 | darkcave-vis (level, 4,966) |
| DEM | 17 | 5,454 | 8 | 5,434 | 12 | 0 | tpage-1485 (textures, 449) |
| SUB | 21 | 4,698 | 0 | 1,228 | 2,731 | 739 | sunkenb-vis (level, 2,731) |
| JUB | 18 | 4,103 | 72 | 841 | 1,998 | 1,193 | jungleb-vis (level, 1,998) |
| INT | 8 | 1,612 | 4 | 551 | 771 | 285 | intro-vis (level, 771) |
| TIT | 15 | 1,294 | 25 | 628 | 189 | 451 | tpage-1609 (textures, 449) |

All 24 levels: 204,025 KiB; code 2%, textures 20%, BSP 63%, art 15%.

## Inside the BSP

`--bsp` splits each BSP object by basic type. Each basic's type slot is found
from the type links in the object's v4 link table, as the decompiler reads them,
and each basic owns the bytes up to the next one, so structures that follow a
basic count for it. The basics account for every byte of each BSP's data.

| Level | BSP | tfrag | tie | shrub | visibility | lighting | collision | entities | tree | other |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| BEA | 6,172 | 2,866 | 1,763 | 287 | 87 | 326 | 451 | 98 | 32 | 261 |
| CIT | 5,660 | 202 | 3,834 | 100 | 527 | 594 | 132 | 31 | 26 | 215 |
| DAR | 4,934 | 3,751 | 267 | 105 | 18 | 137 | 597 | 8 | 17 | 35 |
| DEM | 11 | 5 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 5 |
| FIC | 4,925 | 749 | 3,554 | 86 | 42 | 284 | 74 | 18 | 16 | 102 |
| FIN | 5,862 | 3,865 | 1,394 | 134 | 137 | 193 | 59 | 5 | 45 | 29 |
| INT | 764 | 130 | 460 | 61 | 1 | 61 | 41 | 1 | 3 | 6 |
| JUB | 1,979 | 7 | 1,583 | 9 | 44 | 195 | 71 | 8 | 2 | 59 |
| JUN | 6,252 | 1,576 | 2,696 | 547 | 312 | 372 | 445 | 51 | 32 | 221 |
| LAV | 8,081 | 923 | 5,974 | 137 | 108 | 369 | 151 | 45 | 62 | 312 |
| MAI | 7,548 | 6,012 | 127 | 238 | 247 | 144 | 550 | 33 | 26 | 171 |
| MIS | 6,687 | 1,757 | 3,294 | 353 | 31 | 473 | 360 | 84 | 33 | 302 |
| OGR | 6,879 | 1,539 | 3,761 | 487 | 146 | 498 | 234 | 21 | 35 | 158 |
| ROB | 7,674 | 4,672 | 1,545 | 365 | 53 | 211 | 528 | 37 | 29 | 233 |
| ROL | 7,898 | 2,283 | 3,832 | 711 | 61 | 384 | 273 | 44 | 45 | 264 |
| SNO | 6,651 | 1,720 | 3,420 | 223 | 137 | 463 | 210 | 95 | 34 | 348 |
| SUB | 2,701 | 1,061 | 1,041 | 51 | 72 | 174 | 61 | 33 | 12 | 195 |
| SUN | 6,384 | 2,639 | 2,375 | 212 | 250 | 281 | 264 | 46 | 29 | 287 |
| SWA | 5,854 | 1,302 | 2,657 | 622 | 39 | 378 | 446 | 64 | 38 | 308 |
| TIT | 183 | 0 | 0 | 0 | 20 | 0 | 0 | 0 | 0 | 162 |
| TRA | 4,402 | 1,191 | 2,407 | 218 | 9 | 231 | 172 | 30 | 24 | 120 |
| VI1 | 7,242 | 1,966 | 2,931 | 360 | 890 | 556 | 258 | 31 | 44 | 205 |
| VI2 | 6,000 | 1,295 | 2,615 | 553 | 804 | 350 | 184 | 33 | 38 | 127 |
| VI3 | 7,214 | 3,916 | 1,417 | 271 | 770 | 385 | 261 | 18 | 36 | 139 |

tfrag, tie and shrub are the background renderers' geometry (with their
instances and prototypes); visibility is `level-vis-info`, lighting the
`time-of-day-palette`s, collision the collide fragments and meshes, entities the
actors, ambients and nav meshes, tree the `draw-node`s. "other" is mostly float,
string, symbol, vector and int32 arrays, likely the entities' res properties,
which this scan cannot attribute.

## What it means for level loading

- **Background geometry is most of a level.** tfrag, TIE and shrub together are
  up to 7.0 MiB (lavatube) and 6.4 MiB (maincave), 75 to 90% of each BSP. It is
  in the PS2's VU1 packet formats, which a PowerVR renderer cannot draw as they
  are, so it gets converted offline either way. Its converted size, not these
  numbers, is what will have to be resident or streamed.
- **Textures are 0.8 to 2.3 MiB per level** (the demo DGO is all textures).
  The memory plan already sends textures to VRAM, which needs no main-RAM copy.
- **The rest of a level is small.** Collision is 0.04 to 0.6 MiB, entities under
  0.1 MiB (plus their share of "other"), code under 0.5 MiB. Visibility data is
  up to 0.9 MiB and lighting palettes up to 0.6 MiB.
- **Art groups are 0.3 to 2.6 MiB per level**: models and their animations, which
  calc-animation-from-spr decompresses (the one live mips2c function without a
  case).
- At these sizes no level fits beside the engine as the PS2 lays it out: the
  arena has about 2.8 MiB free after GAME.CGO (10,673,360 of 13,631,488 bytes
  used). A level design has to start from converted geometry and VRAM textures,
  and measure again. The native walking prototype already streams windows of
  training's converted terrain ([TRAINING_STREAM.md](TRAINING_STREAM.md)); a whole
  converted level, beside the GOAL engine, is the next measurement.
