/* Independent native same-stack snapshot acceptance (AI-assisted), ISC. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "goal_register_snapshot.h"
#include "runtime_cases.h"

typedef struct {
 uint32_t expected_sp,expected_pr,result,after_sp,after_pr,after_fpul,after_fpscr,after_arg;
 uint32_t gpr[7],fr[4],sr_before,sr_after;
} snapshot_observation;
typedef char observation_size[sizeof(snapshot_observation)==84?1:-1];
typedef char observation_fr[offsetof(snapshot_observation,fr)==60?1:-1];
typedef char observation_sr[offsetof(snapshot_observation,sr_before)==76?1:-1];
extern void dc_goal_snapshot_probe(void *,snapshot_observation *,uint32_t,const uint32_t *);

static float from_bits(uint32_t word) { float value; memcpy(&value,&word,4); return value; }
static uint32_t bits(float value) { uint32_t word; memcpy(&word,&value,4); return word; }
/* The independent GCC caller has its own register and stack canaries. */
static unsigned guarded_probe(void *out,snapshot_observation *obs,uint32_t mode,const uint32_t *pattern) {
 register uint32_t a __asm__("r8")=0x10203040,b __asm__("r9")=0xaabbccdd;
 register uint32_t c __asm__("r10")=0x31415926,d __asm__("r11")=0xabcdef01;
 register uint32_t e __asm__("r12")=0x76543210,f __asm__("r13")=0xfedcba98;
 register float x __asm__("fr12")=from_bits(0x7fc12345),y __asm__("fr13")=from_bits(0x80000000);
 register float z __asm__("fr14")=from_bits(0x00000001),w __asm__("fr15")=from_bits(0xbf800000);
 uint32_t before_fpul,before_fpscr,after_fpul,after_fpscr;
 volatile uint32_t guards[4]={0x11112222,0x33334444,0x55556666,0x77778888};
 __asm__ volatile("sts fpul,%0\n\tsts fpscr,%1"
  : "=r"(before_fpul),"=r"(before_fpscr),"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),
    "+f"(x),"+f"(y),"+f"(z),"+f"(w) :: "memory");
 dc_goal_snapshot_probe(out,obs,mode,pattern);
 __asm__ volatile("sts fpul,%0\n\tsts fpscr,%1"
  : "=r"(after_fpul),"=r"(after_fpscr),"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),
    "+f"(x),"+f"(y),"+f"(z),"+f"(w) :: "memory");
 return a==0x10203040 && b==0xaabbccdd && c==0x31415926 && d==0xabcdef01 &&
  e==0x76543210 && f==0xfedcba98 && before_fpul==after_fpul && before_fpscr==after_fpscr &&
  bits(x)==0x7fc12345 && bits(y)==0x80000000 && bits(z)==1 && bits(w)==0xbf800000 &&
  guards[0]==0x11112222 && guards[1]==0x33334444 && guards[2]==0x55556666 && guards[3]==0x77778888;
}
static int filled(const void *memory,size_t bytes,unsigned char value) {
 const unsigned char *p=memory; size_t i; for(i=0;i<bytes;++i) if(p[i]!=value) return 0; return 1;
}
void dc_runtime_register_snapshot_cases(dc_runtime_check check,void *context,unsigned *id) {
 static const uint32_t floats[4][4]={
  {0,0x80000000,0x7fc12345,1},{0x3f800000,0xff800000,0x7faabcde,0x80000001},
  {0x7f800000,0xffc54321,0x007fffff,0x80800000},{0xffffffff,0x12345678,0x87654321,0x00800000}};
 static const uint32_t modes[4]={0,0x00040000,1,0x0004007d};
 struct {uint32_t low[4];struct dc_goal_native_regs image;uint32_t high[5];} out;
 struct {uint32_t low[4];snapshot_observation state;uint32_t high[4];} observed;
 uint32_t expected[15],pattern[12],mode,kind,round,i,p,valid_mode;
 unsigned char before[sizeof out];
 void *destination;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 CHECK(sizeof(out.image),60,"native snapshot layout is60 bytes");
 CHECK(sizeof(observed.state),84,"independent native observation layout is84 bytes");
 for(p=0;p<4;++p) {
  for(i=0;i<7;++i) pattern[i]=(0x13579bdfu*(i+1))^(0xf0e1d2c3u*p);
  pattern[7]=0x87654321u^(p*0x55555555u);
  memcpy(pattern+8,floats[p],16);
  for(round=0;round<11;++round) {
   valid_mode=round<4;
   mode=valid_mode?modes[round]:((round-3)<<19)|0x0004007d;
   for(kind=0;kind<(valid_mode?5u:1u);++kind) {
    memset(&out,0xa5,sizeof out); memcpy(before,&out,sizeof out);
    memset(&observed,0x5b,sizeof observed);
    destination=kind==4?NULL:(unsigned char *)&out.image+kind;
    CHECK(guarded_probe(destination,&observed.state,mode,pattern),1,"snapshot probe restores independent GCC caller registers/status/stack");
    CHECK(observed.state.result,valid_mode && kind==0,"snapshot returns exact mode/pointer acceptance");
    CHECK(observed.state.after_sp,observed.state.expected_sp,"snapshot leaves entry SP unchanged");
    CHECK(observed.state.after_pr,observed.state.expected_pr,"snapshot leaves exact JSR return-site PR unchanged");
    CHECK(observed.state.after_fpul,pattern[7],"snapshot preserves every FPUL bit");
    CHECK(observed.state.after_fpscr,mode,"snapshot preserves full tested FPSCR including flags/mode");
    CHECK(observed.state.after_arg,(uint32_t)(uintptr_t)destination,"snapshot leaves input R4 unchanged");
    for(i=0;i<7;++i) CHECK(observed.state.gpr[i],pattern[i],"snapshot preserves patterned R8..R14");
    for(i=0;i<4;++i) CHECK(observed.state.fr[i],pattern[8+i],"snapshot preserves raw FPR payloads without arithmetic");
    CHECK((observed.state.sr_before^observed.state.sr_after)&~1u,0,"snapshot preserves SR bits except caller-clobbered T");
    CHECK(observed.state.expected_sp&3,0,"measured probe SP follows four-byte ABI boundary");
    CHECK(observed.state.expected_pr!=0 && !(observed.state.expected_pr&1),1,"measured exact PR is nonzero instruction-aligned");
    CHECK(filled(observed.low,sizeof observed.low,0x5b),1,"observation lower canary untouched");
    CHECK(filled(observed.high,sizeof observed.high,0x5b),1,"observation upper canary untouched");
    CHECK(filled(out.low,sizeof out.low,0xa5),1,"snapshot lower canary untouched");
    CHECK(filled(out.high,sizeof out.high,0xa5),1,"snapshot upper canary untouched");
    if(valid_mode && kind==0) {
     memcpy(expected,pattern,28);expected[7]=observed.state.expected_pr;expected[8]=observed.state.expected_sp;
     expected[9]=pattern[7];expected[10]=mode;memcpy(expected+11,pattern+8,16);
     for(i=0;i<15;++i) {uint32_t actual;memcpy(&actual,(unsigned char *)&out.image+4*i,4);
      CHECK(actual,expected[i],"snapshot field matches independent integer/NaN/denormal/state oracle");}
    } else CHECK(memcmp(&out,before,sizeof out),0,"rejected snapshot leaves complete destination and guards untouched");
   }
  }
 }
#undef CHECK
}
