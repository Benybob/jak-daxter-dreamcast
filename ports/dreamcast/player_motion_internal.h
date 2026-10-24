/* Shared drive/animation rules for synthetic and triangle worlds (AI-assisted). */
#ifndef DC_PLAYER_MOTION_INTERNAL_H
#define DC_PLAYER_MOTION_INTERNAL_H
#include "player_motion.h"
int dc_player_drive(dc_player *,float,float,int,float *);
void dc_player_finish_tick(dc_player *,float);
#endif
