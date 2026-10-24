/* Serial KOS GPU layer for the bounded training prototype (AI-assisted), ISC. */
#ifndef DC_TRAINING_LAYER_H
#define DC_TRAINING_LAYER_H
#include <kos.h>
#include "textured_scene.h"
#include "scene_projection.h"
typedef struct {
 pvr_poly_hdr_t *headers;
 pvr_ptr_t *textures;
 dc_render_vertex *vertices;
 uint32_t texture_count;
} dc_training_layer;
/* PVR initialized, validated scene, empty zeroed layer. Success consumes CPU
 * texture pixels. Failure empties the layer; unconsumed pixels remain scene-owned.
 * Does not own the scene's other arrays. A nonempty layer is refused unchanged. */
int dc_training_layer_make(dc_training_layer *,dc_textured_scene *);
/* Caller waits for PVR first and shuts it down afterward. Idempotent. */
void dc_training_layer_free(dc_training_layer *);
/* Caller prepares view/projected vertices first. Serial/non-reentrant, 3KiB
 * scratch. Mixed near-plane triangles are clipped before division, then fanned. */
void dc_training_layer_draw(const dc_training_layer *,const dc_textured_scene *,int);
#endif
