"""Original-terrain prototype controller/boundary oracle (AI-assisted), ISC.

Captures frames for separate visual review; file size does not prove visibility.
"""
import argparse,hashlib,json,os,re,shutil,struct,subprocess,time
from pathlib import Path
LUA="\nlocal phase=0\nlocal start_frame=0\nlocal stopped_frame=0\nlocal ticks=0\nlocal done=false\nlocal function word(offset) return flycast.memory.read32(ADDRESS+offset) end\nlocal function snapshot(name)\n  local f=assert(io.open(OUT..'/'..name..'.bin','wb'))\n  for i=0,127 do f:write(string.char(flycast.memory.read8(ADDRESS+i))) end\n  for i=0,55 do f:write(string.char(flycast.memory.read8(PLAYER+i))) end\n  for i=0,3 do f:write(string.char(flycast.memory.read8(CLIP+i))) end\n  for i=0,59 do f:write(string.char(flycast.memory.read8(CAMERA+i))) end\n  f:close()\nend\nlocal function capture(name)\n  local control=flycast.memory.read32(0xa05f8044)\n  assert(((control >> 2)&3)==1,'expected RGB565 framebuffer')\n  local offset=flycast.memory.read32(0xa05f8050)&0x7fffff\n  local f=assert(io.open(OUT..'/'..name..'.ppm','wb'))\n  f:write('P6\\n640 480\\n255\\n')\n  for y=0,479 do\n    local row={}\n    for x=0,639 do\n      local c=flycast.memory.read16(0xa5000000+offset+(y*640+x)*2)\n      row[#row+1]=string.char(math.floor(((c >> 11)&31)*255/31),math.floor(((c >> 5)&63)*255/63),math.floor((c&31)*255/31))\n    end\n    f:write(table.concat(row))\n  end\n  f:close()\nend\nlocal function finish(name)\n  snapshot(name)\n  local f=assert(io.open(OUT..'/done.txt','w')); f:write(name); f:close()\n  done=true\nend\nlocal mark=0\nlocal platform=PLATFORM\nlocal function vblank()\n if done then return end\n ticks=ticks+1\n if word(0)~=0x3243444a then return end\n local state=word(8);local frames=flycast.memory.read32(TICKS)\n if state==4 then finish('error');return end\n if state==3 then finish('exit');return end\n if state~=2 then return end\n if phase==0 and frames>=60 then snapshot('idle');capture('idle');phase=1;mark=frames\n elseif phase==1 and frames>=mark+40 then snapshot('walk');capture('walk');phase=2;mark=frames\n elseif phase==2 and frames>=mark+30 then snapshot('run');capture('run');phase=3;mark=frames\n elseif phase==3 and frames>=mark+(platform and 30 or 12) then snapshot('air');capture('air');phase=4;mark=frames\n elseif phase==4 and frames>=mark+90 then snapshot('land');capture('land');phase=5;mark=frames\n elseif phase==5 and frames>=mark+15 then phase=6;mark=frames\n elseif phase==6 and frames>=mark+12 then snapshot('second_jump');phase=7;mark=frames\n elseif phase==7 and frames>=mark+90 then snapshot('final_idle');phase=8;mark=frames\n elseif phase==8 and frames>=mark+30 then snapshot('camera');capture('camera');phase=9;mark=frames\n elseif phase==9 and frames>=mark+150 then snapshot('boundary');capture('boundary');local f=assert(io.open(OUT..'/respawns.bin','wb'));for i=0,3 do f:write(string.char(flycast.memory.read8(RESPAWNS+i))) end;f:close();local g=assert(io.open(OUT..'/clipped.bin','wb'));for i=0,3 do g:write(string.char(flycast.memory.read8(CLIPPED+i))) end;g:close();phase=10;flycast.input.pressButtons(1,8)\n end\n if ticks>=1500 then finish('timeout') end\nend\nlocal function overlay()\n flycast.input.setAxis(1,1,phase==9 and -32768 or (platform and ((phase==1 or phase==2 or phase==3) and 32767 or 0) or (phase==3 and -16384 or 0)))\n flycast.input.setAxis(1,2,platform and (phase==7 and -32768 or 0) or (phase==1 and -16384 or (phase==2 and -32768 or 0)))\n flycast.input.setAxis(1,6,phase==8 and 50000 or 0)\n if phase==3 or phase==4 or phase==6 then flycast.input.pressButtons(1,4) else flycast.input.releaseButtons(1,4) end\nend\nflycast_callbacks={vblank=vblank,overlay=overlay}\n"

def read_snapshot(path):
 b=path.read_bytes()
 if len(b)!=248:raise ValueError('short playable snapshot')
 names=('x','y','z','vx','vy','vz','yaw','anim_time','grounded','jump_held','mode','jumps','landings','ticks')
 camera=dict(zip(('target_x','target_y','target_z','dir_x','dir_y','dir_z','eye_x','eye_y','eye_z','distance','limit','initialized','shortened','fallbacks','coverage_limited'),struct.unpack_from('<11f4I',b,188)))
 return dict(camera=camera,scene=dict(zip(('magic','version','state','source','triangles','geometry_bytes','system_ram','allocator_live','allocator_arena','vram_free','frames','yaw','pitch','distance','pvr_fps','frame_us'),struct.unpack_from('<11I4fI',b))),player=dict(zip(names,struct.unpack_from('<8f6I',b,128))),clip=struct.unpack_from('<I',b,184)[0],error=b[64:128].split(b'\0')[0].decode(errors='replace'))
def render_lua(template,values):
 return re.sub(r'\b(?:'+'|'.join(map(re.escape,values))+r')\b',lambda m:values[m.group(0)],template)

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('flycast',type=Path);ap.add_argument('elf',type=Path);ap.add_argument('--nm',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--expected-triangles',type=int,default=6566,help='Expected combined actor + terrain source triangles');ap.add_argument('--stream-bundle',type=Path,help='Private STREAM_BUNDLE.json with full-source reachable window oracles');a=ap.parse_args();a.platform=False
 if not 1<=a.expected_triangles<=16384:ap.error('--expected-triangles must be 1..16384')
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
  CAMERA=str(address('dc_camera_state')),CLIP=str(address('dc_play_clip')),TICKS=str(address('dc_training_ticks')),
  CLIPPED=str(address('dc_training_clipped_triangles')),RESPAWNS=str(address('dc_world_respawns')),OUT=json.dumps(out.as_posix()))
 template=LUA
 stream_fields=('commits','refusals','faces','max_faces','anchor_x','anchor_z','texture_bytes','gpu_slots','payload_crc','clipped','near_clipped')
 if a.stream_bundle:
  stream_bundle=json.loads(a.stream_bundle.read_text())
  addresses=[address('dc_render_stream_'+name) for name in stream_fields]
  extra="""local original_snapshot=snapshot
snapshot=function(name)
 original_snapshot(name)
 local f=assert(io.open(OUT..'/'..name..'-stream.bin','wb'))
 local addresses={STREAM_ADDRESSES}
 for _,address in ipairs(addresses) do
  for byte=0,3 do f:write(string.char(flycast.memory.read8(address+byte))) end
 end
 f:close()
end
""".replace('STREAM_ADDRESSES',','.join(map(str,addresses)))
  template=template.replace('local function capture(name)',extra+'local function capture(name)')
  # A vblank can interrupt publication of several separate telemetry words.
  # Delay that observation until the count/anchor/CRC form one known window;
  # a permanent disagreement still fails at the normal completion deadline.
  oracle_rows=','.join('['+json.dumps(str(row['x'])+','+str(row['z']))+']={'+str(row['count'])+','+str(row['crc'])+'}' for row in stream_bundle['source_window_oracles'])
  guard="""local stream_oracles={ORACLE_ROWS}
local function stream_coherent()
 if word(8)~=2 then return true end
 local function signed(address)
  local v=flycast.memory.read32(address);if v>=2147483648 then v=v-4294967296 end;return v
 end
 local x=signed(ANCHOR_X);local z=signed(ANCHOR_Z)
 local known=stream_oracles[string.format('%d,%d',x,z)]
 local faces=flycast.memory.read32(FACES)
 return known and faces==known[1] and flycast.memory.read32(CRC)==known[2] and word(16)==ACTOR+faces
end
""".replace('ORACLE_ROWS',oracle_rows).replace('ANCHOR_X',str(addresses[4])).replace('ANCHOR_Z',str(addresses[5])).replace('FACES',str(addresses[2])).replace('CRC',str(addresses[8])).replace('ACTOR',str(stream_bundle['actor_triangles']))
  template=template.replace('local function vblank()',guard+'local function vblank()')
  template=template.replace('if done then return end\n ticks=ticks+1','if done then return end\n if not stream_coherent() then return end\n ticks=ticks+1')
 lua=render_lua(template,values)
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
  follow_motion=p['run']['z']-p['idle']['z']>2.5 and all(0<=x['scene']['distance']<=2.60001 for x in snapshots.values()),
  camera=s['camera']['yaw']>s['final_idle']['yaw']+.2,cleanup=s['exit']['state']==3,
  framebuffer=all((out/(n+'.ppm')).stat().st_size==921615 for n in ('idle','walk','run','air','land','camera')))
 checks['boundary_respawn']=struct.unpack('<I',(out/'respawns.bin').read_bytes())[0]>=1 and abs(read_snapshot(out/'boundary.bin')['player']['x'])<=6.8
 checks['camera_fresh_bounds']=all(x['camera']['initialized']==1 and 0<=x['camera']['distance']<=x['camera']['limit']<=10.40001 for x in snapshots.values())
 checks['camera_eye_ray']=all(abs(x['camera']['eye_'+axis]-(x['camera']['target_'+axis]+x['camera']['dir_'+axis]*x['camera']['distance']))<.0001 for x in snapshots.values() for axis in ('x','y','z'))
 checks['near_clipping_active']=struct.unpack('<I',(out/'clipped.bin').read_bytes())[0]>0
 checks['training_triangles']=s['idle']['triangles']==a.expected_triangles
 checks['heap_below_4mib']=s['idle']['allocator_live']<4*1024*1024
 stream=None
 if a.stream_bundle:
  bundle=json.loads(a.stream_bundle.read_text());oracle={(row['x'],row['z']):row for row in bundle['source_window_oracles']}
  names=(*snapshots,'boundary');stream={name:dict(zip(stream_fields,struct.unpack('<4I2i5I',(out/(name+'-stream.bin')).read_bytes()))) for name in names}
  active={name:row for name,row in stream.items() if name!='exit'}
  checks['near_clipping_active']=checks['near_clipping_active'] or any(row['near_clipped']>0 for row in active.values())
  checks['training_triangles']=all(s[name]['triangles']==bundle['actor_triangles']+stream[name]['faces'] for name in snapshots)
  checks['stream_multiple_commits']=max(row['commits'] for row in active.values())>=2
  checks['stream_multiple_anchors']=len({(row['anchor_x'],row['anchor_z']) for row in active.values()})>=2
  checks['stream_no_refusals']=all(row['refusals']==0 for row in stream.values())
  checks['stream_exact_source_windows']=all((row['anchor_x'],row['anchor_z']) in oracle and row['faces']==oracle[row['anchor_x'],row['anchor_z']]['count'] and row['payload_crc']==oracle[row['anchor_x'],row['anchor_z']]['crc'] for row in active.values())
  checks['stream_resident_texture_owner']=len({row['gpu_slots'] for row in active.values()})==1 and all(row['gpu_slots'] and row['texture_bytes']==bundle['resident_color_bytes'] for row in active.values())
  checks['stream_GPU_cleanup']=stream['exit']['gpu_slots']==0 and stream['exit']['texture_bytes']==0
 result=dict(ai_assisted=True,result='PASS' if all(checks.values()) else 'FAIL',elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),checks=checks,snapshots=snapshots,timebase='monotonic fixed60Hz simulation ticks',scope='Native C original-training patch prototype; captured images require visual review; not original GOAL gameplay or hardware validation')
 if stream:
  result['stream_snapshots']=stream;result['scope']='Native C bounded original training prototype with transactional render-window changes, full-source payload oracles and stable resident texture owner; base pass only, not original GOAL gameplay/full-game streaming or hardware validation'
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(result=result['result'],checks=checks),indent=2))
 if result['result']!='PASS':raise SystemExit(1)
if __name__=='__main__':main()
