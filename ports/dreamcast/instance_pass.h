/* Instanced TIE/shrub pass over a DCI1 package (AI-assisted), ISC.
 *
 * DCI1 (prototype_geometry.py): 'DCI1', prototype count, instance count; then
 * per prototype vertex count, index count, first vertex, first index, material,
 * a 32-byte name; then 24-byte vertices (float xyz meters in the prototype frame,
 * float uv, ARGB8888); u16 indices, three a triangle, padded to 4 bytes; and 52-byte instances (u16
 * prototype, u16 pad, a 3x4 float matrix: rows 0-2 then the translation, meters).
 * world = x row0 + y row1 + z row2 + translation.
 *
 * DCI2 (tie_lods.py) adds TIE's lower LODs: 'DCI2', prototype, geometry and
 * instance counts; the geometry table (DCI1 prototype records); a prototype table
 * of u16 geometry per LOD 0-3 (0xffff none, LOD 0 set) and float near, mid and far
 * planes (meters), and u8 extra geometries per LOD (LOD n draws geometry[n] and
 * the count[n] after it, one per material); then vertices, indices and instances
 * as DCI1. An
 * instance draws LOD n where n counts the planes p with d2 > p*p (d2 the draw
 * rule's squared horizontal distance from the camera target), stepped down to the
 * nearest geometry that exists. A DCI1 package is one LOD a prototype. */
#ifndef DC_INSTANCE_PASS_H
#define DC_INSTANCE_PASS_H
#include <stddef.h>
#include <stdint.h>

typedef struct {uint32_t verts,indices,first_vert,first_index,material;char name[32];} dc_instance_proto;
typedef struct {float x,y,z,u,v;uint32_t argb;} dc_instance_vertex;
typedef struct {uint16_t proto,pad;float m[12];} dc_instance;
typedef struct {uint16_t geometry[4];float plane[3];uint8_t count[4];} dc_instance_lod;
typedef struct {
 const dc_instance_proto *protos;const dc_instance_vertex *verts;const uint16_t *indices;const dc_instance *instances;
 uint32_t proto_count,instance_count,vert_count,index_count,max_proto_verts;
 const dc_instance_lod *lods;uint32_t lod_count; /* DCI2: lods[lod_count] index protos (the geometries); DCI1: NULL */
} dc_instance_package;

/* Validates a whole package in place (4-byte aligned, the caller keeps it alive):
 * sizes add up exactly, every prototype's vertex and index ranges lie inside the
 * arrays, its index count is a multiple of three, every index names one of its
 * own vertices, every instance names a prototype, and every float is finite.
 * Returns NULL and fills out, or a reason with out unchanged. */
const char *dc_instance_package_open(const void *data,size_t size,dc_instance_package *out);

typedef struct {float tx,ty,tz,yaw,pitch,distance,draw_distance;} dc_instance_camera;
typedef struct {uint32_t instances,drawn_instances,triangles,submitted,near_clipped,culled,invalid,lod_instances[4];} dc_instance_stats;

/* The camera of render_world_view.c (dc_render_world_prepare): a point's offset
 * from the target, rotated by yaw and pitch and scaled by DC_RENDER_WORLD_SCALE,
 * pushed distance along view z. basis is three rows of three, then the z offset.
 * Exposed for host tests. */
void dc_instance_view(const dc_instance_camera *,float basis[10]);
void dc_instance_to_view(const float basis[10],const dc_instance_camera *,const float world[3],float view[3]);
/* The geometry (an index into protos) and LOD an instance draws at squared
 * horizontal distance d2; for DCI1 the instance's own prototype at LOD 0. */
uint32_t dc_instance_geometry(const dc_instance_package *,const dc_instance *,float d2,unsigned *lod);
/* As dc_instance_geometry, also giving how many geometries the LOD runs to. */
uint32_t dc_instance_run(const dc_instance_package *,const dc_instance *,float d2,unsigned *lod,unsigned *count);

#ifdef _arch_dreamcast
/* Draws every instance whose translation lies within the camera's draw distance
 * (horizontally, meters) into the open PVR_LIST_OP_POLY list as untextured,
 * double-sided Gouraud triangles in the package's vertex colours: each instance's
 * prototype goes to view space once, then each triangle is clipped to the near
 * plane and viewport by dc_render_clip_viewport and projected as render_base.c
 * does (x=320+390x/z, y=260-390y/z). scratch holds max_proto_verts view-space
 * vertices. Returns 0 on invalid arguments or an invalid clip. */
int dc_instance_draw(const dc_instance_package *,const dc_instance_camera *,float (*scratch)[3],
                     dc_instance_stats *);
/* A material's PVR header and list for the textured draw (header NULL: none). */
typedef struct {const void *header;uint32_t list;} dc_instance_material;
/* The textured draw: geometries whose material (an index into materials) has a
 * header in this list are drawn with it and their UVs; a geometry whose material
 * has none is drawn untextured in the opaque list. Instance and LOD counts are
 * taken on the PVR_LIST_OP_POLY call only, triangles on every call, so one
 * opaque and one punch-through call count each instance once and each triangle
 * once. Faces stay double-sided, as in dc_instance_draw. */
int dc_instance_draw_list(const dc_instance_package *,const dc_instance_camera *,float (*scratch)[3],
                          dc_instance_stats *,int list,const dc_instance_material *materials,uint32_t material_count);
#endif
#endif
