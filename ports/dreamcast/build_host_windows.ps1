# Build and verify the host-side port tools using MinGW (AI-assisted).
param(
    [string]$Compiler = 'C:\msys64\mingw64\bin\gcc.exe',
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$port = $PSScriptRoot
$build = Join-Path $port 'build'
$pythonCommand = (Get-Command $Python).Source
$env:PATH = (Split-Path -Parent $Compiler) + ';' + $env:PATH
New-Item -ItemType Directory -Force $build | Out-Null
$flags = @('-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic')
& $Compiler @flags (Join-Path $port 'host_probe.c') (Join-Path $port 'dgo_stream.c') -o (Join-Path $build 'dgo-probe.exe')
if ($LASTEXITCODE -ne 0) { throw 'Archive probe compilation failed' }
& $Compiler @flags (Join-Path $port 'host_scene.c') (Join-Path $port 'scene_io.c') -o (Join-Path $build 'scene-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Scene checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_textured.c') (Join-Path $port 'textured_scene.c') -o (Join-Path $build 'textured-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Textured scene checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_runtime.c') (Join-Path $port 'runtime_cases.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'runtime-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Runtime checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_module.c') (Join-Path $port 'goal_module.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'module-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Module loader checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_float.c') -o (Join-Path $build 'float-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'SSE float oracle compilation failed' }
& $Compiler @flags (Join-Path $port 'host_vq.c') -o (Join-Path $build 'vq-encode.exe')
if ($LASTEXITCODE -ne 0) { throw 'Offline VQ encoder compilation failed' }
& $Compiler @flags (Join-Path $port 'host_type.c') (Join-Path $port 'type_cases.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'type-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Type/method checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_object.c') (Join-Path $port 'goal_object.c') -o (Join-Path $build 'object-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Original object checker compilation failed' }
& $pythonCommand (Join-Path $port 'make_object_fixture.py') --header (Join-Path $build 'object_fixture_host.h') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Host object fixture generation failed' }
& $Compiler @flags (Join-Path $port 'host_object_load.c') (Join-Path $port 'object_load_cases.c') (Join-Path $port 'goal_object_load.c') (Join-Path $port 'goal_object.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'object-load-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Original data loader compilation failed' }
& $Compiler @flags (Join-Path $port 'host_kernel_types.c') (Join-Path $port 'kernel_type_cases.c') (Join-Path $port 'goal_kernel_types.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'kernel-types-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Fixed kernel type checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_shift.c') -o (Join-Path $build 'shift-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Integer shift oracle compilation failed' }
& $Compiler @flags (Join-Path $port 'host_division.c') -o (Join-Path $build 'division-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Integer division oracle compilation failed' }
& $Compiler @flags (Join-Path $port 'host_native_file.c') (Join-Path $port 'goal_native_file.c') (Join-Path $port 'goal_module.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'native-file-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Native module-file checker compilation failed' }
& $pythonCommand -m unittest discover -s (Join-Path $port 'tests') -v
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
