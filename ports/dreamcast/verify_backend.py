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
parser.add_argument('--suite',choices=('codegen','runtime','runtime-goal','objects','projection','movement','collision','world-manifest','view-clip','camera','sectors','compression','render-io','gpu-catalog','render-cache','world-view','frustum','render-base','training-render','instances'),default='codegen')
parser.add_argument('--timeout',type=float,default=30,help='Native completion deadline in seconds, 1..300')
parser.add_argument('--capture',action='store_true',help='Capture completed RGB565 framebuffer before recording terminal success')
parser.add_argument('--dump',nargs=2,metavar=('SYMBOL','BYTES'),help='Also write BYTES of a guest symbol to dump.bin at the end')
args=parser.parse_args()
if args.tests<1: parser.error('--tests must be positive')
if not 1<=args.timeout<=300: parser.error('--timeout must be 1..300 seconds')
symbols=subprocess.check_output([str(args.nm),str(args.elf)],text=True)
match=re.search(r'^([0-9a-fA-F]+)\s+\w\s+_?dc_backend$',symbols,re.MULTILINE)
if not match: parser.error('dc_backend symbol missing')
address=int(match[1],16)
cache_match=re.search(r'^([0-9a-fA-F]+)\s+\w\s+_?dc_cache_report$',symbols,re.MULTILINE)
cache_address=int(cache_match[1],16) if args.suite=='render-cache' and cache_match else 0
metrics_match=re.search(r'^([0-9a-fA-F]+)\s+\w\s+_?dc_base_metrics$',symbols,re.MULTILINE)
metrics_address=int(metrics_match[1],16) if args.suite=='render-base' and metrics_match else 0
metrics_bytes=40
if args.dump:
    dump_match=re.search(r'^([0-9a-fA-F]+)\s+\w\s+_?'+re.escape(args.dump[0])+'$',symbols,re.MULTILINE)
    if not dump_match: parser.error(args.dump[0]+' symbol missing')
    if metrics_address: parser.error('--dump and render-base metrics are exclusive')
    metrics_address,metrics_bytes=int(dump_match[1],16),int(args.dump[1])
out=args.output.resolve(); out.mkdir(parents=True,exist_ok=True)
result_path=out/'target.bin'; result_path.unlink(missing_ok=True)
progress_path=out/'progress.bin'; progress_path.unlink(missing_ok=True)
(out/'cache-progress.bin').unlink(missing_ok=True)
(out/'metrics.bin').unlink(missing_ok=True)
(out/'frame.ppm').unlink(missing_ok=True)
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
capture_helper=''
if args.capture:
    from verify_training import LUA as capture_source
    capture_helper=capture_source[capture_source.index('local function capture(name)'):capture_source.index('local function finish(name)')]
    capture_helper=capture_helper.replace('OUT',json.dumps(out.as_posix()))
lua=r'''
CAPTURE_HELPER
local done=false
local samples=0
local function vblank()
  if done or flycast.memory.read32(ADDRESS)~=0x34485347 then return end
  local state=flycast.memory.read32(ADDRESS+8)
  local terminal=state==2 or state==3
  samples=samples+1
  if not terminal and samples%60~=1 then return end
  if terminal then
    CAPTURE_CALL
    if METRICS~=0 then
      local m=assert(io.open(METRICS_OUTPUT,'wb'))
      for i=0,METRIC_BYTES-1 do m:write(string.char(flycast.memory.read8(METRICS+i))) end
      m:close()
    end
  end
  local f=assert(io.open(terminal and OUTPUT or PROGRESS,'wb'))
  for i=0,127 do f:write(string.char(flycast.memory.read8(ADDRESS+i))) end
  f:close(); done=terminal
  if CACHE~=0 then
    local c=assert(io.open(CACHE_OUTPUT,'wb'))
    for i=0,23 do c:write(string.char(flycast.memory.read8(CACHE+i))) end
    c:close()
  end
end
flycast_callbacks={vblank=vblank}
'''.replace('CAPTURE_HELPER',capture_helper).replace('CAPTURE_CALL',"if state==2 then capture('frame') end" if args.capture else '')
lua=lua.replace('METRIC_BYTES',str(metrics_bytes))
lua=lua.replace('METRICS_OUTPUT',json.dumps((out/('dump.bin' if args.dump else 'metrics.bin')).as_posix())).replace('METRICS',str(metrics_address))
lua=lua.replace('CACHE_OUTPUT',json.dumps((out/'cache-progress.bin').as_posix())).replace('CACHE',str(cache_address)).replace('ADDRESS',str(address)).replace('OUTPUT',json.dumps(result_path.as_posix())).replace('PROGRESS',json.dumps(progress_path.as_posix()))
lua_path.write_text(lua)
command=[str(exe),'-config','config:UseReios=yes','-config','config:Dreamcast.RamMod32MB=no',
         '-config','config:Debug.SerialConsoleEnabled=no','-config','config:LuaFileName=verify.lua',
         '-config','audio:backend=null','-config','log:LogToFile=yes',str(args.elf.resolve())]
if args.capture:command[1:1]=['-config','config:rend.EmulateFramebuffer=yes']
with (out/'frontend.log').open('w') as log:
    process=subprocess.Popen(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,startupinfo=startup)
    try:
        deadline=time.monotonic()+args.timeout
        while process.poll() is None and time.monotonic()<deadline:
            if result_path.is_file() and result_path.stat().st_size==128: break
            time.sleep(0.1)
    finally:
        if process.poll() is None:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
if not result_path.is_file() or result_path.stat().st_size!=128:
    cache_progress=out/'cache-progress.bin'
    if cache_progress.is_file() and cache_progress.stat().st_size==24:
        print('Cache report total=%d passed=%d failed_line=%d windows=%d maxfaces=%d peakbytes=%d'%struct.unpack('<6I',cache_progress.read_bytes()))
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
if args.suite=='movement': result['scope']='Native C test-map movement/collision oracle; not original GOAL gameplay or physical hardware validation'
if args.suite=='collision': result['scope']='Native capsule/controller/PAT oracle, optionally original bounded collision patch; not original GOAL physics or hardware validation'
if args.suite=='world-manifest': result['scope']='Native WLD2/3 byte decoding, transactional refusal, CRC, count/origin/coverage and surface-event guards; not original GOAL gameplay or hardware validation'
if args.suite=='camera': result['scope']='Native sphere-boom and camera-rig synthetic/original-patch oracle; not original GOAL camera or hardware validation'
if args.suite=='sectors': result['scope']='Native bounded sector IO/cache, exact rebased geometry payload and rollback oracle; not render streaming, original GOAL gameplay or hardware validation'
if args.suite=='compression': result['scope']='Native exact bounded zlib block decoding, framing, workspace and canary oracle; not integrated sector/render streaming or hardware validation'
if args.suite=='render-io': result['scope']='Native DRM2/DRS2 catalog and compressed geometry reader, source ledger and failure cleanup; no GPU/cache integration or hardware validation'
if args.suite=='gpu-catalog': result['scope']='Native resident PVR base/specular color catalog, exact VRAM readback and staged allocation failure ownership; strength retained on CPU, no geometry/cache/reflection shader integration or hardware validation'
if args.suite=='render-cache': result['scope']='Native bounded transactional CPU render-window cache, exact original payload CRC and fault rollback; no GPU drawing/game integration or hardware validation'
if args.suite=='world-view': result['scope']='Native packed world geometry SH4ZAM camera transform, large-coordinate precision, projection/near masking and matrix/FPU controls; no GPU drawing/game integration or hardware validation'
if args.suite=='frustum': result['scope']='Native full viewport/near clipping against exact rational barycentric hulls; raw UV/color/winding and long-face coverage, no GPU drawing/game integration or hardware validation'
if args.suite=='render-base': result['scope']='Native cached geometry, independent resident textures, SH4ZAM world transform and clipped base-pass raster; allocation rollback/cleanup, no reflection/TOD shader, playable streaming integration or hardware validation'
if args.suite=='view-clip': result['scope']='Native SH4ZAM view/cache accuracy, matrix/control preservation, mode refusal and analytic near-plane clipping; not hardware performance or full gameplay'
if args.suite=='runtime-goal': result['scope']='Native compiled GOAL scalar runtime, original list routines, typed memory, startup and static strings/floats; incomplete kernel/game'
if args.suite=='objects': result['scope']='Native original V2/V4 data-only relocation validation; retail code never executed'
if args.suite=='instances': result['scope']='Native DCI1 instanced TIE/shrub package, per-instance view transform, viewport clipping and untextured Gouraud raster; no textures, LOD, streaming, game integration or hardware validation'
if args.suite=='training-render': result['scope']='Native combined render owner source binding, frame-allocation and selected-file rollback, reuse, known-empty and CPU/GPU cleanup; no full-game or hardware validation'
passed=result['state']==2 and result['total']==args.tests and result['passed']==args.tests and result['system_ram']==16777216
result['result']='PASS' if passed else 'FAIL'
if args.capture and passed:
    frame=out/'frame.ppm'
    if not frame.is_file() or frame.stat().st_size!=921615 or not frame.read_bytes().startswith(b'P6\n640 480\n255\n'):
        parser.exit(1,'FAIL: completed framebuffer capture missing or malformed\n')
    result['framebuffer_sha256']=hashlib.sha256(frame.read_bytes()).hexdigest()
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
raise SystemExit(0 if passed else 1)
