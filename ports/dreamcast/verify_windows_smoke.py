"""Windows Flycast framebuffer smoke test (AI-assisted).

Uses Flycast's Lua memory API to capture actual emulated RGB565 VRAM. This
does not replace verify_flycast.py's Linux serial/exit/controller assertions.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

LUA = r'''
local frames=0
local function capture(path)
  local control=flycast.memory.read32(0xa05f8044)
  assert(((control >> 2)&3)==1,'expected RGB565')
  local offset=flycast.memory.read32(0xa05f8050)&0x7fffff
  local f=assert(io.open(path,'wb'))
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
local function vblank()
  frames=frames+1
  if frames==80 then capture(BEFORE) end
  if frames==240 then
    capture(AFTER)
    local f=assert(io.open(DONE,'w')); f:write('240'); f:close()
  end
  if frames==260 then flycast.input.pressButtons(1,8) end
end
local function overlay()
  local active=frames>=100 and frames<180
  flycast.input.setAxis(1,1,active and 20480 or 0)
  flycast.input.setAxis(1,6,active and 46080 or 0)
end
flycast_callbacks={vblank=vblank,overlay=overlay}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('flycast',type=Path)
    parser.add_argument('elf',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if os.name!='nt':
        parser.error('use verify_flycast.py on Linux')
    out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    exe=out/'flycast.exe'
    shutil.copy2(args.flycast,exe)
    paths={key:out/name for key,name in [('BEFORE','before.ppm'),('AFTER','frame.ppm'),('DONE','done.txt')]}
    lua=LUA
    for key,path in paths.items():
        path.unlink(missing_ok=True)
        lua=lua.replace(key,json.dumps(path.as_posix()))
    (out/'verify.lua').write_text(lua)
    command=[str(exe),'-config','config:UseReios=yes','-config','config:Debug.SerialConsoleEnabled=no',
             '-config','config:Dreamcast.RamMod32MB=no','-config','config:LuaFileName=verify.lua',
             '-config','config:rend.EmulateFramebuffer=yes','-config','audio:backend=null',
             '-config','log:LogToFile=yes',str(args.elf.resolve())]
    startup=subprocess.STARTUPINFO()
    startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow=0
    with (out/'frontend.log').open('w') as log:
        process=subprocess.Popen(command,cwd=out,stdout=log,stderr=subprocess.STDOUT,startupinfo=startup)
        try:
            deadline=time.monotonic()+30
            while process.poll() is None and time.monotonic()<deadline and not paths['DONE'].exists():
                time.sleep(0.1)
        finally:
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
    for key in ('BEFORE','AFTER'):
        if not paths[key].is_file() or paths[key].stat().st_size!=921615:
            parser.exit(1,'FAIL: incomplete framebuffer capture\n')
    before,after=(paths[k].read_bytes() for k in ('BEFORE','AFTER'))
    if before==after or not paths['DONE'].exists():
        parser.exit(1,'FAIL: renderer did not progress\n')
    # Ignore the text label; count non-background geometry in the scene area.
    pixels=after[15:]
    background=pixels[(479*640+639)*3:(479*640+640)*3]
    # RGB565 background dithering varies by one quantization step.
    geometry=sum(max(abs(pixels[i+k]-background[k]) for k in range(3))>24
                 for i in range(80*640*3,len(pixels),3))
    if geometry<1000:
        parser.exit(1,'FAIL: no substantial rendered geometry\n')
    emulator_log=(out/'flycast.log').read_text(errors='replace')
    if 'RAM(16 MB)' not in emulator_log:
        parser.exit(1,'FAIL: 16 MiB emulated RAM not confirmed\n')
    report={'result':'PASS','scope':'Windows framebuffer smoke test',
            'elf_sha256':hashlib.sha256(args.elf.read_bytes()).hexdigest(),
            'verified_system_ram_bytes':16777216,'vblanks':240,
            'non_background_pixels_below_label':geometry,
            'framebuffers_changed':True,'input_injected':True,
            'serial_verified':False,'clean_target_exit_verified':False,
            'note':'Automatic rotation also changes pixels; input injection alone does not prove control response.'}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    main()
