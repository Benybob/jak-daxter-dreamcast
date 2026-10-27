/* A zstd-compressed DGO read as a stdio stream (AI-assisted), ISC.
 * The romdisk test ELFs carry GAME.CGO compressed: 4.9 MB of DCSH objects is
 * about 0.66 MB as zstd, so the copy resident in RAM costs an eighth as much.
 * Decompression uses the vendored third-party/zstd decompressor; the build
 * limits the window to 128 KiB, so streaming memory stays bounded. The stream
 * is read-only and sequential: exactly what dc_goal_load_dgo reads. */
#ifndef DC_GOAL_DGO_ZST_H
#define DC_GOAL_DGO_ZST_H
#include <stdio.h>
/* NULL if the file is missing or unreadable. fclose releases the decoder and
 * the underlying file. A corrupt or truncated frame reads as an error. */
FILE *dc_goal_open_zst(const char *path);
#endif
