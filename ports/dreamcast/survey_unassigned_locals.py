"""Conservative source candidates for commented-out local assignments (AI-assisted).

Uses the existing GOOS reader. No macro expansion or control-flow proof: all
syntactic conditional branches count as live; nested functions are separate.
Only declared locals with a commented setter and no explicit live setter are
reported. Alias/shadow/address cases are uncertain, never automatic approvals.
"""
import argparse
import json
from pathlib import Path
import re
from audit_target_config import Atom, Node, read_forms

DEFINERS = {'defun', 'defun-debug', 'defmethod', 'defbehavior', 'lambda', 'behavior'}
STORES = {'.svf', '.sqc2', '.sw', '.sd', '.sb', '.sh', '.swc1', '.sdc1'}


def symbol(node):
    return node.text if isinstance(node, Atom) and node.kind == 'symbol' else None


def walk(node, nested=True):
    if not isinstance(node, Node):
        return
    yield node
    for item in node.items:
        if isinstance(item, Node) and (nested or item.head() not in DEFINERS):
            yield from walk(item, nested)


def comments(text):
    """Yield semicolon and nested block-comment text with original line numbers."""
    block, quoted, escaped = 0, False, False
    for line_no, line in enumerate(text.splitlines(), 1):
        i, pieces = 0, []
        while i < len(line):
            if block:
                if line.startswith('#|', i):
                    block += 1; i += 2
                elif line.startswith('|#', i):
                    block -= 1; i += 2
                else:
                    pieces.append(line[i]); i += 1
            elif quoted:
                ch = line[i]; i += 1
                if escaped:
                    escaped = False
                elif ch == '\\':
                    escaped = True
                elif ch == '"':
                    quoted = False
            elif line.startswith('#|', i):
                block = 1; i += 2
            elif line[i] == '"':
                quoted = True; i += 1
            elif line[i] == ';':
                pieces.append(line[i:]); break
            else:
                i += 1
        if pieces:
            yield line_no, ''.join(pieces).strip()


def setter(op):
    return op in {'set!', '+!', '-!', '*!', '/!', 'inc!', 'dec!'} or (
        bool(op) and op.startswith('.') and op not in STORES and not op.startswith(('.b', '.j')))


def scan_source(text, filename='<source>'):
    forms = read_forms(text, filename)
    comment_lines = list(comments(text))
    functions = [n for form in forms for n in walk(form) if n.head() in DEFINERS]
    results = []
    for function in functions:
        nodes = list(walk(function, nested=False))
        declarations = [n for n in nodes if n.head() == 'local-vars']
        locals_ = {symbol(d.items[0]) for n in declarations for d in n.items[1:]
                   if isinstance(d, Node) and d.items and symbol(d.items[0])}
        if not locals_:
            continue
        name = symbol(function.items[1]) if len(function.items) > 1 else None
        if function.head() == 'defmethod' and len(function.items) > 2:
            name = 'method ' + str(name) + '/' + str(symbol(function.items[2]) or 'typed-arguments')
        name = name or f'anonymous@{function.line}'
        end = max([function.line] + [a.line for n in walk(function) for a in n.items if isinstance(a, Atom)])
        nested_ranges = [(n.line, max([n.line] + [a.line for x in walk(n) for a in x.items if isinstance(a, Atom)]))
                         for n in walk(function) if n is not function and n.head() in DEFINERS]
        for local in sorted(locals_):
            live = [n for n in nodes if len(n.items) > 1 and setter(n.head()) and symbol(n.items[1]) == local]
            if live:
                continue
            evidence = []
            for number, line in comment_lines:
                if not function.line <= number <= end or any(a <= number <= b for a, b in nested_ranges):
                    continue
                for match in re.finditer(r'\(\s*([^\s()]+)\s+([^\s()]+)', line):
                    if setter(match[1]) and match[2] == local:
                        evidence.append(dict(line=number, text=line))
            if not evidence:
                continue
            reasons = []
            uses = sum(symbol(a) == local for n in nodes for a in n.items if isinstance(a, Atom)) - 1
            if uses <= 0:
                reasons.append('no live use either; unused declaration, not a proven uninitialized read')
            if any(n.head() in {'let', 'let*', 'rlet', 'dotimes', 'dolist'} and
                   any(symbol(a) == local for child in n.items[1:2] if isinstance(child, Node)
                       for decl in child.items if isinstance(decl, Node) for a in decl.items[:1]) for n in nodes):
                reasons.append('shadowing/binding or register alias requires review')
            if any(n.head() == '&' and len(n.items) > 1 and symbol(n.items[1]) == local for n in nodes):
                reasons.append('address escapes; possible indirect write')
            results.append(dict(file=filename, function=name, function_line=function.line, local=local,
                                status='uncertain' if reasons else 'candidate', comments=evidence,
                                live_mentions=max(0, uses),
                                reasons=reasons or ['no explicit live setter; macro effects not proven']))
    return results


def survey(root):
    root = Path(root)
    paths = sorted((root / 'goal_src/jak1/engine').rglob('*.gc'))
    records = [r for p in paths for r in scan_source(p.read_text(encoding='utf-8'), p.relative_to(root).as_posix())]
    return dict(files_scanned=len(paths), records=records)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    result = survey(args.root)
    if args.json:
        print(json.dumps(result, indent=2))
        return
    print(f"{result['files_scanned']} engine files scanned; {len(result['records'])} local candidates")
    for r in result['records']:
        print(f"{r['file']}:{r['function_line']} {r['function']} {r['local']} [{r['status']}]")
        for line in r['comments']:
            print(f"  {line['line']}: {line['text']}")


if __name__ == '__main__':
    main()
