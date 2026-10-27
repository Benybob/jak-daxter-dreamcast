/* A zstd-compressed DGO read as a stdio stream (AI-assisted), ISC. */
#define _DEFAULT_SOURCE 1  /* newlib funopen */
#include "goal_dgo_zst.h"
#include <stdlib.h>
#include "zstd.h"
#define DC_ZST_INPUT 4096u
#define DC_ZST_WINDOW_LOG 17  /* build_kernel_boot.py compresses with wlog=17 */
typedef struct {
  FILE *file;ZSTD_DCtx *dctx;ZSTD_inBuffer in;int done,failed;
  unsigned char input[DC_ZST_INPUT];
} dc_zst_stream;
static int zst_read(void *cookie,char *buf,int n){
  dc_zst_stream *s=cookie;
  ZSTD_outBuffer out={buf,(size_t)n,0};
  if(s->failed)return -1;
  while(out.pos<out.size && !s->done){
    size_t hint;
    if(s->in.pos==s->in.size){
      s->in.size=fread(s->input,1,DC_ZST_INPUT,s->file);s->in.pos=0;
      if(!s->in.size){s->failed=1;return -1;}  /* truncated frame */
    }
    hint=ZSTD_decompressStream(s->dctx,&out,&s->in);
    if(ZSTD_isError(hint)){s->failed=1;return -1;}
    if(!hint)s->done=1;  /* one frame, fully decoded and checked */
  }
  return (int)out.pos;
}
static int zst_close(void *cookie){
  dc_zst_stream *s=cookie;int status=fclose(s->file);
  ZSTD_freeDCtx(s->dctx);free(s);return status;
}
FILE *dc_goal_open_zst(const char *path){
  dc_zst_stream *s=calloc(1,sizeof *s);FILE *file;
  if(!s)return NULL;
  s->file=fopen(path,"rb");s->dctx=ZSTD_createDCtx();s->in.src=s->input;
  if(!s->file || !s->dctx ||
     ZSTD_isError(ZSTD_DCtx_setParameter(s->dctx,ZSTD_d_windowLogMax,DC_ZST_WINDOW_LOG)))goto fail;
  file=funopen(s,zst_read,NULL,NULL,zst_close);
  if(file)return file;
fail:
  if(s->file)fclose(s->file);
  ZSTD_freeDCtx(s->dctx);free(s);return NULL;
}
