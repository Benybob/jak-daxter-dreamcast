/* Portable refusal checks used by the live renderer (AI-assisted), ISC. */
#include "arena_live_assets.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
static int ledger_valid(const dc_live_cell_entry *ledger,uint32_t count){
 uint32_t i,j;if(!ledger||!count||count>4096)return 0;
 for(i=0;i<count;++i){
  if(ledger[i].ix < -62500 || ledger[i].ix>62500 || ledger[i].iz < -62500 || ledger[i].iz>62500 ||
     !ledger[i].count||ledger[i].count>65536)return 0;
  for(j=0;j<i;++j)if(ledger[i].ix==ledger[j].ix&&ledger[i].iz==ledger[j].iz)return 0;
 }
 return 1;
}
int dc_live_ledger_files(const char *root,const dc_live_cell_entry *ledger,uint32_t count){
 uint32_t i;unsigned char data[512];char path[256];
 if(!root||strlen(root)>200||!ledger_valid(ledger,count))return 0;
 for(i=0;i<count;++i){
  FILE *f;size_t n,total=0;uint32_t crc=0;int ok;
  snprintf(path,sizeof path,"%s/cells/%ld_%ld.dci",root,(long)ledger[i].ix,(long)ledger[i].iz);
  f=fopen(path,"rb");if(!f)return 0;
  while((n=fread(data,1,sizeof data,f))!=0){
   crc=crc32(crc,data,(unsigned)n);total+=n;
   if(total>(size_t)ledger[i].count*52u){fclose(f);return 0;}
  }
  ok=!ferror(f)&&total==(size_t)ledger[i].count*52u&&crc==ledger[i].crc;
  if(fclose(f)||!ok)return 0;
 }
 return 1;
}
int dc_live_ledger_cells(const dc_instance_cells *cells,const dc_live_cell_entry *ledger,uint32_t count){
 uint32_t i,j,total=0;
 if(!cells||cells->cell_count>DC_INSTANCE_CELLS_MAX||!ledger_valid(ledger,count))return 0;
 for(i=0;i<cells->cell_count;++i){
  uint32_t expected=0;
  for(j=0;j<count;++j)if(cells->cells[i].ix==ledger[j].ix&&cells->cells[i].iz==ledger[j].iz){expected=ledger[j].count;break;}
  if(expected!=cells->cells[i].count||expected>cells->capacity||total>cells->capacity-expected)return 0;
  total+=expected;
 }
 return total==cells->count;
}
static int finite_double(double d){uint64_t b;memcpy(&b,&d,8);return (b&UINT64_C(0x7fffffffffffffff))<UINT64_C(0x7ff0000000000000);}
int dc_live_coverage(const dc_game_camera *c,const dc_render_query *q,const double crop[4],const float initial[3],float half,float draw){
 unsigned i;
 if(!c||!q||!crop||!initial||!dc_live_bounded(half,18)||half<=0||!dc_live_bounded(draw,128)||draw<=0)return 0;
 for(i=0;i<3;++i)if(!dc_live_bounded(c->target[i],1000000)||!dc_live_bounded(initial[i],1000000))return 0;
 for(i=0;i<4;++i)if(!finite_double(crop[i])||fabs(crop[i])>1000000)return 0;
 if(!finite_double(q->x0)||!finite_double(q->x1)||!finite_double(q->z0)||!finite_double(q->z1))return 0;
 return crop[0]<crop[1]&&crop[2]<crop[3]&&q->x0<=q->x1&&q->z0<=q->z1&&
        q->x0>=crop[0]&&q->x1<=crop[1]&&q->z0>=crop[2]&&q->z1<=crop[3]&&
        fabsf(c->target[0]-initial[0])<=6&&fabsf(c->target[2]-initial[2])<=6&&fabsf(c->target[1]-initial[1])<=1;
}
