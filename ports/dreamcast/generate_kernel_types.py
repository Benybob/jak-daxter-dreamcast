"""Extract original fixed-type metadata, without desktop function bodies (AI-assisted)."""
import argparse
import hashlib
import json
from pathlib import Path
import re

def schema(repo):
    path=repo/'game/kernel/jak1/kscheme.cpp'
    source=path.read_text()
    pattern=(r'set_fixed_type\(\s*(FIX_SYM_\w+),\s*"([^"]+)",\s*'
             r'\(s7\s*\+\s*(FIX_SYM_\w+)\)\.cast<Symbol>\(\),\s*'
             r'pack_type_flag\(\s*(0x[0-9a-fA-F]+|\d+),\s*'
             r'(0x[0-9a-fA-F]+|\d+),\s*(0x[0-9a-fA-F]+|\d+)\)')
    matches=re.findall(pattern,source)
    calls=re.findall(r'(?m)^\s+set_fixed_type\(',source)
    if not matches or len(matches)!=len(calls): raise ValueError('original type setup needs a new schema audit')
    entries=[]; seen=set()
    for offset,name,parent,methods,heap,size in matches:
        if offset in seen or (parent not in seen and parent!=offset):
            raise ValueError('duplicate or unordered original fixed type')
        seen.add(offset)
        methods,heap,size=(int(n,0) for n in (methods,heap,size))
        if not 2<=methods<127 or size>65520 or heap>65535: raise ValueError('unsupported original type flags')
        entries.append(dict(offset=offset,name=name,parent=parent,methods=methods,heap_base=heap,size=size))
    placeholders=[dict(offset=offset,name=name) for offset,name in
                  re.findall(r'set_fixed_symbol\(\s*(FIX_SYM_\w+),\s*"([^"]+)",\s*0\s*\);',source)]
    return entries,placeholders

def generate(repo):
    entries,placeholders=schema(repo)
    lines=['/* Original Jak 1 fixed-type schema (AI-assisted). */','#ifndef DC_KERNEL_TYPES_DATA_H',
           '#define DC_KERNEL_TYPES_DATA_H','#include "goal_runtime.h"',
           'typedef struct { uint32_t offset,parent; const char *name; uint16_t methods,heap_base,size; } dc_kernel_type_schema;',
           'static const dc_kernel_type_schema dc_kernel_types[] = {']
    for e in entries:
        lines.append('{DC_'+e['offset']+',DC_'+e['parent']+','+json.dumps(e['name'])+
                     f",{e['methods']}u,{e['heap_base']}u,{e['size']}u"+'},')
    lines+=['};','typedef struct { uint32_t offset; const char *name; } dc_kernel_placeholder;',
            'static const dc_kernel_placeholder dc_kernel_placeholders[] = {']
    for e in placeholders: lines.append('{DC_'+e['offset']+','+json.dumps(e['name'])+'},')
    lines+=['};',f'#define DC_KERNEL_TYPE_COUNT {len(entries)}u',
            f'#define DC_KERNEL_PLACEHOLDER_COUNT {len(placeholders)}u']
    for file in ('game/kernel/jak1/kscheme.cpp','common/symbols.h'):
        lines.append('/* '+file+' SHA256 '+hashlib.sha256((repo/file).read_bytes()).hexdigest()+' */')
    return '\n'.join(lines+['#endif',''])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository',type=Path,default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output',type=Path,default=Path(__file__).with_name('kernel_types_data.h'))
    args=parser.parse_args(); args.output.write_text(generate(args.repository))
    entries,placeholders=schema(args.repository)
    print(len(entries),'original fixed types,',len(placeholders),'original zero-valued placeholders')
if __name__=='__main__': main()
