"""Compile a prefix of the original GAME.CGO to DCSH and pack it (AI-assisted).

Compile order follows goal_src/jak1/game.gp, as on desktop: each file is
compiled after its game.gp dependency closure, which can include files the DGO
loads later (compile order is not load order). One frontend run (exporter
--batch) compiles the Dreamcast kernel contexts and then every needed file once,
exporting typed IR for the GAME.CGO files. Each of those is then lowered to SH-4
as a whole module (--all-functions, its top-level startup as an initializer) and
packed, in DGO order, into build/kernel-dgo/GAME.CGO next to KERNEL.CGO. Stops at
the first file that does not compile, frontend or lowering, and reports it.
"""
import argparse
import json
import re
from pathlib import Path
import subprocess
import sys
import gp_deps
from pack_native_dgo import write_dgo

KERNEL_STEMS = ('gcommon', 'gstring-h', 'gkernel-h', 'gkernel', 'pskernel', 'gstring', 'dgo-h', 'gstate')


def game_order(root):
    """Object names of GAME.CGO in original order, from dgos/game.gd."""
    text = re.sub(r';[^\n]*', '', (root / 'goal_src/jak1/dgos/game.gd').read_text())  # drop commented-out objects
    return [m.group(1) for m in re.finditer(r'"([^"]+)\.o"', text)]


def kernel_contexts(root, port):
    """The kernel, in the Dreamcast target configuration used by the boot."""
    kernel = root / 'goal_src/jak1/kernel'
    return [kernel / 'gcommon.gc', port / 'scalar_validation_context.gc', port / 'dreamcast_profiler_context.gc',
            port / 'dreamcast_memory_context.gc', kernel / 'gstring-h.gc', kernel / 'gkernel-h.gc',
            port / 'dreamcast_thread_context.gc', kernel / 'gkernel.gc', kernel / 'pskernel.gc',
            kernel / 'gstring.gc', kernel / 'dgo-h.gc', kernel / 'gstate.gc']


def compile_order(graph, targets):
    """game.gp-closure compile order for targets (kernel excluded), and the targets
    that are GOAL sources. A target without a game.gp source ends the list."""
    order, seen, sources = [], set(KERNEL_STEMS), []

    def visit(stem, stack):
        if stem in seen:
            return
        if stem in stack:
            raise ValueError(f'game.gp dependency cycle through {stem}')
        if stem not in graph:
            raise ValueError(f'game.gp has no source for dependency {stem}')
        stack.add(stem)
        for dep in graph[stem][1]:
            visit(dep, stack)
        stack.discard(stem)
        seen.add(stem)
        order.append(stem)
    for name in targets:
        if name not in graph:
            break
        visit(name, set())
        sources.append(name)
    return order, sources


def main():
    port = Path(__file__).resolve().parent
    root = port.parents[1]
    work = port.parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', type=Path, default=work / 'og-host/bin/goalc-dreamcast-ir.exe')
    parser.add_argument('--msys', type=Path, default=Path('C:/msys64'))
    parser.add_argument('--count', type=int, required=True, help='compile the first COUNT GAME.CGO files')
    args = parser.parse_args()
    graph = gp_deps.load(root)
    targets = game_order(root)[:args.count]
    order, sources = compile_order(graph, targets)
    report = {'compiled': [], 'failed': None, 'frontend_files': len(order)}
    if len(sources) < len(targets):
        report['stop'] = {'object': targets[len(sources)], 'reason': 'not a game.gp GOAL source (data object)'}
    build = port / 'build'
    dcsh = build / 'game-dcsh'
    ir = {name: build / f'game_{name.replace("-", "_")}.ir.json' for name in sources}
    batch = [{'source': str(p)} for p in kernel_contexts(root, port)]
    for stem in order:
        entry = {'source': str(root / 'goal_src/jak1' / graph[stem][0])}
        if stem in ir: entry['output'] = str(ir[stem])
        batch.append(entry)
    (build / 'game-batch.json').write_text(json.dumps(batch, indent=1) + '\n')
    status_path = build / 'game-batch-status.json'
    subprocess.run([str(args.frontend), str(root), '--batch', str(build / 'game-batch.json'), str(status_path)],
                   capture_output=True, text=True)
    status = json.loads(status_path.read_text())
    exported = {Path(s).resolve() for s in status['exported']}
    export_errors = {Path(e['source']).resolve(): e['error'] for e in status.get('export_errors', [])}
    for name in sources:
        source = (root / 'goal_src/jak1' / graph[name][0]).resolve()
        if source not in exported:
            failed = status['failed'] or {}
            if source in export_errors:
                report['failed'] = {'object': name, 'stage': 'export', 'source': graph[name][0],
                                    'reason': [export_errors[source]]}
            else:
                report['failed'] = {'object': name, 'stage': 'frontend',
                                    'source': failed.get('source'), 'reason': [failed.get('error', 'not exported')]}
            break
        stem = name.replace('-', '_')
        result = subprocess.run([sys.executable, str(port / 'lower_sh4_ir.py'), str(ir[name]),
                                 '--header', str(build / f'game_{stem}.h'),
                                 '--manifest', str(build / f'game_{stem}.manifest.json'),
                                 '--runtime', '--all-functions', '--initializer', f'dc-game-{name}',
                                 '--native-module', str(dcsh / f'{name}.dcsh')], capture_output=True, text=True)
        if result.returncode:
            lines = (result.stdout + result.stderr).splitlines()
            reason = [l for l in lines if 'error' in l.lower()]
            report['failed'] = {'object': name, 'stage': 'lowering', 'source': graph[name][0],
                                'reason': (reason or lines)[-6:]}
            break
        report['compiled'].append(name)
        print(f'compiled {len(report["compiled"])}: {name}', flush=True)
    if report['compiled']:
        write_dgo(build / 'kernel-dgo/GAME.CGO', 'GAME.CGO', [(n, dcsh / f'{n}.dcsh') for n in report['compiled']])
        names = ','.join(f'"{n}"' for n in report['compiled'])
        (build / 'game_cgo_order.h').write_text(
            '/* Generated by build_game_cgo.py (AI-assisted): compiled GAME.CGO prefix. */\n'
            f'#define DC_GAME_PREFIX {len(report["compiled"])}u\n'
            f'static const char *const dc_game_order[DC_GAME_PREFIX]={{{names}}};\n')
    (build / 'game-cgo-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
