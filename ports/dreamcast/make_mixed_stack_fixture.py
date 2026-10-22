"""Generate synthetic fixed mixed-stack native boundary probes (AI-assisted)."""
from pathlib import Path
PORT=Path(__file__).resolve().parent
# Integer stack spill positions and float positions at register/stack boundaries.
PATTERNS=('iiif','fiii','iifi','ifii','iiiiffff','ffffiiii','ifififii','iifififi',
          'iiiiiiif','fiiiiiii','iiiiifff','fffiiiii','iiifffii','ifiiiiif','fiifiiii','iifiiiif')

def generate():
    goal=[';; Generated synthetic mixed-stack ABI fixtures (AI-assisted), ISC.']
    native=['/* Generated GCC-to-GOAL and GOAL-to-GOAL probes (AI-assisted), ISC. */',
            'static void mixed_stack_probes(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id) {',
            'unsigned v;',
            'for(v=0;v<8;++v) {']
    names=[]
    for p,pattern in enumerate(PATTERNS):
        parameters=' '.join(f'(v{i} {"int" if kind=="i" else "float"})' for i,kind in enumerate(pattern))
        arguments=' '.join('v'+str(i) for i in range(len(pattern)))
        cparams=','.join('uint64_t' if kind=='i' else 'float' for kind in pattern)
        cargs=','.join(f'ints[(v+{i})%8]' if kind=='i' else f'as_float(fps[(v+{i})%8])' for i,kind in enumerate(pattern))
        native+=['{','uint32_t before=stack_pointer();']
        for i,kind in enumerate(pattern):
            name=f'dc-ms-p{p}-a{i}'; wrapper=name+'-call'; names.extend((name,wrapper))
            goal+=[f'(defun {name} ({parameters}) v{i})',f'(defun {wrapper} ({parameters}) ({name} {arguments}))']
            result='uint64_t' if kind=='i' else 'float'; expected=f'ints[(v+{i})%8]' if kind=='i' else f'fps[(v+{i})%8]'
            for symbol in (name,wrapper):
                call=f'(({result}(*)({cparams}))(uintptr_t)entry(rt,"{symbol}"))({cargs})'
                if kind=='f': call='bits('+call+')'
                native.append(f'CHECK({call},{expected},"mixed fixed stack argument {p}/{i} '+('GOAL call' if symbol==wrapper else 'GCC entry')+'");')
        native+=['CHECK(stack_pointer(),before,"mixed stack calls restore native stack pointer");','}']
    native+=['}','}']
    goal+=['''(defun dc-ms-live ((v0 float) (v1 int) (v2 int) (v3 int) (v4 int) (v5 int) (v6 int) (v7 int))
  (let ((keep (+ v1 v2 v3 v4 v5 v6 v7)))
    (+ keep (dc-ms-p9-a7 v0 v1 v2 v3 v4 v5 v6 v7))))
(defun dc-ms-repeat ((n int) (f float) (a int) (b int))
  (let ((sum 0))
    (dotimes (i n)
      (set! sum (+ sum a b (dc-ms-p0-a2 a b i f))))
    sum))
(none)''']
    names+=['dc-ms-live','dc-ms-repeat']
    return '\n'.join(goal)+'\n','\n'.join(native)+'\n',names

if __name__=='__main__':
    goal,native,names=generate(); (PORT/'build').mkdir(exist_ok=True)
    (PORT/'build/mixed_stack_fixture.gc').write_text(goal,encoding='utf-8',newline='\n')
    (PORT/'build/mixed_stack_cases.h').write_text(native,encoding='utf-8',newline='\n')
    print(f'Generated {len(names)} mixed-stack functions across {len(PATTERNS)} layouts (AI-assisted).')
