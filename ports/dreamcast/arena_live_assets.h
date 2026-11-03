/* Live renderer asset coverage/occupied-cell ledger (AI-assisted), ISC. */
#ifndef DC_ARENA_LIVE_ASSETS_H
#define DC_ARENA_LIVE_ASSETS_H
#include "arena_live_math.h"
#include "instance_cells.h"
typedef struct {int32_t ix,iz;uint32_t count,crc;} dc_live_cell_entry;
int dc_live_ledger_files(const char *,const dc_live_cell_entry *,uint32_t);
int dc_live_ledger_cells(const dc_instance_cells *,const dc_live_cell_entry *,uint32_t);
int dc_live_coverage(const dc_game_camera *,const dc_render_query *,const double [4],const float [3],float,float);
#endif
