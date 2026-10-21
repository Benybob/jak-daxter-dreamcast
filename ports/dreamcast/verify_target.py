"""Verify native renderer telemetry, real input response, and cleanup (AI-assisted).

The native 128-byte dc_telemetry structure is located through the ELF symbol
table. Flycast's documented Lua memory/input APIs read it, inject controls, and
capture actual RGB565 VRAM. Windows gets a private portable emulator copy.
"""
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
local function vblank()
  if done then return end
  ticks=ticks+1
  if word(0)~=0x3243444a then return end
  local state=word(8)
  local frames=word(40)
  if state==4 then finish('error'); return end
  if state==3 then finish('exit'); return end
  if state==2 and phase==0 and frames>=60 then
    snapshot('before'); capture('before'); start_frame=frames; phase=1
  elseif state==2 and phase==1 and frames>=start_frame+80 then
    snapshot('input'); stopped_frame=frames; phase=2
  elseif state==2 and phase==2 and frames>=stopped_frame+10 then
    capture('frame'); flycast.input.pressButtons(1,8); phase=3
  end
  if ticks>=1200 then finish('timeout') end
end
local function overlay()
  flycast.input.setAxis(1,1,phase==1 and 20480 or 0)
  flycast.input.setAxis(1,6,phase==1 and 46080 or 0)
end
flycast_callbacks={vblank=vblank,overlay=overlay}
'''

def telemetry(path):
    data=path.read_bytes()
    if len(data)!=128: raise ValueError('short telemetry')
    names=('magic','version','state','source','triangles','geometry_bytes','system_ram',
           'allocator_live','allocator_arena','vram_free','frames','yaw','pitch','distance','pvr_fps','frame_us')
    result=dict(zip(names,struct.unpack_from('<11I4fI',data)))
    result['error']=data[64:].split(b'\0',1)[0].decode('utf-8',errors='replace')
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('flycast',type=Path)
    parser.add_argument('elf',type=Path)
    parser.add_argument('--nm',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--triangles',type=int,required=True)
    parser.add_argument('--source',type=int,default=2)
    parser.add_argument('--expect-error')
    args=parser.parse_args()
    nm=subprocess.check_output([str(args.nm),str(args.elf)],text=True)
    match=re.search(r'^([0-9a-fA-F]+)\s+\w\s+_?dc_telemetry$',nm,re.MULTILINE)
    if not match: parser.error('ELF must contain the dc_telemetry symbol')
    address=int(match[1],16)
    out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    for name in ('before.bin','input.bin','exit.bin','error.bin','timeout.bin','before.ppm','frame.ppm','done.txt','result.json'):
        (out/name).unlink(missing_ok=True)
    exe=args.flycast.resolve()
    if os.name=='nt':
        exe=out/'flycast.exe'
        shutil.copy2(args.flycast,exe)
        lua_path=out/'verify.lua'
    else:
        lua_path=out/'config/flycast/verify.lua'
        lua_path.parent.mkdir(parents=True,exist_ok=True)
    lua=LUA.replace('ADDRESS',str(address)).replace('OUT',json.dumps(out.as_posix()))
    lua_path.write_text(lua)
    command=[str(exe),'-config','config:UseReios=yes','-config','config:Debug.SerialConsoleEnabled=no',
             '-config','config:Dreamcast.RamMod32MB=no','-config','config:LuaFileName=verify.lua',
             '-config','config:rend.EmulateFramebuffer=yes','-config','audio:backend=null',
             '-config','log:LogToFile=yes',str(args.elf.resolve())]
    env=os.environ.copy()
    startup=None
    if os.name=='nt':
        startup=subprocess.STARTUPINFO()
        startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow=0
    else:
        env.update(SDL_VIDEODRIVER='offscreen',LIBGL_ALWAYS_SOFTWARE='1',
                   XDG_CONFIG_HOME=str(out/'config'),XDG_DATA_HOME=str(out/'data'))
    with (out/'frontend.log').open('w') as log:
        process=subprocess.Popen(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,startupinfo=startup)
        try:
            deadline=time.monotonic()+40
            while process.poll() is None and time.monotonic()<deadline and not (out/'done.txt').exists():
                time.sleep(0.1)
        finally:
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
    if not (out/'done.txt').exists(): parser.exit(1,'FAIL: target did not complete; inspect emulator log\n')
    final=(out/'done.txt').read_text().strip()
    result={'elf_sha256':hashlib.sha256(args.elf.read_bytes()).hexdigest(),'telemetry_address':hex(address),'final':final}
    if args.expect_error:
        actual=telemetry(out/'error.bin') if final=='error' else {}
        result['error']=actual
        passed=actual.get('error')==args.expect_error and actual.get('state')==4
    else:
        if final!='exit': parser.exit(1,f'FAIL: target ended in {final}\n')
        before,after,exit_state=(telemetry(out/(name+'.bin')) for name in ('before','input','exit'))
        result.update(before=before,after_input=after,exit=exit_state)
        result['checks']={
            'protocol':before['magic']==0x3243444a and before['version']==1,
            'real_scene':before['source']==args.source and before['triangles']==args.triangles,
            'ram_16_mib':before['system_ram']==16777216,
            'trigger_zoom':after['distance']<before['distance']-0.2,
            'stick_orbit':after['yaw']>before['yaw']+0.7,
            'main_cleanup_completed':exit_state['state']==3 and exit_state['frames']>=after['frames'],
            'framebuffer':all((out/(name+'.ppm')).is_file() and (out/(name+'.ppm')).stat().st_size==921615 for name in ('before','frame')),
        }
        passed=all(result['checks'].values())
    result['result']='PASS' if passed else 'FAIL'
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    if not passed: raise SystemExit(1)

if __name__=='__main__': main()
