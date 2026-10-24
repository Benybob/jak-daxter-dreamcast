"""Real controller/animation/framebuffer playable-slice oracle (AI-assisted)."""
import argparse,hashlib,json,os,re,shutil,struct,subprocess,time
from pathlib import Path
LUA=r'''
local phase=0
local start_frame=0
local stopped_frame=0
local ticks=0
local done=false
local function word(offset) return flycast.memory.read32(ADDRESS+offset) end
local function snapshot(name)
  local f=assert(io.open(OUT..'/'..name..'.bin','wb'))
  for i=0,127 do f:write(string.char(flycast.memory.read8(ADDRESS+i))) end
  for i=0,55 do f:write(string.char(flycast.memory.read8(PLAYER+i))) end
  for i=0,3 do f:write(string.char(flycast.memory.read8(CLIP+i))) end
  f:close()
end
local function capture(name)
  local control=flycast.memory.read32(0xa05f8044)
  assert(((control >> 2)&3)==1,'expected RGB565 framebuffer')
  local offset=flycast.memory.read32(0xa05f8050)&0x7fffff
  local f=assert(io.open(OUT..'/'..name..'.ppm','wb'))
  f:write('P6\n640 480\n255\n')
  for y=0,479 do
    local row={}
    for x=0,639 do
      local c=flycast.memory.read16(0xa5000000+offset+(y*640+x)*2)
      row[#row+1]=string.char(math.floor(((c >> 11)&31)*255/31),math.floor(((c >> 5)&63)*255/63),math.floor((c&31)*255/31))
    end
    f:write(table.concat(row))
  end
  f:close()
end
local function finish(name)
  snapshot(name)
  local f=assert(io.open(OUT..'/done.txt','w')); f:write(name); f:close()
  done=true
end
local mark=0
local platform=PLATFORM
local function vblank()
 if done then return end
 ticks=ticks+1
 if word(0)~=0x3243444a then return end
 local state=word(8);local frames=word(40)
 if state==4 then finish('error');return end
 if state==3 then finish('exit');return end
 if state~=2 then return end
 if phase==0 and frames>=60 then snapshot('idle');capture('idle');phase=1;mark=frames
 elseif phase==1 and frames>=mark+60 then snapshot('walk');capture('walk');phase=2;mark=frames
 elseif phase==2 and frames>=mark+60 then snapshot('run');capture('run');phase=3;mark=frames
 elseif phase==3 and frames>=mark+(platform and 30 or 12) then snapshot('air');capture('air');phase=4;mark=frames
 elseif phase==4 and frames>=mark+90 then snapshot('land');capture('land');phase=5;mark=frames
 elseif phase==5 and frames>=mark+15 then phase=6;mark=frames
 elseif phase==6 and frames>=mark+12 then snapshot('second_jump');phase=7;mark=frames
 elseif phase==7 and frames>=mark+90 then snapshot('final_idle');phase=8;mark=frames
 elseif phase==8 and frames>=mark+30 then snapshot('camera');capture('camera');phase=9;flycast.input.pressButtons(1,8)
 end
 if ticks>=1500 then finish('timeout') end
end
local function overlay()
 flycast.input.setAxis(1,1,platform and ((phase==1 or phase==2 or phase==3) and 32767 or 0) or (phase==3 and -16384 or 0))
 flycast.input.setAxis(1,2,platform and (phase==7 and -32768 or 0) or (phase==1 and -16384 or (phase==2 and -32768 or 0)))
 flycast.input.setAxis(1,6,phase==8 and 50000 or 0)
 if phase==3 or phase==4 or phase==6 then flycast.input.pressButtons(1,4) else flycast.input.releaseButtons(1,4) end
end
flycast_callbacks={vblank=vblank,overlay=overlay}
'''

def read_snapshot(path):
 b=path.read_bytes()
 if len(b)!=188:raise ValueError('short playable snapshot')
 names=('x','y','z','vx','vy','vz','yaw','anim_time','grounded','jump_held','mode','jumps','landings','ticks')
 return dict(scene=dict(zip(('magic','version','state','source','triangles','geometry_bytes','system_ram','allocator_live','allocator_arena','vram_free','frames','yaw','pitch','distance','pvr_fps','frame_us'),struct.unpack_from('<11I4fI',b))),player=dict(zip(names,struct.unpack_from('<8f6I',b,128))),clip=struct.unpack_from('<I',b,184)[0],error=b[64:128].split(b'\0')[0].decode(errors='replace'))
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('flycast',type=Path);ap.add_argument('elf',type=Path);ap.add_argument('--nm',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--platform',action='store_true');a=ap.parse_args()
 symbols=subprocess.check_output([str(a.nm),str(a.elf)],text=True)
 def address(name):return int(re.search(r'^([0-9a-fA-F]+)\s+\w\s+_?'+name+r'$',symbols,re.M)[1],16)
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 for pattern in ('*.bin','*.ppm','done.txt','result.json'):
  for old in out.glob(pattern):old.unlink()
 exe=a.flycast.resolve();env=os.environ.copy();startup=None
 if os.name=='nt':
  exe=out/'flycast.exe';shutil.copy2(a.flycast,exe)
  lua_path=out/'verify.lua'
  startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
 else:
  lua_path=out/'config/flycast/verify.lua';lua_path.parent.mkdir(parents=True,exist_ok=True)
  env.update(SDL_VIDEODRIVER='offscreen',LIBGL_ALWAYS_SOFTWARE='1',XDG_CONFIG_HOME=str(out/'config'),XDG_DATA_HOME=str(out/'data'))
 lua=LUA.replace('PLATFORM','true' if a.platform else 'false').replace('ADDRESS',str(address('dc_telemetry'))).replace('PLAYER',str(address('dc_player_state'))).replace('CLIP',str(address('dc_play_clip'))).replace('OUT',json.dumps(out.as_posix()))
 lua_path.write_text(lua)
 command=[str(exe),'-config','config:UseReios=yes','-config','config:Dreamcast.RamMod32MB=no','-config','config:Debug.SerialConsoleEnabled=no','-config','config:LuaFileName=verify.lua','-config','config:rend.EmulateFramebuffer=yes','-config','audio:backend=null','-config','log:LogToFile=yes',str(a.elf.resolve())]
 with (out/'frontend.log').open('w') as log:
  proc=subprocess.Popen(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,startupinfo=startup)
  try:
   deadline=time.monotonic()+50
   while proc.poll() is None and time.monotonic()<deadline and not (out/'done.txt').exists():time.sleep(.1)
  finally:
   if proc.poll() is None:
    proc.terminate()
    try:proc.wait(timeout=5)
    except subprocess.TimeoutExpired:proc.kill();proc.wait()
 final=(out/'done.txt').read_text().strip() if (out/'done.txt').exists() else 'no completion'
 if final!='exit':ap.exit(1,'FAIL: '+final+'; inspect target log\n')
 snapshots={n:read_snapshot(out/(n+'.bin')) for n in ('idle','walk','run','air','land','second_jump','final_idle','camera','exit')}
 p={k:v['player'] for k,v in snapshots.items()};s={k:v['scene'] for k,v in snapshots.items()}
 checks=dict(ram_16mib=all(x['system_ram']==16777216 for x in s.values()),idle=p['idle']['mode']==0,
  walk=p['walk']['mode']==1 and p['walk']['z']>p['idle']['z']+.7,
  run=p['run']['mode']==2 and p['run']['z']-p['walk']['z']>2*(p['walk']['z']-p['idle']['z']),
  airborne=p['air']['mode']==3 and p['air']['y']>.7 and p['air']['jumps']==1,
  landing=p['land']['grounded']==1 and abs(p['land']['y'])<.01 and p['land']['landings']==1,
  held_jump_no_repeat=p['land']['jumps']==1,second_press=p['second_jump']['jumps']==2 and p['second_jump']['y']>.7,
  second_landing=p['final_idle']['grounded']==1 and p['final_idle']['landings']==2,
  animation_states=[snapshots[k]['clip'] for k in ('idle','walk','run')]==[0,1,2] and snapshots['air']['clip'] in (3,4),
  follow_motion=p['run']['z']>4 and snapshots['idle']['scene']['distance']==snapshots['run']['scene']['distance'],
  camera=s['camera']['yaw']>s['final_idle']['yaw']+.2,cleanup=s['exit']['state']==3,
  framebuffer=all((out/(n+'.ppm')).stat().st_size==921615 for n in ('idle','walk','run','air','land','camera')))
 if a.platform:
  checks=dict(ram_16mib=all(x['system_ram']==16777216 for x in s.values()),
   blocked_by_platform=1.70<p['walk']['x']<1.73 and p['walk']['y']==0,
   airborne=p['air']['y']>.8 and p['air']['jumps']==1,
   platform_landing=p['land']['grounded']==1 and abs(p['land']['y']-.55)<.01 and 2<p['land']['x']<4.28,
   held_jump_no_repeat=p['land']['jumps']==1,
   second_jump_from_platform=p['second_jump']['jumps']==2 and p['second_jump']['y']>1.2,
   walked_off_platform=p['final_idle']['grounded']==1 and p['final_idle']['y']==0 and p['final_idle']['z']>2.28,
   cleanup=s['exit']['state']==3,
   framebuffer=all((out/(n+'.ppm')).stat().st_size==921615 for n in ('idle','air','land')))
 result=dict(ai_assisted=True,result='PASS' if all(checks.values()) else 'FAIL',elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),checks=checks,snapshots=snapshots,scope='Native C test-map prototype using locally baked original skeletal clips; not original GOAL gameplay or hardware validation')
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(result=result['result'],checks=checks),indent=2))
 if result['result']!='PASS':raise SystemExit(1)
if __name__=='__main__':main()
