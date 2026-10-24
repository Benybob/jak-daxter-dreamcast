/* Known-empty sector versus unloaded data (AI-assisted), ISC. */
#include "world_collision.h"
#include "camera_boom.h"
#include "player_world_motion.h"
#include <string.h>
unsigned dc_sector_air_cases(unsigned *count){
 dc_collision_world world={0},saved;dc_collision_result hit,before;dc_player player,old;
 uint32_t events[2]={123,456};float distance=123;unsigned n=0,bad=0,i;
#define CHECK(x) do{++n;if(!(x))++bad;}while(0)
 memset(&hit,0x5a,sizeof hit);before=hit;
 CHECK(!dc_collision_clear(&world,(dc_vec3){0},.28f,1.7f));
 CHECK(!dc_collision_move(&world,(dc_vec3){0},(dc_vec3){1,0,0},.28f,1.7f,&hit));
 CHECK(!memcmp(&hit,&before,sizeof hit));
 CHECK(!dc_collision_surface_events(&world,events)&&events[0]==123&&events[1]==456);
 CHECK(!dc_camera_boom_query(&world,(dc_vec3){0},(dc_vec3){0,0,-1},10.4f,&distance)&&distance==123);
 CHECK(dc_collision_empty(&world)==NULL);CHECK(world.ready==1&&world.count==0&&!world.triangles);
 saved=world;CHECK(dc_collision_empty(&world)!=NULL);CHECK(!memcmp(&world,&saved,sizeof world));
 CHECK(dc_collision_clear(&world,(dc_vec3){0},.28f,1.7f));
 CHECK(dc_collision_surface_events(&world,events)&&events[0]==0&&events[1]==0);
 CHECK(dc_collision_move(&world,(dc_vec3){2,3,4},(dc_vec3){1,-1,2},.28f,1.7f,&hit));
 CHECK(hit.position.x==3&&hit.position.y==2&&hit.position.z==6&&!hit.contacts&&!hit.grounded);
 CHECK(dc_collision_cast(&world,(dc_vec3){0},(dc_vec3){0,-4,0},.28f,1.7f,&hit));
 CHECK(hit.position.y==-4&&!hit.contacts&&!hit.grounded);
 CHECK(dc_camera_boom_query(&world,(dc_vec3){0},(dc_vec3){0,0,-1},10.4f,&distance)&&distance==10.4f);
 dc_player_init(&player);player.y=2;player.grounded=0;old=player;
 CHECK(!dc_player_world_place(&player,&world,(dc_vec3){0,3,0}));CHECK(!memcmp(&player,&old,sizeof player));
 for(i=0;i<60;++i)CHECK(dc_player_world_tick(&player,&world,0,0,0,&hit));
 CHECK(player.y<0&&!player.grounded&&player.landings==0);
 dc_collision_free(&world);CHECK(!world.ready&&!world.triangles&&!world.count);
 CHECK(!dc_collision_clear(&world,(dc_vec3){0},.28f,1.7f));
 *count=n;return bad;
#undef CHECK
}
