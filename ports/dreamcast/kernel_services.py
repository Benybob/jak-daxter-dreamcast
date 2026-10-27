"""Source inventory of desktop kernel registrations and native bridges (AI-assisted).

Lexical C/C++ analysis, not preprocessing or execution. Includes conditional
branches, ignores comments/definitions, preserves repeated calls. Anonymous C
trampolines are method entries, not invented global GOAL symbol exports.
"""
import argparse
import json
from pathlib import Path
import re

FACTORIES = {'make_function_symbol_from_c', 'make_function_from_c',
             'make_stack_arg_function_symbol_from_c', 'make_func_symbol_func'}
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[A-Za-z_]\w*|::|[^\s]', re.S)

# Summaries reviewed against the desktop bodies, not inferred native behavior.
DESCRIPTIONS = {
    'DecodeAspect': 'Return configured aspect ratio.',
    'DecodeInactiveTimeout': 'Return configured inactive timeout.',
    'DecodeLanguage': 'Return configured language.',
    'DecodeTerritory': 'Return the fixed SCEA territory value.',
    'DecodeTimeout': 'Return configured timeout.',
    'DecodeVolume': 'Return configured volume.',
    'FlushCache': 'Desktop no-op stub.',
    'InstallDebugHandler': 'Assert: debug handler installation is unsupported on desktop.',
    'InstallHandler': 'Install the VBlank or VIF1 GOAL handler; reject other indices.',
    'LoadDGOTest': 'Assert unreachable; the old DGO test implementation is commented out.',
    'MC_check_result': 'Return the current memory-card operation result.',
    'MC_createfile': 'Return OK immediately; the old asynchronous create operation is commented out.',
    'MC_format': 'Return OK immediately; the old format operation is commented out.',
    'MC_get_status': 'Populate slot/file status from emulated memory-card state.',
    'MC_load': 'Queue a load operation if the memory-card state machine is idle.',
    'MC_makefile': 'Create the save directory/file via memory-card calls and wait for results.',
    'MC_run': 'Poll/advance the memory-card operation and callback state machine.',
    'MC_save': 'Queue save-data and summary buffers when the memory-card state machine is idle.',
    'MC_set_language': 'Record and print the memory-card language setting.',
    'MC_unformat': 'Return OK immediately; the old unformat operation is commented out.',
    'PutDisplayEnv': 'Extract display alpha and update renderer blackout intensity.',
    'RpcBusy': 'Query SIF RPC channel busy status.',
    'RpcCall_wrapper': 'Unpack seven GOAL stack arguments and invoke SIF RPC.',
    'dma_to_iop': 'Assert: this desktop DMA entry is unsupported.',
    'inspect_object': 'Validate/dispatch object inspection by tag and runtime type.',
    'intern': 'Intern the contents of a GOAL string and return its symbol.',
    'jak1::KernelShutdown': 'Set MasterExit so the GOAL kernel dispatch loop exits.',
    'kclose': 'Close a valid file-stream descriptor and clear its flags.',
    'klength': 'Seek to end to obtain length, then reset position to the beginning.',
    'kopen': 'Open a GOAL file stream for read or truncate/create/write mode.',
    'kread': 'Read into a GOAL buffer and flag stream errors.',
    'kseek': 'Seek a valid file stream and flag errors.',
    'kwrite': 'Write a GOAL buffer and flag stream errors.',
    'link_and_exec_wrapper': 'Unpack stack arguments and link/execute an object.',
    'link_begin': 'Begin incremental linking and finish immediately if work completes.',
    'link_resume': 'Resume saved linking work and finish when complete.',
    'load': 'Decode a file name, set the loading heap, load/link/execute, then restore the heap.',
    'load_and_link_dgo': 'Load and link a named DGO using the supplied heap, flags and buffer size.',
    'loado': 'Load/link a data object without the ordinary load notification/execution flags.',
    'new_dynamic_structure': 'Allocate the requested structure byte size from a selected heap.',
    'pc_set_levels': 'Tell the renderer which non-false level names should be loaded.',
    'sceCdMmode': 'Ignore the media argument and return success (1).',
    'sceDmaSync': 'Desktop stub returning completed transfer status (0).',
    'sceGsExecStoreImage': 'Assert: desktop image-store entry is unsupported.',
    'sceGsGetIMR': 'Assert: desktop GS interrupt-mask read is unsupported.',
    'sceGsPutIMR': 'Assert: desktop GS interrupt-mask write is unsupported.',
    'sceGsResetGraph': 'Log the supplied graphics reset parameters.',
    'sceGsResetPath': 'Desktop no-op graphics-path reset.',
    'sceGsSyncPath': 'Validate mode/timeout and synchronize the desktop graphics path.',
    'sceGsSyncV': 'Validate mode and wait for desktop graphics vsync.',
    'set_fade_hack': 'Set the music fade compatibility value.',
    'set_flava_hack': 'Set the sound flavor compatibility value.',
    'sprint': 'Print an object followed by a newline.',
    'test_function': 'Return a weighted sum of four integer arguments.',
    'unload': 'Emit an unload notification for the GOAL string name.',
    'update_discord_rpc': 'Build/update rich presence when enabled, otherwise clear it.',
    'set_discord_rpc': 'Set the desktop Discord RPC enabled flag.',
    'read_ee_timer': 'Convert elapsed nanoseconds to a synthetic 300 MHz EE counter.',
    'send_gfx_dma_chain': 'Submit a GOAL-memory DMA chain to the desktop renderer.',
    'pc_memmove': 'Move bytes between GOAL memory addresses using memmove.',
    'pc_texture_upload_now': 'Ask the desktop renderer to upload a texture page immediately.',
    'pc_texture_relocate': 'Ask the desktop renderer to relocate texture data.',
    'pc_encode_utf8_string': 'Convert UTF-8 text to the selected game font encoding.',
    'pc_filepath_exists': 'Return whether the host filesystem path exists as a GOAL boolean.',
    'pc_mkdir_filepath': 'Create parent directories needed for a host file path.',
    'pc_filter_debug_string': 'Filter debug text by distance and configured substring/regex rules.',
    'pc_is_imgui_visible': 'Return the desktop ImGui visibility setting as a GOAL boolean.',
    'pc_is_supported_resolution': 'Query whether the desktop display manager supports a width/height.',
    'pc_renderer_tree_set_lod': 'Set the desktop TFRAG or TIE renderer tree LOD setting.',
    'pc_prof': 'Send a named event/kind to the desktop profiler.',
    'pc_rand': 'Return a value from the extra random-number generator.',
    'pc_screen_shot': 'Set the request flag for a desktop screenshot.',
    'pc_register_screen_shot_settings': 'Register the supplied screenshot settings object.',
    'pc_treat_pad0_as_pad1': 'Set the desktop controller-port compatibility option.',
    'pc_get_os': 'Return the compile-time host OS symbol.',
    'pc_get_unix_timestamp': 'Return std::time wall-clock seconds.',
    'pc_get_axis_scale': 'Return input axis scale as float bits, defaulting to 1.33.',
    'pc_get_collision_mask': 'Query the desktop collision-renderer mask as a GOAL boolean.',
    'pc_get_current_bind': 'Write the current input binding name in game font encoding.',
    'pc_get_controller_name': 'Write the desktop controller name in game font encoding.',
    'pc_get_display_name': 'Write the connected display name, or report none.',
    'pc_clear_trigger_effect': 'Enqueue clearing a controller-0 haptic trigger effect.',
    'pc_ignore_background_controller_events': 'Enqueue the background-controller event policy.',
    'pc_reset_bindings_to_defaults': 'Restore default input bindings for a port/device.',
    'pc_stop_waiting_for_bind': 'Stop desktop input binding capture.',
    'pc_waiting_for_bind': 'Query whether desktop input binding capture is active.',
    'Gfx::CollisionRendererSetMode': 'Set the desktop collision-renderer display mode.',
    'new_illegal': 'Report an illegal static-type constructor call and return false.',
    'delete_illegal': 'Report illegal deletion of a static object and return false.',
    'copy_fixed': 'Return the original object without copying it.',
    'copy_basic': 'Query the size method, allocate/copy a basic or use a raw destination.',
    'asize_of_basic': 'Return the object type allocated_size field.',
    'alloc_heap_object': 'Allocate heap bytes and initialize/tag the basic object.',
}


def description(function):
    if function in DESCRIPTIONS:
        return DESCRIPTIONS[function]
    if function.startswith(('print_', 'inspect_')):
        verb, kind = function.split('_', 1)
        return f'{verb.capitalize()} the {kind.replace("_", "-")} representation/fields using desktop print facilities.'
    if function.startswith('delete_'):
        return 'Pass the adjusted object address to kfree (the desktop freeing path is nonfunctional).'
    if function.startswith('new_'):
        return 'Allocate/initialize the corresponding kernel object/type using the desktop heap/type helpers.'
    if function.startswith('pc_get_'):
        return 'Query desktop ' + function[7:].replace('_', ' ') + ' through its display/input manager.'
    if function.startswith('pc_set_'):
        return 'Update/request desktop ' + function[7:].replace('_', ' ') + ' through its settings/manager bridge.'
    if function.startswith('pc_current_controller_has_'):
        return 'Query controller-0 ' + function[26:].replace('_', ' ') + ' support as a GOAL boolean.'
    if function.startswith('pc_send_trigger_'):
        return 'Enqueue controller-0 ' + function[3:].replace('_', ' ') + ' parameters.'
    if function.startswith('pc_force_reload_'):
        return 'Request renderer reload of ' + function[16:].replace('_', ' ') + ' level resources.'
    raise ValueError('unreviewed desktop service: ' + function)


def tokens(text):
    return [(m.group(), m.start(), m.end()) for m in TOKEN.finditer(text)
            if not m.group().startswith(('//', '/*'))]


def call_sites(text, names):
    ts = tokens(text)
    for i, (name, start, _) in enumerate(ts[:-1]):
        if name not in names or ts[i + 1][0] != '(':
            continue
        depth, begin, args = 1, i + 2, []
        for j in range(i + 2, len(ts)):
            value = ts[j][0]
            if value in ('(', '[', '{'):
                depth += 1
            elif value in (')', ']', '}'):
                depth -= 1
            if depth == 0 or (value == ',' and depth == 1):
                args.append(''.join(t[0] for t in ts[begin:j]))
                begin = j + 1
            if depth == 0:
                # Definitions are not registrations. Prototypes don't have literals/casts.
                if j + 1 < len(ts) and ts[j + 1][0] == '{':
                    break
                yield dict(factory=name, line=text.count('\n', 0, start) + 1,
                           offset=start, end=ts[j][2], args=args)
                break


def desktop_services(text):
    result = []
    for call in call_sites(text, FACTORIES):
        args = call['args']
        named = call['factory'] != 'make_function_from_c'
        if named and (not args or not args[0].startswith('"')):
            continue  # declaration or nonliteral wrapper
        function = args[1] if named else args[0]
        function = re.sub(r'^\(void\*\)', '', function).lstrip('&')
        if not re.fullmatch(r'[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*', function):
            raise ValueError(f"unrecognized function expression at line {call['line']}: {function}")
        result.append(dict(goal=json.loads(args[0]) if named else None, function=function,
                           factory=call['factory'], line=call['line'],
                           forwarding=not named and function == 'f'))
    return result


def native_services(text):
    ts, exports = tokens(text), []
    for i, (value, start, _) in enumerate(ts):
        if value != 'dc_goal_code_export':
            continue
        j = i + 1
        while j < len(ts) and ts[j][0] not in ('=', ';'):
            j += 1
        if j == len(ts) or ts[j][0] != '=':
            continue
        j += 1
        depth = 0
        while j < len(ts):
            value, offset, _ = ts[j]
            if value.startswith('"') and ts[j - 1][0] == '{':
                exports.append(dict(goal=json.loads(value), line=text.count('\n', 0, offset) + 1))
            if value == '{':
                depth += 1
            elif value == '}':
                depth -= 1
                if depth == 0:
                    break
            j += 1
    methods = [dict(factory=c['factory'], line=c['line'], arguments=c['args'])
               for c in call_sites(text, {'dc_goal_set_method', 'dc_goal_method_set'})]
    return dict(exports=exports, methods=methods)


def references(root, name, corpus=None):
    # Literal grep-like symbol boundaries; comments count and are labelled as such.
    pattern = re.compile(r'(?<![\w!?*+<>:/.-])' + re.escape(name) + r'(?![\w!?*+<>:/.-])')
    found = []
    if corpus is None:
        corpus = [(path, path.read_text(encoding='utf-8').splitlines())
                  for path in sorted((Path(root) / 'goal_src/jak1').rglob('*.gc'))]
    for path, source_lines in corpus:
        lines = [i for i, line in enumerate(source_lines, 1)
                 if pattern.search(line)]
        if lines:
            found.append(dict(file=path.relative_to(root).as_posix(), lines=lines))
    return found


def inventory(root):
    root = Path(root)
    desktop = []
    for folder in ('jak1', 'common'):
        for path in sorted((root / f'game/kernel/{folder}').glob('*.cpp')):
            for record in desktop_services(path.read_text(encoding='utf-8')):
                record['file'] = path.relative_to(root).as_posix()
                desktop.append(record)
    native = native_services((root / 'ports/dreamcast/goal_bridge.c').read_text(encoding='utf-8'))
    exported = {r['goal'] for r in native['exports']}
    missing = sorted({r['goal'] for r in desktop if r['goal'] and r['goal'] not in exported})
    corpus = [(p, p.read_text(encoding='utf-8').splitlines())
              for p in sorted((root / 'goal_src/jak1').rglob('*.gc'))]
    # One grep pass for all absent names instead of rescanning every file/name pair.
    grep = re.compile(r'(?<![\w!?*+<>:/.-])(?:' + '|'.join(map(re.escape, sorted(missing, key=len, reverse=True)))
                      + r')(?![\w!?*+<>:/.-])') if missing else None
    refs = {name: [] for name in missing}
    for path, source_lines in corpus:
        matches = {}
        for number, line in enumerate(source_lines, 1):
            for name in {m.group() for m in grep.finditer(line)} if grep else ():
                matches.setdefault(name, []).append(number)
        for name, numbers in matches.items():
            refs[name].append(dict(file=path.relative_to(root).as_posix(), lines=numbers))
    by_name = {r['goal']: r['function'] for r in desktop if r['goal']}
    return dict(desktop=desktop, native=native,
                missing=[dict(goal=name, function=by_name[name], summary=description(by_name[name]),
                              references=refs[name]) for name in missing])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    result = inventory(args.root)
    if args.json:
        print(json.dumps(result, indent=2))
        return
    for item in result['desktop']:
        print(f"desktop {item['goal'] or '[method/forwarder]'} -> {item['function']} ({item['file']}:{item['line']})")
    for item in result['native']['exports']:
        print(f"native {item['goal']} (goal_bridge.c:{item['line']})")
    for item in result['native']['methods']:
        print(f"method {item['factory']}({', '.join(item['arguments'])}) (goal_bridge.c:{item['line']})")
    for item in result['missing']:
        print('no same-name bridge: ' + item['goal'] + ' — ' + item['summary'])
        for ref in item['references']:
            print(f"  {ref['file']}:{','.join(map(str, ref['lines']))}")
    print('Body-reviewed summaries and method/alias qualifications: KERNEL_SERVICES.md')


if __name__ == '__main__':
    main()
