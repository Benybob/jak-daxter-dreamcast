"""Reproduce native runtime/compiled GOAL integration checks (AI-assisted)."""
import argparse
from pathlib import Path
import subprocess
import sys

def main():
    port=Path(__file__).resolve().parent
    work=port.parents[2]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend',type=Path,default=work/'og-host/bin/goalc-dreamcast-ir.exe')
    parser.add_argument('--verify',action='store_true')
    parser.add_argument('--flycast',type=Path,default=work/'flycast/flycast.exe')
    parser.add_argument('--nm',type=Path,default=work/'dc-sdk/sh-elf/bin/sh-elf-nm.exe')
    parser.add_argument('--output',type=Path,default=work/'flycast-runtime-goal')
    parser.add_argument('--msys',type=Path,default=Path('C:/msys64'),help='Windows SDK runtime/build shell root')
    args=parser.parse_args()
    compiler=[sys.executable,str(port/'compile_goal_subset.py'),'--frontend',str(args.frontend),'--runtime','--msys',str(args.msys)]
    names=('read','write','false','true','not','empty','call-read','constant')
    command=compiler+['--source',str(port/'runtime_fixture.gc'),'--header',str(port/'build/runtime_fixture.h')]
    for name in names: command+=['--function','dc-runtime-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'initializer_fixture.gc'),
                      '--header',str(port/'build/initializer_fixture.h'),
                      '--initializer','dc-init-top-level']
    for name in ('read','bump','result','alias-value'): command+=['--function','dc-init-'+name]
    subprocess.run(command,check=True)
    identity=compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                       '--function','identity','--select-defuns']
    subprocess.run(identity+['--initializer','dc-identity-top-level',
                             '--header',str(port/'build/runtime_identity.h')],check=True)
    subprocess.run(identity+['--no-optimize','--header',str(port/'build/runtime_identity_baseline.h')],check=True)
    names=['car','cdr','set-car','set-cdr']+['load-'+kind for kind in
            ('i8','u8','i16','u16','i32','u32','i64','u64')]
    names+=['store-'+kind for kind in ('u8','u16','u32','u64')]+['load-float','store-float','type','offset']
    command=compiler+['--source',str(port/'memory_fixture.gc'),'--header',str(port/'build/memory_fixture.h')]
    for name in names: command+=['--function','dc-mem-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'static_fixture.gc'),'--header',str(port/'build/static_fixture.h'),
                      '--initializer','dc-static-top-level']
    for name in ('read-text','literal','read-float','number','float-add','string-length','first-char'):
        command+=['--function','dc-static-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                      '--select-defuns','--header',str(port/'build/original_lists.h')]
    for name in ('ref','last','member','assoc'): command+=['--function',name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'float_fixture.gc'),'--header',str(port/'build/float_fixture.h')]
    for name in ('eq','ne','lt','le','gt','ge','to-int','to-float','min','max','sqrt'):
        command+=['--function','dc-fp-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port.parents[1]/'goal_src/jak1/engine/math/math.gc'),
                      '--select-defuns','--header',str(port/'build/original_math.h')]
    for name in ('truncate','integral?','fractional-part','seek','lerp-clamp','lerp-scale'):
        command+=['--function',name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'bridge_fixture.gc'),'--header',str(port/'build/bridge_fixture.h')]
    for name in ('cons','type','third','eighth','stack-call','return-cdr'): command+=['--function','dc-bridge-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'quoted_fixture.gc'),'--header',str(port/'build/quoted_fixture.h'),
                      '--initializer','dc-quoted-top-level']
    for name in ('root','literal','head'): command+=['--function','dc-quoted-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'void_fixture.gc'),'--header',str(port/'build/void_fixture.h'),
                      '--initializer','dc-void-top-level']
    for name in ('write','clear','add','empty','read','call','call-read','dispatch'):
        command+=['--function','dc-void-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'type_fixture.gc'),'--header',str(port/'build/type_fixture.h'),
                      '--initializer','dc-type-top-level']
    for name in ('(method new dc-native-record)','(method score dc-native-record)'):
        command+=['--function',name]
    for name in ('create','child','read','write','score','of','alternate','install'):
        command+=['--function','dc-type-'+name]
    subprocess.run(command,check=True)
    subprocess.run(compiler+['--source',str(port/'void_empty.gc'),'--header',str(port/'build/void_empty.h'),
                             '--initializer','dc-void-empty-top-level'],check=True)
    command=compiler+['--source',str(port/'shift_fixture.gc'),'--header',str(port/'build/shift_fixture.h')]
    for name in ('left','logical','arithmetic','self','mix'):
        command+=['--function','dc-shift-'+name]
    for kind in ('left','logical','arithmetic'):
        for amount in (0,1,31,32,63): command+=['--function',f'dc-shift-{kind}-{amount}']
    subprocess.run(command,check=True)
    baseline=list(command); baseline[baseline.index('--header')+1]=str(port/'build/shift_baseline.h')
    subprocess.run(baseline+['--no-optimize'],check=True)
    subprocess.run(compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                             '--select-defuns','--function','ash','--header',str(port/'build/original_shift.h')],check=True)
    command=compiler+['--source',str(port/'division_fixture.gc'),'--header',str(port/'build/division_fixture.h')]
    for name in ('signed','signed-rem','unsigned','unsigned-rem','mix','call','same','seven'):
        command+=['--function','dc-div-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                      '--select-defuns','--header',str(port/'build/original_division.h')]
    for name in ('/','mod','rem'): command+=['--function',name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'native_file_fixture.gc'),'--header',str(port/'build/native_file_fixture.h'),
                      '--initializer','dc-file-top-level','--native-module',str(port/'build/native_fixture.dcn')]
    for name in ('read','add','string','list'): command+=['--function','dc-file-'+name]
    subprocess.run(command,check=True)
    subprocess.run([sys.executable,str(port/'embed_native_fixture.py'),str(port/'build/native_fixture.dcn'),
                    str(port/'build/native_file_blob.h')],check=True)
    command=compiler+['--source',str(port/'locals_fixture.gc'),'--header',str(port/'build/locals_fixture.h')]
    for name in ('zero','basic','mutate','eight','float','branch'): command+=['--function','dc-local-'+name]
    command+=['--function','(method score dc-local-record)']
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'early_fixture.gc'),'--header',str(port/'build/early_fixture.h')]
    for name in ('int','loop','float','effect','object','nested','eight'): command+=['--function','dc-early-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                      '--select-defuns','--header',str(port/'build/original_early.h')]
    for name in ('basic-type?','type-type?','find-parent-method'): command+=['--function',name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'sort_fixture.gc'),'--header',str(port/'build/sort_fixture.h')]
    for name in ('integer','boolean'): command+=['--function','dc-sort-'+name]
    subprocess.run(command,check=True)
    subprocess.run(compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                             '--select-defuns','--function','sort','--header',str(port/'build/original_sort.h')],check=True)
    command=compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                      '--select-defuns','--header',str(port/'build/original_operators.h')]
    for name in ('1/','+','-','*','/','mod','rem'): command+=['--function',name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'fixed_fixture.gc'),'--header',str(port/'build/fixed_fixture.h')]
    for name in ('read','low','high','alias'): command+=['--function','dc-fixed-'+name]
    subprocess.run(command,check=True)
    subprocess.run([sys.executable,str(port/'make_far_fixture.py')],check=True)
    subprocess.run(compiler+['--source',str(port/'dependency_context.gc'),'--function','dc-context-helper',
                             '--initializer','dc-context-top-level','--header',str(port/'build/dependency_context.h')],check=True)
    command=compiler+['--source',str(port/'dependency_fixture.gc'),'--preload',str(port/'dependency_context.gc'),
                      '--header',str(port/'build/dependency_fixture.h')]
    for name in ('compute','call','field','forward'): command+=['--function','dc-context-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'mixed_fixture.gc'),'--header',str(port/'build/mixed_fixture.h')]
    for name in ('left','right','f0','f1','sum','floats','call','float-call','eight-last','eight-int'):
        command+=['--function','dc-mixed-'+name]
    subprocess.run(command,check=True)
    validation=compiler+['--preload',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                         '--preload',str(port/'scalar_validation_context.gc')]
    subprocess.run(validation+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
        '--select-defuns','--function','valid?','--header',str(port/'build/original_valid.h')],check=True)
    command=validation+['--source',str(port/'scalar_validation_fixture.gc'),
                         '--header',str(port/'build/scalar_validation_fixture.h')]
    for name in ('start','end','ram-end'): command+=['--function','dc-validation-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
                      '--prefix-through-global','*last-loado-debug-usage*',
                      '--initializer','dc-original-kernel-globals-init',
                      '--header',str(port/'build/original_kernel_globals.h')]
    for source in (port.parents[1]/'goal_src/jak1/kernel/gcommon.gc',port/'scalar_validation_context.gc',
                   port.parents[1]/'goal_src/jak1/kernel/gkernel-h.gc',port.parents[1]/'goal_src/jak1/kernel/gstring-h.gc'):
        command+=['--preload',str(source)]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'enum_fixture.gc'),'--header',str(port/'build/enum_fixture.h')]
    for name in ('u32','u64','s32','s64','call','mixed','mixed-call','load-u32','load-s32','load-u64','load-s64','bits32','bits64','u8','u16','s8','s16','load-u8','load-s8','load-u16','load-s16'):
        command+=['--function','dc-enum-'+name]
    subprocess.run(command,check=True)
    subprocess.run([sys.executable,str(port/'make_mixed_stack_fixture.py')],check=True)
    from make_mixed_stack_fixture import generate
    command=compiler+['--source',str(port/'build/mixed_stack_fixture.gc'),'--header',str(port/'build/mixed_stack_fixture.h')]
    for name in generate()[2]: command+=['--function',name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
                      '--select-defuns','--header',str(port/'build/original_process_tree.h')]
    for source in (port.parents[1]/'goal_src/jak1/kernel/gcommon.gc',port/'scalar_validation_context.gc',
                   port.parents[1]/'goal_src/jak1/kernel/gkernel-h.gc',port.parents[1]/'goal_src/jak1/kernel/gstring-h.gc',
                   port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'):
        command+=['--preload',str(source)]
    for name in ('iterate-process-tree','execute-process-tree','search-process-tree','change-parent'):
        command+=['--function',name]
    subprocess.run(command,check=True)
    process_context=[]
    for source in (port.parents[1]/'goal_src/jak1/kernel/gcommon.gc',port/'scalar_validation_context.gc',
                   port.parents[1]/'goal_src/jak1/kernel/gkernel-h.gc',port.parents[1]/'goal_src/jak1/kernel/gstring-h.gc'):
        process_context+=['--preload',str(source)]
    subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--function','(method new process-tree)','--function','(method new process)',
        '--header',str(port/'build/original_process_new.h')],check=True)
    subprocess.run([sys.executable,str(port/'make_dead_pool_schema.py'),'--frontend',str(args.frontend),
                    '--runtime-bin',str(args.msys/'mingw64/bin')],check=True)
    subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--function','(method new dead-pool)','--function','(method return-process dead-pool)',
        '--header',str(port/'build/original_dead_pool.h')],check=True)
    subprocess.run(compiler+process_context+['--source',str(port/'dead_pool_fixture.gc'),
        '--function','dc-dead-pool-create','--function','dc-dead-pool-return',
        '--header',str(port/'build/dead_pool_fixture.h')],check=True)
    command=compiler+process_context+['--source',str(port/'process_new_fixture.gc'),
                                     '--header',str(port/'build/process_new_fixture.h')]
    for name in ('tree','process','match'): command+=['--function','dc-new-'+name]
    subprocess.run(command,check=True)
    command=compiler+process_context+['--source',str(port/'process_heap_fixture.gc'),
                                     '--header',str(port/'build/process_heap_fixture.h')]
    for name in ('create','read','tree'): command+=['--function','dc-process-heap-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'structured_static_fixture.gc'),
                      '--preload',str(port/'structured_static_context.gc'),
                      '--initializer','dc-structured-init','--header',str(port/'build/structured_static_fixture.h')]
    for name in ('get','get-plain','value','flags','child','serial','call','count','get-record',
                 'callback-root','callback-child','callback-plain'):
        command+=['--function','dc-structured-'+name]
    subprocess.run(command,check=True)
    subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--from-global','*kernel-context*','--prefix-through-global','*kernel-context*',
        '--initializer','dc-original-context-init','--header',str(port/'build/original_kernel_context.h')],check=True)
    subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--function','remove-exit','--function','(method new protect-frame)',
        '--header',str(port/'build/original_process_entry.h')],check=True)
    subprocess.run(compiler+process_context+['--source',str(port/'context_read_fixture.gc'),
        '--function','dc-context-read-loop','--header',str(port/'build/context_read_fixture.h')],check=True)
    command=compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--header',str(port/'build/original_process_query.h')]
    for name in ('process-by-name','process-not-name','process-count','anon-function-0','anon-function-1','anon-function-2'):
        command+=['--function',name]
    subprocess.run(command,check=True)
    for start,end,stem,init in (('*null-kernel-context*','*null-kernel-context*','original_null_context','dc-null-context-init'),
                              ('*global-search-name*','*global-search-count*','original_query_globals','dc-query-globals-init')):
        subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
            '--from-global',start,'--prefix-through-global',end,'--initializer',init,
            '--header',str(port/'build'/f'{stem}.h')],check=True)
    subprocess.run(compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gstring.gc'),
        '--preload',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
        '--preload',str(port.parents[1]/'goal_src/jak1/kernel/gstring-h.gc'),
        '--select-defuns','--function','string=','--function','name=',
        '--header',str(port/'build/original_name_compare.h')],check=True)
    command=compiler+['--source',str(port/'call_guard_fixture.gc'),'--header',str(port/'build/call_guard_fixture.h')]
    for name in ('identity','float-identity','effect','last','int','float','void','eight','dynamic'):
        command+=['--function','dc-call-guard-'+name]
    subprocess.run(command,check=True)
    subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--function','(method new cpu-thread)','--function','(method asize-of cpu-thread)',
        '--header',str(port/'build/original_cpu_thread.h')],check=True)
    subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--function','(method stack-size-set! thread)','--header',str(port/'build/original_thread_resize.h')],check=True)
    subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--function','set-to-run','--header',str(port/'build/original_set_to_run.h')],check=True)
    subprocess.run(compiler+['--source',str(port/'fiber_fixture.gc'),'--function','dc-fiber-inner',
        '--function','dc-fiber-main','--header',str(port/'build/fiber_fixture.h')],check=True)
    subprocess.run(compiler+['--source',str(port/'fiber_words_fixture.gc'),'--function','dc-fiber-words-six',
        '--function','dc-fiber-words-eight','--header',str(port/'build/fiber_words_fixture.h')],check=True)
    subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--function','(method activate process)','--header',str(port/'build/original_process_activate.h')],check=True)
    subprocess.run(compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
        '--select-defuns','--function','mem-set32!','--header',str(port/'build/original_memset32.h')],check=True)
    subprocess.run(compiler+process_context+['--preload',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--source',str(port.parents[1]/'goal_src/jak1/kernel/gstate.gc'),'--function','send-event-function',
        '--header',str(port/'build/original_send_event.h')],check=True)
    subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--function','(method deactivate process)','--header',str(port/'build/original_deactivate.h')],check=True)
    subprocess.run(compiler+process_context+['--preload',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--source',str(port.parents[1]/'goal_src/jak1/kernel/gstate.gc'),'--function','looping-code',
        '--header',str(port/'build/original_looping_code.h')],check=True)
    subprocess.run(compiler+process_context+['--preload',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--source',str(port.parents[1]/'goal_src/jak1/kernel/gstate.gc'),'--function','enter-state',
        '--header',str(port/'build/original_enter_state.h')],check=True)
    # The original scheduler under the explicit Dreamcast no-desktop-profiler context.
    dispatcher_context=[]
    for source in (port.parents[1]/'goal_src/jak1/kernel/gcommon.gc',port/'scalar_validation_context.gc',
                   port/'dreamcast_profiler_context.gc',port.parents[1]/'goal_src/jak1/kernel/gkernel-h.gc',
                   port.parents[1]/'goal_src/jak1/kernel/gstring-h.gc'):
        dispatcher_context+=['--preload',str(source)]
    subprocess.run(compiler+dispatcher_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--function','kernel-dispatcher','--function','anon-function-5','--function','(method run-logic? process)',
        '--function','(method delete thread)',
        '--header',str(port/'build/original_dispatcher.h')],check=True)
    stack_functions=['(method new dc-stack-record)']+['dc-stack-'+name for name in
        ('distinct','alignment','high-word','bounds','boxed','sum4','outgoing','touch','mixed','spill','plus-one','lexical','suspend')]
    for baseline in (False,True):
        stem='stack_objects_baseline' if baseline else 'stack_objects_fixture'
        command=compiler+process_context+['--preload',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
            '--source',str(port/'stack_objects_fixture.gc'),'--header',str(port/'build'/(stem+'.h')),
            '--initializer','dc-stack-baseline-init' if baseline else 'dc-stack-fixture-init']
        if baseline:command+=['--no-optimize']
        for name in stack_functions:command+=['--function',name]
        subprocess.run(command,check=True)
    subprocess.run(compiler+process_context+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--function','run-function-in-process','--function','throw','--function','(method new protect-frame)',
        '--header',str(port/'build/original_initialization.h')],check=True)
    subprocess.run(compiler+process_context+['--preload',str(port.parents[1]/'goal_src/jak1/kernel/gkernel.gc'),
        '--source',str(port/'initialization_fixture.gc'),'--function','dc-initialize-return',
        '--function','dc-initialize-throw','--function','dc-initialize-protect','--function','dc-initialize-error','--function','dc-initialize-die',
        '--function','dc-initialize-nested','--function','dc-initialize-scoped',
        '--header',str(port/'build/initialization_fixture.h')],check=True)
    subprocess.run([sys.executable,str(port/'build_dreamcast.py'),'--msys',str(args.msys),'--target','jak-dc-runtime-goal.elf'],check=True)
    if args.verify:
        subprocess.run([sys.executable,str(port/'verify_backend.py'),str(args.flycast),
                        str(port/'jak-dc-runtime-goal.elf'),'--nm',str(args.nm),'--output',str(args.output),
                        '--tests','24844','--suite','runtime-goal'],check=True)

if __name__=='__main__': main()
