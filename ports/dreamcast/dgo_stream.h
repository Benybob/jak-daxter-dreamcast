/* Experimental Dreamcast asset probe. See the repository ISC license. */
#ifndef JAK_DC_DGO_STREAM_H
#define JAK_DC_DGO_STREAM_H

#include <stdint.h>
#include <stdio.h>

#define DC_DGO_BUFFER_BYTES 4096u
#define DC_DGO_MAX_OBJECTS 65536u

typedef struct {
  char name[60];
  uint32_t index, size, crc32;
  uint64_t data_offset;
} dc_dgo_object;

typedef struct {
  char name[60];
  uint32_t declared_objects, completed_objects, largest_object;
  uint64_t bytes_read, payload_bytes;
} dc_dgo_stats;

typedef void (*dc_dgo_visitor)(const dc_dgo_object *object, void *context);

/* Strict DGO container validation only. Never links or executes object data.
 * Reads sequentially with a fixed 4 KiB payload buffer and no heap allocation.
 * Visitor results are provisional until NULL (success) is returned.
 * stats must be non-NULL; partial stats are retained on failure.
 */
const char *dc_dgo_scan(FILE *file, dc_dgo_stats *stats,
                        dc_dgo_visitor visitor, void *context);

#endif
