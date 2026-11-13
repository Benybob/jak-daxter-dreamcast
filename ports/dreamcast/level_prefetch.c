/* Disc files read ahead on a thread for the level draw (AI-assisted), ISC.
 *
 * The terrain block cache reads a sector's .drx when the window first needs
 * it: 100-250 ms of disc commands in the frame that crosses into new ground.
 * The level draw asks for the files of a wider window here as Jak moves; a
 * KOS thread reads each whole into the KOS heap, and the renderer's fopen
 * (arena_slab.c) takes a staged file instead of going to the disc. A file not
 * staged in time is read as before. */
#include <kos.h>
#include <malloc.h>
#include <stdint.h>
#include <string.h>
#include "level_prefetch.h"

enum {SLOTS=24,PATH_BYTES=64,BUDGET=160*1024,STALE_FRAMES=240};
enum {EMPTY=0,QUEUED,READING,READY};
typedef struct {char path[PATH_BYTES];unsigned char *data;uint32_t bytes,stamp;int state;} slot;
static slot slots[SLOTS];
static mutex_t lock=MUTEX_INITIALIZER;
static kthread_t *reader;
static uint32_t staged_bytes,frame;
volatile dc_level_prefetch_counts dc_level_prefetch;

static void drop(slot *s){
 if(s->data){free(s->data);staged_bytes-=s->bytes;}
 memset(s,0,sizeof *s);
}
static void *read_ahead(void *unused){
 (void)unused;
 for(;;){
  slot *s=NULL;char path[PATH_BYTES];unsigned i;unsigned char *data=NULL;uint32_t bytes=0;int ok=0;file_t fd;
  mutex_lock(&lock);
  for(i=0;i<SLOTS;++i)if(slots[i].state==QUEUED){s=&slots[i];s->state=READING;memcpy(path,s->path,sizeof path);break;}
  mutex_unlock(&lock);
  if(!s){thd_sleep(10);continue;}
  fd=fs_open(path,O_RDONLY);
  if(fd>=0){
   size_t total=fs_total(fd);
   /* 32-byte aligned and whole sectors long: fs_iso9660 then reads the file's
    * full sectors as one DMA command, not a command a sector */
   if(total&&total<=BUDGET&&(data=memalign(32,(total+2047u)&~(size_t)2047u))){
    size_t done=0;
    while(done<total){ssize_t n=fs_read(fd,data+done,total-done);if(n<=0)break;done+=(size_t)n;}
    ok=done==total;bytes=(uint32_t)total;
   }
   fs_close(fd);
  }
  mutex_lock(&lock);
  /* the slot is still this request: only the reader leaves READING */
  if(ok&&staged_bytes+bytes<=BUDGET){s->data=data;s->bytes=bytes;staged_bytes+=bytes;s->state=READY;++dc_level_prefetch.staged;data=NULL;}
  else{memset(s,0,sizeof *s);++dc_level_prefetch.dropped;}
  mutex_unlock(&lock);
  free(data);
 }
 return NULL;
}
void dc_level_prefetch_frame(void){
 unsigned i;
 mutex_lock(&lock);
 ++frame;
 for(i=0;i<SLOTS;++i)if(slots[i].state==READY&&frame-slots[i].stamp>STALE_FRAMES){drop(&slots[i]);++dc_level_prefetch.stale;}
 mutex_unlock(&lock);
}
void dc_level_prefetch_request(const char *path){
 unsigned i;slot *empty=NULL;
 if(!path||strlen(path)>=PATH_BYTES)return;
 if(!reader&&!(reader=thd_create(1,read_ahead,NULL)))return;
 mutex_lock(&lock);
 for(i=0;i<SLOTS;++i){
  if(slots[i].state!=EMPTY&&!strcmp(slots[i].path,path)){slots[i].stamp=frame;empty=NULL;break;}
  if(slots[i].state==EMPTY&&!empty)empty=&slots[i];
 }
 if(i==SLOTS&&empty){strcpy(empty->path,path);empty->stamp=frame;empty->state=QUEUED;++dc_level_prefetch.requested;}
 mutex_unlock(&lock);
}
int dc_level_prefetch_take(const char *path,unsigned char **data,uint32_t *bytes){
 unsigned i;
 if(!reader||!path)return 0;
 for(;;){
  int wait=0;
  mutex_lock(&lock);
  for(i=0;i<SLOTS;++i)if(slots[i].state!=EMPTY&&!strcmp(slots[i].path,path)){
   if(slots[i].state==READY){
    *data=slots[i].data;*bytes=slots[i].bytes;staged_bytes-=slots[i].bytes;
    memset(&slots[i],0,sizeof slots[i]);++dc_level_prefetch.taken;
    mutex_unlock(&lock);return 1;
   }
   if(slots[i].state==QUEUED){memset(&slots[i],0,sizeof slots[i]);break;}  /* not begun: the caller reads it */
   wait=1;break;  /* being read now: one read, not two */
  }
  mutex_unlock(&lock);
  if(!wait){++dc_level_prefetch.missed;return 0;}
  ++dc_level_prefetch.waited;thd_sleep(1);
 }
}
void dc_level_prefetch_free(unsigned char *data){free(data);}
