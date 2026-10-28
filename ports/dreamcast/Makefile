.DEFAULT_GOAL := all
HOST_CC ?= cc
PYTHON ?= python3
ifeq ($(OS),Windows_NT)
HOST_EXE := .exe
HOST_SHARED := .dll
endif
HOST_SHARED ?= .so
TARGET := jak-dc-probe.elf
OBJS := main.o dgo_stream.o romdisk.o
SCENE_TARGET := jak-dc-scene.elf
SCENE_OBJS := scene_main.o scene_io.o romdisk.o
TEXTURED_TARGET := jak-dc-textured.elf
TEXTURED_OBJS := textured_main.o textured_scene.o scene_projection_sh4zam.o scene_projection_gate.o romdisk.o
BACKEND_TARGET := jak-dc-backend.elf
RUNTIME_TARGET := jak-dc-runtime.elf
KOS_ROMDISK_DIR := romdisk

.PHONY: all host test fixture clean
# An explicit asset-directory change can point at OLDER files. Timestamps of
# the previous directory must never leave an ELF embedding the wrong packet.
# Also refresh on removed files, which Make's wildcard alone cannot detect.
.PHONY: private-romdisk-refresh
private-romdisk-refresh:
build/player-romdisk.o build/world-romdisk.o build/training-romdisk.o build/camera-world-romdisk.o build/sector-romdisk.o build/render-romdisk.o build/gpu-catalog-romdisk.o build/render-cache-romdisk.o: private-romdisk-refresh
build/render-base-romdisk.o: private-romdisk-refresh
build/training-stream-romdisk.o: private-romdisk-refresh
build/training-owner-romdisk.o: private-romdisk-refresh
all: $(TARGET) $(SCENE_TARGET) $(TEXTURED_TARGET) $(BACKEND_TARGET) $(RUNTIME_TARGET)

# Host-only commands need no KallistiOS installation.
ifneq ($(filter-out host test fixture clean,$(or $(MAKECMDGOALS),all)),)
ifndef KOS_BASE
$(error Source your KallistiOS environ.sh before building the Dreamcast target)
endif
include $(KOS_BASE)/Makefile.rules
endif

CFLAGS += -std=c99 -Wall -Wextra
main.o dgo_stream.o: dgo_stream.h

$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS)

$(SCENE_TARGET): $(SCENE_OBJS)
	kos-cc -o $@ $(SCENE_OBJS) -lm

$(TEXTURED_TARGET): $(TEXTURED_OBJS)
	kos-cc -o $@ $(TEXTURED_OBJS) -lsh4zam -lm

# Install the upstream KOS port first; the project contains no vendored library.
scene_projection_sh4zam.o: scene_projection_sh4zam.c scene_projection.h
	$(KOS_CC) $(KOS_CFLAGS) -std=gnu17 -Wall -Wextra -Werror -c $< -o $@
scene_projection_reference.o: scene_projection_reference.c scene_projection.h
projection_main.o: projection_main.c scene_projection.h
	$(KOS_CC) $(KOS_CFLAGS) -std=gnu17 -Wall -Wextra -Werror -c $< -o $@
jak-dc-projection.elf: projection_main.o scene_projection_sh4zam.o scene_projection_reference.o scene_projection_gate.o projection_mode_probe.o
	kos-cc -o $@ $^ -lsh4zam -lm

textured_main.o: textured_scene.h scene_telemetry.h scene_projection.h
textured_scene.o: textured_scene.h

# Private generated assets are supplied explicitly; never add them to Git.
PLAY_ASSET_DIR ?= build/private-player-assets
play_main.o: play_main.c player_motion.h player_anim.h textured_scene.h scene_projection.h scene_telemetry.h
player_motion.o player_motion_cases.o player_motion_main.o: player_motion.h
player_anim.o: player_anim.h
build/player-romdisk.o: $(PLAY_ASSET_DIR)/player.dct $(PLAY_ASSET_DIR)/player.dca
	mkdir -p build
	$(KOS_GENROMFS) -f build/player-romdisk.img -d $(PLAY_ASSET_DIR) -x report.json
	$(KOS_BASE)/utils/bin2c/bin2c build/player-romdisk.img build/player-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/player-romdisk.c -o build/player-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/player-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
jak-dc-play.elf: play_main.o player_motion.o player_anim.o textured_scene.o scene_projection_sh4zam.o scene_projection_gate.o build/player-romdisk.o
	kos-cc -o $@ $^ -lsh4zam -lm
jak-dc-motion.elf: player_motion.o player_motion_cases.o player_motion_main.o
	kos-cc -o $@ $^ -lm

WORLD_OBJS := world_collision.o world_collision_cases.o world_player_cases.o world_patch_cases.o player_world_motion.o player_motion.o
world_collision.o world_collision_cases.o: world_collision.h
world_player_cases.o world_patch_cases.o player_world_motion.o: player_world_motion.h player_motion_internal.h world_collision.h
player_motion.o: player_motion_internal.h
jak-dc-world.elf: world_collision_main.o $(WORLD_OBJS)
	kos-cc -o $@ $^ -lm
WORLD_ASSET_DIR ?= build/private-world-assets
build/world-romdisk.o: $(WORLD_ASSET_DIR)/world.dcc
	mkdir -p build
	$(KOS_GENROMFS) -f build/world-romdisk.img -d $(WORLD_ASSET_DIR) -x report.json
	$(KOS_BASE)/utils/bin2c/bin2c build/world-romdisk.img build/world-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/world-romdisk.c -o build/world-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/world-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
world_real_main.o: world_collision_main.c
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_WORLD_REAL_PATCH -c $< -o $@
jak-dc-world-real.elf: world_real_main.o $(WORLD_OBJS) build/world-romdisk.o
	kos-cc -o $@ $^ -lm

$(BACKEND_TARGET): backend_main.o
	kos-cc -o $@ $^ -lm

jak-dc-object.elf: object_main.o object_cases.o object_load_cases.o goal_object_load.o goal_object.o goal_kernel_types.o goal_runtime.o goal_runtime_dc.o
	kos-cc -o $@ $^
object_main.o: object_main.c object_cases.h object_load_cases.h runtime_cases.h goal_runtime.h goal_kernel_types.h
object_cases.o: object_cases.c object_cases.h goal_object.h build/object_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c object_cases.c -o $@
goal_object.o: goal_object.c goal_object.h
goal_object_load.o: goal_object_load.c goal_object_load.h goal_object.h goal_runtime.h
object_load_cases.o: object_load_cases.c object_load_cases.h goal_object_load.h goal_runtime.h build/object_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c object_load_cases.c -o $@

$(RUNTIME_TARGET): runtime_main.o runtime_cases.o goal_runtime.o
	kos-cc -o $@ $^

runtime_main.o: runtime_cases.h
runtime_cases.o: runtime_cases.h goal_runtime.h goal_layout.h
goal_runtime.o: goal_runtime.h goal_layout.h
goal_runtime_dc.o: goal_runtime_dc.c goal_runtime.h goal_layout.h
goal_module.o: goal_module.h goal_runtime.h goal_layout.h
goal_bridge.o: goal_bridge.c goal_bridge.h goal_module.h goal_runtime.h goal_layout.h goal_process_heap.h goal_mmi.h goal_pad.h
goal_mmi.o: goal_mmi.c goal_mmi.h
goal_pad.o: goal_pad.c goal_pad.h
goal_process_heap.o: goal_process_heap.c goal_process_heap.h goal_runtime.h goal_layout.h
goal_thread_stack.o: goal_thread_stack.c goal_thread_stack.h goal_process_heap.h goal_runtime.h
thread_stack_cases.o: thread_stack_cases.c thread_stack_cases.h goal_thread_stack.h goal_kernel_types.h goal_runtime.h
process_heap_cases.o: process_heap_cases.c process_heap_cases.h goal_process_heap.h goal_kernel_types.h goal_runtime.h
runtime_goal_cases.o: runtime_goal_cases.c runtime_cases.h kernel_type_cases.h thread_stack_cases.h goal_module.h goal_bridge.h goal_runtime.h goal_layout.h build/runtime_fixture.h build/runtime_identity.h build/runtime_identity_baseline.h build/initializer_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_goal_cases.c -o $@

runtime_goal_main.o: runtime_main.c runtime_cases.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_RUNTIME_GOAL -c runtime_main.c -o $@

runtime_memory_cases.o: runtime_memory_cases.c runtime_cases.h goal_module.h goal_runtime.h goal_layout.h build/memory_fixture.h build/original_lists.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_memory_cases.c -o $@
runtime_static_cases.o: runtime_static_cases.c runtime_cases.h goal_module.h goal_runtime.h goal_layout.h build/static_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_static_cases.c -o $@
runtime_float_cases.o: runtime_float_cases.c runtime_cases.h goal_module.h goal_runtime.h goal_layout.h float_vectors.h build/float_fixture.h build/original_math.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_float_cases.c -o $@
runtime_bridge_cases.o: runtime_bridge_cases.c runtime_cases.h goal_bridge.h goal_module.h goal_runtime.h goal_layout.h build/bridge_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_bridge_cases.c -o $@
runtime_quoted_cases.o: runtime_quoted_cases.c runtime_cases.h goal_module.h goal_runtime.h goal_layout.h build/quoted_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_quoted_cases.c -o $@

runtime_void_cases.o: runtime_void_cases.c runtime_cases.h goal_module.h goal_runtime.h build/void_fixture.h build/void_empty.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_void_cases.c -o $@

runtime_type_cases.o: runtime_type_cases.c type_cases.h goal_bridge.h goal_module.h goal_runtime.h build/type_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_type_cases.c -o $@
type_cases.o: type_cases.c type_cases.h goal_runtime.h goal_layout.h

goal_kernel_types.o: goal_kernel_types.c goal_kernel_types.h kernel_types_data.h goal_runtime.h goal_layout.h
kernel_type_cases.o: kernel_type_cases.c kernel_type_cases.h goal_kernel_types.h kernel_types_data.h goal_runtime.h goal_layout.h
runtime_shift_cases.o: runtime_shift_cases.c runtime_cases.h goal_module.h goal_runtime.h shift_vectors.h build/shift_fixture.h build/original_shift.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_shift_cases.c -o $@
runtime_shift_baseline.o: runtime_shift_baseline.c goal_module.h goal_runtime.h build/shift_baseline.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_shift_baseline.c -o $@

runtime_locals_cases.o: runtime_locals_cases.c runtime_cases.h goal_module.h goal_runtime.h build/locals_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_locals_cases.c -o $@
runtime_early_cases.o: runtime_early_cases.c runtime_cases.h goal_module.h goal_runtime.h build/early_fixture.h build/original_early.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_early_cases.c -o $@
runtime_sort_cases.o: runtime_sort_cases.c runtime_cases.h goal_module.h goal_runtime.h build/sort_fixture.h build/original_sort.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_sort_cases.c -o $@
runtime_operator_cases.o: runtime_operator_cases.c runtime_cases.h goal_module.h goal_runtime.h build/original_operators.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_operator_cases.c -o $@
runtime_fixed_cases.o: runtime_fixed_cases.c runtime_cases.h goal_module.h goal_runtime.h build/fixed_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_fixed_cases.c -o $@
runtime_far_cases.o: runtime_far_cases.c runtime_cases.h goal_module.h goal_runtime.h build/far_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_far_cases.c -o $@
runtime_dependency_cases.o: runtime_dependency_cases.c runtime_cases.h goal_module.h goal_runtime.h build/dependency_context.h build/dependency_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_dependency_cases.c -o $@
runtime_mixed_cases.o: runtime_mixed_cases.c runtime_cases.h goal_module.h goal_runtime.h build/mixed_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_mixed_cases.c -o $@
runtime_validation_cases.o: runtime_validation_cases.c runtime_cases.h goal_module.h goal_runtime.h build/original_valid.h build/scalar_validation_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_validation_cases.c -o $@
runtime_kernel_globals_cases.o: runtime_kernel_globals_cases.c runtime_cases.h goal_module.h goal_runtime.h build/original_kernel_globals.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_kernel_globals_cases.c -o $@
runtime_enum_cases.o: runtime_enum_cases.c runtime_cases.h goal_module.h goal_runtime.h build/enum_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_enum_cases.c -o $@
runtime_mixed_stack_cases.o: runtime_mixed_stack_cases.c runtime_cases.h goal_module.h goal_runtime.h build/mixed_stack_fixture.h build/mixed_stack_cases.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_mixed_stack_cases.c -o $@
runtime_process_tree_cases.o: runtime_process_tree_cases.c runtime_cases.h goal_module.h goal_runtime.h build/original_process_tree.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_process_tree_cases.c -o $@
runtime_process_new_cases.o: runtime_process_new_cases.c runtime_cases.h goal_bridge.h goal_module.h goal_runtime.h build/original_process_new.h build/process_new_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_process_new_cases.c -o $@
runtime_process_heap_cases.o: runtime_process_heap_cases.c runtime_cases.h goal_bridge.h goal_process_heap.h process_heap_cases.h build/process_heap_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_process_heap_cases.c -o $@
runtime_structured_cases.o: runtime_structured_cases.c runtime_cases.h goal_module.h goal_runtime.h build/structured_static_fixture.h build/original_kernel_context.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_structured_cases.c -o $@
runtime_process_entry_cases.o: runtime_process_entry_cases.c runtime_cases.h goal_bridge.h goal_runtime.h build/original_process_entry.h build/context_read_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_process_entry_cases.c -o $@
runtime_process_query_cases.o: runtime_process_query_cases.c runtime_cases.h goal_bridge.h goal_runtime.h build/original_process_query.h build/original_name_compare.h build/original_null_context.h build/original_query_globals.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_process_query_cases.c -o $@
runtime_call_guard_cases.o: runtime_call_guard_cases.c runtime_cases.h goal_bridge.h goal_runtime.h build/call_guard_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_call_guard_cases.c -o $@
runtime_cpu_thread_cases.o: runtime_cpu_thread_cases.c runtime_cases.h goal_bridge.h goal_runtime.h goal_thread_stack.h build/original_cpu_thread.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_cpu_thread_cases.c -o $@
runtime_division_cases.o: runtime_division_cases.c runtime_cases.h goal_bridge.h goal_module.h goal_runtime.h division_vectors.h build/division_fixture.h build/original_division.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_division_cases.c -o $@
goal_native_file.o: goal_native_file.c goal_native_file.h goal_module.h goal_runtime.h
runtime_native_file_cases.o: runtime_native_file_cases.c runtime_cases.h goal_native_file.h goal_module.h goal_runtime.h build/native_file_blob.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_native_file_cases.c -o $@

kernel_types_data.h: generate_kernel_types.py ../../game/kernel/jak1/kscheme.cpp ../../common/symbols.h
	$(PYTHON) generate_kernel_types.py

jak-dc-runtime-goal.elf: goal_thread_resize.o thread_resize_cases.o runtime_thread_resize_cases.o runtime_fiber_words_cases.o goal_scalar_call.o scalar_call_cases.o runtime_activation_cases.o native_context_swap.o goal_fiber.o pp_scope.o runtime_fiber_cases.o native_stack_call.o goal_stack_call.o runtime_stack_call_cases.o code_entry_cases.o goal_register_snapshot.o register_snapshot_probe.o runtime_register_snapshot_cases.o runtime_goal_main.o runtime_goal_cases.o runtime_memory_cases.o runtime_static_cases.o runtime_float_cases.o runtime_bridge_cases.o runtime_quoted_cases.o runtime_void_cases.o runtime_type_cases.o runtime_shift_cases.o runtime_shift_baseline.o runtime_division_cases.o runtime_native_file_cases.o runtime_locals_cases.o runtime_early_cases.o runtime_sort_cases.o runtime_operator_cases.o runtime_fixed_cases.o runtime_far_cases.o runtime_dependency_cases.o runtime_mixed_cases.o runtime_validation_cases.o runtime_kernel_globals_cases.o runtime_enum_cases.o runtime_mixed_stack_cases.o runtime_process_tree_cases.o runtime_process_new_cases.o runtime_dead_pool_cases.o runtime_process_heap_cases.o runtime_structured_cases.o runtime_process_entry_cases.o runtime_process_query_cases.o runtime_call_guard_cases.o runtime_cpu_thread_cases.o runtime_pp_scope_cases.o goal_native_file.o type_cases.o kernel_type_cases.o goal_kernel_types.o runtime_cases.o goal_runtime.o goal_runtime_dc.o goal_module.o goal_bridge.o goal_mmi.o goal_pad.o goal_process_heap.o process_heap_cases.o goal_thread_stack.o thread_stack_cases.o
	kos-cc -o $@ $^
goal_layout.h: generate_runtime_layout.py ../../common/goal_constants.h ../../common/symbols.h ../../game/kernel/jak1/kscheme.h
	$(PYTHON) generate_runtime_layout.py

backend_main.o: build/backend_cases.h
goal_main.o: backend_main.c build/backend_cases.h build/goal_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_GOAL_FRONTEND -c backend_main.c -o $@

jak-dc-goal.elf: goal_main.o
	kos-cc -o $@ $^ -lm

goal_leaf_main.o: backend_main.c build/backend_cases.h build/goal_fixture.h build/game_leaf.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_GOAL_FRONTEND -DDC_GAME_LEAF -c backend_main.c -o $@

jak-dc-goal-leaf.elf: goal_leaf_main.o
	kos-cc -o $@ $^ -lm

build/backend_cases.h: sh4_backend.py
	$(PYTHON) sh4_backend.py --header $@

scene_main.o scene_io.o: scene_io.h
scene_main.o: scene_telemetry.h

# Directory mtime also detects a removed external scene, avoiding stale assets.
romdisk.img: romdisk/probe.dgo romdisk/fixture.dcm romdisk $(wildcard romdisk/*)
romdisk:
	mkdir -p $@
romdisk/probe.dgo: make_fixture.py
	$(PYTHON) make_fixture.py $@

fixture: romdisk/probe.dgo romdisk/fixture.dcm

romdisk/fixture.dcm: make_scene_fixture.py convert_scene.py
	mkdir -p build
	$(PYTHON) make_scene_fixture.py build/fixture.glb
	$(PYTHON) convert_scene.py build/fixture.glb $@

host: build/dgo-probe$(HOST_EXE) build/scene-check$(HOST_EXE) build/textured-check$(HOST_EXE) build/runtime-check$(HOST_EXE) build/module-check$(HOST_EXE) build/float-check$(HOST_EXE) build/vq-encode$(HOST_EXE) build/type-check$(HOST_EXE) build/object-check$(HOST_EXE) build/object-load-check$(HOST_EXE) build/kernel-types-check$(HOST_EXE) build/shift-check$(HOST_EXE) build/division-check$(HOST_EXE) build/native-file-check$(HOST_EXE) build/process-heap-check$(HOST_EXE) build/thread-stack-check$(HOST_EXE)
build/process-heap-check$(HOST_EXE): host_process_heap.c process_heap_cases.c process_heap_cases.h goal_process_heap.c goal_process_heap.h goal_kernel_types.c goal_runtime.c goal_runtime.h goal_layout.h
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_process_heap.c process_heap_cases.c goal_process_heap.c goal_kernel_types.c goal_runtime.c -o $@
build/thread-stack-check$(HOST_EXE): host_thread_stack.c thread_stack_cases.c thread_stack_cases.h goal_thread_stack.c goal_thread_stack.h goal_process_heap.c goal_process_heap.h goal_kernel_types.c goal_runtime.c goal_runtime.h goal_layout.h
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_thread_stack.c thread_stack_cases.c goal_thread_stack.c goal_process_heap.c goal_kernel_types.c goal_runtime.c -o $@
build/native-file-check$(HOST_EXE): host_native_file.c goal_native_file.c goal_native_file.h goal_module.c goal_module.h goal_runtime.c goal_runtime.h goal_layout.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_native_file.c goal_native_file.c goal_module.c goal_runtime.c -o $@
build/division-check$(HOST_EXE): host_division.c division_vectors.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_division.c -o $@
build/shift-check$(HOST_EXE): host_shift.c shift_vectors.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_shift.c -o $@
build/kernel-types-check$(HOST_EXE): host_kernel_types.c kernel_type_cases.c kernel_type_cases.h goal_kernel_types.c goal_kernel_types.h kernel_types_data.h goal_runtime.c goal_runtime.h goal_layout.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_kernel_types.c kernel_type_cases.c goal_kernel_types.c goal_runtime.c -o $@
build/object_fixture_host.h: make_object_fixture.py
	$(PYTHON) make_object_fixture.py --header $@
build/object-load-check$(HOST_EXE): host_object_load.c object_load_cases.c object_load_cases.h goal_object_load.c goal_object_load.h goal_object.c goal_object.h goal_runtime.c goal_runtime.h goal_layout.h build/object_fixture_host.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_object_load.c object_load_cases.c goal_object_load.c goal_object.c goal_runtime.c -o $@
build/object-check$(HOST_EXE): host_object.c goal_object.c goal_object.h
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_object.c goal_object.c -o $@
build/type-check$(HOST_EXE): host_type.c type_cases.c type_cases.h goal_runtime.c goal_runtime.h goal_layout.h
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_type.c type_cases.c goal_runtime.c -o $@
build/scene-check$(HOST_EXE): host_scene.c scene_io.c scene_io.h
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_scene.c scene_io.c -o $@ -lm
build/dgo-probe$(HOST_EXE): host_probe.c dgo_stream.c dgo_stream.h
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_probe.c dgo_stream.c -o $@

build/textured-check$(HOST_EXE): host_textured.c textured_scene.c textured_scene.h
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_textured.c textured_scene.c -o $@ -lm

build/runtime-check$(HOST_EXE): host_runtime.c runtime_cases.c runtime_cases.h goal_runtime.c goal_runtime.h goal_layout.h
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_runtime.c runtime_cases.c goal_runtime.c -o $@

build/module-check$(HOST_EXE): host_module.c goal_module.c goal_module.h goal_runtime.c goal_runtime.h goal_layout.h
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_module.c goal_module.c goal_runtime.c -o $@
build/float-check$(HOST_EXE): host_float.c float_vectors.h
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_float.c -o $@
build/vq-encode$(HOST_EXE): host_vq.c
	mkdir -p build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_vq.c -o $@

test: host
	$(PYTHON) -m unittest discover -s tests -v

clean:
	rm -f runtime_initialization_cases.o
	rm -f native_catch.o catch_escape_probe.o runtime_catch_raw_cases.o
	rm -f runtime_stack_objects_cases.o runtime_stack_objects_baseline.o
	rm -f goal_thread_resize.o thread_resize_cases.o runtime_thread_resize_cases.o
	rm -f runtime_fiber_words_cases.o
	rm -f goal_scalar_call.o scalar_call_cases.o
	rm -f runtime_activation_cases.o
	rm -f native_context_swap.o goal_fiber.o pp_scope.o process_spans.o runtime_process_spans_cases.o runtime_fiber_cases.o
	rm -f native_stack_call.o goal_stack_call.o runtime_stack_call_cases.o
	rm -f code_entry_cases.o
	rm -f goal_register_snapshot.o register_snapshot_probe.o runtime_register_snapshot_cases.o
	rm -f goal_thread_stack.o thread_stack_cases.o
	rm -f goal_process_heap.o process_heap_cases.o runtime_process_heap_cases.o runtime_structured_cases.o runtime_process_entry_cases.o runtime_process_query_cases.o runtime_call_guard_cases.o runtime_cpu_thread_cases.o runtime_pp_scope_cases.o
	rm -f runtime_validation_cases.o runtime_kernel_globals_cases.o runtime_enum_cases.o runtime_mixed_stack_cases.o runtime_process_tree_cases.o runtime_process_new_cases.o runtime_dead_pool_cases.o
	rm -f runtime_locals_cases.o runtime_early_cases.o runtime_sort_cases.o runtime_operator_cases.o runtime_fixed_cases.o runtime_far_cases.o runtime_dependency_cases.o runtime_mixed_cases.o
	rm -f runtime_native_file_cases.o goal_native_file.o
	rm -f runtime_division_cases.o
	rm -f runtime_shift_baseline.o
	rm -f runtime_shift_cases.o
	rm -f kernel_type_cases.o goal_kernel_types.o
	rm -f object_load_cases.o goal_object_load.o
	rm -f object_main.o object_cases.o goal_object.o jak-dc-object.elf
	rm -f runtime_type_cases.o type_cases.o
	rm -f runtime_void_cases.o
	rm -f runtime_quoted_cases.o
	rm -f runtime_bridge_cases.o goal_bridge.o
	rm -f runtime_float_cases.o
	rm -f runtime_static_cases.o
	rm -f goal_runtime_dc.o
	rm -f runtime_memory_cases.o
	rm -f runtime_goal_main.o runtime_goal_cases.o runtime_lexical_pp_cases.o runtime_deactivate_cases.o runtime_suspend_cases.o runtime_enter_state_cases.o runtime_dispatcher_cases.o goal_module.o jak-dc-runtime-goal.elf
	rm -f $(TARGET) $(SCENE_TARGET) $(TEXTURED_TARGET) $(BACKEND_TARGET) $(RUNTIME_TARGET) $(OBJS) $(SCENE_OBJS) $(TEXTURED_OBJS) backend_main.o runtime_main.o runtime_cases.o goal_runtime.o goal_main.o goal_leaf_main.o jak-dc-goal.elf jak-dc-goal-leaf.elf romdisk.img romdisk_tmp.c romdisk_tmp.o romdisk/probe.dgo romdisk/fixture.dcm
	rm -rf build

# Native same-stack register observation only (AI-assisted).
goal_register_snapshot.o: goal_register_snapshot.S goal_register_snapshot.h
	$(KOS_CC) $(KOS_CFLAGS) -c $< -o $@
register_snapshot_probe.o: register_snapshot_probe.S
	$(KOS_CC) $(KOS_CFLAGS) -c $< -o $@
runtime_register_snapshot_cases.o: runtime_register_snapshot_cases.c goal_register_snapshot.h runtime_cases.h

code_entry_cases.o: code_entry_cases.c code_entry_cases.h goal_module.h goal_runtime.h runtime_cases.h

host: build/code-entry-check$(HOST_EXE)
build/code-entry-check$(HOST_EXE): host_code_entry.c code_entry_cases.c code_entry_cases.h goal_module.c goal_module.h goal_runtime.c goal_runtime.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_code_entry.c code_entry_cases.c goal_module.c goal_runtime.c -o $@

# Synchronous alternate-stack diagnostic (AI-assisted).
native_stack_call.o: native_stack_call.S
	$(KOS_CC) $(KOS_CFLAGS) -c $< -o $@
goal_stack_call.o: goal_stack_call.c goal_stack_call.h goal_module.h goal_register_snapshot.h
runtime_stack_call_cases.o: runtime_stack_call_cases.c goal_stack_call.h goal_module.h goal_register_snapshot.h goal_bridge.h runtime_cases.h

host: build/stack-call-check$(HOST_EXE)
build/stack-call-check$(HOST_EXE): host_stack_call.c goal_stack_call.c goal_stack_call.h goal_module.c goal_module.h goal_runtime.c goal_runtime.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_stack_call.c goal_stack_call.c goal_module.c goal_runtime.c -o $@

# Scoped cooperative bootstrap contexts (AI-assisted).
native_context_swap.o: native_context_swap.S
	$(KOS_CC) $(KOS_CFLAGS) -c $< -o $@
goal_fiber.o: goal_fiber.c pp_scope.c goal_fiber.h goal_module.h goal_register_snapshot.h goal_thread_stack.h
runtime_fiber_cases.o: runtime_fiber_cases.c goal_fiber.h goal_bridge.h runtime_cases.h build/fiber_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c $< -o $@
goal_bridge.o: goal_fiber.h

host: build/fiber-check$(HOST_EXE)
build/fiber-check$(HOST_EXE): host_fiber.c goal_fiber.c process_spans.c pp_scope.c goal_fiber.h goal_thread_stack.c goal_process_heap.c goal_module.c goal_kernel_types.c goal_runtime.c
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_fiber.c goal_fiber.c process_spans.c pp_scope.c goal_thread_stack.c goal_process_heap.c goal_module.c goal_kernel_types.c goal_runtime.c -o $@

goal_fiber.o: fiber_bounds.h
runtime_fiber_cases.o: fiber_bounds.h fiber_bounds_cases.h
build/fiber-check$(HOST_EXE): fiber_bounds.h fiber_bounds_cases.h goal_register_snapshot.h

# Guarded unchanged base-process activation (AI-assisted).
runtime_activation_cases.o: runtime_activation_cases.c goal_bridge.h goal_runtime.h build/original_process_activate.h build/original_memset32.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c $< -o $@

# Explicit scalar arguments into native continuations (AI-assisted).
goal_scalar_call.o: goal_scalar_call.c goal_scalar_call.h
scalar_call_cases.o: scalar_call_cases.c scalar_call_cases.h goal_scalar_call.h
goal_fiber.o: goal_scalar_call.h
runtime_goal_cases.o: scalar_call_cases.h
host: build/scalar-call-check$(HOST_EXE)
build/scalar-call-check$(HOST_EXE): host_scalar_call.c scalar_call_cases.c scalar_call_cases.h goal_scalar_call.c goal_scalar_call.h runtime_cases.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_scalar_call.c scalar_call_cases.c goal_scalar_call.c -o $@

runtime_fiber_words_cases.o: runtime_fiber_words_cases.c fiber_test_support.h goal_fiber.h goal_bridge.h runtime_cases.h build/fiber_words_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c $< -o $@
runtime_fiber_cases.o: fiber_test_support.h
build/fiber-check$(HOST_EXE): goal_scalar_call.h

# Guard the original void main-thread backup setter (AI-assisted).
goal_thread_resize.o: goal_thread_resize.c goal_thread_resize.h goal_thread_stack.h goal_runtime.h
thread_resize_cases.o: thread_resize_cases.c thread_resize_cases.h goal_thread_resize.h goal_kernel_types.h
runtime_thread_resize_cases.o: runtime_thread_resize_cases.c goal_bridge.h goal_thread_resize.h goal_fiber.h fiber_test_support.h runtime_cases.h build/original_thread_resize.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c $< -o $@
goal_bridge.o: goal_thread_resize.h
runtime_goal_cases.o: thread_resize_cases.h
host: build/thread-resize-check$(HOST_EXE)
build/thread-resize-check$(HOST_EXE): host_thread_resize.c thread_resize_cases.c thread_resize_cases.h goal_thread_resize.c goal_thread_resize.h goal_thread_stack.c goal_thread_stack.h goal_process_heap.c goal_kernel_types.c goal_runtime.c
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_thread_resize.c thread_resize_cases.c goal_thread_resize.c goal_thread_stack.c goal_process_heap.c goal_kernel_types.c goal_runtime.c -o $@

# Original arming bytes and bounded native import (AI-assisted).
jak-dc-runtime-goal.elf: goal_main_thread.o main_thread_cases.o goal_main_start.o runtime_main_start_cases.o
jak-dc-runtime-goal.elf: goal_kos_stack.o
jak-dc-runtime-goal.elf: runtime_kos_stack_cases.o
runtime_kos_stack_cases.o: runtime_kos_stack_cases.c goal_bridge.h goal_fiber.h goal_stack_call.h fiber_test_support.h runtime_cases.h
goal_kos_stack.o: goal_kos_stack.c goal_kos_stack.h
goal_fiber.o goal_stack_call.o: goal_kos_stack.h
goal_main_thread.o: goal_main_thread.c goal_main_thread.h goal_thread_stack.h goal_runtime.h
main_thread_cases.o: main_thread_cases.c main_thread_cases.h goal_main_thread.h goal_kernel_types.h
goal_main_start.o: goal_main_start.c goal_main_start.h goal_main_thread.h goal_fiber.h goal_bridge.h goal_scalar_call.h
runtime_main_start_cases.o: runtime_main_start_cases.c goal_main_start.h goal_main_thread.h goal_bridge.h goal_fiber.h fiber_test_support.h runtime_cases.h build/original_set_to_run.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c $< -o $@
goal_bridge.o: goal_main_start.h
runtime_goal_cases.o: main_thread_cases.h
host: build/main-thread-check$(HOST_EXE)
build/main-thread-check$(HOST_EXE): host_main_thread.c main_thread_cases.c main_thread_cases.h goal_main_thread.c goal_main_thread.h goal_thread_stack.c goal_thread_stack.h goal_process_heap.c goal_kernel_types.c goal_runtime.c
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_main_thread.c main_thread_cases.c goal_main_thread.c goal_thread_stack.c goal_process_heap.c goal_kernel_types.c goal_runtime.c -o $@

# Original-main completion receipt oracle; bridge liveness modeled on host only.
host: build/main-completion-check$(HOST_EXE)
build/main-completion-check$(HOST_EXE): host_main_completion.c goal_main_start.c goal_main_thread.c goal_fiber.c process_spans.c pp_scope.c goal_scalar_call.c goal_thread_stack.c goal_process_heap.c goal_module.c goal_kernel_types.c goal_runtime.c goal_fiber.h goal_main_start.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_main_completion.c goal_main_start.c goal_main_thread.c goal_fiber.c process_spans.c pp_scope.c goal_scalar_call.c goal_thread_stack.c goal_process_heap.c goal_module.c goal_kernel_types.c goal_runtime.c -o $@

host: build/player-motion-check$(HOST_EXE) build/player-anim-check$(HOST_EXE)
build/player-motion-check$(HOST_EXE): host_player_motion.c player_motion.c player_motion_cases.c player_motion.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_player_motion.c player_motion.c player_motion_cases.c -o $@ -lm
build/player-anim-check$(HOST_EXE): host_player_anim.c player_anim.c player_anim.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_player_anim.c player_anim.c -o $@ -lm
host: build/world-collision-check$(HOST_EXE) build/world-load-check$(HOST_EXE)
build/world-collision-check$(HOST_EXE): host_world_collision.c world_collision.c world_collision_cases.c world_player_cases.c world_patch_cases.c player_motion.c player_world_motion.c world_collision.h player_world_motion.h player_motion_internal.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_world_collision.c world_collision.c world_collision_cases.c world_player_cases.c world_patch_cases.c player_motion.c player_world_motion.c -o $@ -lm
build/world-load-check$(HOST_EXE): host_world_load.c world_collision.c world_collision.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_world_load.c world_collision.c -o $@ -lm

host: build/world-manifest-check$(HOST_EXE)
build/world-manifest-check$(HOST_EXE): host_world_manifest.c world_manifest_cases.c world_manifest.c world_manifest.h world_collision.c world_collision.h textured_scene.c textured_scene.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_world_manifest.c world_manifest_cases.c world_manifest.c world_collision.c textured_scene.c -o $@ -lm
world_manifest.o: world_manifest.c world_manifest.h world_collision.h textured_scene.h
world_manifest_cases.o: world_manifest_cases.c world_manifest.h world_collision.h textured_scene.h
jak-dc-world-manifest.elf: world_manifest_main.o world_manifest_cases.o world_manifest.o world_collision.o
	kos-cc -o $@ $^ -lm

# Original training patch; all five assets are private local conversion outputs.
TRAINING_ASSET_DIR ?= build/private-training-assets
training_main.o: training_main.c training_layer.h world_manifest.h world_collision.h camera_rig.h camera_boom.h player_motion.h player_anim.h player_world_motion.h textured_scene.h scene_projection.h scene_telemetry.h
training_layer.o: training_layer.c training_layer.h textured_scene.h scene_projection.h scene_clip.h
host: build/training-layer-check$(HOST_EXE)
build/training-layer-check$(HOST_EXE): host_training_layer.c training_layer.c training_layer.h scene_clip.c scene_clip.h tests/training-stubs/kos.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Itests/training-stubs host_training_layer.c training_layer.c scene_clip.c -o $@ -lm
build/training-romdisk.o: $(TRAINING_ASSET_DIR)/player.dct $(TRAINING_ASSET_DIR)/player.dca $(TRAINING_ASSET_DIR)/world.dct $(TRAINING_ASSET_DIR)/world.dcc $(TRAINING_ASSET_DIR)/world.xfm
	mkdir -p build
	$(KOS_GENROMFS) -f build/training-romdisk.img -d $(TRAINING_ASSET_DIR) -x report.json
	$(KOS_BASE)/utils/bin2c/bin2c build/training-romdisk.img build/training-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/training-romdisk.c -o build/training-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/training-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
jak-dc-training.elf: training_main.o training_layer.o scene_clip.o world_manifest.o camera_boom.o camera_rig.o world_collision.o player_world_motion.o player_motion.o player_anim.o textured_scene.o scene_projection_sh4zam.o scene_projection_gate.o build/training-romdisk.o
	kos-cc -o $@ $^ -lsh4zam -lm
scene_clip.o scene_clip_cases.o: scene_clip.h scene_projection.h
camera_boom.o camera_boom_cases.o: camera_boom.h camera_rig.h world_collision.h
camera_rig.o: camera_rig.c camera_rig.h camera_boom.h world_collision.h
sector_air_cases.o: sector_air_cases.c world_collision.h camera_boom.h player_world_motion.h
jak-dc-sector-air.elf: sector_air_main.o sector_air_cases.o world_collision.o camera_boom.o player_motion.o player_world_motion.o
	kos-cc -o $@ $^ -lm
host: build/sector-air-check$(HOST_EXE)
build/sector-air-check$(HOST_EXE): host_sector_air.c sector_air_cases.c world_collision.c camera_boom.c player_motion.c player_world_motion.c world_collision.h camera_boom.h player_world_motion.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_sector_air.c sector_air_cases.c world_collision.c camera_boom.c player_motion.c player_world_motion.c -o $@ -lm

# Instrumented test objects stay separate from the production cache/world objects.
SECTOR_TEST_DEFS := -Dmalloc=dc_test_malloc -Dfree=dc_test_free -Dfopen=dc_test_fopen -Dfclose=dc_test_fclose -Dfread=dc_test_fread -Dfgetc=dc_test_fgetc -Dferror=dc_test_ferror
sector_cache.o: sector_cache.c sector_cache.h world_collision.h
sector_cache_test.o: sector_cache.c sector_cache.h world_collision.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) $(SECTOR_TEST_DEFS) -c $< -o $@
sector_cache_bridge.o: tests/sector_cache_bridge.c tests/sector_cache_bridge.h world_collision.c world_collision.h sector_cache.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -c $< -o $@
sector_cache_cases.o: sector_cache_cases.c sector_cache_cases.h tests/sector_cache_bridge.h sector_cache.h
sector_cache_main.o: sector_cache_main.c sector_cache_cases.h
SECTOR_ASSET_DIR ?= build/private-sector-assets
build/sector-romdisk.o: $(SECTOR_ASSET_DIR)/manifest.dcs $(SECTOR_ASSET_DIR)/oracle.bin $(wildcard $(SECTOR_ASSET_DIR)/sectors/*.dcs)
	mkdir -p build
	$(KOS_GENROMFS) -f build/sector-romdisk.img -d $(SECTOR_ASSET_DIR)
	$(KOS_BASE)/utils/bin2c/bin2c build/sector-romdisk.img build/sector-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/sector-romdisk.c -o build/sector-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/sector-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
jak-dc-sector-cache.elf: sector_cache_main.o sector_cache_cases.o sector_cache_bridge.o sector_cache_test.o build/sector-romdisk.o | sector_cache.o
	kos-cc -o $@ $^ -lm
host: build/sector-cache-check$(HOST_SHARED) build/sector-cache-check$(HOST_EXE)
build/sector-cache-host.o: sector_cache.c sector_cache.h world_collision.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic -fPIC $(SECTOR_TEST_DEFS) -c $< -o $@
build/sector-cache-check$(HOST_SHARED): tests/sector_cache_bridge.c world_collision.c world_collision.h sector_cache.h build/sector-cache-host.o
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic -fPIC -shared tests/sector_cache_bridge.c build/sector-cache-host.o -o $@ -lm
build/sector-cache-check$(HOST_EXE): host_sector_cache.c sector_cache_cases.c sector_cache_cases.h tests/sector_cache_bridge.c world_collision.c world_collision.h sector_cache.h build/sector-cache-host.o
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_sector_cache.c sector_cache_cases.c tests/sector_cache_bridge.c build/sector-cache-host.o -o $@ -lm
build/inflate_fixture.h: make_inflate_fixture.py
	$(PYTHON) make_inflate_fixture.py $@
sector_inflate.o: sector_inflate.c sector_inflate.h
sector_inflate_bridge.o: tests/sector_inflate_bridge.c sector_inflate.c sector_inflate.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -c $< -o $@
sector_inflate_cases.o: sector_inflate_cases.c sector_inflate_cases.h sector_inflate.h build/inflate_fixture.h
sector_inflate_main.o: sector_inflate_main.c sector_inflate_cases.h
jak-dc-inflate.elf: sector_inflate_main.o sector_inflate_cases.o sector_inflate_bridge.o | sector_inflate.o
	kos-cc -o $@ $^ -lz
host: build/sector-inflate-check$(HOST_EXE)
build/sector-inflate-check$(HOST_EXE): host_sector_inflate.c sector_inflate_cases.c tests/sector_inflate_bridge.c sector_inflate_cases.h sector_inflate.c sector_inflate.h build/inflate_fixture.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_sector_inflate.c sector_inflate_cases.c tests/sector_inflate_bridge.c -o $@ -lz

# DRM2 CPU reader: global texture/material catalog, one bounded sector at a time.
RENDER_ASSET_DIR ?= build/private-render-assets
RENDER_TEST_DEFS := -Dmalloc=dc_render_test_malloc -Dfree=dc_render_test_free -DDC_RENDER_TEST_IO
render_package.o: render_package.c render_package.h sector_inflate.h
render_package_test.o: render_package.c render_package.h sector_inflate.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) $(RENDER_TEST_DEFS) -c $< -o $@
render_package_bridge.o: tests/render_package_bridge.c render_package.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c $< -o $@
build/render_fixture.h:
	$(PYTHON) make_render_fixture.py build/private-render-assets $@
render_package_main.o: render_package_main.c render_package.h build/render_fixture.h
$(RENDER_ASSET_DIR)/synthetic/case000/m: build/render_fixture.h
	@test -f $@
build/render-romdisk.o: $(RENDER_ASSET_DIR)/synthetic/case000/m $(wildcard $(RENDER_ASSET_DIR)/real/sectors/*.drs)
	mkdir -p build
	$(KOS_GENROMFS) -f build/render-romdisk.img -d $(RENDER_ASSET_DIR) -x report.json
	$(KOS_BASE)/utils/bin2c/bin2c build/render-romdisk.img build/render-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/render-romdisk.c -o build/render-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/render-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
jak-dc-render-io.elf: render_package_main.o render_package_bridge.o render_package_test.o sector_inflate.o build/render-romdisk.o | render_package.o
	kos-cc -o $@ $^ -lz -lm
host: build/render-package-check$(HOST_SHARED)
build/render-package-host.o: render_package.c render_package.h sector_inflate.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic -fPIC $(RENDER_TEST_DEFS) -c $< -o $@
build/render-package-check$(HOST_SHARED): tests/render_package_bridge.c render_package.h sector_inflate.c sector_inflate.h build/render-package-host.o
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic -fPIC -shared tests/render_package_bridge.c build/render-package-host.o sector_inflate.c -o $@ -lz -lm

GPU_CATALOG_ASSET_DIR ?= build/private-gpu-catalog-assets
render_gpu.o: render_gpu.c render_gpu.h render_package.h
render_gpu_test.o: render_gpu.c render_gpu.h render_package.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_RENDER_GPU_TEST_ALLOC -c $< -o $@
render_gpu_cases.o: render_gpu_cases.c render_gpu_cases.h render_gpu.h
render_gpu_main.o: render_gpu_main.c render_gpu_cases.h
build/private-gpu-catalog-assets/manifest.drm:
	$(PYTHON) make_render_gpu_fixture.py build/private-gpu-catalog-assets
build/gpu-catalog-romdisk.o: $(GPU_CATALOG_ASSET_DIR)/manifest.drm
	mkdir -p build
	$(KOS_GENROMFS) -f build/gpu-catalog-romdisk.img -d $(GPU_CATALOG_ASSET_DIR) -x report.json
	$(KOS_BASE)/utils/bin2c/bin2c build/gpu-catalog-romdisk.img build/gpu-catalog-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/gpu-catalog-romdisk.c -o build/gpu-catalog-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/gpu-catalog-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
jak-dc-render-gpu.elf: render_gpu_main.o render_gpu_cases.o render_gpu_test.o render_package.o sector_inflate.o build/gpu-catalog-romdisk.o | render_gpu.o
	kos-cc -o $@ $^ -lz -lm
host: build/render-gpu-check$(HOST_EXE)
build/render-gpu-check$(HOST_EXE): host_render_gpu.c render_gpu.c render_gpu_cases.c render_gpu_cases.h render_gpu.h render_package.c render_package.h sector_inflate.c sector_inflate.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic -DDC_RENDER_GPU_TEST_ALLOC -Itests/render-gpu-stubs host_render_gpu.c render_gpu.c render_gpu_cases.c render_package.c sector_inflate.c -o $@ -lz -lm

RENDER_CACHE_ASSET_DIR ?= build/private-render-cache-assets
render_cache.o: render_cache.c render_cache.h render_package.h
render_block_cache.o: render_block_cache.c render_block_cache.h render_cache.h
render_cache_test.o: render_cache.c render_cache.h render_package.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_RENDER_CACHE_TEST_IO -c $< -o $@
render_cache_bridge.o: tests/render_cache_bridge.c render_cache.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c $< -o $@
build/render_cache_fixture.h:
	$(PYTHON) make_render_cache_fixture.py build/private-render-cache-assets --header $@
$(RENDER_CACHE_ASSET_DIR)/CASES.json: build/render_cache_fixture.h
	@test -f $@
render_cache_cases.o: render_cache_cases.c render_cache_cases.h build/render_cache_fixture.h
render_cache_main.o: render_cache_main.c render_cache_cases.h
build/render-cache-romdisk.o: $(RENDER_CACHE_ASSET_DIR)/CASES.json
	mkdir -p build
	$(KOS_GENROMFS) -f build/render-cache-romdisk.img -d $(RENDER_CACHE_ASSET_DIR) -x '*.json'
	$(KOS_BASE)/utils/bin2c/bin2c build/render-cache-romdisk.img build/render-cache-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/render-cache-romdisk.c -o build/render-cache-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/render-cache-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
jak-dc-render-cache.elf: render_cache_main.o render_cache_cases.o render_cache_bridge.o render_cache_test.o render_package_test.o sector_inflate.o build/render-cache-romdisk.o | render_cache.o
	kos-cc -o $@ $^ -lz -lm
host: build/render-cache-check$(HOST_SHARED)
build/render-cache-host.o: render_cache.c render_cache.h render_package.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic -fPIC -DDC_RENDER_CACHE_TEST_IO -c $< -o $@
build/render-cache-check$(HOST_SHARED): tests/render_cache_bridge.c render_cache.h build/render-cache-host.o build/render-package-host.o sector_inflate.c
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic -fPIC -shared tests/render_cache_bridge.c build/render-cache-host.o build/render-package-host.o sector_inflate.c -o $@ -lz -lm
host: build/render-block-cache-check$(HOST_SHARED)
build/render-block-cache-check$(HOST_SHARED): tests/render_block_cache_bridge.c render_block_cache.c render_block_cache.h render_cache.c render_cache.h render_package.c sector_inflate.c
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic -fPIC -shared -DDC_RENDER_BLOCK_CACHE_TEST tests/render_block_cache_bridge.c render_block_cache.c render_cache.c render_package.c sector_inflate.c -o $@ -lz -lm
jak-dc-camera.elf: camera_boom_main.o camera_boom.o camera_rig.o camera_boom_cases.o world_collision.o
	kos-cc -o $@ $^ -lm
camera_real_main.o: camera_boom_main.c
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_CAMERA_REAL_PATCH -c $< -o $@
CAMERA_ASSET_DIR ?= build/private-camera-assets
build/camera-world-romdisk.o: $(CAMERA_ASSET_DIR)/world.dcc
	mkdir -p build
	$(KOS_GENROMFS) -f build/camera-world-romdisk.img -d $(CAMERA_ASSET_DIR) -x report.json
	$(KOS_BASE)/utils/bin2c/bin2c build/camera-world-romdisk.img build/camera-world-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/camera-world-romdisk.c -o build/camera-world-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/camera-world-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
jak-dc-camera-real.elf: camera_real_main.o camera_boom.o camera_rig.o camera_boom_cases.o world_collision.o build/camera-world-romdisk.o
	kos-cc -o $@ $^ -lm
host: build/camera-boom-check$(HOST_EXE)
build/camera-boom-check$(HOST_EXE): host_camera_boom.c camera_boom.c camera_rig.c camera_boom_cases.c world_collision.c camera_boom.h camera_rig.h world_collision.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_camera_boom.c camera_boom.c camera_rig.c camera_boom_cases.c world_collision.c -o $@ -lm
view_projection_main.o: view_projection_main.c scene_projection.h
	$(KOS_CC) $(KOS_CFLAGS) -std=gnu17 -Wall -Wextra -Werror -c $< -o $@
clip_scene_main.o: clip_scene_main.c training_layer.h scene_projection.h scene_telemetry.h
jak-dc-view-clip.elf: view_projection_main.o view_projection_mode_probe.o scene_projection_sh4zam.o scene_projection_gate.o scene_clip.o scene_clip_cases.o
	kos-cc -o $@ $^ -lsh4zam -lm
render_world_view.o: render_world_view.c render_world_view.h render_cache.h scene_projection.h
	$(KOS_CC) $(KOS_CFLAGS) -std=gnu17 -Wall -Wextra -Werror -c $< -o $@
render_world_view_main.o: render_world_view_main.c render_world_view.h
	$(KOS_CC) $(KOS_CFLAGS) -std=gnu17 -Wall -Wextra -Werror -c $< -o $@
jak-dc-world-view.elf: render_world_view.o render_world_view_gate.o render_world_view_main.o render_world_view_mode_probe.o
	kos-cc -o $@ $^ -lsh4zam -lm
RENDER_BASE_ASSET_DIR ?= build/private-render-base-assets
build/render_base_fixture.h:
	$(PYTHON) make_render_base_fixture.py build/private-render-base-assets --header $@
$(RENDER_BASE_ASSET_DIR)/BASE_CASE.json: build/render_base_fixture.h
	@test -f $@
render_base.o: render_base.c render_base.h render_frustum.h render_world_view.h
render_base_test.o: render_base.c render_base.h render_frustum.h render_world_view.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_RENDER_BASE_TEST_ALLOC -c $< -o $@
# A restored fixture header can also have an OLDER timestamp than the previous
# asset choice's object. Recompile its embedded binding with the fresh ROM disk.
render_base_main.o: render_base_main.c render_base.h build/render_base_fixture.h private-romdisk-refresh
build/render-base-romdisk.o: $(RENDER_BASE_ASSET_DIR)/BASE_CASE.json
	mkdir -p build
	$(KOS_GENROMFS) -f build/render-base-romdisk.img -d $(RENDER_BASE_ASSET_DIR) -x '*.json'
	$(KOS_BASE)/utils/bin2c/bin2c build/render-base-romdisk.img build/render-base-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/render-base-romdisk.c -o build/render-base-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/render-base-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
jak-dc-render-base.elf: render_base_main.o render_base_test.o render_gpu.o render_cache.o render_package.o sector_inflate.o render_world_view.o render_world_view_gate.o render_frustum.o build/render-base-romdisk.o | render_base.o
	kos-cc -o $@ $^ -lsh4zam -lz -lm
TRAINING_STREAM_ASSET_DIR ?= build/private-training-stream-assets
training_render.o: training_render.c training_render.h render_base.h render_block_cache.h
training_stream_main.o: training_main.c training_render.h build/training_render_fixture.h private-romdisk-refresh
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_TRAINING_STREAM -c $< -o $@
build/training-stream-romdisk.o: $(TRAINING_STREAM_ASSET_DIR)/STREAM_BUNDLE.json
	mkdir -p build
	$(KOS_GENROMFS) -f build/training-stream-romdisk.img -d $(TRAINING_STREAM_ASSET_DIR) -x '*.json'
	$(KOS_BASE)/utils/bin2c/bin2c build/training-stream-romdisk.img build/training-stream-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/training-stream-romdisk.c -o build/training-stream-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/training-stream-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
jak-dc-training-stream.elf: training_stream_main.o training_render.o render_block_cache.o render_base.o render_gpu.o render_cache.o render_package.o sector_inflate.o render_world_view.o render_world_view_gate.o render_frustum.o training_layer.o scene_clip.o player_motion.o player_world_motion.o player_anim.o world_collision.o world_manifest.o camera_boom.o camera_rig.o textured_scene.o scene_projection_sh4zam.o scene_projection_gate.o build/training-stream-romdisk.o
	kos-cc -o $@ $^ -lsh4zam -lz -lm
TRAINING_OWNER_ASSET_DIR ?= build/private-training-owner-assets
build/training_owner_fixture.h: make_training_owner_fixture.py
	$(PYTHON) make_training_owner_fixture.py build/private-training-owner-assets --header $@
training_render_cases_main.o: training_render_cases_main.c training_render.h build/training_owner_fixture.h private-romdisk-refresh
build/training-owner-romdisk.o: $(TRAINING_OWNER_ASSET_DIR)/OWNER_CASES.json
	mkdir -p build
	$(KOS_GENROMFS) -f build/training-owner-romdisk.img -d $(TRAINING_OWNER_ASSET_DIR) -x '*.json'
	$(KOS_BASE)/utils/bin2c/bin2c build/training-owner-romdisk.img build/training-owner-romdisk.c romdisk
	$(KOS_CC) $(KOS_CFLAGS) -c build/training-owner-romdisk.c -o build/training-owner-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/training-owner-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
jak-dc-training-render.elf: training_render_cases_main.o training_render.o render_block_cache.o render_base_test.o render_gpu.o render_cache.o render_package.o sector_inflate.o render_world_view.o render_world_view_gate.o render_frustum.o build/training-owner-romdisk.o
	kos-cc -o $@ $^ -lsh4zam -lz -lm
build/frustum_fixture.h: make_frustum_fixture.py frustum_oracle.py
	$(PYTHON) make_frustum_fixture.py $@
render_frustum.o: render_frustum.c render_frustum.h scene_clip.h
render_frustum_cases.o: render_frustum_cases.c render_frustum_cases.h build/frustum_fixture.h
render_frustum_main.o: render_frustum_main.c render_frustum_cases.h
jak-dc-frustum.elf: render_frustum.o render_frustum_cases.o render_frustum_main.o
	kos-cc -o $@ $^ -lm
host: build/render-frustum-check$(HOST_SHARED) build/render-frustum-cases$(HOST_EXE)
build/render-frustum-check$(HOST_SHARED): render_frustum.c render_frustum.h scene_clip.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic -fPIC -shared render_frustum.c -o $@ -lm
build/render-frustum-cases$(HOST_EXE): host_render_frustum.c render_frustum.c render_frustum_cases.c build/frustum_fixture.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_render_frustum.c render_frustum.c render_frustum_cases.c -o $@ -lm
jak-dc-clip-scene.elf: clip_scene_main.o training_layer.o scene_clip.o scene_projection_sh4zam.o scene_projection_gate.o
	kos-cc -o $@ $^ -lsh4zam -lm
host: build/scene-clip-check$(HOST_EXE)
build/scene-clip-check$(HOST_EXE): host_scene_clip.c scene_clip.c scene_clip_cases.c scene_clip.h scene_projection.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_scene_clip.c scene_clip.c scene_clip_cases.c -o $@ -lm

runtime_dead_pool_cases.o: runtime_dead_pool_cases.c goal_bridge.h build/original_dead_pool.h build/dead_pool_fixture.h build/original_dead_pool_schema.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c $< -o $@

pp_scope.o: pp_scope.c pp_scope.h goal_process_heap.h goal_fiber.h
goal_fiber.o goal_bridge.o: pp_scope.h
host: build/pp-scope-check$(HOST_EXE)
build/pp-scope-check$(HOST_EXE): host_pp_scope.c pp_scope.c pp_scope.h goal_runtime.c goal_kernel_types.c goal_process_heap.c
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_pp_scope.c pp_scope.c goal_runtime.c goal_kernel_types.c goal_process_heap.c -o $@

runtime_pp_scope_cases.o: runtime_pp_scope_cases.c goal_bridge.h pp_scope.h fiber_test_support.h runtime_cases.h

process_spans.o goal_bridge.o: process_spans.h
host: build/process-spans-check$(HOST_EXE)
build/process-spans-check$(HOST_EXE): host_process_spans.c process_spans.c process_spans.h goal_fiber.h goal_runtime.c
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_process_spans.c process_spans.c goal_runtime.c -o $@
jak-dc-runtime-goal.elf: process_spans.o runtime_process_spans_cases.o
runtime_process_spans_cases.o: runtime_process_spans_cases.c process_spans.h goal_bridge.h pp_scope.h fiber_test_support.h goal_fiber.h runtime_cases.h
jak-dc-runtime-goal.elf: runtime_lexical_pp_cases.o
runtime_lexical_pp_cases.o: runtime_lexical_pp_cases.c process_spans.h goal_bridge.h pp_scope.h runtime_cases.h build/original_send_event.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_lexical_pp_cases.c -o $@
jak-dc-runtime-goal.elf: runtime_deactivate_cases.o
runtime_deactivate_cases.o: runtime_deactivate_cases.c process_spans.h goal_bridge.h pp_scope.h runtime_cases.h fiber_test_support.h goal_fiber.h build/original_deactivate.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_deactivate_cases.c -o $@
jak-dc-runtime-goal.elf: runtime_suspend_cases.o
runtime_suspend_cases.o: runtime_suspend_cases.c process_spans.h goal_bridge.h pp_scope.h runtime_cases.h fiber_test_support.h goal_fiber.h build/original_looping_code.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_suspend_cases.c -o $@
jak-dc-runtime-goal.elf: runtime_enter_state_cases.o
runtime_enter_state_cases.o: runtime_enter_state_cases.c process_spans.h goal_bridge.h pp_scope.h runtime_cases.h fiber_test_support.h goal_fiber.h build/original_enter_state.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_enter_state_cases.c -o $@
jak-dc-runtime-goal.elf: runtime_dispatcher_cases.o
runtime_dispatcher_cases.o: runtime_dispatcher_cases.c process_spans.h goal_bridge.h pp_scope.h runtime_cases.h goal_fiber.h build/original_dispatcher.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c runtime_dispatcher_cases.c -o $@
goal_fiber.o: process_spans.h

jak-dc-runtime-goal.elf: runtime_stack_objects_cases.o runtime_stack_objects_baseline.o
jak-dc-runtime-goal.elf: native_catch.o catch_escape_probe.o runtime_catch_raw_cases.o
jak-dc-runtime-goal.elf: runtime_initialization_cases.o
runtime_initialization_cases.o: runtime_initialization_cases.c goal_bridge.h pp_scope.h goal_main_start.h fiber_test_support.h build/original_initialization.h build/initialization_fixture.h build/original_set_to_run.h
	kos-cc $(CFLAGS) -I. -c $< -o $@
native_catch.o: native_catch.S
catch_escape_probe.o: catch_escape_probe.S
runtime_catch_raw_cases.o: runtime_catch_raw_cases.c goal_catch_raw.h goal_register_snapshot.h runtime_cases.h
runtime_stack_objects_cases.o: runtime_stack_objects_cases.c build/stack_objects_fixture.h goal_bridge.h goal_fiber.h fiber_test_support.h pp_scope.h
	kos-cc $(CFLAGS) -I. -c $< -o $@
runtime_stack_objects_baseline.o: runtime_stack_objects_baseline.c build/stack_objects_baseline.h goal_module.h
	kos-cc $(CFLAGS) -I. -c $< -o $@

# Native boot of the unchanged original jak1 kernel startup (AI-assisted).
KERNEL_BOOT_LIBS=goal_runtime.o goal_runtime_dc.o goal_module.o goal_bridge.o goal_mmi.o goal_pad.o goal_process_heap.o goal_kernel_types.o \
 goal_fiber.o native_context_swap.o pp_scope.o process_spans.o goal_kos_stack.o goal_stack_call.o native_stack_call.o \
 goal_scalar_call.o goal_main_start.o goal_main_thread.o goal_thread_stack.o goal_thread_resize.o goal_register_snapshot.o \
 native_catch.o goal_native_file.o
kernel_boot_main.o: runtime_main.c runtime_cases.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_KERNEL_BOOT -c runtime_main.c -o $@
kernel_boot_cases.o: kernel_boot_cases.c goal_bridge.h goal_fiber.h goal_kernel_types.h goal_main_start.h runtime_cases.h \
 build/boot_gcommon.h build/boot_gstring_h.h build/boot_gkernel_h.h build/boot_gkernel.h build/boot_pskernel.h \
 build/boot_gstring.h build/boot_dgo_h.h build/boot_gstate.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c kernel_boot_cases.c -o $@
kernel_boot_valid.o: kernel_boot_valid.c goal_module.h build/boot_valid.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c kernel_boot_valid.c -o $@
jak-dc-kernel-boot.elf: kernel_boot_main.o kernel_boot_cases.o kernel_boot_valid.o $(KERNEL_BOOT_LIBS)
	kos-cc -o $@ $^
# The same boot, loading the kernel from a KERNEL.CGO DGO of DCSH objects (AI-assisted).
goal_dgo.o: goal_dgo.c goal_dgo.h goal_native_file.h goal_module.h
# The vendored zstd decompressor (BSD), built for SH-4 with its own flags.
ZSTD_LIB=../../third-party/zstd/lib
# xxhash mixes unsigned int and U32 (unsigned long on SH-4, both 32-bit) in XXH32,
# which decompression never calls (frames check XXH64).
ZSTD_FLAGS=-Wno-incompatible-pointer-types -DZSTD_LEGACY_SUPPORT=0 -DZSTD_DISABLE_ASM -DZSTD_NO_TRACE -DDEBUGLEVEL=0 -I$(ZSTD_LIB) -I$(ZSTD_LIB)/common
ZSTD_OBJS=$(addprefix build/zstd-,entropy_common.o error_private.o fse_decompress.o xxhash.o zstd_common.o  huf_decompress.o zstd_ddict.o zstd_decompress.o zstd_decompress_block.o)
build/zstd-%.o: $(ZSTD_LIB)/common/%.c
	$(KOS_CC) $(KOS_CFLAGS) $(ZSTD_FLAGS) -c $< -o $@
build/zstd-%.o: $(ZSTD_LIB)/decompress/%.c
	$(KOS_CC) $(KOS_CFLAGS) $(ZSTD_FLAGS) -c $< -o $@
goal_dgo_zst.o: goal_dgo_zst.c goal_dgo_zst.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I$(ZSTD_LIB) -c $< -o $@
kernel_dgo_cases.o: kernel_boot_cases.c goal_dgo.h goal_dgo_zst.h goal_bridge.h goal_fiber.h goal_main_start.h runtime_cases.h  build/game_cgo_order.h build/vf_cases.h mips2c_dc.h mips2c_cases.h build/mips2c/mips2c_cases_expect.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -DDC_KERNEL_DGO -I. -c kernel_boot_cases.c -o $@
# The romdisk holds KERNEL.CGO and a zstd GAME.CGO (build_kernel_boot.py).
build/kernel-dgo-romdisk.o: build/kernel-dgo-rd/KERNEL.CGO build/kernel-dgo-rd/GAME.CGO.zst
	$(KOS_GENROMFS) -f build/kernel-dgo-romdisk.img -d build/kernel-dgo-rd
	$(KOS_BASE)/utils/bin2c/bin2c build/kernel-dgo-romdisk.img build/kernel-dgo-romdisk.c romdisk
	# Linked high in main RAM, then staged into VRAM by romdisk_stage.c.
	sed -i 's/^const unsigned char romdisk_data\[/const unsigned char __attribute__((section(".dc_stage"),aligned(32))) romdisk_data[/' build/kernel-dgo-romdisk.c
	$(KOS_CC) $(KOS_CFLAGS) -c build/kernel-dgo-romdisk.c -o build/kernel-dgo-romdisk-tmp.o
	$(KOS_CC) -o $@ -r build/kernel-dgo-romdisk-tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
kernel_vf_fixture.o: kernel_vf_fixture.c goal_module.h build/vf_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c kernel_vf_fixture.c -o $@
romdisk_stage.o: romdisk_stage.c
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -c romdisk_stage.c -o $@
# build/mips2c/libdcmips2c.a comes from build_mips2c.py (sh-elf-g++), run by build_kernel_boot.py.
jak-dc-kernel-dgo.elf: kernel_boot_main.o kernel_dgo_cases.o kernel_vf_fixture.o goal_dgo.o goal_dgo_zst.o romdisk_stage.o $(ZSTD_OBJS) build/kernel-dgo-romdisk.o $(KERNEL_BOOT_LIBS) build/mips2c/libdcmips2c.a
	kos-c++ -o $@ $^ -Wl,--section-start=.dc_stage=0x8cd80000
