"""Real-patch camera obstruction/orbit trace oracle (AI-assisted), ISC."""
import argparse,hashlib,json,math,os,re,shutil,struct,subprocess,time
from pathlib import Path
from verify_training import LUA as TRAINING_LUA,render_lua,read_snapshot
CAPTURE=TRAINING_LUA[TRAINING_LUA.index('local function capture(name)'):TRAINING_LUA.index('local function finish(name)')]
SNAPSHOT=TRAINING_LUA[TRAINING_LUA.index('local function snapshot(name)'):TRAINING_LUA.index('local function capture(name)')]
LUA='''local phase=0
local mark=0
local done=false
local trace=nil
local seen=false
SNAPSHOT
CAPTURE
local function finish(name)
 snapshot(name)
 if trace then trace:close();trace=nil end
 local f=assert(io.open(OUT..'/done.txt','w'));f:write(name);f:close();done=true
end
local function vblank()
 if done or flycast.memory.read32(ADDRESS)~=0x3243444a then return end
 local state=flycast.memory.read32(ADDRESS+8)
 if state==4 then finish('error');return end
 if state==3 then finish('exit');return end
 if state~=2 then return end
 local tick=flycast.memory.read32(TICKS)
 if phase==0 and tick>=60 then snapshot('idle');capture('idle');phase=1;mark=tick
  trace=assert(io.open(OUT..'/trace.bin','wb'))
 end
 if phase==1 then
  for i=0,59 do trace:write(string.char(flycast.memory.read8(CAMERA+i))) end
  for i=0,3 do trace:write(string.char(flycast.memory.read8(ADDRESS+44+i))) end
  for i=0,3 do trace:write(string.char(flycast.memory.read8(TICKS+i))) end
  if not seen and flycast.memory.read32(CAMERA+48)>0 then snapshot('occluded');capture('occluded');seen=true end
  if tick>=mark+240 then snapshot('orbit');capture('orbit');phase=2;flycast.input.pressButtons(1,8) end
 end
end
local function overlay()flycast.input.setAxis(1,6,phase==1 and 65535 or 0) end
flycast_callbacks={vblank=vblank,overlay=overlay}
'''.replace('SNAPSHOT',SNAPSHOT).replace('CAPTURE',CAPTURE)


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('flycast',type=Path);ap.add_argument('elf',type=Path)
    ap.add_argument('--nm',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    for pattern in ('*.bin','*.ppm','done.txt','result.json'):
        for old in out.glob(pattern):old.unlink()
    symbols=subprocess.check_output([str(a.nm),str(a.elf)],text=True)
    def addr(n):return str(int(re.search(r'^([0-9a-fA-F]+)\s+\w\s+_?'+n+'$',symbols,re.M)[1],16))
    values={k:addr(v) for k,v in dict(ADDRESS='dc_telemetry',PLAYER='dc_player_state',CLIP='dc_play_clip',CAMERA='dc_camera_state',TICKS='dc_training_ticks').items()}
    values['OUT']=json.dumps(out.as_posix());script=render_lua(LUA,values)
    exe=a.flycast.resolve();env=os.environ.copy();startup=None
    if os.name=='nt':
        exe=out/'flycast.exe';shutil.copy2(a.flycast,exe);lua=out/'verify.lua'
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    else:
        lua=out/'config/flycast/verify.lua';lua.parent.mkdir(parents=True,exist_ok=True)
        env.update(SDL_VIDEODRIVER='offscreen',LIBGL_ALWAYS_SOFTWARE='1',XDG_CONFIG_HOME=str(out/'config'),XDG_DATA_HOME=str(out/'data'))
    lua.write_text(script)
    cmd=[str(exe),'-config','config:UseReios=yes','-config','config:Dreamcast.RamMod32MB=no','-config',
        'config:Debug.SerialConsoleEnabled=no','-config','config:LuaFileName=verify.lua','-config',
        'config:rend.EmulateFramebuffer=yes','-config','audio:backend=null','-config','log:LogToFile=yes',str(a.elf.resolve())]
    with (out/'frontend.log').open('w') as log:
        proc=subprocess.Popen(cmd,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,startupinfo=startup)
        try:
            deadline=time.monotonic()+35
            while proc.poll() is None and time.monotonic()<deadline and not (out/'done.txt').exists():time.sleep(.1)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=5)
                except subprocess.TimeoutExpired:proc.kill();proc.wait()
    terminal=(out/'done.txt').read_text().strip() if (out/'done.txt').exists() else 'timeout'
    if terminal!='exit':ap.exit(1,'FAIL camera orbit: '+terminal+'\n')
    data=(out/'trace.bin').read_bytes()
    if not data or len(data)%68:ap.exit(1,'FAIL: invalid camera trace\n')
    names=('target_x','target_y','target_z','dir_x','dir_y','dir_z','eye_x','eye_y','eye_z','distance','limit','initialized','shortened','fallbacks','coverage_limited','yaw','ticks')
    trace=[dict(zip(names,v)) for v in struct.iter_unpack('<11f4IfI',data)]
    snapshots={n:read_snapshot(out/(n+'.bin')) for n in ('idle','orbit','exit')}
    before=snapshots['idle']['player'];after=snapshots['orbit']['player']
    checks=dict(ram_16mib=all(s['scene']['system_ram']==16777216 for s in snapshots.values()),
        full_orbit=trace[-1]['yaw']-trace[0]['yaw']>2*math.pi,
        actual_obstruction=any(t['limit']<10.35 for t in trace) and (out/'occluded.bin').exists(),
        fresh_caps=all(t['initialized']==1 and 0<=t['distance']<=t['limit']<=10.40001 for t in trace),
        immediate_shrink=all(abs(t['distance']-t['limit'])<.0001 for prior,t in zip(trace,trace[1:]) if t['limit']<prior['distance']),
        eased_extension=any(t['limit']>t['distance']+.01 and t['distance']>prior['distance'] for prior,t in zip(trace,trace[1:])),
        eye_ray=all(abs(t['eye_'+axis]-(t['target_'+axis]+t['dir_'+axis]*t['distance']))<.0001 for t in trace for axis in ('x','y','z')),
        covered=all(abs(t['eye_x'])<=17.8801 and abs(t['eye_z'])<=17.8801 and abs(t['eye_y'])<=11.8801 for t in trace),
        stationary_player=abs(before['x']-after['x'])<.0001 and abs(before['z']-after['z'])<.0001,
        clean_exit=snapshots['exit']['scene']['state']==3,
        framebuffers=all((out/(n+'.ppm')).is_file() and (out/(n+'.ppm')).stat().st_size==921615 for n in ('idle','occluded','orbit')))
    result=dict(ai_assisted=True,result='PASS' if all(checks.values()) else 'FAIL',checks=checks,trace_samples=len(trace),
        min_distance_meters=min(t['distance'] for t in trace),max_distance_meters=max(t['distance'] for t in trace),
        elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),scope='Original training-patch camera orbit and obstruction in stock16MiB Flycast; native prototype, not original GOAL camera or hardware validation')
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    if result['result']!='PASS':raise SystemExit(1)


if __name__=='__main__':main()
