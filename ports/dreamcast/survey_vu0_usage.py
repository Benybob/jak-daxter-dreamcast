"""Static GAME.CGO vector/platform inventory, not lowering coverage (AI-assisted).

Counts syntactic forms in all conditional branches and quoted macro templates;
does not expand macros. Comments and string contents never count as operations.
Feature counts are indicators, not a claim that every occurrence blocks lowering.
Source hashes normalize CRLF to LF to match Git text across host platforms.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

PORT = Path(__file__).resolve().parent
ROOT = PORT.parents[1]


class String(str):
    """Keep strings distinct from GOOS symbols."""


def forms(text):
    """Small lexical reader for surveying, including nested GOOS block comments."""
    tokens, i = [], 0
    while i < len(text):
        if text[i].isspace():
            i += 1
        elif text[i] == ';':
            end = text.find('\n', i)
            i = len(text) if end < 0 else end + 1
        elif text.startswith('#|', i):
            depth, i = 1, i + 2
            while depth:
                if i >= len(text):
                    raise ValueError('unterminated block comment')
                if text.startswith('#|', i):
                    depth, i = depth + 1, i + 2
                elif text.startswith('|#', i):
                    depth, i = depth - 1, i + 2
                else:
                    i += 1
        elif text[i] == '"':
            start, i = i, i + 1
            while i < len(text) and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
            if i >= len(text):
                raise ValueError('unterminated string')
            # Filenames need no GOOS escape decoding. Reject escaped filenames later.
            tokens.append(String(text[start + 1:i]))
            i += 1
        elif text[i] in '()':
            tokens.append(text[i]); i += 1
        elif text[i] in "'`,":
            i += 1  # reader prefixes do not affect syntactic operation counts
            if i < len(text) and text[i] == '@':
                i += 1
        else:
            start = i
            while i < len(text) and not text[i].isspace() and text[i] not in '();"':
                i += 1
            tokens.append(text[start:i])
    stack, result = [], []
    for token in tokens:
        if type(token) is str and token == '(':
            node = []
            (stack[-1] if stack else result).append(node)
            stack.append(node)
        elif type(token) is str and token == ')':
            if not stack:
                raise ValueError('unmatched close parenthesis')
            stack.pop()
        else:
            (stack[-1] if stack else result).append(token)
    if stack:
        raise ValueError('unclosed parenthesis')
    return result


def walk(node):
    if isinstance(node, list):
        yield node
        for child in node:
            yield from walk(child)


def symbol(value):
    return value if type(value) is str else ''


def register_class(decl, name):
    return isinstance(decl, list) and any(
        symbol(a) == ':class' and symbol(b) == name for a, b in zip(decl, decl[1:]))


MMI = re.compile(r'^\.(?:pext|ppac|pcpy|padd|psub|pabs|pdiv|pmul|pmadd|pmsub|pmax|pmin|pand|por|pxor|pnor|psll|psrl|psra|prot|prev|pexch|pexeh|pexew|pceq|pcgt|plzcw|qfsrv|pmf|pmt)')


def count_source(text):
    tree = forms(text)
    ops, features = Counter(), Counter()
    vf_blocks = 0
    for node in walk(tree):
        if not node:
            continue
        head = symbol(node[0])
        if head.startswith('.') and len(head) > 1:
            ops[head] += 1
            if head in ('.lqc2', '.sqc2'):
                features[head] += 1
            if MMI.match(head):
                features['mmi'] += 1
        if head == 'rlet' and len(node) > 1 and isinstance(node[1], list):
            vf_blocks += any(register_class(d, 'vf') for d in node[1])
            features['rlet_i128'] += int(any(register_class(d, 'i128') for d in node[1]))
        if head == 'the' and len(node) > 1 and symbol(node[1]) == 'uint128':
            features['the_uint128'] += 1
        if head in ('fmax', 'fmin'):
            features[head] += 1
        # Symbol occurrences, including names/types, not text matches in comments/strings.
        for atom in node:
            name = symbol(atom)
            for feature in ('vu1', 'dma', 'vif'):
                if re.search(r'(^|[-:*])' + feature + r'($|[-:*0-9])', name):
                    features[feature] += 1
    return dict(rlet_vf_blocks=vf_blocks, asm_ops=dict(sorted(ops.items())),
                features=dict(sorted((k, v) for k, v in features.items() if v)))


def survey(root=ROOT):
    root = Path(root)
    dgo = root / 'goal_src/jak1/dgos/game.gd'
    tree = forms(dgo.read_text(encoding='utf-8'))
    if len(tree) != 1 or len(tree[0]) != 2 or tree[0][0] != 'GAME.CGO':
        raise ValueError('unexpected GAME.CGO manifest shape')
    names = tree[0][1]
    index = defaultdict(list)
    for path in (root / 'goal_src/jak1').rglob('*.gc'):
        index[path.stem].append(path)
    files, ops, features = [], Counter(), Counter()
    for name in names:
        if not isinstance(name, String) or not name.endswith(('.o', '.go')) or '\\' in name or '/' in name:
            raise ValueError('unexpected manifest object name')
        if name.endswith('.go'):
            files.append(dict(object=name, source=None, status='data object; no GOAL source surveyed',
                              rlet_vf_blocks=0, asm_ops={}, features={}))
            continue
        matches = index[name[:-2]]
        if len(matches) != 1:
            raise ValueError(f'{name}: expected exactly one source, found {len(matches)}')
        path = matches[0]
        raw = path.read_bytes().replace(b'\r\n', b'\n')
        record = dict(object=name, source=path.relative_to(root).as_posix(),
                      sha256_lf=hashlib.sha256(raw).hexdigest(), **count_source(raw.decode('utf-8')))
        files.append(record)
        ops.update(record['asm_ops']); features.update(record['features'])
    return dict(ai_assisted=True, scope=__doc__.strip(),
                manifest_sha256_lf=hashlib.sha256(dgo.read_bytes().replace(b'\r\n', b'\n')).hexdigest(),
                file_count=len(files), source_file_count=sum(f['source'] is not None for f in files),
                rlet_vf_blocks=sum(f['rlet_vf_blocks'] for f in files),
                first_40=files[:40], files=files,
                asm_op_frequency=dict(sorted(ops.items(), key=lambda item: (-item[1], item[0]))),
                features=dict(sorted(features.items())))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check', type=Path)
    args = parser.parse_args(argv)
    report = survey(args.root)
    if args.check and report != json.loads(args.check.read_text(encoding='utf-8')):
        raise SystemExit('VU usage survey differs')
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(files=report['file_count'], first_40=len(report['first_40']),
                          top_ops=list(report['asm_op_frequency'].items())[:15])))


if __name__ == '__main__':
    main()
