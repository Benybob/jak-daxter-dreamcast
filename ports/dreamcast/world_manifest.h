/* Bounded native training-map transform/file contract (AI-assisted), ISC. */
#ifndef DC_WORLD_MANIFEST_H
#define DC_WORLD_MANIFEST_H
#include "textured_scene.h"
#include "world_collision.h"

/* WLD2: exactly 84 little-endian bytes, never a native struct dump.
 * magic, version; origin[3], shift[3], scale, spawn[3] (float32);
 * DCT bytes/CRC32, DCC bytes/CRC32, vertex/index/batch/texture/collision counts.
 * local meters = normalized DCT position * scale + shift. DCC is already local.
 * CRCs detect accidentally mixed private outputs; they are not authentication. */
typedef struct {
 dc_vec3 origin,shift;
 float scale;
 dc_vec3 spawn;
 uint32_t dct_bytes,dct_crc,dcc_bytes,dcc_crc;
 uint32_t vertices,indices,batches,textures,triangles;
} dc_world_manifest;

/* Loading/validation failure leaves the output unchanged. */
const char *dc_world_manifest_load(FILE *,dc_world_manifest *);
/* Seekable file is rewound on success; fixed 512-byte scratch, no allocation. */
const char *dc_world_manifest_check_file(FILE *,uint32_t,uint32_t);
/* Loaded scene/world must still be caller-owned; does not consume either. */
const char *dc_world_manifest_match(const dc_world_manifest *,const dc_textured_scene *,
                                   const dc_collision_world *,dc_vec3);
#endif
