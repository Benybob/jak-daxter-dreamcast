/* Bounded DRM2/DRS2 CPU reader (AI-assisted), ISC. No GPU/cache ownership. */
#ifndef DC_RENDER_PACKAGE_H
#define DC_RENDER_PACKAGE_H
#include <stdint.h>
#include <stdio.h>

enum { DC_RENDER_FACE_BYTES=116, DC_RENDER_VERTEX_BYTES=36,
       DC_RENDER_FILE_CAP=4096, DC_RENDER_WINDOW_CAP=8192 };
#define DC_RENDER_ABSENT_TEXTURE UINT32_C(0xffffffff)
typedef struct {
  unsigned char *storage;
  uint32_t bytes,source_count,sector_count,global_count,replica_count;
  uint32_t material_count,texture_count,texture_bytes,package_bytes,crc;
  uint32_t pixel_offset,material_offset,entry_offset;
  float cell_size;
} dc_render_catalog;
typedef struct {
  int32_t ix,iz;
  uint32_t count,raw_bytes,file_bytes,payload_crc,compressed_crc;
} dc_render_entry;
typedef struct {
  uint32_t width,height,format,bytes;
  const unsigned char *pixels; /* borrowed from catalog; invalid after free */
} dc_render_texture;
typedef struct {
  uint32_t base_texture,base_flags,alpha_mode,strength_texture,strength_flags;
  uint32_t spec_texture,spec_flags;
  float cutoff,metallic,roughness,specular,spec_color[3];
} dc_render_material;
typedef struct {
  unsigned char *records; /* count * 116 exact little-endian bytes */
  uint32_t count;
} dc_render_block;

/* Caller provides zeroed/unowned output. Owned output is refused unchanged.
 * Failure leaves unowned output zero, releases all temporary allocations.
 * Neither function closes FILE; caller must check close before committing.
 * No external file data or structs may change concurrently. */
const char *dc_render_catalog_load(FILE *,dc_render_catalog *);
void dc_render_catalog_free(dc_render_catalog *);
int dc_render_entry_get(const dc_render_catalog *,uint32_t,dc_render_entry *);
int dc_render_global_get(const dc_render_catalog *,dc_render_entry *);
int dc_render_texture_get(const dc_render_catalog *,uint32_t,dc_render_texture *);
int dc_render_material_get(const dc_render_catalog *,uint32_t,dc_render_material *);
/* Entry must be an exact member of this catalog; global is explicit.
 * Reads/CRC-checks exact file and inflater framing, then validates every face:
 * source/material ids, ordering, finite positions/UV, unit-or-zero normals.
 * Does not prove spatial coverage or cross-sector replica equality. Those are
 * the future cache's responsibility. An empty global block is valid/unowned. */
const char *dc_render_block_load(FILE *,const dc_render_catalog *,
                                const dc_render_entry *,int global,dc_render_block *);
void dc_render_block_free(dc_render_block *);
#endif
