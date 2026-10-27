"""Rank stack frames in native GAME manifests (AI-assisted)."""
import argparse
import json
from pathlib import Path


def report(directory, count=20):
    if count < 0:
        raise ValueError('count must be nonnegative')
    functions = []
    for path in sorted(Path(directory).glob('game_*.manifest.json')):
        for fn in json.loads(path.read_text(encoding='utf-8'))['functions']:
            functions.append(dict(file=path.name, name=fn['name'], bytes=fn['bytes'],
                                  stack_bytes=fn['stack_bytes']))
    functions.sort(key=lambda fn: (-fn['stack_bytes'], fn['file'], fn['name']))
    return dict(total=len(functions), exceeds_4096=sum(f['stack_bytes'] > 4096 for f in functions),
                largest=functions[:count])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path(__file__).resolve().parent / 'build')
    parser.add_argument('-n', '--count', type=int, default=20)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    try:
        result = report(args.build, args.count)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        for fn in result['largest']:
            print(f"{fn['stack_bytes']:>8}  {fn['file']}  {fn['name']}")
        print(f"{result['exceeds_4096']} of {result['total']} functions exceed 4096 bytes")


if __name__ == '__main__':
    main()
