# Build and verify the host-side port tools using MinGW (AI-assisted).
param(
    [string]$Compiler = 'C:\msys64\mingw64\bin\gcc.exe',
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$port = $PSScriptRoot
$build = Join-Path $port 'build'
$hostZlib = Join-Path (Split-Path (Split-Path $Compiler)) 'lib/libz.a'
if (!(Test-Path -LiteralPath $hostZlib)) { throw 'Host zlib development archive lib/libz.a is required in the selected MinGW compiler prefix; see SECTOR_INFLATE.md' }
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
& $Compiler @flags (Join-Path $port 'host_process_heap.c') (Join-Path $port 'process_heap_cases.c') (Join-Path $port 'goal_process_heap.c') (Join-Path $port 'goal_kernel_types.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'process-heap-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Process heap checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_thread_stack.c') (Join-Path $port 'thread_stack_cases.c') (Join-Path $port 'goal_thread_stack.c') (Join-Path $port 'goal_process_heap.c') (Join-Path $port 'goal_kernel_types.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'thread-stack-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Thread stack checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_code_entry.c') (Join-Path $port 'code_entry_cases.c') (Join-Path $port 'goal_module.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'code-entry-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Code entry checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_stack_call.c') (Join-Path $port 'goal_stack_call.c') (Join-Path $port 'goal_module.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'stack-call-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Stack call boundary checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_fiber.c') (Join-Path $port 'goal_fiber.c') (Join-Path $port 'goal_thread_stack.c') (Join-Path $port 'goal_process_heap.c') (Join-Path $port 'goal_module.c') (Join-Path $port 'goal_kernel_types.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'fiber-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Fiber lifecycle checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_scalar_call.c') (Join-Path $port 'scalar_call_cases.c') (Join-Path $port 'goal_scalar_call.c') -o (Join-Path $build 'scalar-call-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Scalar argument ABI checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_thread_resize.c') (Join-Path $port 'thread_resize_cases.c') (Join-Path $port 'goal_thread_resize.c') (Join-Path $port 'goal_thread_stack.c') (Join-Path $port 'goal_process_heap.c') (Join-Path $port 'goal_kernel_types.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'thread-resize-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Read-only main-thread resize checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_main_thread.c') (Join-Path $port 'main_thread_cases.c') (Join-Path $port 'goal_main_thread.c') (Join-Path $port 'goal_thread_stack.c') (Join-Path $port 'goal_process_heap.c') (Join-Path $port 'goal_kernel_types.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'main-thread-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Read-only main-thread state checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_main_completion.c') (Join-Path $port 'goal_main_start.c') (Join-Path $port 'goal_main_thread.c') (Join-Path $port 'goal_fiber.c') (Join-Path $port 'goal_scalar_call.c') (Join-Path $port 'goal_thread_stack.c') (Join-Path $port 'goal_process_heap.c') (Join-Path $port 'goal_module.c') (Join-Path $port 'goal_kernel_types.c') (Join-Path $port 'goal_runtime.c') -o (Join-Path $build 'main-completion-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Original main completion checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_player_motion.c') (Join-Path $port 'player_motion.c') (Join-Path $port 'player_motion_cases.c') -o (Join-Path $build 'player-motion-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Native movement checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_player_anim.c') (Join-Path $port 'player_anim.c') -o (Join-Path $build 'player-anim-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Baked animation checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_world_collision.c') (Join-Path $port 'world_collision.c') (Join-Path $port 'world_collision_cases.c') (Join-Path $port 'world_player_cases.c') (Join-Path $port 'world_patch_cases.c') (Join-Path $port 'player_motion.c') (Join-Path $port 'player_world_motion.c') -o (Join-Path $build 'world-collision-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Capsule world checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_world_load.c') (Join-Path $port 'world_collision.c') -o (Join-Path $build 'world-load-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Collision loader checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_world_manifest.c') (Join-Path $port 'world_manifest_cases.c') (Join-Path $port 'world_manifest.c') (Join-Path $port 'world_collision.c') (Join-Path $port 'textured_scene.c') -o (Join-Path $build 'world-manifest-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Training bundle checker compilation failed' }
& $Compiler @flags ('-I' + (Join-Path $port 'tests/training-stubs')) (Join-Path $port 'host_training_layer.c') (Join-Path $port 'training_layer.c') (Join-Path $port 'scene_clip.c') -o (Join-Path $build 'training-layer-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Training GPU layer checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_scene_clip.c') (Join-Path $port 'scene_clip.c') (Join-Path $port 'scene_clip_cases.c') -o (Join-Path $build 'scene-clip-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Near clipping checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_camera_boom.c') (Join-Path $port 'camera_boom.c') (Join-Path $port 'camera_rig.c') (Join-Path $port 'camera_boom_cases.c') (Join-Path $port 'world_collision.c') -o (Join-Path $build 'camera-boom-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Camera boom/rig checker compilation failed' }
& $Compiler @flags (Join-Path $port 'host_sector_air.c') (Join-Path $port 'sector_air_cases.c') (Join-Path $port 'world_collision.c') (Join-Path $port 'camera_boom.c') (Join-Path $port 'player_motion.c') (Join-Path $port 'player_world_motion.c') -o (Join-Path $build 'sector-air-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Committed air checker compilation failed' }
$sectorDefs = @('-Dmalloc=dc_test_malloc','-Dfree=dc_test_free','-Dfopen=dc_test_fopen','-Dfclose=dc_test_fclose','-Dfread=dc_test_fread','-Dfgetc=dc_test_fgetc','-Dferror=dc_test_ferror')
& $Compiler @flags @sectorDefs -c (Join-Path $port 'sector_cache.c') -o (Join-Path $build 'sector-cache-host.o')
if ($LASTEXITCODE -ne 0) { throw 'Sector cache instrumented compilation failed' }
& $Compiler @flags -shared (Join-Path $port 'tests/sector_cache_bridge.c') (Join-Path $build 'sector-cache-host.o') -o (Join-Path $build 'sector-cache-check.dll') -lm
if ($LASTEXITCODE -ne 0) { throw 'Sector cache ctypes bridge compilation failed' }
& $Compiler @flags (Join-Path $port 'host_sector_cache.c') (Join-Path $port 'sector_cache_cases.c') (Join-Path $port 'tests/sector_cache_bridge.c') (Join-Path $build 'sector-cache-host.o') -o (Join-Path $build 'sector-cache-check.exe') -lm
if ($LASTEXITCODE -ne 0) { throw 'Sector cache oracle compilation failed' }
& $pythonCommand (Join-Path $port 'make_inflate_fixture.py') (Join-Path $build 'inflate_fixture.h')
if ($LASTEXITCODE -ne 0) { throw 'Compressed fixture generation failed' }
& $Compiler @flags (Join-Path $port 'host_sector_inflate.c') (Join-Path $port 'sector_inflate_cases.c') (Join-Path $port 'tests/sector_inflate_bridge.c') $hostZlib -o (Join-Path $build 'sector-inflate-check.exe')
if ($LASTEXITCODE -ne 0) { throw 'Exact sector decompression checker compilation failed' }
$renderDefs = @('-Dmalloc=dc_render_test_malloc','-Dfree=dc_render_test_free','-DDC_RENDER_TEST_IO')
& $Compiler @flags @renderDefs -c (Join-Path $port 'render_package.c') -o (Join-Path $build 'render-package-host.o')
if ($LASTEXITCODE -ne 0) { throw 'Render package reader compilation failed' }
& $Compiler @flags -shared (Join-Path $port 'tests/render_package_bridge.c') (Join-Path $build 'render-package-host.o') (Join-Path $port 'sector_inflate.c') $hostZlib -o (Join-Path $build 'render-package-check.dll') -lm
if ($LASTEXITCODE -ne 0) { throw 'Render package ctypes bridge compilation failed' }
& $pythonCommand -m unittest discover -s (Join-Path $port 'tests') -v
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
