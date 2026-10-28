"""Survey desktop C services missing from Dreamcast (AI-assisted).

ISC License
Copyright (c) 2026
Permission to use, copy, modify, and/or distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright notice
and this permission notice appear in all copies.
THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
"""

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

# Keep strings intact while excluding commented registrations/literals.
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', re.S)
def uncomment(text):
    return TOKEN.sub(lambda m: m[0] if m[0].startswith('"') else ' ' * len(m[0]), text)

def survey(root):
    root = Path(root)
    desktop = defaultdict(set)
    registration = re.compile(r'\b(?:make_function_symbol_from_c|make_stack_arg_function_symbol_from_c|make_func_symbol_func)\s*\(\s*("(?:\\.|[^"\\])*")')
    for folder in ('jak1', 'common'):
        for path in sorted((root / 'game/kernel' / folder).glob('*.cpp')):
            for match in registration.finditer(uncomment(path.read_text())):
                desktop[json.loads(match[1])].add(path.relative_to(root).as_posix())
    port = root / 'ports/dreamcast'
    paths = set(port.glob('goal_runtime*.c'))
    paths.update(port / name for name in ('goal_bridge.c', 'goal_mmi.c', 'goal_pad.c'))
    provided = set()
    for path in sorted(paths):
        if path.exists():
            provided.update(json.loads(m[0]) for m in re.finditer(r'"(?:\\.|[^"\\])*"', uncomment(path.read_text())))
    references = defaultdict(Counter)
    def visit(value, file):
        if isinstance(value, dict):
            if value.get('kind') in ('symbol_value', 'function_address'):
                name = value.get('symbol', value.get('function'))
                if name in desktop:
                    references[name][file] += 1
            for child in value.values():
                visit(child, file)
        elif isinstance(value, list):
            for child in value:
                visit(child, file)
    ir_files = sorted((port / 'build').glob('game_*.ir.json'))
    for path in ir_files:
        visit(json.loads(path.read_text()), path.relative_to(root).as_posix())
    missing = [dict(name=name, desktop_sources=sorted(desktop[name]),
                    reference_count=sum(references[name].values()),
                    game_files=dict(sorted(references[name].items())))
               for name in desktop.keys() - provided]
    missing.sort(key=lambda row: (-row['reference_count'], row['name']))
    return dict(ir_files_scanned=len(ir_files), missing=missing)

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv)
    print(json.dumps(survey(args.root), indent=2))

if __name__ == '__main__':
    main()
