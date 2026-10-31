/* Serial renderer heap, no libc fallback (AI-assisted), ISC. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct block {uint32_t bytes;int busy;uint32_t pad[6];} block;
typedef char slab_header_size[sizeof(block)==32?1:-1];
static unsigned char *start,*limit;
volatile uint32_t dc_arena_slab_live,dc_arena_slab_high_water,dc_arena_slab_failures;
void dc_arena_slab_init(void *memory,uint32_t bytes){
 start=memory;limit=start+bytes;
 dc_arena_slab_live=dc_arena_slab_high_water=dc_arena_slab_failures=0;
 memset(start,0,sizeof(block));((block *)start)->bytes=bytes;
}
void *dc_arena_memalign(size_t alignment,size_t bytes){
 unsigned char *p;size_t need;
 if(!start||!alignment||alignment>32u||(alignment&(alignment-1u))||
    bytes>SIZE_MAX-63u)goto fail;
 need=((bytes?bytes:1u)+31u)&~(size_t)31u;need+=sizeof(block);
 for(p=start;p<limit;p+=((block *)p)->bytes){
  block *b=(block *)p;
  if(b->busy||b->bytes<need)continue;
  if(b->bytes-need>=sizeof(block)+32u){
   block *tail=(block *)(p+need);memset(tail,0,sizeof(*tail));
   tail->bytes=b->bytes-need;b->bytes=need;
  }
  b->busy=1;dc_arena_slab_live+=(uint32_t)b->bytes;
  if(dc_arena_slab_live>dc_arena_slab_high_water)dc_arena_slab_high_water=dc_arena_slab_live;
  return p+sizeof(block);
 }
fail:
 ++dc_arena_slab_failures;return NULL;
}
void *dc_arena_malloc(size_t bytes){return dc_arena_memalign(32,bytes);}
void *dc_arena_calloc(size_t count,size_t bytes){
 void *p;
 if(bytes&&count>SIZE_MAX/bytes){++dc_arena_slab_failures;return NULL;}
 p=dc_arena_malloc(count*bytes);if(p)memset(p,0,count*bytes);return p;
}
void dc_arena_free(void *memory){
 unsigned char *p;block *b,*previous=NULL;
 if(!memory)return;
 /* Walk headers: reject foreign pointers and double frees without writing them. */
 for(p=start;p&&p<limit;p+=b->bytes){
  b=(block *)p;
  if(p+sizeof(block)==memory){
   block *next;
   if(!b->busy)break;
   b->busy=0;dc_arena_slab_live-=(uint32_t)b->bytes;
   next=(block *)(p+b->bytes);
   if((unsigned char *)next<limit&&!next->busy)b->bytes+=next->bytes;
   if(previous&&!previous->busy)previous->bytes+=b->bytes;
   return;
  }
  previous=b;
 }
 ++dc_arena_slab_failures;
}

#ifdef _arch_dreamcast
#include <kos.h>
#include <errno.h>
#include "arena_slab.h"
/* These opaque FILE tokens are used only by the redirected objects. KOS VFS
 * does the disc I/O; newlib never allocates FILEs or buffering for the renderer. */
typedef struct {file_t fd;int error;} slab_file;
FILE *dc_arena_fopen(const char *path,const char *mode){
 slab_file *f;file_t fd;
 if(strcmp(mode,"rb")){errno=EINVAL;return NULL;}
 fd=fs_open(path,O_RDONLY);
 /* instance_cells treats ENOENT as an empty cell. KOS usually sets errno;
  * a bare failure would otherwise look like a hard open error. */
 if(fd<0){if(!errno)errno=ENOENT;return NULL;}
 f=dc_arena_malloc(sizeof(*f));if(!f){fs_close(fd);errno=ENOMEM;return NULL;}
 f->fd=fd;f->error=0;return (FILE *)(void *)f;
}
int dc_arena_fclose(FILE *file){
 slab_file *f=(slab_file *)(void *)file;int result=fs_close(f->fd);
 dc_arena_free(f);return result?EOF:0;
}
size_t dc_arena_fread(void *data,size_t bytes,size_t count,FILE *file){
 slab_file *f=(slab_file *)(void *)file;size_t total,done=0;
 if(!bytes||!count)return 0;
 if(count>SIZE_MAX/bytes){f->error=1;errno=EOVERFLOW;return 0;}
 total=bytes*count;
 while(done<total){
  ssize_t n=fs_read(f->fd,(unsigned char *)data+done,total-done);
  if(n<0){f->error=1;break;}if(!n)break;done+=(size_t)n;
 }
 return done/bytes;
}
int dc_arena_fgetc(FILE *file){
 unsigned char byte;return dc_arena_fread(&byte,1,1,file)==1?byte:EOF;
}
int dc_arena_fseek(FILE *file,long offset,int whence){
 slab_file *f=(slab_file *)(void *)file;
 if(fs_seek(f->fd,offset,whence)<0){f->error=1;return -1;}return 0;
}
long dc_arena_ftell(FILE *file){
 slab_file *f=(slab_file *)(void *)file;off_t at=fs_tell(f->fd);
 if(at<0)f->error=1;
 return (long)at;
}
int dc_arena_ferror(FILE *file){return ((slab_file *)(void *)file)->error;}
#endif
