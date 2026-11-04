"""Compile a level DGO's code objects to SH-4 and pack them (AI-assisted), ISC.

A level DGO carries a few GOAL code objects (Geyser Rock: training-obs and
training-part) next to its data. They are compiled from goal_src after all of
GAME.CGO, exactly as build_game_cgo.py compiles GAME.CGO: one frontend batch
over the kernel contexts and the game.gp dependency closure, IR exported only
for the level's objects, each lowered as a whole module with its top level as
an initializer, then packed in DGO order into build/level-code/<LEVEL>-CODE.DGO
for the native DGO loader. Built from source; no retail bytes.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

import build_game_cgo as game
import gp_deps
from pack_native_dgo import write_dgo


def level_code(root, level):
    """Code object names of dgos/<level>.gd, in DGO order (the .o entries)."""
    text = re.sub(r';[^\n]*', '', (root / f'goal_src/jak1/dgos/{level.lower()}.gd').read_text())
    return [m.group(1) for m in re.finditer(r'"([^"]+)\.o"', text)]


def main():
    port = Path(__file__).resolve().parent
    root = port.parents[1]
    work = port.parents[2]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('level', help='level DGO name without .DGO, e.g. TRA')
    parser.add_argument('--frontend', type=Path, default=work / 'og-host/bin/goalc-dreamcast-ir.exe')
    args = parser.parse_args()
    graph = gp_deps.load(root)
    code = level_code(root, args.level)
    order, sources = game.compile_order(graph, game.game_order(root) + code)
    missing = [name for name in code if name not in sources]
    if missing:
        raise SystemExit(f'level code without a game.gp source: {missing}')
    build = port / 'build'
    out = build / 'level-code'
    dcsh = out / 'dcsh'
    dcsh.mkdir(parents=True, exist_ok=True)
    ir = {name: out / f'{name.replace("-", "_")}.ir.json' for name in code}
    batch = [{'source': str(p)} for p in game.kernel_contexts(root, port)]
    for stem in order:
        entry = {'source': str(root / 'goal_src/jak1' / graph[stem][0])}
        if stem in ir:
            entry['output'] = str(ir[stem])
        batch.append(entry)
    batch_path = out / 'batch.json'
    batch_path.write_text(json.dumps(batch, indent=1) + '\n')
    report = {'level': args.level.upper(), 'code': code, 'compiled': [], 'failed': None, 'frontend_files': len(order)}
    status = game.export_batch(args.frontend, root, batch_path, out / 'batch-status.json',
                               port / 'dreamcast_memory_budget.gc')
    exported = {Path(s).resolve() for s in status['exported']}
    for name in code:
        source = (root / 'goal_src/jak1' / graph[name][0]).resolve()
        if source not in exported:
            failed = status.get('failed') or {}
            report['failed'] = {'object': name, 'stage': 'frontend', 'reason': [failed.get('error', 'not exported')]}
            break
        stem = name.replace('-', '_')
        result = subprocess.run([sys.executable, str(port / 'lower_sh4_ir.py'), str(ir[name]),
                                 '--header', str(out / f'{stem}.h'),
                                 '--manifest', str(out / f'{stem}.manifest.json'),
                                 '--runtime', '--all-functions', '--initializer', f'dc-level-{name}',
                                 '--native-module', str(dcsh / f'{name}.dcsh'),
                                 '--unassigned-locals', str(port / 'dreamcast_unassigned_locals.json'),
                                 '--qualify-anonymous', name, '--no-debug-segment'],
                                capture_output=True, text=True)
        if result.returncode:
            lines = (result.stdout + result.stderr).splitlines()
            report['failed'] = {'object': name, 'stage': 'lowering',
                                'reason': ([l for l in lines if 'error' in l.lower()] or lines)[-6:]}
            break
        report['compiled'].append(name)
        report[f'{name}_bytes'] = (dcsh / f'{name}.dcsh').stat().st_size
    if report['compiled'] == code:
        name = f'{args.level.upper()}-CODE.DGO'
        write_dgo(out / name, name, [(n, dcsh / f'{n}.dcsh') for n in code])
        report['dgo'] = str(out / name)
    (out / f'{args.level.upper()}-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if report['failed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
