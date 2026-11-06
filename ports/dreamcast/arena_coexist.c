/* GOAL/PVR arena coexistence (AI-assisted), ISC. */
#ifdef DC_ARENA_COEXIST
#include <kos.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "goal_runtime.h"
#include "goal_fiber.h"
#include "goal_kos_stack.h"
#if defined(DC_ARENA_LIVE)
#include "arena_live.h"
#elif defined(DC_ARENA_CAMERA)
#include "arena_camera.h"
#endif

/* runtime_main.c's unchanged 128-byte verify_backend.py record. */
extern volatile struct {
  uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
  uint32_t observed_lo,observed_hi,expected_lo,expected_hi;
  char description[80];
} dc_backend;
typedef char arena_telemetry_layout[sizeof dc_backend==128?1:-1];
extern const int romdisk_size;
extern const unsigned char romdisk_data[];
int dc_stage_romdisk_at(void *);

#ifdef DC_ARENA_JAK_PLAYER
#include "arena_jak_memory.h"
#include <string.h>
enum {SLAB_BYTES=DC_JAK_RENDER_SLAB_BYTES,ARENA_BYTES=DC_JAK_ARENA_BYTES,DMA_BYTES=3506176,
#elif defined(DC_LEVEL_DRAW)
/* The level ELF: the renderer slab is the Jak ELF's measured one (high water
 * 1,441,856 plus a 65,536 margin, build/arena-jak-budget/budget.json), and the
 * GOAL arena keeps the rest of the 13 MiB both shared before. */
enum {SLAB_BYTES=1507392,ARENA_BYTES=13u*1024u*1024u-1507392,DMA_BYTES=3506176,
#else
enum {SLAB_BYTES=2621440,ARENA_BYTES=11010048,DMA_BYTES=3506176,
#endif
      /* dma-buffer new adds its type's size minus 4, rounded to 16. */
      EXTENSION_BYTES=DMA_BYTES+64};
static void *extension,*boot_romdisk,*slab,*root_stack;
static uint32_t root_owner,root_stack_bytes,scenes;
#ifdef DC_ARENA_JAK_PLAYER
static unsigned char *combined;
static dc_goal_runtime *boot_runtime;
static uint32_t boot_borrow;
/* The old 1.94 MB boot image does not fit the smaller renderer slab. Store it
 * across the unused GOAL tail and slab until all boot files close. The runtime
 * capacity is reduced at prepare, protecting that tail even when the loader
 * releases temporary top-level code. No second malloc or extra RAM is needed. */
void *dc_arena_jak_root_alloc(size_t alignment,size_t bytes){
 return combined && alignment==32 && bytes==ARENA_BYTES+32u?combined:NULL;
}
void dc_arena_jak_root_free(void *p){if(p==combined){free(combined);combined=NULL;}}
#endif
#ifdef DC_ARENA_FLY
static void *fly_romdisk;
static uint32_t fly_bytes;
extern const unsigned char romdisk_data[];
extern const unsigned char flydisk_data[];
extern const int flydisk_size;
/* Main RAM bytes, 32-bit VRAM stores. A short tail is one word so the source
 * array is never read past flydisk_size. */
static void copy_vram(volatile uint32_t *dest,const unsigned char *source,uint32_t bytes){
  uint32_t i,n=bytes>>2;
  for(i=0;i<n;++i){
    const unsigned char *p=source+i*4u;
    dest[i]=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
  }
  if(bytes&3u){
    uint32_t word=0;const unsigned char *p=source+n*4u;
    for(i=0;i<(bytes&3u);++i)word|=(uint32_t)p[i]<<(8u*i);
    dest[n]=word;
  }
}
#endif
volatile uint32_t dc_arena_sync_requested,dc_arena_chain_seen,dc_arena_dma_bank,dc_arena_dma_chain;
#ifdef DC_ARENA_FLY
#include "arena_slab.h"
void dc_arena_slab_init(void *,uint32_t);
int dc_arena_fly_frame(void);
int dc_arena_fly_finish(void);
#endif

static int check(uint64_t actual,uint64_t expected,const char *name){
  ++dc_backend.total;
  if(actual==expected){++dc_backend.passed;return 1;}
  if(!dc_backend.failed_case){
    dc_backend.failed_case=10000u+dc_backend.total;
    dc_backend.observed_lo=(uint32_t)actual;dc_backend.observed_hi=(uint32_t)(actual>>32);
    dc_backend.expected_lo=(uint32_t)expected;dc_backend.expected_hi=(uint32_t)(expected>>32);
    snprintf((char *)dc_backend.description,sizeof dc_backend.description,"%s",name);
  }
  dc_backend.state=3;return 0;
}
#ifdef DC_ARENA_LIVE
int dc_arena_live_check(uint64_t actual,uint64_t expected,const char *name){return check(actual,expected,name);}
#endif
#ifdef DC_ARENA_CAMERA
int dc_arena_camera_check(uint64_t actual,uint64_t expected,const char *name){return check(actual,expected,name);}
#endif
#ifdef DC_LEVEL_DRAW
#include "arena_slab.h"
void dc_arena_slab_init(void *,uint32_t);
int dc_level_draw_frame(dc_goal_runtime *);
int dc_level_draw_finish(void);
int dc_level_draw_check(uint64_t actual,uint64_t expected,const char *name){return check(actual,expected,name);}
/* After GAME.CGO, when the renderer starts: the boot romdisk is no longer read,
 * so its VRAM loan goes back to the PVR for textures and the slab is handed to
 * the renderer's allocator. */
int dc_level_draw_release_boot(uint32_t *available){
 if(!check(fs_romdisk_unmount("/rd"),0,"level draw boot romdisk unmounted"))return 0;
 pvr_mem_free(boot_romdisk);boot_romdisk=NULL;
 dc_arena_slab_init(slab,SLAB_BYTES);
 if(available)*available=pvr_mem_available();
 return 1;
}
#endif
#ifdef DC_ARENA_FLY
int dc_arena_fly_check(uint64_t actual,uint64_t expected,const char *name){return check(actual,expected,name);}
/* Only after GAME.CGO and its checks have closed every boot file. The boot
 * borrowed the slab before any renderer owner existed; now return all of it. */
int dc_arena_fly_release_boot(uint32_t *available){
 if(!check(fs_romdisk_unmount("/rd"),0,"arena fly boot romdisk unmounted"))return 0;
#ifdef DC_ARENA_JAK_PLAYER
 if(!boot_runtime || boot_runtime->temporary || boot_runtime->capacity!=ARENA_BYTES-boot_borrow ||
    boot_runtime->used>boot_runtime->capacity)return 0;
 boot_runtime->capacity=ARENA_BYTES;
#endif
 dc_arena_slab_init(slab,SLAB_BYTES);boot_romdisk=NULL;
 if(!check(fly_romdisk && fs_romdisk_mount("/rd",(const uint8 *)fly_romdisk,0)==0,1,
           "arena fly level romdisk mounted at /rd"))return 0;
 if(available)*available=pvr_mem_available();
 return 1;
}
#endif
#define CHECK(a,b,label) do{if(!check((uint64_t)(a),(uint64_t)(b),label))return 0;}while(0)
static int disjoint(const void *a,uint32_t na,const void *b,uint32_t nb){
  /* Compare physical aliases, including the arena's cached P0 references. */
  uint32_t pa=(uint32_t)(uintptr_t)a&0x1fffffffu,pb=(uint32_t)(uintptr_t)b&0x1fffffffu;
  return pa+na<=pb || pb+nb<=pa;
}
static int loan(const void *p,uint32_t bytes){
  uintptr_t address=(uintptr_t)p;
  return !(address&15u) && address>=0xa4000000u && address<=0xa4800000u-bytes;
}
static int on_root(void){
  kthread_t *owner=thd_get_current();
  return !dc_goal_fiber_current() && dc_goal_kos_stack_owner()==root_owner &&
    owner && owner->stack==root_stack && owner->stack_size==root_stack_bytes;
}
uint32_t dc_renderer_reserve(void){return 13u*1024u*1024u-ARENA_BYTES;}  /* the GOAL arena is the rest of 13 MiB */
uint64_t dc_arena_syncv(void){dc_arena_sync_requested=1;return 0;}
uint64_t dc_arena_chain(uint64_t bank,uint64_t chain){
  dc_arena_dma_bank=(uint32_t)bank;dc_arena_dma_chain=(uint32_t)chain;dc_arena_chain_seen=1;return 0;
}

int dc_arena_stage(void){
#ifdef DC_ARENA_JAK_PLAYER
  dc_jak_memory_layout plan={0};
#endif
  pvr_init_params_t params={.opb_sizes={PVR_BINSIZE_32,0,PVR_BINSIZE_32,0,PVR_BINSIZE_32},
    .vertex_buf_size=256*1024,.opb_overflow_count=1};
  kthread_t *owner=thd_get_current();
#ifdef DC_ARENA_FLY
  params.vertex_buf_size=DC_ARENA_FLY_VERTEX_BYTES;
#endif
  dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
  CHECK(dc_backend.system_ram,16777216u,"arena stock 16 MiB RAM");
  root_owner=dc_goal_kos_stack_owner();
  CHECK(root_owner!=0 && owner!=NULL,1,"arena starts on the KOS stack");
  root_stack=owner->stack;root_stack_bytes=owner->stack_size;
  vid_set_mode(DM_640x480,PM_RGB565);
  CHECK(pvr_init(&params),0,"arena PVR init");
  CHECK(romdisk_size>0 && romdisk_size<=2*1024*1024,1,"arena boot romdisk fits 2 MiB cap");
#ifdef DC_ARENA_FLY
  /* texture_budget.py's pvr_allocate_buffers with this init: vertex 1048576,
   * three 32-word bins, overflow 1, 640x480. Reserved 3801344, so
   * pvr_mem_available() models as 4587264. The DMA loan takes 3506240.
   * The rest, 1081024, is the level romdisk plus textures. Runtime uses
   * pvr_mem_available(), not that model. Copy the level image before the
   * slab memalign: that alloc may reuse the image's main-RAM pages. */
  CHECK(pvr_mem_available()>=EXTENSION_BYTES,1,"arena fly VRAM fits DMA loan");
  extension=pvr_mem_malloc(EXTENSION_BYTES);
  CHECK(loan(extension,EXTENSION_BYTES),1,"arena aligned PVR extension loan");
  fly_bytes=((uint32_t)flydisk_size+3u)&~3u;
  CHECK((uint32_t)flydisk_size>0 && fly_bytes<=pvr_mem_available(),1,
        "arena fly level romdisk fits VRAM after DMA loan");
  fly_romdisk=pvr_mem_malloc(fly_bytes);
  CHECK(loan(fly_romdisk,fly_bytes),1,"arena fly level romdisk VRAM loan");
  CHECK(disjoint(fly_romdisk,fly_bytes,extension,EXTENSION_BYTES),1,
        "arena fly level romdisk disjoint from DMA loan");
  /* .dc_fly is linked after .dc_stage and must end by 0x8d000000. The main
   * stack also lives at the top of this hole; its size is not in the tree. */
  CHECK((uint32_t)(uintptr_t)flydisk_data>=(uint32_t)(uintptr_t)romdisk_data+(uint32_t)romdisk_size
        && (uint32_t)(uintptr_t)flydisk_data+(uint32_t)flydisk_size<=0x8d000000u,1,
        "arena fly level image above boot romdisk");
  CHECK(disjoint(flydisk_data,(uint32_t)flydisk_size,owner->stack,(uint32_t)owner->stack_size),1,
        "arena fly level image disjoint from KOS stack");
  CHECK(disjoint(flydisk_data,(uint32_t)flydisk_size,fly_romdisk,fly_bytes),1,
        "arena fly level copy ranges differ");
  copy_vram(fly_romdisk,flydisk_data,(uint32_t)flydisk_size);
#ifdef DC_ARENA_JAK_PLAYER
  combined=memalign(32,ARENA_BYTES+32u+SLAB_BYTES);
  slab=combined?combined+ARENA_BYTES+32u:NULL;
#else
  slab=memalign(32,SLAB_BYTES);
#endif
  CHECK(slab!=NULL,1,"arena renderer slab allocated before GOAL arena");
#ifdef DC_ARENA_JAK_PLAYER
  CHECK(combined && SLAB_BYTES+ARENA_BYTES==13u*1024u*1024u &&
        dc_jak_memory_plan((uint32_t)romdisk_size,&plan),1,
        "jak arena and slab share the original combined RAM budget");
#else
  CHECK(disjoint(slab,SLAB_BYTES,romdisk_data,(uint32_t)romdisk_size),1,
        "arena fly slab below staged boot source");
#endif
#ifdef DC_ARENA_JAK_PLAYER
  /* combined+16 is the GOAL base. Leave its final 16 bytes as an alignment
   * gap and borrow an aligned tail sufficient for the entire boot image. */
  boot_borrow=plan.borrowed;
  boot_romdisk=combined+plan.boot_offset;
  /* The large allocation may cover the staged source. memmove handles either
   * overlap direction before any GOAL writes. dc_stage_romdisk_at would copy
   * forward and corrupt an overlapping source. */
  CHECK(fs_romdisk_unmount("/rd"),0,"jak boot source unmounted before overlapping move");
  memmove(boot_romdisk,romdisk_data,(uint32_t)romdisk_size);
  CHECK(fs_romdisk_mount("/rd",(const uint8 *)boot_romdisk,0),0,"jak protected tail boot mounted");
#else
  boot_romdisk=slab;
  CHECK(dc_stage_romdisk_at(boot_romdisk),1,"arena fly boot borrows slab before renderer starts");
#endif
#else
  CHECK(pvr_mem_available()>=(uint32_t)romdisk_size+DMA_BYTES,1,"arena VRAM fits boot romdisk and DMA payload");
  extension=pvr_mem_malloc(EXTENSION_BYTES);
  CHECK(loan(extension,EXTENSION_BYTES),1,"arena aligned PVR extension loan");
  boot_romdisk=pvr_mem_malloc(((uint32_t)romdisk_size+3u)&~3u);
  CHECK(loan(boot_romdisk,((uint32_t)romdisk_size+3u)&~3u),1,"arena aligned PVR romdisk loan");
  CHECK(disjoint(extension,EXTENSION_BYTES,boot_romdisk,((uint32_t)romdisk_size+3u)&~3u),1,
        "arena VRAM loans are disjoint");
  CHECK(dc_stage_romdisk_at(boot_romdisk),1,"arena romdisk copied and mounted before large allocations");
  CHECK(memcmp(boot_romdisk,romdisk_data,(uint32_t)romdisk_size),0,"arena romdisk reads back from its VRAM loan");
  slab=memalign(32,SLAB_BYTES);
  CHECK(slab!=NULL,1,"arena renderer slab allocated before GOAL arena");
#endif
  return 1;
}
int dc_arena_prepare(dc_goal_runtime *rt){
  CHECK(rt->capacity,ARENA_BYTES,"arena reduced GOAL capacity");
#ifdef DC_ARENA_JAK_PLAYER
  CHECK(rt->memory==combined+16 && rt->temporary==0 && rt->used<=rt->capacity-boot_borrow,1,
        "jak boot tail protects unallocated GOAL bytes");
  rt->capacity-=boot_borrow;boot_runtime=rt;
#endif
  CHECK(disjoint(slab,SLAB_BYTES,rt->memory,rt->capacity),1,"arena renderer slab outside GOAL arena");
  CHECK(disjoint(extension,EXTENSION_BYTES,rt->memory,rt->capacity),1,"arena extension outside GOAL arena");
  CHECK(disjoint(boot_romdisk,(uint32_t)romdisk_size,rt->memory,rt->capacity),1,"arena boot romdisk outside GOAL arena");
#ifdef DC_ARENA_FLY
  CHECK(disjoint(fly_romdisk,fly_bytes,rt->memory,rt->capacity),1,"arena fly level romdisk outside GOAL arena");
#endif
  return 1;
}
int dc_arena_extension(dc_goal_runtime *rt){
  return dc_goal_register_extension(rt,extension,(uint32_t)(uintptr_t)extension,EXTENSION_BYTES);
}
int dc_arena_dispatch(dc_goal_runtime *rt){
#ifdef DC_ARENA_FLY
  uint32_t used=rt->used;
#endif
#ifdef DC_ARENA_LIVE
  if(!dc_arena_live_frame(rt))return 0;
#elif defined(DC_ARENA_CAMERA)
  if(!dc_arena_camera_frame(rt))return 0;
#endif
  CHECK(rt->error,DC_GOAL_OK,"arena dispatch leaves no runtime fault");
  CHECK(on_root(),1,"arena clear starts outside fibers on restored KOS stack");
#ifdef DC_ARENA_FLY
  if(!dc_arena_fly_frame())return 0;
  CHECK(rt->used,used,"arena fly rendering leaves GOAL bump unchanged");
#elif defined(DC_LEVEL_DRAW)
  if(!dc_level_draw_frame(rt))return 0;
#else
  CHECK(pvr_wait_ready(),0,"arena PVR ready between GOAL frames");
  pvr_scene_begin();pvr_set_bg_color(.05f,.1f,.2f);
  CHECK(pvr_scene_finish(),0,"arena empty PVR scene submitted");
#endif
  CHECK(on_root(),1,"arena clear ends outside fibers on restored KOS stack");
  ++scenes;return 1;
}
int dc_arena_finish(dc_goal_runtime *rt){
#if !defined(DC_ARENA_FLY) && !defined(DC_LEVEL_DRAW)
  uint32_t used=rt->used,i;volatile uint32_t *words=slab;
#endif
#if defined(DC_LEVEL_DRAW)
  CHECK(scenes,DC_LEVEL_DISPATCHES,"level scene after every dispatcher call");
#elif defined(DC_ARENA_LIVE)
  CHECK(scenes,DC_LIVE_DISPATCHES,"arena clear after every live camera dispatcher call");
#elif defined(DC_ARENA_CAMERA)
  CHECK(scenes,65,"arena clear after all 65 camera dispatcher calls including display initialization");
#else
  CHECK(scenes,64,"arena clear after all 64 dispatcher calls");
#endif
#ifndef DC_LEVEL_DRAW  /* the level ELF's frames never reach syncv or the DMA chain */
  CHECK(dc_arena_sync_requested,1,"arena syncv request stashed");
  CHECK(dc_arena_chain_seen,1,"arena DMA chain stashed without walking it");
#endif
  CHECK(pvr_wait_ready(),0,"arena final scene ready");
  CHECK(pvr_wait_render_done(),0,"arena final clear rendered");
#ifdef DC_LEVEL_DRAW
  CHECK(rt->used<=rt->capacity,1,"level GOAL arena within its reduced capacity");
#else
  CHECK(rt->used<=10700000u+310048u && rt->used<=rt->capacity,1,"arena used bytes within milestone bound");
#endif
#ifdef DC_ARENA_FLY
  if(!dc_arena_fly_finish())return 0;
#elif defined(DC_LEVEL_DRAW)
  if(!dc_level_draw_finish())return 0;
#else
  for(i=0;i<SLAB_BYTES/4u;++i)words[i]=0xa5a5a5a5u;
  CHECK(rt->used,used,"arena slab fill leaves GOAL bump unchanged");
#endif
  CHECK(rt->error,DC_GOAL_OK,"arena heap probe leaves no runtime fault");
#ifdef DC_ARENA_LIVE
  if(!dc_arena_live_finish(rt))return 0;
#elif defined(DC_ARENA_CAMERA)
  if(!dc_arena_camera_finish(rt))return 0;
#endif
  return 1;
}
#undef CHECK
#endif
