"""Generate native SH-4 ABI/control-flow probes beyond BRA range (AI-assisted)."""
import json
from pathlib import Path
from sh4_backend import Emitter

def probes():
    out={}
    e=Emitter(); e.mov(0,4); e.mov(1,5); e.branch('bra','end')
    for _ in range(2200): e.nop()
    e.label('end'); e.ret(); out['forward']=e
    e=Emitter(); e.branch('bra','setup'); e.label('end'); e.ret()
    for _ in range(2200): e.nop()
    e.label('setup'); e.mov(0,4); e.mov(1,5); e.branch('bra','end'); out['backward']=e
    e=Emitter(); e.mov(0,6); e.mov(1,7); e.compare_pz(4); e.branch('bt','end')
    e.clrt(); e.immediate(2,1); e.immediate(3,0); e.addc(0,2); e.addc(1,3)
    for _ in range(2200): e.nop()
    e.label('end'); e.ret(); out['conditional']=e
    for flag in (0,1):
        e=Emitter(); e.mov(0,4); e.word(0x0018 if flag else 0x0008); e.branch('bra','end')
        for _ in range(2200): e.nop()
        e.label('end'); e.word(0x0129); e.ret(); out['flag'+str(flag)]=e
    e=Emitter(); e.literal(0,('symbol','pointer:#t')); e.immediate(1,0); e.branch('bra','end')
    for _ in range(2200): e.nop()
    e.label('end'); e.ret(); out['literal']=e
    return out

def main():
    root=Path(__file__).resolve().parent/'build'; root.mkdir(exist_ok=True)
    lines=['/* Native far-branch probes; not original GOAL game bodies (AI-assisted). */','#include "goal_module.h"']
    reports=[]
    for name,e in probes().items():
        data,relocs,stats=e.finish_with_stats(relax=True,literal_islands=True,far_branches=True)
        lines.append('static const unsigned char dc_far_'+name+'[]={'+','.join(str(b) for b in data)+'};')
        if relocs:
            lines.append('static const dc_goal_code_relocation dc_far_'+name+'_relocs[]={')
            lines += ['{'+str(r['offset'])+'u,"'+r['symbol']+'"},' for r in relocs]
            lines.append('};')
        reports.append(dict(name='dc-far-'+name,c_name=name,bytes=len(data),relocations=relocs,**stats))
    lines.append('static const dc_goal_code_export goal_far_fixture_exports[]={')
    for report in reports:
        name=report['c_name']; relocs='dc_far_'+name+'_relocs' if report['relocations'] else 'NULL'
        lines.append('{"'+report['name']+'",dc_far_'+name+','+str(report['bytes'])+'u,'+relocs+','+str(len(report['relocations']))+'u},')
    lines += ['};','#define GOAL_FAR_FIXTURE_EXPORT_COUNT 6u']
    (root/'far_fixture.h').write_text('\n'.join(lines)+'\n')
    (root/'far_fixture.manifest.json').write_text(json.dumps(reports,indent=2)+'\n')
    print(json.dumps(reports,indent=2))
if __name__=='__main__': main()
