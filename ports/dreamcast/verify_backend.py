"""Read native SH-4 codegen probe results through Flycast memory (AI-assisted)."""
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

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('flycast',type=Path)
parser.add_argument('elf',type=Path)
parser.add_argument('--nm',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--tests',type=int,default=17)
parser.add_argument('--suite',choices=('codegen','runtime','runtime-goal','objects','projection'),default='codegen')
args=parser.parse_args()
if args.tests<1: parser.error('--tests must be positive')
symbols=subprocess.check_output([str(args.nm),str(args.elf)],text=True)
match=re.search(r'^([0-9a-fA-F]+)\s+\w\s+_?dc_backend$',symbols,re.MULTILINE)
if not match: parser.error('dc_backend symbol missing')
address=int(match[1],16)
out=args.output.resolve(); out.mkdir(parents=True,exist_ok=True)
result_path=out/'target.bin'; result_path.unlink(missing_ok=True)
progress_path=out/'progress.bin'; progress_path.unlink(missing_ok=True)
exe=args.flycast.resolve()
startup=None
env=os.environ.copy()
if os.name=='nt':
    exe=out/'flycast.exe'; shutil.copy2(args.flycast,exe)
    lua_path=out/'verify.lua'
    startup=subprocess.STARTUPINFO(); startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow=0
else:
    lua_path=out/'config/flycast/verify.lua'; lua_path.parent.mkdir(parents=True,exist_ok=True)
    env.update(SDL_VIDEODRIVER='offscreen',LIBGL_ALWAYS_SOFTWARE='1',XDG_CONFIG_HOME=str(out/'config'),XDG_DATA_HOME=str(out/'data'))
lua=r'''
local done=false
local function vblank()
  if done or flycast.memory.read32(ADDRESS)~=0x34485347 then return end
  local state=flycast.memory.read32(ADDRESS+8)
  local terminal=state==2 or state==3
  local f=assert(io.open(terminal and OUTPUT or PROGRESS,'wb'))
  for i=0,127 do f:write(string.char(flycast.memory.read8(ADDRESS+i))) end
  f:close(); done=terminal
end
flycast_callbacks={vblank=vblank}
'''.replace('ADDRESS',str(address)).replace('OUTPUT',json.dumps(result_path.as_posix())).replace('PROGRESS',json.dumps(progress_path.as_posix()))
lua_path.write_text(lua)
command=[str(exe),'-config','config:UseReios=yes','-config','config:Dreamcast.RamMod32MB=no',
         '-config','config:Debug.SerialConsoleEnabled=no','-config','config:LuaFileName=verify.lua',
         '-config','audio:backend=null','-config','log:LogToFile=yes',str(args.elf.resolve())]
with (out/'frontend.log').open('w') as log:
    process=subprocess.Popen(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,startupinfo=startup)
    try:
        deadline=time.monotonic()+30
        while process.poll() is None and time.monotonic()<deadline:
            if result_path.is_file() and result_path.stat().st_size==128: break
            time.sleep(0.1)
    finally:
        if process.poll() is None:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
if not result_path.is_file() or result_path.stat().st_size!=128:
    if progress_path.is_file() and progress_path.stat().st_size==128:
        progress=struct.unpack_from('<12I',progress_path.read_bytes())
        print('Last native progress: state=%d total=%d passed=%d failed_case=%d'%progress[2:6])
    parser.exit(1,'FAIL: native backend tests did not complete\n')
data=result_path.read_bytes()
names=('magic','version','state','total','passed','failed_case','system_ram','code_bytes',
       'observed_lo','observed_hi','expected_lo','expected_hi')
result=dict(zip(names,struct.unpack_from('<12I',data)))
result['description']=data[48:].split(b'\0',1)[0].decode('utf-8',errors='replace')
result['elf_sha256']=hashlib.sha256(args.elf.read_bytes()).hexdigest()
result['scope']=('Native typed GOAL subset and codegen/ABI bootstrap; not a complete compiler or game runtime'
                 if args.tests!=17 else 'Native codegen/ABI bootstrap; not an integrated GOAL compiler or game runtime')
if args.suite=='runtime': result['scope']='Native C runtime layout, symbols, types and bounded allocation foundation; not a complete GOAL kernel or game'
if args.suite=='projection': result['scope']='SH4ZAM viewer projection accuracy/state oracle; Flycast timing only, not physical hardware performance'
if args.suite=='runtime-goal': result['scope']='Native compiled GOAL scalar runtime, original list routines, typed memory, startup and static strings/floats; incomplete kernel/game'
if args.suite=='objects': result['scope']='Native original V2/V4 data-only relocation validation; retail code never executed'
passed=result['state']==2 and result['total']==args.tests and result['passed']==args.tests and result['system_ram']==16777216
result['result']='PASS' if passed else 'FAIL'
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
raise SystemExit(0 if passed else 1)
