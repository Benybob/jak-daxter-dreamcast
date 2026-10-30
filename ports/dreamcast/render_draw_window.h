/* A terrain window of shared vertices in fixed buffers (AI-assisted), ISC. */
#ifndef DC_RENDER_DRAW_WINDOW_H
#define DC_RENDER_DRAW_WINDOW_H
#include "render_world_view.h"

#define DC_RENDER_DRAW_VERTEX_BYTES 24u
/* id: source face, ascending; v: vertices; material: catalog material. */
typedef struct {uint32_t id;uint16_t v[3];uint16_t material;} dc_render_draw_face;
/* Every buffer is allocated once by make, so retargets cannot move the heap's
 * top; faces and vertices are rewritten in place. Never free or rewrite while
 * PVR or a frame still borrows them. */
typedef struct {
 dc_render_draw_face *faces;unsigned char *vertices;dc_render_vertex *view;
 uint16_t *order,*remap;
 uint32_t count,vertex_count,face_cap,vertex_cap,remap_cap,catalog_crc,valid,prepared;
 dc_render_query coverage;
} dc_render_draw_window;
/* Zeroed/unowned output. face_cap 1..65535, vertex_cap 1..65535, remap_cap 1..
 * 65535: vertices of every block a window touches, together, which it must hold.
 * Failure leaves the output zero. */
const char *dc_render_draw_window_make(dc_render_draw_window *,uint32_t face_cap,
    uint32_t vertex_cap,uint32_t remap_cap);
void dc_render_draw_window_free(dc_render_draw_window *);
/* Scalar floats, the matrix of render_world_view.c's scalar build: one view
 * entry per shared vertex. Returns 1, or 0 on a bad camera with view unwritten.
 * Any failure, and any rebuild of the window, leaves it unprepared, and a frame
 * over an unprepared window refuses to draw. */
int dc_render_draw_window_prepare(dc_render_draw_window *,const dc_render_camera *);
/* The 116-byte record of a face with zero normals, as a checked window holds. */
void dc_render_draw_window_face(const dc_render_draw_window *,uint32_t index,unsigned char *out);
#endif
