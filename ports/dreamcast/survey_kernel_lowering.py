"""Per-function SH-4 lowering coverage of the original jak1 kernel (AI-assisted).

Exports each original kernel file through the original typed frontend with the
ordered contexts the native runtime uses, lowers every non-top-level function
with the whole module's functions and statics, and reports which lower and why
the rest refuse. This measures compilation only: it does not link or execute.
--check compares against a committed report and exits 1 when coverage changes.
"""
import argparse
import collections
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

PORT = Path(__file__).resolve().parent
ROOT = PORT.parents[1]
sys.path.insert(0, str(PORT))
from lower_sh4_ir import compile_function  # noqa: E402

KERNEL = ROOT / 'goal_src/jak1/kernel'
BASE = [KERNEL / 'gcommon.gc', PORT / 'scalar_validation_context.gc', PORT / 'dreamcast_profiler_context.gc',
        KERNEL / 'gkernel-h.gc', KERNEL / 'gstring-h.gc']
TARGETS = [('gcommon.gc', []), ('gkernel.gc', BASE), ('gstate.gc', BASE + [KERNEL / 'gkernel.gc']),
           ('gstring.gc', [KERNEL / 'gcommon.gc', KERNEL / 'gstring-h.gc'])]


def reason(name, error):
    text = str(error)
    if text.startswith(name + ' '):
        text = text[len(name) + 1:]
    text = re.sub(r'^IR \d+ \(.*\): ', '', text)  # operand text may nest parentheses
    return re.sub(r'\[[\d, ]+\]|static_\d+|anon-function-\d+', '#', text)[:100]


def survey(frontend):
    env = os.environ.copy()
    if os.name == 'nt':
        env['PATH'] = 'C:/msys64/mingw64/bin' + os.pathsep + env['PATH']
    files = {}
    with tempfile.TemporaryDirectory() as directory:
        for name, deps in TARGETS:
            out = Path(directory) / (name + '.json')
            cmd = [str(frontend), str(ROOT), str(KERNEL / name), str(out)]
            for dep in deps:
                cmd += ['--preload', str(dep)]
            run = subprocess.run(cmd, env=env, capture_output=True, text=True)
            if run.returncode:
                raise SystemExit(f'{name}: frontend export failed: {run.stderr[-400:]}')
            doc = json.loads(out.read_text())
            module = {f['name']: f for f in doc['functions']}
            statics = {s['id']: s for s in doc.get('statics', [])}
            lowered, refused = [], {}
            for function in doc['functions']:
                if function.get('top_level'):
                    continue
                try:
                    compile_function(function, module, True, True, statics)
                    lowered.append(function['name'])
                except ValueError as error:
                    refused[function['name']] = reason(function['name'], error)
            files[name] = dict(lowered=len(lowered), total=len(lowered) + len(refused), refused=dict(sorted(refused.items())))
    reasons = collections.Counter(r for f in files.values() for r in f['refused'].values())
    return dict(ai_assisted=True,
                scope='Compilation only with the native runtime contexts; no linking or execution',
                contexts=[str(p.relative_to(ROOT).as_posix()) for p in BASE],
                files=files,
                summary=dict(lowered=sum(f['lowered'] for f in files.values()),
                             total=sum(f['total'] for f in files.values()),
                             reasons=dict(reasons.most_common())))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--frontend', type=Path, default=ROOT.parent / 'og-host/bin/goalc-dreamcast-ir.exe')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check', type=Path)
    args = parser.parse_args(argv)
    if not args.frontend.is_file():
        parser.error(f'original frontend missing: {args.frontend}')
    report = survey(args.frontend)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8', newline='\n')
    for name, data in report['files'].items():
        print(f"{name}: {data['lowered']}/{data['total']} lower")
    if args.check and json.loads(args.check.read_text(encoding='utf-8')) != report:
        print(f'kernel lowering coverage differs from {args.check}; regenerate with --output', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
