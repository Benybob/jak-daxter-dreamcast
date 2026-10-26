"""Generate original-free exact-rational native clipping cases (AI-assisted)."""
from pathlib import Path
import argparse,json
from frustum_oracle import cases,oracle
def generate(path,count=256):
    lines=['/* Exact rational barycentric hulls (AI-assisted), ISC. */','static const struct frustum_case frustum_cases[]={']
    checks=0;max_vertices=0
    def vertex(v):return '{'+','.join(repr(x)+'f' for x in v[:5])+','+str(v[5])+'u}'
    for tri in cases(count):
        expected=oracle(tri);n=len(expected);checks+=3+14*n+(n>=3);max_vertices=max(max_vertices,n)
        lines.append('{{'+','.join(vertex(v) for v in tri)+'},'+str(n)+',{'+(','.join(vertex(v) for v in expected) or '{0,0,0,0,0,0}')+'}},')
    lines+=['};',f'#define FRUSTUM_NATIVE_CHECKS {checks}',f'#define FRUSTUM_MAX_VERTICES {max_vertices}']
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True);path.write_text('\n'.join(lines)+'\n')
    return dict(ai_assisted=True,cases=count,native_checks=checks,max_vertices=max_vertices,native_execution=False)
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('header',type=Path);a=ap.parse_args();print(json.dumps(generate(a.header)))
