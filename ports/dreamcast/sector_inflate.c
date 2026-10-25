/* Exact, bounded zlib framing; caller commits only validated staging (AI-assisted), ISC. */
#include "sector_inflate.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <zlib.h>
typedef union dc_inflate_block dc_inflate_block;
union dc_inflate_block {
 long double alignment;
 struct {dc_inflate_block *previous,*next;size_t bytes;} h;
};
typedef struct {dc_inflate_block *head;size_t live;} dc_inflate_pool;
static voidpf pool_alloc(voidpf opaque,uInt items,uInt size){
 dc_inflate_pool *pool=opaque;dc_inflate_block *b;size_t bytes;
 if(!items || !size || pool->live>DC_INFLATE_WORKSPACE_MAX-sizeof(*b))return NULL;
 if((size_t)items>(DC_INFLATE_WORKSPACE_MAX-sizeof(*b)-pool->live)/(size_t)size)return NULL;
 bytes=(size_t)items*size+sizeof(*b);b=malloc(bytes);if(!b)return NULL;
 b->h.previous=NULL;b->h.next=pool->head;b->h.bytes=bytes;
 if(pool->head)pool->head->h.previous=b;
 pool->head=b;pool->live+=bytes;return b+1;
}
static void pool_free(voidpf opaque,voidpf allocation){
 dc_inflate_pool *pool=opaque;dc_inflate_block *b;
 if(!allocation)return;
 b=(dc_inflate_block *)allocation-1;
 if(b->h.previous)b->h.previous->h.next=b->h.next;else pool->head=b->h.next;
 if(b->h.next)b->h.next->h.previous=b->h.previous;
 pool->live-=b->h.bytes;free(b);
}
static void pool_release(dc_inflate_pool *pool){while(pool->head)pool_free(pool,pool->head+1);}
const char *dc_sector_inflate(const void *input,uint32_t input_bytes,
                             void *output,uint32_t output_bytes,uint32_t output_crc){
 z_stream stream;dc_inflate_pool pool={0};uintptr_t a=(uintptr_t)input,b=(uintptr_t)output;
 const char *error=NULL;int result,ended;
 if(!input || !output || !input_bytes || !output_bytes || input_bytes>DC_INFLATE_INPUT_MAX || output_bytes>DC_INFLATE_RAW_MAX)
  return "invalid compressed block bounds";
 if((a<=b && b-a<input_bytes) || (b<a && a-b<output_bytes))return "compressed block buffers overlap";
 memset(&stream,0,sizeof(stream));stream.zalloc=pool_alloc;stream.zfree=pool_free;stream.opaque=&pool;
 result=inflateInit(&stream);
 if(result!=Z_OK){pool_release(&pool);return "compressed block initialization failed";}
 stream.next_in=(Bytef *)input;stream.avail_in=(uInt)input_bytes;
 stream.next_out=output;stream.avail_out=(uInt)output_bytes;
 result=inflate(&stream,Z_FINISH);
 if(result!=Z_STREAM_END)error="compressed block decode failed";
 else if(stream.total_in!=input_bytes || stream.total_out!=output_bytes)error="compressed block length mismatch";
 ended=inflateEnd(&stream);pool_release(&pool);
 if(!error && ended!=Z_OK)error="compressed block cleanup failed";
 if(!error && (uint32_t)crc32(0,output,(uInt)output_bytes)!=output_crc)error="compressed block checksum mismatch";
 return error;
}
