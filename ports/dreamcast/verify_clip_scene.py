"""Original-free native clipping raster oracle (AI-assisted), ISC."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
from verify_training import LUA as TRAINING_LUA

CAPTURE=TRAINING_LUA[TRAINING_LUA.index('local function capture(name)'):TRAINING_LUA.index('local function finish(name)')]
LUA='''local phase=0
local mark=0
local done=false
CAPTURE
local function finish(name)
 local f=assert(io.open(OUT..'/done.txt','w'));f:write(name);f:close();done=true
end
local function snapshot(name)
 local f=assert(io.open(OUT..'/'..name..'.bin','wb'))
 for i=0,127 do f:write(string.char(flycast.memory.read8(ADDRESS+i))) end
 f:close()
end
local function vblank()
 if done or flycast.memory.read32(ADDRESS)~=0x3243444a then return end
 local state=flycast.memory.read32(ADDRESS+8)
 if state==4 then snapshot('error');finish('error');return end
 if state==3 then snapshot('exit');finish('exit');return end
 if state~=2 then return end
 local frames=flycast.memory.read32(ADDRESS+40)
 if frames<mark+30 or phase>=4 then return end
 local names={'crossing','front','behind','plane'}
 capture(names[phase+1]);snapshot(names[phase+1]);mark=frames;phase=phase+1
 if phase<4 then flycast.memory.write32(PHASE,phase)
 else
  local f=assert(io.open(OUT..'/clipped.bin','wb'))
  for i=0,3 do f:write(string.char(flycast.memory.read8(CLIPPED+i))) end
  f:close();flycast.input.pressButtons(1,8)
 end
end
flycast_callbacks={vblank=vblank}
'''.replace('CAPTURE',CAPTURE)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('flycast',type=Path);p.add_argument('elf',type=Path)
    p.add_argument('--nm',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    for pattern in ('*.bin','*.ppm','done.txt','result.json'):
        for old in out.glob(pattern):old.unlink()
    symbols=subprocess.check_output([str(a.nm),str(a.elf)],text=True)
    def address(name):return int(re.search(r'^([0-9a-fA-F]+)\s+\w\s+_?'+name+'$',symbols,re.M)[1],16)
    script=LUA.replace('ADDRESS',str(address('dc_telemetry'))).replace('PHASE',str(address('dc_clip_fixture_phase')))
    script=script.replace('CLIPPED',str(address('dc_training_clipped_triangles'))).replace('OUT',json.dumps(out.as_posix()))
    exe=a.flycast.resolve();env=os.environ.copy();startup=None
    if os.name=='nt':
        exe=out/'flycast.exe';shutil.copy2(a.flycast,exe);lua=out/'verify.lua'
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    else:
        lua=out/'config/flycast/verify.lua';lua.parent.mkdir(parents=True,exist_ok=True)
        env.update(SDL_VIDEODRIVER='offscreen',LIBGL_ALWAYS_SOFTWARE='1',XDG_CONFIG_HOME=str(out/'config'),XDG_DATA_HOME=str(out/'data'))
    lua.write_text(script)
    command=[str(exe),'-config','config:UseReios=yes','-config','config:Dreamcast.RamMod32MB=no',
        '-config','config:Debug.SerialConsoleEnabled=no','-config','config:LuaFileName=verify.lua',
        '-config','config:rend.EmulateFramebuffer=yes','-config','audio:backend=null',str(a.elf.resolve())]
    with (out/'frontend.log').open('w') as log:
        proc=subprocess.Popen(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,startupinfo=startup)
        try:
            deadline=time.monotonic()+40
            while proc.poll() is None and time.monotonic()<deadline and not (out/'done.txt').exists():time.sleep(.1)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=5)
                except subprocess.TimeoutExpired:proc.kill();proc.wait()
    terminal=(out/'done.txt').read_text().strip() if (out/'done.txt').exists() else 'timeout'
    if terminal!='exit':p.exit(1,'FAIL: '+terminal+'; inspect fixture log\n')
    images={name:(out/(name+'.ppm')).read_bytes() for name in ('crossing','front','behind','plane')}
    for raw in images.values():
        if len(raw)!=921615 or not raw.startswith(b'P6\n640 480\n255\n'):p.exit(1,'FAIL: framebuffer format\n')
    def rgb(name,x,y):return tuple(images[name][15+(y*640+x)*3:15+(y*640+x)*3+3])
    def magenta(c):return c[0]>=240 and c[1]<10 and c[2]>=240
    snapshots={name:struct.unpack_from('<11I',(out/(name+'.bin')).read_bytes()) for name in (*images,'exit')}
    checks=dict(ram_16mib=all(v[6]==16777216 for v in snapshots.values()),
        crossing_visible=magenta(rgb('crossing',320,260)),crossing_quad_visible=magenta(rgb('crossing',250,270)),
        front_visible=magenta(rgb('front',320,260)),front_outside=not magenta(rgb('front',250,270)),
        behind_rejected=rgb('behind',320,260)==rgb('front',250,270),
        exact_plane_visible=magenta(rgb('plane',320,260)),background=all(not magenta(rgb(n,100,260)) for n in images),
        clip_path_executed=struct.unpack('<I',(out/'clipped.bin').read_bytes())[0]>0,clean_exit=snapshots['exit'][2]==3)
    result=dict(ai_assisted=True,result='PASS' if all(checks.values()) else 'FAIL',checks=checks,
        sampled_rgb={n:{str(point):rgb(n,*point) for point in ((320,260),(250,270),(100,260))} for n in images},
        elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),scope='Original-free near-plane raster samples in stock16MiB Flycast; not hardware or full-game validation')
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    if result['result']!='PASS':raise SystemExit(1)


if __name__=='__main__':main()
