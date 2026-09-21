"""Checked translation of a bounded sorting DSL into ordinary C++ and Rust.
Only numeric genomes are read; no input text is evaluated or executed as source.
"""
import json
from pathlib import Path

OPS = ('CX', 'MIN', 'MAX', 'COPY')

def unpack(text):
    tokens = text.split()
    if len(tokens) > 256 or any(not t.isascii() or not t.isdecimal() for t in tokens):
        raise ValueError('invalid genome integers')
    values = iter(map(int, tokens))
    def sequence(lo, hi):
        n = next(values)
        if not lo <= n <= hi: raise ValueError('invalid sequence size')
        return [next(values) for _ in range(n)]
    try:
        program = sequence(1, 64)
        n = next(values)
        if not 0 <= n <= 16: raise ValueError('too many blocks')
        blocks = [sequence(2, 8) for _ in range(n)]
        if next(values, None) is not None: raise ValueError('trailing data')
    except StopIteration as exc:
        raise ValueError('truncated genome') from exc
    for i, block in enumerate(blocks):
        if any(t >= 256 + i for t in block): raise ValueError('forward/cyclic block')
    flat = []
    def expand(seq, depth=0):
        if depth > 8: raise ValueError('too much nesting')
        for t in seq:
            if t < 256:
                flat.append(t)
                if len(flat) > 64: raise ValueError('expanded limit')
            else:
                if t-256 >= len(blocks): raise ValueError('missing block')
                expand(blocks[t-256], depth+1)
    expand(program)
    return {'program': program, 'blocks': blocks, 'flat': flat}

def execute(flat, row):
    row = list(row)
    for t in flat:
        k, a, b = t//64, (t//8)%8, t%8
        x, y = row[a], row[b]
        if k == 0: row[a], row[b] = min(x,y), max(x,y)
        elif k == 1: row[a] = min(x,y)
        elif k == 2: row[a] = max(x,y)
        else: row[a] = y
    return row

def certify(flat):
    for bits in range(256):
        row = [(bits>>i)&1 for i in range(8)]
        if execute(flat,row) != sorted(row): return False
    return True

def insertion(): return [8*(j-1)+j for i in range(1,8) for j in range(i,0,-1)]

def network19():
    pairs=[]
    def merge(lo,n,r):
        m=r*2
        if m<n:
            merge(lo,n,m); merge(lo+r,n,m)
            for i in range(lo+r,lo+n-r,m): pairs.append(8*i+i+r)
        else: pairs.append(8*lo+lo+r)
    def sort(lo,n):
        if n>1: sort(lo,n//2); sort(lo+n//2,n//2); merge(lo,n,1)
    sort(0,8)
    assert len(pairs)==19 and certify(pairs)
    return pairs

def body(flat, style, rust=False):
    lines=[f'{"let mut " if rust else "uint32_t "}x{i} = v[{i}];' for i in range(8)]
    for t in flat:
        kind,a,b=t//64,(t//8)%8,t%8
        x,y=f'x{a}',f'x{b}'
        if kind==0 and style=='branch':
            if rust: lines.append(f'if {x} > {y} {{ let tmp={x}; {x}={y}; {y}=tmp; }}')
            else: lines.append(f'if ({x} > {y}) {{ auto tmp={x}; {x}={y}; {y}=tmp; }}')
        elif kind==0:
            if rust: lines.append(f'{{ let tmp={x}.min({y}); {y}={x}.max({y}); {x}=tmp; }}')
            else: lines.append(f'{{ auto tmp=std::min({x},{y}); {y}=std::max({x},{y}); {x}=tmp; }}')
        elif kind in (1,2):
            method='min' if kind==1 else 'max'
            lines.append(f'{x}={x}.{method}({y});' if rust else f'{x}=std::{method}({x},{y});')
        else: lines.append(f'{x}={y};')
    lines.extend(f'v[{i}]=x{i};' for i in range(8))
    return '\n'.join(lines)

def emit(directory, candidates):
    directory=Path(directory); directory.mkdir(parents=True,exist_ok=True)
    variants={
        'cpp_std': {'reference':True,'kind':'std'},
        'cpp_insertion': {'reference':True,'kind':'insertion'},
        'cpp_network19_minmax': {'reference':True,'flat':network19(),'style':'minmax'},
        'cpp_network19_branch': {'reference':True,'flat':network19(),'style':'branch'},
        'cpp_seed28': {'reference':True,'flat':insertion(),'style':'minmax'},
        'rust_std': {'reference':True,'kind':'std'},
        'rust_network19_minmax': {'reference':True,'flat':network19(),'style':'minmax'},
        'rust_network19_branch': {'reference':True,'flat':network19(),'style':'branch'},
    }
    for specimen in candidates:
        g=unpack(specimen['genome'])
        if not certify(g['flat']): raise ValueError('candidate failed independent binary verification')
        for lang in ('cpp','rust'):
            for style in ('minmax','branch'):
                name=f"{lang}_candidate_{int(specimen['id'])}_{style}"
                variants[name]={'reference':False,**g,'style':style,'specimen':specimen}
    cpp=['#pragma once\n#ifdef _MSC_VER\n#define DS_NOINLINE __declspec(noinline)\n#else\n#define DS_NOINLINE __attribute__((noinline))\n#endif\n']
    rs=['#![allow(unused_mut, unused_variables, unused_assignments)]\n']
    for name,entry in variants.items():
        rust=name.startswith('rust_')
        if entry.get('kind')=='std':
            text='v.sort_unstable();' if rust else 'std::sort(v.begin(),v.end());'
        elif entry.get('kind')=='insertion':
            text='for (unsigned i=1;i<8;++i) {auto x=v[i];auto j=i;while(j>0 && v[j-1]>x){v[j]=v[j-1];--j;}v[j]=x;}'
        else: text=body(entry['flat'],entry['style'],rust)
        if rust:
            rs.append(f'#[inline(always)]\nfn one_{name}(v: &mut [u32;8]) {{\n{text}\n}}\n'
                      f'/// Caller supplies n valid, writable, nonoverlapping eight-u32 rows.\n'
                      f'#[no_mangle]\n#[inline(never)]\npub unsafe extern "C" fn {name}(data: *mut [u32;8], n: usize) {{\n'
                      f'for v in std::slice::from_raw_parts_mut(data,n) {{ one_{name}(v); }}\n}}\n')
        else:
            cpp.append(f'inline void one_{name}(Row& v) {{\n{text}\n}}\n'
                       f'DS_NOINLINE void {name}(Row* v,std::size_t n) {{for(std::size_t i=0;i<n;++i)one_{name}(v[i]);}}\n')
    cpp.append('inline std::vector<Variant> cpp_variants(){return {'+','.join('{"'+n+'",'+n+'}' for n in variants if n.startswith('cpp_'))+'};}\n')
    cpp.append('inline std::vector<std::string> rust_names(){return {'+','.join('"'+n+'"' for n in variants if n.startswith('rust_'))+'};}\n')
    (directory/'generated.hpp').write_text('\n'.join(cpp),encoding='utf8')
    (directory/'generated.rs').write_text('\n'.join(rs),encoding='utf8')
    (directory/'variants.json').write_text(json.dumps(variants,indent=2),encoding='utf8')
    return variants
