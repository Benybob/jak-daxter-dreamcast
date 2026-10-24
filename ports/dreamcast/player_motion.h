/* Native test-map controller, not original GOAL movement (AI-assisted), ISC. */
#ifndef DC_PLAYER_MOTION_H
#define DC_PLAYER_MOTION_H
#include <stdint.h>
enum { DC_PLAYER_IDLE, DC_PLAYER_WALK, DC_PLAYER_RUN, DC_PLAYER_AIR };
typedef struct {
 float x,y,z,vx,vy,vz,yaw,anim_time;
 uint32_t grounded,jump_held,mode,jumps,landings,ticks;
} dc_player;
typedef struct { float x0,x1,z0,z1,top; } dc_test_platform;
extern const dc_test_platform dc_test_platforms[2];
void dc_player_init(dc_player *);
/* One 1/60 second simulation tick. World-space stick direction, radial analog
 * magnitude; held jump is edge-triggered. Invalid input leaves state unchanged.
 * Caller owns a valid initialized player; geometry is the fixed test map. */
int dc_player_tick(dc_player *,float,float,int);
#endif
