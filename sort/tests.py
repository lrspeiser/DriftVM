#!/usr/bin/env python3
"""Determinism, replay, controls, independent interpreter and decision tests."""
import csv
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import generate


def run(exe, folder, n=1000, extra=(), good=True):
    p=subprocess.run([str(exe),'--births',str(n),'--report-every','500','--out',str(folder),*map(str,extra)],capture_output=True,text=True)
    assert (p.returncode==0)==good,(p.stdout,p.stderr)
    return p


def main():
    exe=Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='driftsort-tests-') as directory:
        root=Path(directory)
        run(exe,root/'whole',2000)
        run(exe,root/'part',1000)
        run(exe,root/'resume',1000,('--resume',root/'part'/'checkpoint.txt'))
        assert (root/'whole'/'checkpoint.txt').read_bytes()==(root/'resume'/'checkpoint.txt').read_bytes(),'resume changed trajectory'
        for file in (root/'whole').glob('candidates.tsv'):
            rows=list(csv.DictReader(file.open(),delimiter='\t'))
            assert rows
            for r in rows:
                g=generate.unpack(r['genome']);assert generate.certify(g['flat'])
        run(exe,root/'whole',good=False)
        run(exe,root/'bad-resume',extra=('--resume',root/'part'/'checkpoint.txt','--no-modules'),good=False)
        run(exe,root/'controls',extra=('--no-modules','--no-drift'))
        for r in csv.DictReader((root/'controls'/'candidates.tsv').open(),delimiter='\t'):
            assert not generate.unpack(r['genome'])['blocks']
        variants=generate.emit(root/'generated',rows[:2]);assert variants and (root/'generated'/'generated.rs').exists()
        assert generate.certify(generate.insertion()) and generate.certify(generate.network19())
        # An all-zero output is sorted but loses input multiplicities: reject it.
        assert not generate.certify([192+i*8 for i in range(8)])
        for text in ('0 0','1 999 0','1 256 1 2 256 0','1 -1 0','1 0 0 junk'):
            try:generate.unpack(text)
            except ValueError:pass
            else:raise AssertionError('bad genome accepted')
        spec=importlib.util.spec_from_file_location('sort_goal',Path(__file__).resolve().parents[1]/'scripts'/'sort_goal.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        def samples(fast):
            return {name:{w:{i:value for i in range(31)} for w in module.WORKLOADS}
                    for name,value in [('cpp_std',10.),('cpp_network19_minmax',8.),('cpp_candidate_1_minmax',fast)]}
        assert not module.decide(samples(9.),'cpp_candidate_1_minmax')['cpp_goal_met']
        good=module.decide(samples(5.),'cpp_candidate_1_minmax')
        assert good['cpp_goal_met'] and not good['combined_cpp_rust_goal_met'],'missing Rust must never be reported beaten'
        all_present=samples(5.);all_present['rust_std']=all_present['cpp_std']
        assert module.decide(all_present,'cpp_candidate_1_minmax')['combined_cpp_rust_goal_met']
    print('sort-integration: PASS')

if __name__=='__main__':main()
