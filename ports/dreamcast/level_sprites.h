/* The level ELF's particles (AI-assisted), ISC.
 *
 * The original particle system keeps each particle in a slot of
 * *sp-particle-system-2d* (2048 slots): a cpuinfo (144 bytes, valid at +128),
 * its sprite (48 bytes: x y z sx, flag matrix rot sy, r g b a) and its adgif
 * (80 bytes, tex0 at +0, alpha at +64). The port has no texture pages, so
 * dc_level_sprites_install puts a native lookup-texture-by-id in front of the
 * original: for a texture SPRITES.DSK (make_sprite_textures.py) has, it
 * answers a stand-in texture whose tex0 base marks which, and every adgif made
 * from it, or copied from one, carries the mark to its particle's slot.
 * It also puts a native spawn in front of sparticle-launch-control's, which
 * spawns only near the camera. dc_level_sprites_draw draws each valid slot
 * whose texture is known as a camera-facing quad in the translucent list. --dump dc_level_sprite_metrics 64 */
#ifndef DC_LEVEL_SPRITES_H
#define DC_LEVEL_SPRITES_H
#include <stdint.h>
#include "goal_runtime.h"
#include "goal_module.h"
#include "instance_pass.h"

typedef struct {
  uint32_t textures,texture_bytes,recorded,unknown_slot,frames,drawn,valid,no_texture;
  uint32_t screen,behind,last_drawn,max_drawn,load_failures,headers,spawned,culled;
  float sample[12];uint32_t sample_texture,sample_alpha;float sample_view[3],pad2;  /* one drawn particle, for tests */
  uint16_t live[64],shown[64];  /* this frame's valid particles by texture number, and those drawn */
} dc_level_sprite_metrics_t;
extern volatile dc_level_sprite_metrics_t dc_level_sprite_metrics;

/* Native lookup-texture-by-id and spawn in front of the originals; 0 when they are not there. */
int dc_level_sprites_install(dc_goal_runtime *rt,dc_goal_cache_sync sync);
/* SPRITES.DSK into VRAM; 0 on a malformed file. */
int dc_level_sprites_open(const char *path);
/* Valid particles of the 2d system, in the translucent list (it is open). */
int dc_level_sprites_draw(dc_goal_runtime *rt,const dc_instance_camera *cam);
#endif
