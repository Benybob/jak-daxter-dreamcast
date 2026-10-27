/* Native counterparts of the C++ kernel's cpad-open and cpad-get-data
 * (game/kernel/common/kmachine.cpp) on the Dreamcast maple bus (AI-assisted), ISC.
 *
 * The CPadOpen/CPadGetData state machine is the desktop one, unchanged in
 * structure. Its libpad calls get the answers desktop libpad.cpp gives (a
 * DualShock 2 in DualShock mode, synchronous requests), except:
 * - scePadGetState reports the maple controller actually present in that port;
 * - there are no actuators yet (no jump-pack rumble), which the original code
 *   handles by skipping vibration setup.
 * scePadRead fills the original 32-byte hw-cpad layout. The mapping is a first
 * choice: A=cross, B=circle, X=square, Y=triangle, Start=start, D-pad=d-pad,
 * L/R triggers past half travel = L1/R1, analog stick = left stick, right stick
 * centered. */
#include "goal_pad.h"
#include <string.h>
#ifdef __DREAMCAST__
#include <dc/maple.h>
#include <dc/maple/controller.h>
#endif

enum {STATE_DISCON=0,STATE_FINDCTP1=2,STATE_STABLE=6,
      INFO_CUR_ID=1,INFO_CUR_EX_ID=2,INFO_CUR_EX_OFFS=3,INFO_ID_TABLE=4,
      REQ_COMPLETE=0,REQ_FAILED=1,MODE_CONTROLLER=4,MODE_DUALSHOCK2=7};
/* Byte offsets in the original 132-byte cpad-info (kernel_types.h CPadInfo). */
enum {OFF_VALID=0,OFF_STATUS=1,OFF_BUTTON0=2,OFF_RIGHTX=4,OFF_RIGHTY=5,OFF_LEFTX=6,OFF_LEFTY=7,
      OFF_ABUTTON=8,OFF_NUMBER=32,OFF_CPAD_FILE=36,OFF_NEW_PAD=76,OFF_STATE=80,OFF_CHANGE_TIME=124};
enum {BTN_START=3,BTN_UP=4,BTN_RIGHT=5,BTN_DOWN=6,BTN_LEFT=7,BTN_L1=10,BTN_R1=11,
      BTN_TRIANGLE=12,BTN_CIRCLE=13,BTN_X=14,BTN_SQUARE=15};

static int32_t get32(const uint8_t *p,unsigned off){int32_t v;memcpy(&v,p+off,4);return v;}
static void put32(uint8_t *p,unsigned off,int32_t v){memcpy(p+off,&v,4);}

int dc_goal_pad_read_state(int port,dc_goal_pad_state *out){
  memset(out,0,sizeof *out);
#ifdef __DREAMCAST__
  {
    maple_device_t *dev=maple_enum_type(port,MAPLE_FUNC_CONTROLLER);
    cont_state_t *st=dev?(cont_state_t*)maple_dev_status(dev):NULL;
    if(!st)return 0;
    out->buttons=st->buttons;out->ltrig=st->ltrig;out->rtrig=st->rtrig;
    out->joyx=st->joyx;out->joyy=st->joyy;
    return 1;
  }
#else
  (void)port;
  return 0;
#endif
}

/* The 32-byte hw-cpad part, as desktop scePadRead fills it (pressed = 1). */
void dc_goal_pad_fill(uint8_t *cpad,const dc_goal_pad_state *s){
  uint16_t b=0;
  if(s->buttons&DC_PAD_A)b|=1u<<BTN_X;
  if(s->buttons&DC_PAD_B)b|=1u<<BTN_CIRCLE;
  if(s->buttons&DC_PAD_X)b|=1u<<BTN_SQUARE;
  if(s->buttons&DC_PAD_Y)b|=1u<<BTN_TRIANGLE;
  if(s->buttons&DC_PAD_START)b|=1u<<BTN_START;
  if(s->buttons&DC_PAD_UP)b|=1u<<BTN_UP;
  if(s->buttons&DC_PAD_DOWN)b|=1u<<BTN_DOWN;
  if(s->buttons&DC_PAD_LEFT)b|=1u<<BTN_LEFT;
  if(s->buttons&DC_PAD_RIGHT)b|=1u<<BTN_RIGHT;
  if(s->ltrig>=128)b|=1u<<BTN_L1;
  if(s->rtrig>=128)b|=1u<<BTN_R1;
  cpad[OFF_VALID]=0;
  cpad[OFF_STATUS]=0x70|(20/2);  /* DualShock 2, 20 bytes of data */
  memcpy(cpad+OFF_BUTTON0,&b,2);
  cpad[OFF_RIGHTX]=128;cpad[OFF_RIGHTY]=128;
  cpad[OFF_LEFTX]=(uint8_t)(s->joyx<-128?0:s->joyx>127?255:s->joyx+128);
  cpad[OFF_LEFTY]=(uint8_t)(s->joyy<-128?0:s->joyy>127?255:s->joyy+128);
  memset(cpad+OFF_ABUTTON,0,12);  /* not in pressure mode */
}

/* libpad answers (game/sce/libpad.cpp), Dreamcast presence and no actuators. */
static int pad_get_state(int port){dc_goal_pad_state s;return dc_goal_pad_read_state(port,&s)?STATE_STABLE:STATE_DISCON;}
static int pad_info_mode(int term,int offs){
  if(term==INFO_CUR_EX_ID || term==INFO_CUR_ID)return MODE_DUALSHOCK2;
  if(term==INFO_CUR_EX_OFFS)return 1;
  if(term==INFO_ID_TABLE)return offs==-1?2:offs==0?MODE_CONTROLLER:offs==1?MODE_DUALSHOCK2:0;
  return 0;
}

void dc_goal_cpad_open(uint8_t *cpad,int32_t pad_number){
  if(get32(cpad,OFF_CPAD_FILE)==0){
    put32(cpad,OFF_CPAD_FILE,pad_number+1);  /* scePadPortOpen: port + 1, always succeeds */
    put32(cpad,OFF_NEW_PAD,1);
    put32(cpad,OFF_STATE,0);
  }
}

void dc_goal_cpad_get_data(uint8_t *cpad){
  int number=get32(cpad,OFF_NUMBER),state=get32(cpad,OFF_STATE);
  int pad_state=pad_get_state(number);
  if(pad_state==STATE_DISCON){state=0;put32(cpad,OFF_STATE,0);}
  cpad[OFF_VALID]=(uint8_t)(pad_state|0x80);
  switch(state){
  default:  /* controller functioning as normal */
    if(pad_state==STATE_STABLE || pad_state==STATE_FINDCTP1){
      dc_goal_pad_state s;
      if(dc_goal_pad_read_state(number,&s))dc_goal_pad_fill(cpad,&s);
      /* change-time set: scePadSetActDirect, a no-op without actuators */
      cpad[OFF_VALID]=(uint8_t)pad_state;
    }
    break;
  case 0:  /* unavailable */
    if(pad_state==STATE_STABLE || pad_state==STATE_FINDCTP1){
      int mode=pad_info_mode(INFO_CUR_ID,0);
      if(mode!=0){
        int vibration=pad_info_mode(INFO_CUR_EX_ID,0);
        if(vibration>0)mode=vibration;
        put32(cpad,OFF_STATE,mode==MODE_CONTROLLER?40:mode==MODE_DUALSHOCK2?70:90);
      }
    }
    break;
  case 40:
    memset(cpad+OFF_CHANGE_TIME,0,8);
    if(pad_info_mode(INFO_ID_TABLE,-1)==0){put32(cpad,OFF_STATE,90);break;}
    put32(cpad,OFF_STATE,41);
    /* scePadSetMainMode succeeds, as in libpad.cpp */
    /* fall through */
  case 41:
    put32(cpad,OFF_STATE,42);
    break;
  case 42:  /* scePadGetReqState is complete: back to the beginning */
    put32(cpad,OFF_STATE,0);
    break;
  case 70:  /* no actuators: no vibration, done */
    memset(cpad+OFF_CHANGE_TIME,0,8);
    put32(cpad,OFF_STATE,99);
    break;
  case 90:
    break;
  }
}
