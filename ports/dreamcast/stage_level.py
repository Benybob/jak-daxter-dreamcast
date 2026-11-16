"""Stage one level on the play disc, every step (AI-assisted), ISC.

  python stage_level.py BEA --iso <iso_data/jak1> --export <decompiler_out/jak1> [--half 9]

For level BEA (Sentinel Beach) this:
  1. compiles its code objects (build_level_code.py; villagep-obs first for the
     levels whose warp gate or villagers use it, as Geyser Rock and the villages);
  2. builds its logic BSP and art without merc geometry
     (build_logic_bsp.py --art --strip-merc);
  3. packs both as the DGO original load-begin asks for, e.g. BEACH.DGO
     (make_level_dgo.py), and copies <LEVEL>.VIS beside it;
  4. builds its TIE instance package (tie_lods.py --textured --compact);
  5. stages its terrain sectors, cells and LEVEL.DRH over the area its tfrag
     covers (make_level_render_assets.py --large);
  6. bundles its skinned models with Jak's and the common pickups
     (make_skin_models.py);
  7. copies the render data to build/level-disc/<FOLDER>.

Everything it reads is the owner's disc and its decompiler export; everything it
writes is under build/ (git-ignored). The DGO's name is the level's name cut to
8 characters (load-begin's *temp-string*), and the render folder is that name,
which level_dgo_rpc.c maps (TRAINING to GEYSER, the older folder).
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

PORT = Path(__file__).resolve().parent
# nickname: (level name, export folder, needs villagep-obs)
LEVELS = {
    'TRA': ('training', 'training', True), 'VI1': ('village1', 'village1', True),
    'BEA': ('beach', 'beach', False), 'JUN': ('jungle', 'jungle', False), 'JUB': ('jungleb', 'jungleb', False),
    'MIS': ('misty', 'misty', False), 'FIC': ('firecanyon', 'firecanyon', False),
    'VI2': ('village2', 'village2', True), 'SUN': ('sunken', 'sunken', False), 'SUB': ('sunkenb', 'sunkenb', False),
    'SWA': ('swamp', 'swamp', False), 'ROL': ('rolling', 'rolling', False), 'OGR': ('ogre', 'ogre', False),
    'VI3': ('village3', 'village3', True), 'SNO': ('snow', 'snow', False), 'MAI': ('maincave', 'maincave', False),
    'DAR': ('darkcave', 'darkcave', False), 'ROB': ('robocave', 'robocave', False),
    'LAV': ('lavatube', 'lavatube', False), 'CIT': ('citadel', 'citadel', False),
    'FIN': ('finalboss', 'finalboss', False), 'TIT': ('title', 'title', False), 'INT': ('intro', 'intro', False),
}
COMMON = ['crate-wood', 'crate-steel', 'crate-iron', 'money', 'fuel-cell', 'buzzer']


def run(args, python=sys.executable):
    print('>', ' '.join(str(a) for a in args[:4]), '...', flush=True)
    result = subprocess.run([python, *map(str, args)], cwd=PORT, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f'{args[0]} failed:\n{(result.stdout + result.stderr)[-2000:]}')
    return result.stdout


def tfrag_area(glb):
    """The rectangle (x0, z0, x1, z1, mid y) holding 99.6% of the level's tfrag vertices."""
    data = glb.read_bytes()
    n = struct.unpack_from('<I', data, 12)[0]
    doc = json.loads(data[20:20 + n])
    binary = 20 + n + 8
    acc, views = doc['accessors'], doc['bufferViews']
    xs, ys, zs = [], [], []
    roots = [node for node in doc['nodes'] if 'tfrag' in (node.get('name') or '') and 'children' in node]
    for root in roots:
        for child in root['children']:
            node = doc['nodes'][child]
            if 'mesh' not in node:
                continue
            for prim in doc['meshes'][node['mesh']]['primitives']:
                a = acc[prim['attributes']['POSITION']]
                v = views[a['bufferView']]
                at = binary + v.get('byteOffset', 0) + a.get('byteOffset', 0)
                stride = v.get('byteStride', 12)
                for i in range(0, a['count'], 7):
                    x, y, z = struct.unpack_from('<3f', data, at + i * stride)
                    xs.append(x); ys.append(y); zs.append(z)
    if not xs:
        raise SystemExit(f'{glb.name}: no tfrag vertices')
    xs.sort(); ys.sort(); zs.sort()
    q = lambda s, f: s[int(f * (len(s) - 1))]
    return q(xs, .002), q(zs, .002), q(xs, .998), q(zs, .998), q(ys, .5)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level', help='nickname, e.g. BEA')
    ap.add_argument('--iso', type=Path, required=True, help='the extracted disc: has DGO/ and VIS/')
    ap.add_argument('--export', type=Path, required=True, help='decompiler_out/jak1')
    ap.add_argument('--frontend', type=Path, help='goalc-dreamcast-ir.exe for build_level_code.py')
    ap.add_argument('--half', type=float, default=9.0, help="terrain window half width (m)")
    ap.add_argument('--models-python', default=sys.executable, help='a Python with numpy and Pillow')
    ap.add_argument('--skip-code', action='store_true')
    a = ap.parse_args()
    nick = a.level.upper()
    name, folder_name, villagep = LEVELS[nick]
    stem = name[:8].upper()
    disc = PORT / 'build/level-disc'
    work = PORT / 'build' / f'{name}-render'
    work.mkdir(parents=True, exist_ok=True)
    dgo = a.iso / 'DGO' / f'{nick}.DGO'
    levels = a.export / 'levels' / folder_name

    if not a.skip_code:
        code = ['build_level_code.py', nick] + (['--with', 'villagep-obs'] if villagep else [])
        if a.frontend:
            code[1:1] = ['--frontend', a.frontend]
        run(code)
    run(['build_logic_bsp.py', '--art', '--strip-merc', dgo])
    print(run(['make_level_dgo.py', nick, stem])[-300:])
    shutil.copy(a.iso / 'VIS' / f'{nick}.VIS', disc / f'{nick}.VIS')

    glb = levels / f'{folder_name}-background.glb'
    package = work / 'package.dci3'
    run(['tie_lods.py', '--textured', '--compact', glb, dgo, a.export.parent.parent / 'debug_out',
         '--out', package, '--json', work / 'tie3.json'])
    x0, z0, x1, z1, y = tfrag_area(glb)
    report = run(['make_level_render_assets.py', glb, package, work / 'assets',
                  f'--at={(x0 + x1) / 2:.1f},{y:.1f},{(z0 + z1) / 2:.1f}',
                  f'--area={x0:.0f},{z0:.0f},{x1:.0f},{z1:.0f}', '--large', '--half', str(a.half),
                  '--header', work / 'fixture.h'])
    models = [a.export / 'levels/common/eichar-lod0.glb', *sorted(levels.glob('*-lod0.glb')),
              *sorted(levels.glob('*-geo.glb')), *(a.export / 'levels/common' / f'{c}-lod0.glb' for c in COMMON)]
    run(['make_skin_models.py', *models, '--out', work / 'MODELS.DSK'], python=a.models_python)

    folder = disc / ('GEYSER' if stem == 'TRAINING' else stem)
    if folder.exists():
        shutil.rmtree(folder)
    shutil.copytree(work / 'assets', folder, ignore=shutil.ignore_patterns('*.json'))
    shutil.copy(work / 'MODELS.DSK', folder / 'MODELS.DSK')
    tail = json.loads(report[report.index('{'):])
    print(json.dumps(dict(level=nick, dgo=f'{stem}.DGO', folder=folder.name, area=[x0, z0, x1, z1],
                          window=tail.get('window'), records=tail.get('instance_records'),
                          disc_bytes=sum(p.stat().st_size for p in folder.rglob('*') if p.is_file()),
                          ai_assisted=True), indent=1))


if __name__ == '__main__':
    main()
