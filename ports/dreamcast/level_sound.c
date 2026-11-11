/* Streamed dialogue for the level ELF: the IOP sound player's VAG stream (AI-assisted), ISC.
 *
 * SOUND_PLAN.md milestones 3 and 8, served inside rpc-call as the bridge does
 * every IOP RPC (synchronously, rpc-busy? 0):
 * - Loader port 1, get-irx-version (12): writes major 2, minor 0 into the
 *   command and keeps the EE address of *sound-iop-info* (srpc.cpp 384-388).
 * - Player port 0, a batch of 0x50-byte commands: play (3) of "spool-NAME"
 *   starts the dialogue line NAME (srpc.cpp 125-157): its VAGDIR.AYB record (an
 *   8-byte name and a 2048-byte sector) gives its VAGp file in VAGWAD.ENG, a
 *   48-byte big-endian header (size +12, rate +16) and SPU ADPCM. stop (5),
 *   pause (4) and continue (6) of its id stop, freeze and resume it.
 * - Each frame, *sound-iop-info* (the overlord's 0x110-byte DMA, srpc.cpp
 *   464-508): frame, strpos (the stream's position, 1024 a second, -1 when no
 *   stream plays) and the stream's id. The position follows real time, as the
 *   IOP's does (1024/fps added each vblank); the dialogue ends when the line's
 *   length has passed.
 * - The audio: blocks decoded as vag_adpcm.py (voice.cpp) does to 16-bit PCM,
 *   fed to a KOS AICA stream polled once a frame. Bank sound effects and music
 *   (SOUND_PLAN.md milestones 2, 4 to 7) are not here.
 * Files: /cd/VAG/VAGDIR.AYB and /cd/VAG/VAGWAD.ENG (make_vag_subset.py).
 * --dump dc_level_sound_evidence 96 */
#include "level_sound.h"
#include "goal_bridge.h"
#include <kos.h>
#include <dc/sound/stream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {PORT_PLAYER=0,PORT_LOADER=1,COMMAND_BYTES=0x50,SRPC_PLAY=3,SRPC_PAUSE=4,SRPC_STOP=5,SRPC_CONTINUE=6,
      SRPC_IRX=12,DIR_MAX=1024,VAG_HEADER=48,BLOCK=16,BLOCK_SAMPLES=28,PCM_SAMPLES=4096};
volatile struct {
  uint32_t irx_calls,info_address,plays,streams,missing,stops,pauses,frames;
  uint32_t last_id,last_strpos,bytes,rate,length_ms,ended,audio,polls;
  uint32_t pcm_bytes,errors,pad[2];
  char last_name[16];
} dc_level_sound_evidence;

static dc_goal_runtime *runtime;
static dc_goal_rpc_hook next_rpc;
static uint32_t info_address;
static unsigned char dir[4+12*DIR_MAX];
static uint32_t dir_count;
static int dir_loaded;
/* the playing line */
static struct {
  int active,paused;uint32_t id,bytes,rate,offset;int64_t start,paused_at,paused_total;
} line;
/* its audio */
static FILE *wad;
static snd_stream_hnd_t stream=SND_STREAM_INVALID;
static uint32_t audio_left,audio_at;int hist1,hist2;
static int16_t pcm[PCM_SAMPLES] __attribute__((aligned(32)));
/* A block's samples carry over a buffer boundary (4096 is not a multiple of
 * 28), and the wad is read RUN bytes at a time, not a seek per block. */
enum {RUN=2048};
static int16_t carry[BLOCK_SAMPLES];static int carry_at=BLOCK_SAMPLES;
static unsigned char run[RUN];static uint32_t run_at,run_len;

static uint32_t rd32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint32_t be32(const unsigned char *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
/* The stream clock: real time, microseconds. The IOP adds 1024/fps to the
 * stream position once per real vblank (srpc.cpp), so the position follows
 * the audio actually played, not the game's own clock, which runs ahead of it
 * when frames are slow (set-time-ratios lets one frame count for up to 4). */
static int64_t real_us(void){return (int64_t)(dc_goal_clock_ns()/1000u);}
static int load_dir(void){
  FILE *f;size_t n;
  if(dir_loaded)return dir_loaded>0;
  dir_loaded=-1;
  if(!(f=fopen("/cd/VAG/VAGDIR.AYB","rb")))return 0;
  n=fread(dir,1,sizeof dir,f);fclose(f);
  if(n<4 || (dir_count=rd32(dir))>DIR_MAX || 4u+12u*dir_count>n)return 0;
  dir_loaded=1;return 1;
}

/* voice.cpp: nibble in the top of a signed 16-bit word, shifted, plus the two
 * prediction terms each shifted by 6, clamped. */
static const int coef[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
static int decode_block(const unsigned char *b,int16_t *out){
  int shift=b[0]&15,filter=(b[0]>>4)&7,i;
  if(filter>4)return 0;
  for(i=0;i<BLOCK_SAMPLES;++i){
    int nibble=(b[2+i/2]>>((i&1)*4))&15,s=(int16_t)(nibble<<12);
    s>>=shift;
    s+=(coef[filter][0]*hist1)>>6;
    s+=(coef[filter][1]*hist2)>>6;
    if(s>32767)s=32767;
    if(s<-32768)s=-32768;
    hist2=hist1;hist1=s;out[i]=(int16_t)s;
  }
  return 1;
}
/* the next 16 bytes of the line, from the run buffer; consumed only once
 * decoded (consume_block), so a bad block is not skipped */
static const unsigned char *next_block(void){
  if(audio_left<BLOCK || !wad)return NULL;
  if(run_at>=run_len){
    uint32_t want=audio_left<RUN?audio_left&~(BLOCK-1u):RUN;
    if(fseek(wad,(long)audio_at,SEEK_SET) || fread(run,1,want,wad)!=want)return NULL;
    run_at=0;run_len=want;
  }
  return run+run_at;
}
static void consume_block(void){audio_at+=BLOCK;audio_left-=BLOCK;run_at+=BLOCK;}
static void *audio_data(snd_stream_hnd_t hnd,int bytes,int *got){
  int samples=0,want=bytes/2;
  (void)hnd;
  if(want>PCM_SAMPLES)want=PCM_SAMPLES;
  while(samples<want && line.active){
    if(carry_at<BLOCK_SAMPLES){
      int n=BLOCK_SAMPLES-carry_at;if(n>want-samples)n=want-samples;
      memcpy(pcm+samples,carry+carry_at,(size_t)n*2u);carry_at+=n;samples+=n;continue;
    }
    {const unsigned char *b=next_block();
     if(!b || !decode_block(b,carry))break;
     consume_block();carry_at=0;}
  }
  if(samples<want)memset(pcm+samples,0,(size_t)(want-samples)*2u);  /* the line's end: silence */
  dc_level_sound_evidence.pcm_bytes+=(uint32_t)samples*2u;
  *got=want*2;
  return pcm;
}
static void start_audio(void){
  if(stream==SND_STREAM_INVALID){
    if(snd_stream_init()<0){++dc_level_sound_evidence.errors;return;}
    stream=snd_stream_alloc(audio_data,SND_STREAM_BUFFER_MAX);
    if(stream==SND_STREAM_INVALID){++dc_level_sound_evidence.errors;return;}
  } else snd_stream_stop(stream);
  hist1=hist2=0;audio_at=line.offset;audio_left=line.bytes;carry_at=BLOCK_SAMPLES;run_at=run_len=0;
  snd_stream_start(stream,line.rate,0);
  ++dc_level_sound_evidence.audio;
}
static void play_line(const char *name,uint32_t id){
  char key[8];unsigned i;unsigned char header[VAG_HEADER];uint32_t sector=0,found=0;
  for(i=0;i<8;++i){char c=i<15 && name[i]?name[i]:' ';key[i]=c>='a' && c<='z'?(char)(c-32):c;}
  for(i=0;i<15 && name[i] && name[i]!=' ';++i)dc_level_sound_evidence.last_name[i]=name[i];
  dc_level_sound_evidence.last_name[i]=0;
  if(load_dir())
    for(i=0;i<dir_count && !found;++i)if(!memcmp(dir+4+12*i,key,8)){sector=rd32(dir+12+12*i);found=1;}
  if(!found || (!wad && !(wad=fopen("/cd/VAG/VAGWAD.ENG","rb"))) ||
     fseek(wad,(long)sector*2048,SEEK_SET) || fread(header,1,VAG_HEADER,wad)!=VAG_HEADER || memcmp(header,"VAGp",4)){
    ++dc_level_sound_evidence.missing;return;
  }
  line.bytes=be32(header+12);line.rate=be32(header+16);
  if(!line.bytes || (line.bytes&15u) || line.rate<8000 || line.rate>48000){++dc_level_sound_evidence.missing;return;}
  line.active=1;line.paused=0;line.id=id;line.offset=sector*2048u+VAG_HEADER;
  line.start=real_us();line.paused_total=0;
  ++dc_level_sound_evidence.streams;dc_level_sound_evidence.last_id=id;
  dc_level_sound_evidence.bytes=line.bytes;dc_level_sound_evidence.rate=line.rate;
  dc_level_sound_evidence.length_ms=(uint32_t)((uint64_t)line.bytes/BLOCK*BLOCK_SAMPLES*1000u/line.rate);
  start_audio();
}
static void stop_line(void){
  line.active=0;line.id=1;  /* StopVAG: gVAG_Id = 1 (iso.cpp) */
  if(stream!=SND_STREAM_INVALID)snd_stream_stop(stream);
}
static uint64_t sound_rpc(uint64_t port,uint64_t fno,uint64_t async,uint64_t send,
                          uint64_t send_bytes,uint64_t recv,uint64_t recv_bytes){
  unsigned char *cmd;uint32_t n,i;
  if(!runtime || (port!=PORT_PLAYER && port!=PORT_LOADER))
    return next_rpc?next_rpc(port,fno,async,send,send_bytes,recv,recv_bytes):0;
  if(send>UINT32_MAX || send_bytes<4 || send_bytes>128u*COMMAND_BYTES)return 0;
  n=(uint32_t)(send_bytes+COMMAND_BYTES-1)/COMMAND_BYTES;
  for(i=0;i<n;++i){
    uint32_t at=(uint32_t)send+i*COMMAND_BYTES;uint16_t command;
    if(!(cmd=dc_goal_resolve(runtime,at,i+1<n?COMMAND_BYTES:(uint32_t)send_bytes-i*COMMAND_BYTES))){runtime->error=DC_GOAL_OK;break;}
    memcpy(&command,cmd+2,2);
    if(port==PORT_LOADER && command==SRPC_IRX && send_bytes>=16){
      uint32_t major=2,minor=0;
      memcpy(cmd+4,&major,4);memcpy(cmd+8,&minor,4);
      info_address=rd32(cmd+12);dc_level_sound_evidence.info_address=info_address;++dc_level_sound_evidence.irx_calls;
    } else if(port==PORT_PLAYER && send_bytes>=(uint64_t)(i+1)*COMMAND_BYTES){
      uint32_t id=rd32(cmd+4);
      if(command==SRPC_PLAY && id){
        ++dc_level_sound_evidence.plays;
        if(!memcmp(cmd+16,"spool-",6)){char name[11];memcpy(name,cmd+22,10);name[10]=0;play_line(name,id);}
      } else if(command==SRPC_STOP && line.active && id==line.id){++dc_level_sound_evidence.stops;stop_line();}
      else if(command==SRPC_PAUSE && line.active && id==line.id && !line.paused){
        ++dc_level_sound_evidence.pauses;line.paused=1;line.paused_at=real_us();
        /* PauseVAG silences the voice; the decoder keeps its place */
        if(stream!=SND_STREAM_INVALID)snd_stream_stop(stream);}
      else if(command==SRPC_CONTINUE && line.active && id==line.id && line.paused){
        line.paused=0;line.paused_total+=real_us()-line.paused_at;
        if(stream!=SND_STREAM_INVALID)snd_stream_start(stream,line.rate,0);}
    }
  }
  return 0;
}

void dc_level_sound_frame(dc_goal_runtime *rt){
  unsigned char *info;int32_t strpos=-1;uint32_t frame;enum dc_goal_error prior;
  if(!rt || rt!=runtime || !info_address)return;
  prior=rt->error;
  if(line.active){
    int64_t now=line.paused?line.paused_at:real_us();
    int64_t played=(now-line.start-line.paused_total)*1024/1000000;
    int64_t length=(int64_t)line.bytes/BLOCK*BLOCK_SAMPLES*1024/line.rate;
    if(played<0)played=0;
    if(played>=length){stop_line();++dc_level_sound_evidence.ended;}
    else strpos=(int32_t)played;
  }
  if((info=dc_goal_resolve(rt,info_address,12))){
    frame=++dc_level_sound_evidence.frames;
    memcpy(info,&frame,4);memcpy(info+4,&strpos,4);memcpy(info+8,&line.id,4);
  }
  dc_level_sound_evidence.last_strpos=(uint32_t)strpos;
  if(stream!=SND_STREAM_INVALID && line.active && !line.paused){snd_stream_poll(stream);++dc_level_sound_evidence.polls;}
  rt->error=prior;
}
void dc_level_sound_install(dc_goal_runtime *rt){
  runtime=rt;
  if(dc_goal_rpc_handler==sound_rpc)return;  /* already in front */
  next_rpc=dc_goal_rpc_handler;dc_goal_rpc_handler=sound_rpc;
}
void dc_level_sound_summary(dc_level_sound_counts *out){
  out->irx_calls=dc_level_sound_evidence.irx_calls;out->streams=dc_level_sound_evidence.streams;
  out->ended=dc_level_sound_evidence.ended;out->missing=dc_level_sound_evidence.missing;
  out->pcm_bytes=dc_level_sound_evidence.pcm_bytes;out->errors=dc_level_sound_evidence.errors;
}
