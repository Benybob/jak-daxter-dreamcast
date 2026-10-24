/* Original-free native near-clip raster oracle (AI-assisted), ISC. */
#include <kos.h>
#include <dc/maple/controller.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "training_layer.h"
#include "scene_telemetry.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
volatile dc_scene_telemetry dc_telemetry={.magic=DC_TELEMETRY_MAGIC,.version=1};
volatile uint32_t dc_clip_fixture_phase;
static dc_training_layer layer;
int main(void){
 dc_textured_vertex v[3]={{-.02f,-.02f,.025f,0,0,0xffff00ff},{.04f,-.02f,.1f,1,0,0xffff00ff},{0,.04f,.1f,0,1,0xffff00ff}};
 const float original_z[3]={.025f,.1f,.1f};uint16_t indices[3]={0,1,2};
 dc_textured_batch batch={0,3,0,0};dc_texture texture={8,8,DC_TEXTURE_ARGB1555,128,NULL};
 dc_textured_scene scene={3,3,1,1,128,v,indices,&batch,&texture};unsigned i;int status=1;
 pvr_init_params_t params={.opb_sizes={PVR_BINSIZE_32,0,0,0,0},.vertex_buf_size=256*1024};
 dc_telemetry.state=DC_BOOT;dc_telemetry.source=3;
 vid_set_mode(DM_640x480,PM_RGB565);
 texture.pixels=malloc(128);if(!texture.pixels)goto finish;
 memset(texture.pixels,0xff,128);
 if(pvr_init(&params)<0)goto finish;
 pvr_set_bg_color(.05f,.1f,.2f);
 if(!dc_training_layer_make(&layer,&scene))goto shutdown;
 dc_telemetry.system_ram=_arch_mem_top-0x8c000000u;dc_telemetry.state=DC_READY;
 while(1){
  maple_device_t *device=maple_enum_type(0,MAPLE_FUNC_CONTROLLER);
  cont_state_t *input=device?maple_dev_status(device):NULL;
  if(input&&(input->buttons&CONT_START)){status=0;break;}
  if(dc_clip_fixture_phase>3)break;
  for(i=0;i<3;++i)v[i].z=dc_clip_fixture_phase==0?original_z[i]:(dc_clip_fixture_phase==1?.1f:(dc_clip_fixture_phase==2?.025f:DC_SCENE_NEAR));
  if(!dc_scene_prepare(v,sizeof v[0],layer.vertices,3,0,0,0))break;
  pvr_wait_ready();pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);
  dc_training_layer_draw(&layer,&scene,PVR_LIST_OP_POLY);pvr_list_finish();pvr_scene_finish();
  ++dc_telemetry.frames;
 }
shutdown:
 pvr_wait_ready();dc_training_layer_free(&layer);pvr_shutdown();
finish:
 free(texture.pixels);dc_telemetry.state=status?DC_ERROR:DC_EXIT;return status;
}
