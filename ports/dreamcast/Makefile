.DEFAULT_GOAL := all
HOST_CC ?= cc
PYTHON ?= python3
ifeq ($(OS),Windows_NT)
HOST_EXE := .exe
endif
TARGET := jak-dc-probe.elf
OBJS := main.o dgo_stream.o romdisk.o
SCENE_TARGET := jak-dc-scene.elf
SCENE_OBJS := scene_main.o scene_io.o romdisk.o
TEXTURED_TARGET := jak-dc-textured.elf
TEXTURED_OBJS := textured_main.o textured_scene.o romdisk.o
BACKEND_TARGET := jak-dc-backend.elf
RUNTIME_TARGET := jak-dc-runtime.elf
KOS_ROMDISK_DIR := romdisk

.PHONY: all host test fixture clean
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
	kos-cc -o $@ $(TEXTURED_OBJS) -lm

textured_main.o: textured_scene.h scene_telemetry.h
textured_scene.o: textured_scene.h

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
goal_bridge.o: goal_bridge.c goal_bridge.h goal_module.h goal_runtime.h goal_layout.h goal_process_heap.h
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
runtime_process_entry_cases.o: runtime_process_entry_cases.c runtime_cases.h goal_bridge.h goal_runtime.h build/original_process_entry.h
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

jak-dc-runtime-goal.elf: goal_thread_resize.o thread_resize_cases.o runtime_thread_resize_cases.o runtime_fiber_words_cases.o goal_scalar_call.o scalar_call_cases.o runtime_activation_cases.o native_context_swap.o goal_fiber.o runtime_fiber_cases.o native_stack_call.o goal_stack_call.o runtime_stack_call_cases.o code_entry_cases.o goal_register_snapshot.o register_snapshot_probe.o runtime_register_snapshot_cases.o runtime_goal_main.o runtime_goal_cases.o runtime_memory_cases.o runtime_static_cases.o runtime_float_cases.o runtime_bridge_cases.o runtime_quoted_cases.o runtime_void_cases.o runtime_type_cases.o runtime_shift_cases.o runtime_shift_baseline.o runtime_division_cases.o runtime_native_file_cases.o runtime_locals_cases.o runtime_early_cases.o runtime_sort_cases.o runtime_operator_cases.o runtime_fixed_cases.o runtime_far_cases.o runtime_dependency_cases.o runtime_mixed_cases.o runtime_validation_cases.o runtime_kernel_globals_cases.o runtime_enum_cases.o runtime_mixed_stack_cases.o runtime_process_tree_cases.o runtime_process_new_cases.o runtime_process_heap_cases.o runtime_structured_cases.o runtime_process_entry_cases.o runtime_process_query_cases.o runtime_call_guard_cases.o runtime_cpu_thread_cases.o goal_native_file.o type_cases.o kernel_type_cases.o goal_kernel_types.o runtime_cases.o goal_runtime.o goal_runtime_dc.o goal_module.o goal_bridge.o goal_process_heap.o process_heap_cases.o goal_thread_stack.o thread_stack_cases.o
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
	rm -f goal_thread_resize.o thread_resize_cases.o runtime_thread_resize_cases.o
	rm -f runtime_fiber_words_cases.o
	rm -f goal_scalar_call.o scalar_call_cases.o
	rm -f runtime_activation_cases.o
	rm -f native_context_swap.o goal_fiber.o runtime_fiber_cases.o
	rm -f native_stack_call.o goal_stack_call.o runtime_stack_call_cases.o
	rm -f code_entry_cases.o
	rm -f goal_register_snapshot.o register_snapshot_probe.o runtime_register_snapshot_cases.o
	rm -f goal_thread_stack.o thread_stack_cases.o
	rm -f goal_process_heap.o process_heap_cases.o runtime_process_heap_cases.o runtime_structured_cases.o runtime_process_entry_cases.o runtime_process_query_cases.o runtime_call_guard_cases.o runtime_cpu_thread_cases.o
	rm -f runtime_validation_cases.o runtime_kernel_globals_cases.o runtime_enum_cases.o runtime_mixed_stack_cases.o runtime_process_tree_cases.o runtime_process_new_cases.o
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
	rm -f runtime_goal_main.o runtime_goal_cases.o goal_module.o jak-dc-runtime-goal.elf
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
goal_fiber.o: goal_fiber.c goal_fiber.h goal_module.h goal_register_snapshot.h goal_thread_stack.h
runtime_fiber_cases.o: runtime_fiber_cases.c goal_fiber.h goal_bridge.h runtime_cases.h build/fiber_fixture.h
	$(KOS_CC) $(KOS_CFLAGS) $(CFLAGS) -I. -c $< -o $@
goal_bridge.o: goal_fiber.h

host: build/fiber-check$(HOST_EXE)
build/fiber-check$(HOST_EXE): host_fiber.c goal_fiber.c goal_fiber.h goal_thread_stack.c goal_process_heap.c goal_module.c goal_kernel_types.c goal_runtime.c
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic host_fiber.c goal_fiber.c goal_thread_stack.c goal_process_heap.c goal_module.c goal_kernel_types.c goal_runtime.c -o $@

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
