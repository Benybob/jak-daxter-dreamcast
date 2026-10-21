"""Boot-test an ELF using Flycast 2.7's serial console, Lua and offscreen SDL.

Creates an isolated config, captures the actual RGB565 emulated framebuffer,
injects controller input, and checks target serial output. Linux only.
"""
import argparse
import json
import os
import pathlib
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("flycast", type=pathlib.Path)
    parser.add_argument("elf", type=pathlib.Path)
    parser.add_argument("--kind", choices=("probe", "scene"), required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--expect-error", help="expected SCENE_FAIL message for a negative scene test")
    args = parser.parse_args()
    output = args.output.resolve()
    config, data, runtime = (output / name for name in ("config", "data", "runtime"))
    for path in (config / "flycast", data / "flycast", runtime):
        path.mkdir(parents=True, exist_ok=True)
    runtime.chmod(0o700)
    ppm = output / "frame.ppm"
    ppm.unlink(missing_ok=True)
    lua_path = config / "flycast" / "verify.lua"
    # FB_R_SOF1 is the emulated PVR framebuffer read address. Read the 64-bit
    # VRAM window at 0xa5000000, not a host preview or a re-rendered scene.
    lua = r'''
local frames = 0
function cbVBlank()
  frames = frames + 1
  if frames == 100 and IS_SCENE then
    flycast.input.setAxis(1, 1, 20480)
    flycast.input.setAxis(1, 6, 46080)
  end
  if frames == 180 and IS_SCENE then
    flycast.input.setAxis(1, 1, 0)
    flycast.input.setAxis(1, 6, 0)
  end
  if frames == 240 then
    local control = flycast.memory.read32(0xa05f8044)
    assert(((control >> 2) & 3) == 1, 'expected RGB565 framebuffer')
    local offset = flycast.memory.read32(0xa05f8050) & 0x7fffff
    local file = assert(io.open(OUTPUT_PPM, 'wb'))
    file:write('P6\n640 480\n255\n')
    for y=0,479 do
      local row={}
      for x=0,639 do
        local c=flycast.memory.read16(0xa5000000+offset+(y*640+x)*2)
        row[#row+1]=string.char(math.floor(((c >> 11)&31)*255/31),
          math.floor(((c >> 5)&63)*255/63), math.floor((c&31)*255/31))
      end
      file:write(table.concat(row))
    end
    file:close()
    print('CAPTURE_OK frame='..frames)
  end
  if frames == 260 then flycast.input.pressButtons(1, 8) end
end
flycast_callbacks={vblank=cbVBlank}
function cbOverlay()
  if IS_SCENE then
    local active = frames >= 100 and frames < 180
    flycast.input.setAxis(1, 1, active and 20480 or 0)
    flycast.input.setAxis(1, 6, active and 46080 or 0)
  end
end
flycast_callbacks.overlay=cbOverlay
'''.replace("OUTPUT_PPM", json.dumps(str(ppm), ensure_ascii=False))
    lua = lua.replace("IS_SCENE", "true" if args.kind == "scene" else "false")
    lua_path.write_text(lua)
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="offscreen", LIBGL_ALWAYS_SOFTWARE="1",
               XDG_CONFIG_HOME=str(config), XDG_DATA_HOME=str(data), XDG_RUNTIME_DIR=str(runtime))
    command = [str(args.flycast.resolve()), "-config", "config:UseReios=yes",
               "-config", "config:Debug.SerialConsoleEnabled=yes",
               "-config", "config:LuaFileName=verify.lua",
               "-config", "config:rend.EmulateFramebuffer=yes",
               "-config", "config:Dreamcast.RamMod32MB=no",
               "-config", "audio:backend=null", str(args.elf.resolve())]
    log_path = output / "serial.log"
    with log_path.open("w") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=env, cwd=output)
        try:
            deadline = time.monotonic() + 30
            while process.poll() is None and time.monotonic() < deadline:
                text = log_path.read_text(errors="replace")
                if "arch: exit return code" in text:
                    break
                time.sleep(0.1)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    text = log_path.read_text(errors="replace")
    if args.expect_error:
        markers = ["SCENE_FAIL " + args.expect_error, "arch: exit return code 1"]
    elif args.kind == "probe":
        markers = ["crc-vector, 9 bytes, CRC32 cbf43926", "/rd/probe.dgo: OK; completed=2; bytes=240",
                   "CAPTURE_OK", "arch: exit return code 0"]
    else:
        markers = ["SCENE_READY", "SCENE_MEMORY", "system_ram=16777216", "SCENE_FRAME frames=120",
                   "SCENE_FRAME frames=240", "CAPTURE_OK", "SCENE_EXIT", "arch: exit return code 0"]
    missing = [marker for marker in markers if marker not in text]
    if args.kind == "scene" and not args.expect_error:
        distances = [float(value) for value in re.findall(r"distance=([0-9.]+)", text)]
        if not any(distance < 2.5 for distance in distances):
            missing.append("trigger zoom changed camera distance")
    if missing:
        parser.exit(1, f"FAIL: missing {missing}; inspect {log_path}\n")
    if not args.expect_error and (not ppm.is_file() or ppm.stat().st_size != 921615):
        parser.exit(1, f"FAIL: framebuffer capture missing/incomplete; inspect {log_path}\n")
    print(json.dumps({"result": "PASS", "kind": args.kind, "expected_error": args.expect_error,
                      "serial_log": str(log_path), "framebuffer": str(ppm) if ppm.exists() else None}, indent=2))


if __name__ == "__main__":
    main()
