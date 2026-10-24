"""Original-terrain prototype controller/boundary oracle (AI-assisted), ISC.

Captures frames for separate visual review; file size does not prove visibility.
"""
import argparse,hashlib,json,os,re,shutil,struct,subprocess,time
from pathlib import Path
LUA="\nlocal phase=0\nlocal start_frame=0\nlocal stopped_frame=0\nlocal ticks=0\nlocal done=false\nlocal function word(offset) return flycast.memory.read32(ADDRESS+offset) end\nlocal function snapshot(name)\n  local f=assert(io.open(OUT..'/'..name..'.bin','wb'))\n  for i=0,127 do f:write(string.char(flycast.memory.read8(ADDRESS+i))) end\n  for i=0,55 do f:write(string.char(flycast.memory.read8(PLAYER+i))) end\n  for i=0,3 do f:write(string.char(flycast.memory.read8(CLIP+i))) end\n  f:close()\nend\nlocal function capture(name)\n  local control=flycast.memory.read32(0xa05f8044)\n  assert(((control >> 2)&3)==1,'expected RGB565 framebuffer')\n  local offset=flycast.memory.read32(0xa05f8050)&0x7fffff\n  local f=assert(io.open(OUT..'/'..name..'.ppm','wb'))\n  f:write('P6\\n640 480\\n255\\n')\n  for y=0,479 do\n    local row={}\n    for x=0,639 do\n      local c=flycast.memory.read16(0xa5000000+offset+(y*640+x)*2)\n      row[#row+1]=string.char(math.floor(((c >> 11)&31)*255/31),math.floor(((c >> 5)&63)*255/63),math.floor((c&31)*255/31))\n    end\n    f:write(table.concat(row))\n  end\n  f:close()\nend\nlocal function finish(name)\n  snapshot(name)\n  local f=assert(io.open(OUT..'/done.txt','w')); f:write(name); f:close()\n  done=true\nend\nlocal mark=0\nlocal platform=PLATFORM\nlocal function vblank()\n if done then return end\n ticks=ticks+1\n if word(0)~=0x3243444a then return end\n local state=word(8);local frames=flycast.memory.read32(TICKS)\n if state==4 then finish('error');return end\n if state==3 then finish('exit');return end\n if state~=2 then return end\n if phase==0 and frames>=60 then snapshot('idle');capture('idle');phase=1;mark=frames\n elseif phase==1 and frames>=mark+40 then snapshot('walk');capture('walk');phase=2;mark=frames\n elseif phase==2 and frames>=mark+30 then snapshot('run');capture('run');phase=3;mark=frames\n elseif phase==3 and frames>=mark+(platform and 30 or 12) then snapshot('air');capture('air');phase=4;mark=frames\n elseif phase==4 and frames>=mark+90 then snapshot('land');capture('land');phase=5;mark=frames\n elseif phase==5 and frames>=mark+15 then phase=6;mark=frames\n elseif phase==6 and frames>=mark+12 then snapshot('second_jump');phase=7;mark=frames\n elseif phase==7 and frames>=mark+90 then snapshot('final_idle');phase=8;mark=frames\n elseif phase==8 and frames>=mark+30 then snapshot('camera');capture('camera');phase=9;mark=frames\n elseif phase==9 and frames>=mark+150 then snapshot('boundary');capture('boundary');local f=assert(io.open(OUT..'/respawns.bin','wb'));for i=0,3 do f:write(string.char(flycast.memory.read8(RESPAWNS+i))) end;f:close();local g=assert(io.open(OUT..'/clipped.bin','wb'));for i=0,3 do g:write(string.char(flycast.memory.read8(CLIPPED+i))) end;g:close();phase=10;flycast.input.pressButtons(1,8)\n end\n if ticks>=1500 then finish('timeout') end\nend\nlocal function overlay()\n flycast.input.setAxis(1,1,phase==9 and -32768 or (platform and ((phase==1 or phase==2 or phase==3) and 32767 or 0) or (phase==3 and -16384 or 0)))\n flycast.input.setAxis(1,2,platform and (phase==7 and -32768 or 0) or (phase==1 and -16384 or (phase==2 and -32768 or 0)))\n flycast.input.setAxis(1,6,phase==8 and 50000 or 0)\n if phase==3 or phase==4 or phase==6 then flycast.input.pressButtons(1,4) else flycast.input.releaseButtons(1,4) end\nend\nflycast_callbacks={vblank=vblank,overlay=overlay}\n"

def read_snapshot(path):
 b=path.read_bytes()
 if len(b)!=188:raise ValueError('short playable snapshot')
 names=('x','y','z','vx','vy','vz','yaw','anim_time','grounded','jump_held','mode','jumps','landings','ticks')
 return dict(scene=dict(zip(('magic','version','state','source','triangles','geometry_bytes','system_ram','allocator_live','allocator_arena','vram_free','frames','yaw','pitch','distance','pvr_fps','frame_us'),struct.unpack_from('<11I4fI',b))),player=dict(zip(names,struct.unpack_from('<8f6I',b,128))),clip=struct.unpack_from('<I',b,184)[0],error=b[64:128].split(b'\0')[0].decode(errors='replace'))
def render_lua(template,values):
 return re.sub(r'\b(?:'+'|'.join(map(re.escape,values))+r')\b',lambda m:values[m.group(0)],template)

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('flycast',type=Path);ap.add_argument('elf',type=Path);ap.add_argument('--nm',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();a.platform=False
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
 values=dict(PLATFORM='false',ADDRESS=str(address('dc_telemetry')),PLAYER=str(address('dc_player_state')),
  CLIP=str(address('dc_play_clip')),TICKS=str(address('dc_training_ticks')),
  CLIPPED=str(address('dc_training_clipped_triangles')),RESPAWNS=str(address('dc_world_respawns')),OUT=json.dumps(out.as_posix()))
 lua=render_lua(LUA,values)
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
  landing=p['land']['grounded']==1 and abs(p['land']['y'])<1 and p['land']['landings']==1,
  held_jump_no_repeat=p['land']['jumps']==1,second_press=p['second_jump']['jumps']==2 and p['second_jump']['y']>.7,
  second_landing=p['final_idle']['grounded']==1 and p['final_idle']['landings']==2,
  animation_states=[snapshots[k]['clip'] for k in ('idle','walk','run')]==[0,1,2] and snapshots['air']['clip'] in (3,4),
  follow_motion=p['run']['z']-p['idle']['z']>2.5 and snapshots['idle']['scene']['distance']==snapshots['run']['scene']['distance'],
  camera=s['camera']['yaw']>s['final_idle']['yaw']+.2,cleanup=s['exit']['state']==3,
  framebuffer=all((out/(n+'.ppm')).stat().st_size==921615 for n in ('idle','walk','run','air','land','camera')))
 checks['boundary_respawn']=struct.unpack('<I',(out/'respawns.bin').read_bytes())[0]>=1 and abs(read_snapshot(out/'boundary.bin')['player']['x'])<=6.8
 checks['near_clipping_active']=struct.unpack('<I',(out/'clipped.bin').read_bytes())[0]>0
 checks['training_triangles']=s['idle']['triangles']==6566
 checks['heap_below_4mib']=s['idle']['allocator_live']<4*1024*1024
 result=dict(ai_assisted=True,result='PASS' if all(checks.values()) else 'FAIL',elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),checks=checks,snapshots=snapshots,timebase='monotonic fixed60Hz simulation ticks',scope='Native C original-training patch prototype; captured images require visual review; not original GOAL gameplay or hardware validation')
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(result=result['result'],checks=checks),indent=2))
 if result['result']!='PASS':raise SystemExit(1)
if __name__=='__main__':main()
