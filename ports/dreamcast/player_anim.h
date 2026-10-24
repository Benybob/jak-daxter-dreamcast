/* Bounded private baked-pose input (AI-assisted), ISC. */
#ifndef DC_PLAYER_ANIM_H
#define DC_PLAYER_ANIM_H
#include <stdint.h>
#include <stdio.h>
typedef struct {uint32_t first,count;float duration;uint32_t loop;} dc_player_clip;
typedef struct {uint32_t vertices,frames;dc_player_clip clips[6];int16_t *poses;} dc_player_anim;
const char *dc_player_anim_load(FILE *,uint32_t,dc_player_anim *);
void dc_player_anim_free(dc_player_anim *);
/* Output three local floats/vertex in exported 1/4 world units. */
void dc_player_anim_sample(const dc_player_anim *,unsigned,float,float *);
#endif
