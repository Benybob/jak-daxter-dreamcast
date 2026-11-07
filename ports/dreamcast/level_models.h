/* Skinned merc models of the level ELF's processes (AI-assisted), ISC.
 *
 * Each frame dc_level_models_frame walks the original process tree from
 * *active-pool* and keeps every process-drawable (Jak included) whose draw
 * control's merc model (mgeo name, e.g. "eichar-lod0") is in the disc bundle
 * (make_skin_models.py, DSKB), is not hidden and lies within the draw distance
 * and the viewport. A model is loaded from the bundle the first time a process
 * shows it: its geometry to the KOS heap, its textures to VRAM.
 *
 * Skinning uses only the game's own data: for each cspace of the process's
 * node-list with a joint, the joint's bind-pose times the cspace bone's
 * transform (row vectors, game units) is the matrix of joint number n. A
 * vertex is the weighted sum over its up to three joints; translations are
 * divided by 4096 to the renderer's meters. At a model's first use its
 * bundled inverse bind matrices (from the GLB) are compared with the game's
 * bind-poses, which proves the bundle's joint numbers are the game's.
 *
 * dc_level_models_draw draws the kept processes' batches of one PVR list
 * (opaque, punch-through or translucent) with the instance pass camera,
 * clipped and projected as instance_pass.c does. --dump dc_level_model_metrics 128 */
#ifndef DC_LEVEL_MODELS_H
#define DC_LEVEL_MODELS_H
#include <stdint.h>
#include "goal_runtime.h"
#include "instance_pass.h"

typedef struct {
 uint32_t frames,drawables,kept,max_kept,drawn,triangles,max_triangles,loaded;
 uint32_t geometry_bytes,texture_bytes,load_failures,missing,joint_misses,culled,jak_frames,actor_frames;
 uint32_t bind_checked,bind_error_ppm,heap_free_before,heap_free_after,invalid,skin_us_max,last_kept,last_missing;
 char missing_name[32]; /* the first merc model a process showed that the bundle lacks */
} dc_level_model_metrics_t;
extern volatile dc_level_model_metrics_t dc_level_model_metrics;

/* Opens the bundle (path on the disc); 0 on a malformed bundle. */
int dc_level_models_open(const char *path);
/* Collects this frame's drawn processes. */
int dc_level_models_frame(dc_goal_runtime *rt,const dc_instance_camera *cam);
/* Draws their batches in list (the list is open). */
int dc_level_models_draw(dc_goal_runtime *rt,const dc_instance_camera *cam,int list);
/* After the scene: counts the frame toward jak_frames and actor_frames. */
void dc_level_models_end_frame(void);
#endif
