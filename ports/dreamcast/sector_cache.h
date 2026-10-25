/* Native DCS1 sector cache for the prototype collision world (AI-assisted), ISC.
 * Not an execution context and not a claim of original GOAL fidelity.
 * One owner thread. No callbacks, no concurrent retarget, and no pointers
 * retained into world->triangles across a replacing retarget. */
#ifndef DC_SECTOR_CACHE_H
#define DC_SECTOR_CACHE_H

#include <stdint.h>
#include <stdio.h>
#include "world_collision.h"

typedef struct {
  float min_x, max_x, min_z, max_z;
} dc_sector_bounds;

typedef struct {
  int32_t ix, iz;
  uint32_t count, crc, bytes;
} dc_sector_entry;

typedef struct {
  uint32_t source_count, source_crc, package_crc, count;
  float cell_size;
  int32_t ix_min, ix_max, iz_min, iz_max;
  uint32_t halo;
  dc_sector_entry *entries;
} dc_sector_index;

typedef struct {
  dc_collision_world world;
  dc_vec3 origin;
  dc_sector_bounds coverage;
  uint32_t valid, package_crc;
} dc_sector_cache;

/* Caller must pass a zeroed/unowned index (free an existing index first).
 * Zeroes *out first. On failure *out is all zeroes and nothing is allocated.
 * Does not close file. package_crc is the validated manifest footer. */
const char *dc_sector_index_load(FILE *file, dc_sector_index *out);
/* Frees entries and zeroes *index. NULL and repeated calls are safe. */
void dc_sector_index_free(dc_sector_index *index);
/* Nonzero when cache is a committed ready world (including known air) and
 * query is a finite ordered GEOMETRY box inside that coverage. Include the
 * complete swept capsule (radius and solver skin), not just its center path. */
int dc_sector_covers(const dc_sector_cache *cache, dc_sector_bounds query);
/* Conservative XZ geometry bounds for world start/end capsule feet, including
 * radius, the solver's .002m skin and one outward float ULP. No output on error.
 * Union these bounds with other intended queries (e.g. camera) before retarget. */
int dc_sector_swept_bounds(dc_vec3 start,dc_vec3 end,float radius,dc_sector_bounds *out);
/* NULL on success. Rejects bad arguments before reuse. Reuse keeps the
 * committed origin. The cache must be zeroed before its first retarget. */
const char *dc_sector_retarget(const dc_sector_index *index, const char *root,
                               dc_sector_bounds query, dc_vec3 origin,
                               dc_sector_cache *cache);
/* Frees the collision world and zeroes *cache. NULL and repeated calls are safe. */
void dc_sector_cache_free(dc_sector_cache *cache);

#endif
