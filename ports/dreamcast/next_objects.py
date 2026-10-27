"""Show source sizes after the compiled GAME.CGO prefix (AI-assisted)."""
import argparse
import json
from pathlib import Path
import gp_deps
from build_game_cgo import game_order


def next_objects(root, status, count=20):
    if count < 0:
        raise ValueError('count must be nonnegative')
    root = Path(root)
    order = game_order(root)
    compiled = status['compiled']
    if not isinstance(compiled, list) or compiled != order[:len(compiled)]:
        raise ValueError('compiled objects are not a GAME.CGO prefix')
    graph = gp_deps.load(root)
    result = []
    for name in order[len(compiled):len(compiled) + count]:
        if name not in graph:
            raise ValueError(f'game.gp has no source for {name}')
        source = Path('goal_src/jak1') / graph[name][0]
        result.append(dict(name=name, source=source.as_posix(),
                           lines=len((root / source).read_text(encoding='utf-8').splitlines())))
    return result


def main(argv=None):
    port = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=port.parents[1])
    parser.add_argument('--report', type=Path, default=port / 'build/game-cgo-report.json')
    parser.add_argument('-n', '--count', type=int, default=20)
    args = parser.parse_args(argv)
    try:
        rows = next_objects(args.root, json.loads(args.report.read_text(encoding='utf-8')), args.count)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    for row in rows:
        print(f"{row['name']}\t{row['source']}\t{row['lines']}")


if __name__ == '__main__':
    main()
