"""Derive Jak 1 runtime constants from pinned upstream headers (AI-assisted)."""
import argparse
import hashlib
from pathlib import Path
import re

def generate(repo):
    constants=(repo/'common/goal_constants.h').read_text()
    for name,expected in (('BASIC_OFFSET',4),('PAIR_OFFSET',2),('POINTER_SIZE',4),('STRUCTURE_ALIGNMENT',16)):
        value=int(re.search(r'constexpr \w+ '+name+r'\s*=\s*(\d+)\s*;',constants)[1])
        if value!=expected: raise ValueError('Runtime layout requires a new audit: '+name)
    symbols=(repo/'common/symbols.h').read_text()
    jak1=constants.split('namespace jak1 {',1)[1].split('namespace jak2 {',1)[0]
    count=int(re.search(r'constexpr s32 GOAL_MAX_SYMBOLS\s*=\s*(\d+)\s*;',jak1)[1])
    if count<1024 or count&(count-1): raise ValueError('symbol capacity must be a power of two')
    fixed=symbols.split('namespace jak1_symbols {',1)[1].split('namespace jak2_symbols {',1)[0]
    values=re.findall(r'constexpr int (\w+)\s*=\s*(-?(?:0x[0-9a-fA-F]+|\d+))\s*;',fixed)
    lines=['/* Generated from upstream Jak 1 headers (AI-assisted). */','#ifndef DC_GOAL_LAYOUT_H',
           '#define DC_GOAL_LAYOUT_H','#include <stdint.h>','#include <stddef.h>',
           f'#define DC_GOAL_MAX_SYMBOLS {count}u',
           '#define DC_GOAL_SYMBOL_BYTES (DC_GOAL_MAX_SYMBOLS*16u)',
           '#define DC_GOAL_SYM_INFO_OFFSET (DC_GOAL_MAX_SYMBOLS*8u-4u)',
           '#define DC_GOAL_SYM_TO_STRING_OFFSET (DC_GOAL_MAX_SYMBOLS*8u)',
           '#define DC_GOAL_BASIC_OFFSET 4u','#define DC_GOAL_PAIR_OFFSET 2u','enum {']
    lines+=['  DC_'+name+' = '+value+',' for name,value in values]
    lines+=['};',
            '/* game/kernel/jak1/kscheme.h: Type fields, followed by 32-bit methods. */',
            'typedef struct {',
            '  uint32_t symbol,parent;',
            '  uint16_t allocated_size,padded_size,heap_base,num_methods;',
            '} dc_goal_type_fields;',
            'typedef char dc_goal_type_size_check[sizeof(dc_goal_type_fields)==16?1:-1];',
            'typedef char dc_goal_type_methods_check[offsetof(dc_goal_type_fields,num_methods)==14?1:-1];']
    for name in ('common/goal_constants.h','common/symbols.h','game/kernel/jak1/kscheme.h'):
        lines.append('/* '+name+' SHA256 '+hashlib.sha256((repo/name).read_bytes()).hexdigest()+' */')
    lines+=['#endif','']
    return '\n'.join(lines)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository',type=Path,default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().with_name('goal_layout.h'))
    args=parser.parse_args(); args.output.write_text(generate(args.repository))
