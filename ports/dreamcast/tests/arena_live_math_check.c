/* Host fixtures, NOT original GOAL execution (AI-assisted), ISC. */
#include "arena_live.h"
#include "arena_live_assets.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static float bits(uint32_t b){float f;memcpy(&f,&b,4);return f;}
static dc_live_pose make_pose(float yaw,float pitch,float x,float y,float z){
 dc_live_pose p={{0},{0},{0}};unsigned i,j;
 float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
 const float basis[16]={cy,0,sy,0,sp*sy,cp,-sp*cy,0,-cp*sy,sp,cp*cy,0,x,y,z,1};
 memcpy(p.inv,basis,64);p.trans[0]=x;p.trans[1]=y;p.trans[2]=z;p.trans[3]=1;
 for(i=0;i<3;++i)for(j=0;j<3;++j)p.rot[4*i+j]=p.inv[4*j+i];
 for(i=0;i<3;++i){for(j=0;j<3;++j)p.rot[12+i]-=p.inv[4*i+j]*p.trans[j];}
 p.rot[15]=1;return p;
}
static void conversion(void){
 unsigned k;dc_live_pose p;dc_game_camera c;
 for(k=0;k<8;++k){
  float yaw=(float)k*.78539816f-3.14159265f,eye[3]={10,-20,30};
  float points[3][6]={{17,-12,36,0,0,0},{-4,-23,51,0,0,0},{11,-6,22,0,0,0}};
  dc_view_vertex v[3];dc_render_draw_window w={0};dc_render_camera rc;dc_instance_camera ic;
  p=make_pose(yaw,.7f,40960,-81920,122880);
  CHECK(dc_live_convert(&p,20,&c));CHECK(fabsf(c.eye[0]-eye[0])<.0001f&&c.eye[1]==-20&&c.eye[2]==30);
  CHECK(fabsf(sinf(c.yaw)-sinf(yaw))<.0001f&&fabsf(c.pitch-.7f)<.0001f);
  rc=(dc_render_camera){{c.target[0],c.target[1],c.target[2]},c.yaw,c.pitch,c.distance};
  ic=(dc_instance_camera){c.target[0],c.target[1],c.target[2],c.yaw,c.pitch,c.distance,80};
  w.valid=1;w.vertex_count=3;w.vertices=(unsigned char *)points;w.view=v;
  CHECK(dc_render_draw_window_prepare(&w,&rc));CHECK(dc_live_terrain_matches(&p,&w));CHECK(dc_live_instance_matches(&p,&ic));
  rc.yaw+=3.14159265f;CHECK(dc_render_draw_window_prepare(&w,&rc));CHECK(!dc_live_terrain_matches(&p,&w));
  ic.yaw+=3.14159265f;CHECK(!dc_live_instance_matches(&p,&ic));ic.yaw=c.yaw;
  ic.pitch=-c.pitch;CHECK(!dc_live_instance_matches(&p,&ic));ic.pitch=c.pitch;
  ic.tx+=1;CHECK(!dc_live_instance_matches(&p,&ic));
  rc.yaw=c.yaw;rc.pitch=-c.pitch;CHECK(dc_render_draw_window_prepare(&w,&rc));CHECK(!dc_live_terrain_matches(&p,&w));
  rc.pitch=c.pitch;rc.target.x+=1;CHECK(dc_render_draw_window_prepare(&w,&rc));CHECK(!dc_live_terrain_matches(&p,&w));
  w.vertex_count=0;CHECK(!dc_live_terrain_matches(&p,&w));
 }
 p=make_pose(2.2f,.7f,40960,-81920,122880);CHECK(dc_live_convert(&p,20,&c));
 CHECK(fabsf(c.target[0]+2.367449f)<.0001f&&fabsf(c.target[1]+7.115646f)<.0001f&&fabsf(c.target[2]-20.997795f)<.0001f);
 CHECK(dc_live_camera_matches(&p,&c));
 for(k=0;k<9;++k){dc_game_camera bad=c;float *lane;
  lane=k<3?bad.eye+k:k<6?bad.target+k-3:k==6?&bad.yaw:k==7?&bad.pitch:&bad.distance;
  *lane=bits(0x7fc00000);CHECK(!dc_live_camera_matches(&p,&bad));
 }
 {dc_game_camera bad=c;bad.target[0]+=1;CHECK(!dc_live_camera_matches(&p,&bad));
  bad=c;bad.eye[0]+=1;CHECK(!dc_live_camera_matches(&p,&bad));}
}
static void hostile(void){
 static const uint32_t bad[]={0x7fc12345,0x7f800000,0xff800000,0x7f7fffff};
 dc_live_pose good=make_pose(2.2f,-.45f,-1200000,23456,987654),p;dc_game_camera out,saved;
 unsigned i,k;memset(&saved,0xa5,sizeof saved);
 for(i=0;i<36;++i)for(k=0;k<4;++k){
  p=good;out=saved;
  if(i<4)p.trans[i]=bits(bad[k]);else if(i<20)p.inv[i-4]=bits(bad[k]);else p.rot[i-20]=bits(bad[k]);
  CHECK(!dc_live_convert(&p,55,&out));CHECK(!memcmp(&out,&saved,sizeof out));
 }
 for(i=0;i<16;++i){p=good;p.rot[i]+=i>=12&&i<15?50.f:.01f;CHECK(!dc_live_convert(&p,55,&out));}
 for(i=0;i<16;++i){p=good;p.inv[i]+=i>=12&&i<15?50.f:.01f;CHECK(!dc_live_convert(&p,55,&out));}
 for(i=0;i<4;++i){p=good;p.trans[i]+=2;CHECK(!dc_live_convert(&p,55,&out));}
 CHECK(!dc_live_convert(NULL,55,&out));CHECK(!dc_live_convert(&good,55,NULL));
 CHECK(!dc_live_convert(&good,-1,&out));CHECK(!dc_live_convert(&good,1001,&out));
 CHECK(!dc_live_convert(&good,bits(0x7fc00000),&out));CHECK(!dc_live_convert(&good,bits(0x7f800000),&out));
 p=make_pose(0,0,4096000000.f,0,0);CHECK(dc_live_convert(&p,0,&out));
 p=make_pose(0,0,0,0,4096000000.f);CHECK(!dc_live_convert(&p,55,&out));
 p=make_pose(.2f,1.57079632679f,0,0,0);CHECK(!dc_live_convert(&p,0,&out));
 p=make_pose(0,0,0,0,0);p.inv[0]=-1;p.rot[0]=-1;CHECK(!dc_live_convert(&p,0,&out));
 p=make_pose(0,0,0,0,0);p.inv[0]=.8f;p.inv[1]=.6f;p.inv[4]=-.6f;p.inv[5]=.8f;
 CHECK(!dc_live_convert(&p,0,&out));
 CHECK(dc_live_delta(&good,&good)==1);p=good;p.rot[0]=bits(0x7fc00000);CHECK(!dc_live_delta(&good,&p));
 p=make_pose(2.2f,-.45f,-1199000,23456,987654);CHECK(dc_live_delta(&good,&p)==2);
 p=make_pose(2.201f,-.45f,-1200000,23456,987654);CHECK(dc_live_delta(&good,&p)==4);
 p=make_pose(2.201f,-.45f,-1199000,23456,987654);CHECK(dc_live_delta(&good,&p)==6);
}
static unsigned char memory[524288];
static void memory_checks(void){
 dc_goal_runtime rt,broken;uint32_t type,camera,used,tag;unsigned i;
 dc_live_pose good=make_pose(2.2f,-.45f,-1200000,23456,987654),out,saved;
 unsigned char before[DC_GAME_CAMERA_BYTES];
 CHECK(dc_goal_init(&rt,memory,sizeof memory,0x8c200000));
 type=dc_goal_define_type(&rt,"math-camera",rt.basic_type,DC_GAME_CAMERA_BYTES,0,9);
 camera=dc_goal_new_basic(&rt,type,DC_GAME_CAMERA_BYTES);CHECK(type&&camera);
 memcpy(memory+camera-rt.base+DC_GAME_CAMERA_TRANS,good.trans,16);
 memcpy(memory+camera-rt.base+DC_GAME_CAMERA_INV,good.inv,64);
 memcpy(memory+camera-rt.base+DC_GAME_CAMERA_ROT,good.rot,64);
 CHECK(dc_live_read(&rt,camera,type,&out));CHECK(!memcmp(&out,&good,sizeof good));
 used=rt.used;memset(&saved,0xa5,sizeof saved);memcpy(before,memory+camera-rt.base-4,sizeof before);
 for(i=0;i<12;++i){
  broken=rt;out=saved;tag=camera;
  switch(i){
   case 0:broken.memory=NULL;break;case 1:broken.error=DC_GOAL_BAD_OBJECT;break;
   case 2:broken.used=broken.capacity+1;break;case 3:broken.base=UINT32_MAX-16;break;
   case 4:broken.used=camera-rt.base+1055;break;case 5:tag=camera+4;break;
   case 6:tag=0;break;case 7:tag=rt.base-12;break;case 8:tag=rt.base+used;break;
   case 9:tag=0xa5100004;break;case 10:tag=UINT32_MAX-11;break;
   case 11:broken.used=type-rt.base+12;break;
  }
  CHECK(!dc_live_read(&broken,tag,type,&out));CHECK(!memcmp(&saved,&out,sizeof out));
  CHECK(!dc_live_poison(&broken,tag,type,1));CHECK(!memcmp(before,memory+camera-rt.base-4,sizeof before));
 }
 tag=rt.basic_type;memcpy(memory+camera-rt.base-4,&tag,4);CHECK(!dc_live_read(&rt,camera,type,&out));
 memcpy(memory+camera-rt.base-4,&type,4);tag=rt.basic_type;memcpy(memory+type-rt.base-4,&tag,4);
 CHECK(!dc_live_read(&rt,camera,type,&out));tag=rt.type_type;memcpy(memory+type-rt.base-4,&tag,4);
 tag=1061;memcpy(memory+type-rt.base+8,&tag,4);CHECK(!dc_live_read(&rt,camera,type,&out));
 tag=1060;memcpy(memory+type-rt.base+8,&tag,4);
 CHECK(!dc_live_poison(&rt,camera,type,0));CHECK(!dc_live_poison(&rt,camera,type,182));
 CHECK(dc_live_poison(&rt,camera,type,1));CHECK(!dc_live_read(&rt,camera,type,&out));
 for(i=0;i<36;++i){uint32_t word,offset=i<4?DC_GAME_CAMERA_TRANS+4*i:i<20?DC_GAME_CAMERA_INV+4*(i-4):DC_GAME_CAMERA_ROT+4*(i-20);
  memcpy(&word,memory+camera-rt.base+offset,4);CHECK(word==(0x7fc00100u|i));}
 CHECK(rt.used==used);
 CHECK(dc_live_poison(&rt,camera,type,61));
}
static void turn_contract(void){
 static const float ratios[]={1,1.2f,2,3,4,4.8f};unsigned i;
 for(i=0;i<sizeof ratios/sizeof ratios[0];++i){
  float angle=.010471975512f*ratios[i];
  CHECK(dc_live_turn_matches(angle,ratios[i]));
  CHECK(dc_live_turn_matches(angle-.0001f,ratios[i]));
  CHECK(!dc_live_turn_matches(0,ratios[i]));
  CHECK(!dc_live_turn_matches(angle*2,ratios[i]));
  CHECK(!dc_live_turn_matches(angle+.0003f,ratios[i]));
 }
 CHECK(!dc_live_turn_matches(.04189f,4.8f)); /* NTSC step cannot masquerade as PAL */
 CHECK(!dc_live_turn_matches(.05026548f,4));
 CHECK(!dc_live_turn_matches(.01047f,0));CHECK(!dc_live_turn_matches(.05f,5));
 CHECK(!dc_live_turn_matches(bits(0x7fc00000),4));CHECK(!dc_live_turn_matches(.04f,bits(0x7fc00000)));
}
static void inputs(void){
 unsigned i;float target[3]={-281.192993f,31.275928f,-280.458771f},q[8],saved[8];
 dc_goal_pad_state s,again;uint8_t pad[132]={0};dc_live_pose p;
 CHECK(dc_live_initial_transform(2.2f,-.45f,target,55,q));
 p=make_pose(2.2f,-.45f,q[0],q[1],q[2]);CHECK(dc_live_initial_matches(&p,q));
 q[0]+=10;CHECK(!dc_live_initial_matches(&p,q));q[0]-=10;
 q[4]+=.01f;CHECK(!dc_live_initial_matches(&p,q));
 memset(saved,0xa5,sizeof saved);memcpy(q,saved,sizeof q);
 CHECK(!dc_live_initial_transform(0,0,NULL,55,q));CHECK(!memcmp(q,saved,sizeof q));
 CHECK(!dc_live_initial_transform(bits(0x7fc00000),0,target,55,q));
 CHECK(!dc_live_initial_transform(0,2,target,55,q));CHECK(!dc_live_initial_transform(0,0,target,-1,q));
 for(i=0;i<8;++i){
  CHECK(dc_live_initial_transform((float)i*.7f-2.8f,.6f,target,55,q));
  p=make_pose((float)i*.7f-2.8f,.6f,q[0],q[1],q[2]);CHECK(dc_live_initial_matches(&p,q));
 }
 for(i=0;i<8;++i){float bad[8];memcpy(bad,q,sizeof bad);bad[i]=bits(0x7fc00000);CHECK(!dc_live_initial_matches(&p,bad));}
 dc_live_input_advance(0);dc_goal_cpad_open(pad,0);
 for(i=0;i<3;++i)dc_goal_cpad_get_data(pad);
 CHECK(!(pad[0]&128));
 for(i=0;i<=181;++i){
  unsigned j,stage=i<16?0:i<28?1:i<40?2:i<44?3:i<64?4:i<88?5:i<100?6:i<124?7:i<136?8:i<160?9:i<172?10:11;
  int joy=stage==1||stage==7||stage==10?52:stage==5||stage==9?-52:0;
  CHECK(dc_live_input_stage(i)==stage);CHECK(dc_live_recorded_pad(i,0,&s));
  CHECK(s.joyx==joy&&s.buttons==(stage==3&&i>=42?DC_PAD_RX_POS:0));
  CHECK(!dc_live_recorded_pad(i,1,&again));CHECK(!again.buttons&&!again.joyx);
  dc_live_input_advance(i);CHECK(dc_arena_live_pad_state(0,&again));CHECK(!memcmp(&s,&again,sizeof s));
  dc_goal_cpad_get_data(pad);CHECK(!(pad[0]&128));CHECK(pad[6]==joy+128&&pad[4]==(stage==3&&i>=42?207:128)&&pad[5]==128&&pad[7]==128);
  CHECK(pad[2]==0&&pad[3]==0);
  CHECK(!s.ltrig&&!s.rtrig); /* no button/pressure boost */
  for(j=8;j<20;++j)CHECK(pad[j]==0);
 }
 CHECK(!dc_live_recorded_pad(0,0,NULL));
}
static uint32_t reference_crc(const unsigned char *data,size_t n){
 uint32_t crc=0xffffffffu;size_t i;unsigned k;
 for(i=0;i<n;++i){crc^=data[i];for(k=0;k<8;++k)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
 return ~crc;
}
static void assets(const char *root){
 unsigned char data[52]={0};dc_live_cell_entry entry={-2,1,1,0},bad[2];dc_instance_cells cells={0};
 dc_game_camera c={{0},{0},0,0,55};dc_render_query query={-14,-14,14,14},q;
 double crop[4]={-30,30,-40,40},d[4];float initial[3]={0};unsigned i;char path[512];FILE *f;
 entry.crc=reference_crc(data,sizeof data);
 CHECK(dc_live_ledger_files(root,&entry,1));CHECK(!dc_live_ledger_files(NULL,&entry,1));
 CHECK(!dc_live_ledger_files(root,NULL,1));CHECK(!dc_live_ledger_files(root,&entry,0));CHECK(!dc_live_ledger_files(root,&entry,4097));
 bad[0]=entry;bad[0].crc^=1;CHECK(!dc_live_ledger_files(root,bad,1));
 bad[0]=entry;bad[0].count=2;CHECK(!dc_live_ledger_files(root,bad,1));
 bad[0]=entry;bad[0].ix=-3;CHECK(!dc_live_ledger_files(root,bad,1));
 bad[0]=bad[1]=entry;CHECK(!dc_live_ledger_files(root,bad,2));
 bad[0]=entry;bad[0].count=0;CHECK(!dc_live_ledger_files(root,bad,1));
 bad[0]=entry;bad[0].iz=62501;CHECK(!dc_live_ledger_files(root,bad,1));
 cells.capacity=2;cells.cell_count=1;cells.count=1;cells.cells[0]=(dc_instance_cell){-2,1,1,0,0};
 CHECK(dc_live_ledger_cells(&cells,&entry,1));cells.cells[0].count=0;CHECK(!dc_live_ledger_cells(&cells,&entry,1));
 cells.cells[0].count=1;cells.cell_count=2;cells.cells[1]=(dc_instance_cell){-3,1,0,0,0};
 CHECK(dc_live_ledger_cells(&cells,&entry,1));cells.cells[1].count=1;CHECK(!dc_live_ledger_cells(&cells,&entry,1));
 cells.cells[1].count=0;cells.count=0;CHECK(!dc_live_ledger_cells(&cells,&entry,1));
 cells.count=1;cells.capacity=0;CHECK(!dc_live_ledger_cells(&cells,&entry,1));
 cells.cell_count=DC_INSTANCE_CELLS_MAX+1;CHECK(!dc_live_ledger_cells(&cells,&entry,1));
 CHECK(!dc_live_ledger_cells(NULL,&entry,1));
 CHECK(dc_live_coverage(&c,&query,crop,initial,14,80));
 CHECK(!dc_live_coverage(NULL,&query,crop,initial,14,80));CHECK(!dc_live_coverage(&c,NULL,crop,initial,14,80));
 CHECK(!dc_live_coverage(&c,&query,NULL,initial,14,80));CHECK(!dc_live_coverage(&c,&query,crop,NULL,14,80));
 CHECK(!dc_live_coverage(&c,&query,crop,initial,0,80));CHECK(!dc_live_coverage(&c,&query,crop,initial,19,80));
 CHECK(!dc_live_coverage(&c,&query,crop,initial,14,0));CHECK(!dc_live_coverage(&c,&query,crop,initial,14,129));
 CHECK(!dc_live_coverage(&c,&query,crop,initial,bits(0x7fc00000),80));
 CHECK(!dc_live_coverage(&c,&query,crop,initial,14,bits(0x7fc00000)));
 for(i=0;i<4;++i){uint64_t nan=UINT64_C(0x7ff8000000000001);q=query;memcpy(d,crop,sizeof d);
  memcpy(d+i,&nan,8);CHECK(!dc_live_coverage(&c,&q,d,initial,14,80));
  if(i==0)memcpy(&q.x0,&nan,8);else if(i==1)memcpy(&q.z0,&nan,8);else if(i==2)memcpy(&q.x1,&nan,8);else memcpy(&q.z1,&nan,8);
  CHECK(!dc_live_coverage(&c,&q,crop,initial,14,80));
 }
 q=query;q.x0-=20;CHECK(!dc_live_coverage(&c,&q,crop,initial,14,80));
 q=query;q.x0+=1;
 {dc_live_pose raw=make_pose(0,0,0,0,-55*4096);CHECK(dc_live_query_matches(&raw,&query,14));
  CHECK(!dc_live_query_matches(&raw,&q,14));}
 c.target[0]=7;CHECK(!dc_live_coverage(&c,&query,crop,initial,14,80));c.target[0]=0;
 c.target[1]=2;CHECK(!dc_live_coverage(&c,&query,crop,initial,14,80));c.target[1]=0;
 c.target[2]=bits(0x7fc00000);CHECK(!dc_live_coverage(&c,&query,crop,initial,14,80));
 snprintf(path,sizeof path,"%s/cells/-2_1.dci",root);f=fopen(path,"wb");CHECK(f);CHECK(fwrite(data,1,51,f)==51);CHECK(!fclose(f));
 CHECK(!dc_live_ledger_files(root,&entry,1));
 f=fopen(path,"wb");CHECK(f);CHECK(fwrite(data,1,52,f)==52);CHECK(fputc(1,f)!=EOF);CHECK(!fclose(f));
 CHECK(!dc_live_ledger_files(root,&entry,1));
 f=fopen(path,"wb");CHECK(f);CHECK(fwrite(data,1,52,f)==52);CHECK(!fclose(f)); /* next fast-math run */
}
/* Recorded beach start, ratio and first strafe from arena-live-review/dump.bin.
 * This is a host prediction of cam-free-floating-input/move, NOT GOAL execution.
 * Check both yaw signs so the bound does not depend on a guessed local-down
 * convention. Actual sample magnitudes/timing come from the production input.
 * The real update 26 must still fail the production coverage function. */
static void recorded_path(void){
 const float initial[3]={-281.1929931640625f,31.275928497314453f,-280.4587707519531f};
 const double crop[4]={-332.0439758300781,-235.1929931640625,-329.9897766113281,-234.45877075195312};
 const float start[3]={-241.1525421142578f,55.199031829833984f,-251.31349182128906f};
 dc_game_camera c;dc_render_query q;dc_live_pose p;dc_goal_pad_state s;int sign;unsigned seq;uint8_t hw[132]={0};
 float max_x=0,max_z=0,old_step=-12800.f*16.f/62.f/4096.f;
 CHECK(fabsf(old_step*cosf(2.2f)-.474594116f)<.00003f);
 CHECK(fabsf(old_step*sinf(2.2f)+.652008057f)<.00003f);
 p=make_pose(2.200000047683716f,-.44999998807907104f,
             -236.40660095214844f*4096,start[1]*4096,-257.8335876464844f*4096);
 CHECK(dc_live_convert(&p,55,&c));
 q=(dc_render_query){c.target[0]-14.,c.target[2]-14.,c.target[0]+14.,c.target[2]+14.};
 CHECK(q.x0>=crop[0]&&q.x1<=crop[1]&&q.z0>=crop[2]&&q.z1<=crop[3]);
 CHECK(!dc_live_coverage(&c,&q,crop,initial,14,80));
 for(sign=-1;sign<=1;sign+=2){
  float eye[3],yaw=2.200000047683716f;unsigned moves=0,turns=0;
  memcpy(eye,start,sizeof eye);
  for(seq=1;seq<=181;++seq){
   float step;
   CHECK(dc_live_recorded_pad(seq-1,0,&s));
   /* pad.gc analog-input, CAM_FREE speed=1600, no r2/l2, ratio=4.
    * tv.x = -2*1600*4*(leftx-128-48)/(110-48) GOAL units. */
   step=abs(s.joyx)>48 ? -copysignf(12800.f*(abs(s.joyx)-48)/62.f/4096.f,(float)s.joyx) : 0;
   eye[0]+=step*cosf(yaw);eye[2]+=step*sinf(yaw);
   dc_goal_pad_fill(hw,&s);
     {float right=(float)hw[4]-128.f;
      float axis=right>48.f?(right-48.f)/62.f:0.f;
      yaw+=sign*(2.f*.6f*4.f*3.14159265358979323846f/180.f)*axis;
     }
   p=make_pose(yaw,-.44999998807907104f,eye[0]*4096,eye[1]*4096,eye[2]*4096);
   CHECK(dc_live_convert(&p,55,&c));
   q=(dc_render_query){c.target[0]-14.,c.target[2]-14.,c.target[0]+14.,c.target[2]+14.};
   CHECK(dc_live_coverage(&c,&q,crop,initial,14,80));
   if(fabsf(c.target[0]-initial[0])>max_x)max_x=fabsf(c.target[0]-initial[0]);
   if(fabsf(c.target[2]-initial[2])>max_z)max_z=fabsf(c.target[2]-initial[2]);
   if((seq>=20&&seq<=26)||(seq>=68&&seq<=86)||(seq>=104&&seq<=122)||(seq>=140&&seq<=158)||(seq>=164&&seq<=170)){CHECK(step!=0);++moves;}
   if(seq>=43&&seq<=44){CHECK(s.buttons==(DC_PAD_RX_POS)&&step==0);++turns;}
   if((seq>=4&&seq<=14)||(seq>=32&&seq<=38)||(seq>=48&&seq<=60)||(seq>=92&&seq<=98)||(seq>=128&&seq<=134)||(seq>=176&&seq<=180))CHECK(step==0&&!s.buttons);
  }
  CHECK(moves==71&&turns==2);
 }
 printf("HOST path prediction, ratio 4, both yaw signs: max anchor displacement X=%.6f Z=%.6f m\n",(double)max_x,(double)max_z);
}
int main(int argc,char **argv){CHECK(argc==2);conversion();hostile();memory_checks();turn_contract();inputs();assets(argv[1]);recorded_path();printf("live host math/input/asset fixtures: %u assertions\n",checks);return 0;}
