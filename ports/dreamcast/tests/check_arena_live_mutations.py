"""Prove both independent oracles reject producer bugs (AI-assisted), ISC.

Every mutation is in a temporary source copy. No target execution or Git writes.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PORT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "arena_live_math.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
 dc_live_pose p={{0},{0},{0}};dc_game_camera c;dc_render_query q;unsigned i,j;
 float y=2.2f,a=-.45f,cy=cosf(y),sy=sinf(y),cp=cosf(a),sp=sinf(a);
 const float m[16]={cy,0,sy,0,sp*sy,cp,-sp*cy,0,-cp*sy,sp,cp*cy,0,0,0,0,1};
 (void)argv;if(argc!=2)return 1;
 memcpy(p.inv,m,64);p.trans[0]=-241*4096.f;p.trans[1]=55*4096.f;p.trans[2]=-251*4096.f;p.trans[3]=1;
 for(i=0;i<3;++i){p.inv[12+i]=p.trans[i];for(j=0;j<3;++j){p.rot[4*i+j]=p.inv[4*j+i];p.rot[12+i]-=p.inv[4*i+j]*p.trans[j];}}
 p.rot[15]=1;
 if(!dc_live_convert(&p,55,&c))return 2;
 q=(dc_render_query){c.target[0]-14.,c.target[2]-14.,c.target[0]+14.,c.target[2]+14.};
 if(argv[1][0]=='q'){
  if(!dc_live_query_matches(&p,&q,14)){puts("raw GOAL anchor rejected mutated producer query");return 42;}
 }else if(!dc_live_camera_matches(&p,&c)){
  puts("raw GOAL view probes rejected mutated submitted camera");return 43;
 }
 return 0;
}
'''


def main():
    env = os.environ.copy()
    if os.name == 'nt':
        env['PATH'] += ';C:/msys64/mingw64/bin'
    cc = os.environ.get('HOST_CC') or shutil.which('gcc', path=env['PATH'])
    original = (PORT / 'arena_live_math.c').read_text()
    cases = [('baseline', '', 'q', 0), ('baseline', '', 'c', 0),
             ('query', 'c.target[0]+=1;', 'q', 42),
             ('submitted', 'c.yaw+=.1f;', 'c', 43)]
    report = dict(ai_assisted=True, license='ISC', host_only=True, cases=[])
    with tempfile.TemporaryDirectory(prefix='arena-live-mutations-') as tmp:
        root = Path(tmp)
        (root / 'harness.c').write_text(HARNESS)
        for fast in (False, True):
            for name, mutation, arg, expected in cases:
                source = original.replace(' *out=c;return 1;', f' {mutation}\n *out=c;return 1;', 1)
                (root / 'math.c').write_text(source)
                exe = root / 'mutation.exe'
                subprocess.run([cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                                *(['-ffast-math'] if fast else []), '-I', str(PORT),
                                str(root / 'harness.c'), str(root / 'math.c'),
                                str(PORT / 'instance_pass.c'), '-lm', '-o', str(exe)],
                               env=env, check=True)
                result = subprocess.run([str(exe), arg], env=env, capture_output=True, text=True)
                assert result.returncode == expected, (name, result.returncode, result.stderr)
                row = dict(name=name, fast_math=fast, expected_exit=expected, observed_exit=result.returncode,
                           mutation=mutation, source_sha256=hashlib.sha256(source.encode()).hexdigest())
                report['cases'].append(row)
                print(name, 'fast-math' if fast else 'O2', result.returncode, result.stdout.strip())
    (PORT / 'build/arena-live-validation/mutations.json').write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
