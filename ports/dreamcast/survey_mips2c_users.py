"""Map native mips2c registrations to GOAL callers (AI-assisted).

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
import json
from pathlib import Path
import re
import sys

CPP_TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', re.S)
def cpp_code(text):
    return CPP_TOKEN.sub(lambda m: m[0] if m[0].startswith('"') else ' ', text)

def goal_code(text):
    # GOAL has nested block comments as well as semicolon line comments.
    output = []; i = 0; depth = 0
    while i < len(text):
        if text.startswith('#|', i):
            depth += 1; output.append('  '); i += 2
        elif depth and text.startswith('|#', i):
            depth -= 1; output.append('  '); i += 2
        elif depth:
            output.append('\n' if text[i] == '\n' else ' '); i += 1
        elif text[i] == ';':
            end = text.find('\n', i)
            if end == -1: end = len(text)
            output.append(' ' * (end-i)); i = end
        elif text[i] == '"':
            match = re.match(r'"(?:\\.|[^"\\])*"', text[i:])
            if not match: raise ValueError('unterminated GOAL string')
            output.append(match[0]); i += len(match[0])
        else:
            output.append(text[i]); i += 1
    if depth: raise ValueError('unterminated GOAL block comment')
    return ''.join(output)

def survey(root):
    root = Path(root)
    registrations = {}
    for path in sorted((root / 'game/mips2c/jak1_functions').glob('*.cpp')):
        names = sorted(set(json.loads(m[1]) for m in re.finditer(
            r'\bgLinkedFunctionTable\s*\.\s*reg\s*\(\s*("(?:\\.|[^"\\])*")', cpp_code(path.read_text()))))
        registrations[path.relative_to(root).as_posix()] = names
    gd = goal_code((root / 'goal_src/jak1/dgos/game.gd').read_text())
    objects = set(re.findall(r'"([^"\n]+)\.o"', gd))
    callers = []
    pattern = re.compile(r'\(\s*(__pc-get-mips2c|def-mips2c|defmethod-mips2c)\s+("(?:\\.|[^"\\])*"|[^\s()]+)')
    for path in sorted((root / 'goal_src/jak1').rglob('*.gc')):
        code = goal_code(path.read_text())
        for match in pattern.finditer(code):
            token = match[2]
            if match[1] != 'def-mips2c' and not token.startswith('"'):
                continue  # macro implementation, not a literal lookup
            name = json.loads(token) if token.startswith('"') else token
            callers.append(dict(file=path.relative_to(root).as_posix(), name=name,
                                line=code.count('\n', 0, match.start())+1,
                                in_game_cgo=path.stem in objects))
    rows = []
    for file, names in registrations.items():
        uses = [c for c in callers if c['name'] in names]
        rows.append(dict(native_file=file, functions=names, callers=uses,
                         game_cgo_callers=sum(c['in_game_cgo'] for c in uses)))
    rows.sort(key=lambda row: (-row['game_cgo_callers'], row['native_file']))
    known = {n for names in registrations.values() for n in names}
    return dict(modules=rows, unregistered_callers=[c for c in callers if c['name'] not in known])

def main(argv=None):
    parser = argparse.ArgumentParser(description='JSON on stdout; priority table on stderr.')
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv)
    result = survey(args.root)
    print(json.dumps(result, indent=2))
    print('GAME callers  Functions  Native file', file=sys.stderr)
    for row in result['modules']:
        print(f"{row['game_cgo_callers']:12}  {len(row['functions']):9}  {row['native_file']}", file=sys.stderr)

if __name__ == '__main__':
    main()
