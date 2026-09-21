#!/usr/bin/env python3
"""Run fixed native references -> evolve -> measure -> measured-parent feedback.
No model/API dependency. Requires CMake + a C++20 compiler; Rust is optional unless
--require-rust is selected. All results are local, versioned and non-overwriting.
"""
import argparse
import csv
import hashlib
import json
import math
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
import uuid
import webbrowser
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'sort'))
from generate import emit, unpack, body
from report import write_report
WORKLOADS=('random','sorted','reverse','duplicates','nearly_sorted')

def execute(command, logfile, callback=None):
    command=list(map(str,command))
    with Path(logfile).open('a',encoding='utf8') as log:
        log.write('\nCOMMAND '+json.dumps(command)+'\n');log.flush()
        p=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf8',errors='replace')
        try:
            for line in p.stdout:
                print(line,end='',flush=True);log.write(line);log.flush()
                if callback: callback(line)
            code=p.wait()
            if code: raise RuntimeError(f'Command failed ({code}); see {logfile}')
        except BaseException:
            p.terminate()
            try:p.wait(timeout=5)
            except subprocess.TimeoutExpired:p.kill();p.wait()
            raise

def executable(build,name):
    for f in (Path(build)/'Release'/(name+'.exe'),Path(build)/(name+'.exe'),Path(build)/name):
        if f.is_file():return f
    raise RuntimeError(f'Executable {name} not found in {build}')

def parse_samples(file):
    samples={}
    with Path(file).open(newline='',encoding='utf8') as f:
        for row in csv.DictReader(f):
            value=float(row['nanoseconds_per_sort'])
            if not math.isfinite(value) or value<=0:raise RuntimeError('invalid timing sample')
            samples.setdefault(row['name'],{}).setdefault(row['workload'],{})[int(row['trial'])]=value
    for entry in samples.values():
        if set(entry)!=set(WORKLOADS):raise RuntimeError('missing workload')
    return samples

def median(samples,name,workload):return statistics.median(samples[name][workload].values())

def metrics(samples):
    refs=[n for n in samples if '_candidate_' not in n]
    output=[]
    for name in samples:
        for w in WORKLOADS:
            base=min(median(samples,r,w) for r in refs)
            ns=median(samples,name,w)
            output.append({'name':name,'workload':w,'ns':round(ns,3),'ratio':f'{base/ns:.3f}x'})
    return output

def ranking(samples):
    candidates=[n for n in samples if '_candidate_' in n]
    return sorted(candidates,key=lambda n:sum(math.log(median(samples,n,w)) for w in WORKLOADS)/5)

def decide(samples,winner):
    """Predeclared local acceptance rule; not a universal performance theorem.
    Fresh paired samples: >5% advantage, one-sided sign tail with Bonferroni
    correction across all reference/workload checks. Machine noise can correlate
    samples, so a pass must still be replicated in an independent machine session.
    """
    refs=[n for n in samples if '_candidate_' not in n]
    results=[];alpha=.05/(len(refs)*len(WORKLOADS))
    for ref in refs:
        for w in WORKLOADS:
            a=samples[ref][w];b=samples[winner][w]
            if set(a)!=set(b):raise RuntimeError('unpaired confirmation samples')
            ratios=[a[t]/b[t] for t in a];wins=sum(r>1.05 for r in ratios);n=len(ratios)
            p=sum(math.comb(n,i) for i in range(wins,n+1))/(2**n)
            results.append({'reference':ref,'workload':w,'median_speed_ratio':statistics.median(ratios),
                            'wins_over_5_percent':wins,'trials':n,'sign_tail':p,
                            'passes':statistics.median(ratios)>1.05 and p<alpha})
    cpp=all(x['passes'] for x in results if x['reference'].startswith('cpp_'))
    rust_present=any(r.startswith('rust_') for r in refs)
    both=rust_present and all(x['passes'] for x in results)
    return {'cpp_goal_met':cpp,'combined_cpp_rust_goal_met':both,'rust_measured':rust_present,
            'bonferroni_alpha':alpha,'checks':results,
            'limitation':'Local batched-throughput evidence; replicate in a new session. This does not rank programming languages universally.'}

def native_build(folder,candidates,rustc,log):
    variants=emit(folder,candidates);folder=Path(folder)
    cmake=f'''cmake_minimum_required(VERSION 3.20)
project(DriftSortNative LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(sortbench "{(ROOT/'sort'/'bench.cpp').as_posix()}")
target_include_directories(sortbench PRIVATE "{folder.as_posix()}")
target_link_libraries(sortbench PRIVATE ${{CMAKE_DL_LIBS}})
'''
    (folder/'CMakeLists.txt').write_text(cmake,encoding='utf8')
    build=folder/'build'
    execute(['cmake','-S',folder,'-B',build,'-DCMAKE_BUILD_TYPE=Release'],log)
    execute(['cmake','--build',build,'--config','Release','--parallel','2'],log)
    rustlib=None
    if rustc:
        suffix='.dll' if os.name=='nt' else '.dylib' if sys.platform=='darwin' else '.so'
        rustlib=folder/('driftsort_rust'+suffix)
        execute([rustc,'--edition=2021','--crate-type=cdylib','-C','opt-level=3','-C','panic=abort',folder/'generated.rs','-o',rustlib],log)
    return executable(build,'sortbench'),rustlib,variants

def benchmark(binary,rustlib,file,seed,rows,trials,log,only=None):
    cmd=[binary,'--out',file,'--seed',seed,'--rows',rows,'--trials',trials]
    if rustlib:cmd+=['--rustlib',rustlib]
    if only:cmd+=['--only',only]
    execute(cmd,log)
    return parse_samples(file)

def candidates(file,limit):
    with Path(file).open(newline='',encoding='utf8') as f:
        rows=list(csv.DictReader(f,delimiter='\t'))
    # Archive is proxy-ranked; the native tournament, not this proxy, selects winner.
    return rows[:limit]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--births',type=int,default=10000000,help='total NEW offspring across all phases')
    p.add_argument('--rounds',type=int,default=3)
    p.add_argument('--seed',type=int,default=1)
    p.add_argument('--rows',type=int,default=32768)
    p.add_argument('--trials',type=int,default=15)
    p.add_argument('--confirm-trials',type=int,default=31)
    p.add_argument('--finalists',type=int,default=6)
    p.add_argument('--out',type=Path)
    p.add_argument('--resume',type=Path,help='completed search checkpoint; flags/seed must match')
    p.add_argument('--no-modules',action='store_true')
    p.add_argument('--no-drift',action='store_true')
    p.add_argument('--no-browser',action='store_true')
    p.add_argument('--require-rust',action='store_true')
    a=p.parse_args()
    if not (1<=a.rounds<=100 and a.rounds<=a.births<=100000000000 and 0<=a.seed<2**64 and 256<=a.rows<=1048576
            and 1<=a.trials<=101 and 1<=a.confirm_trials<=101 and 1<=a.finalists<=16):p.error('invalid run limits')
    if a.resume and not a.resume.is_file():p.error('checkpoint does not exist')
    rustc=shutil.which('rustc')
    if a.require_rust and not rustc:p.error('rustc is missing. Install Rust, or omit --require-rust for a clearly labeled C++-only experiment.')
    root=(a.out or ROOT/'out'/('sort-goal-'+time.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6])).resolve()
    root.mkdir(parents=True,exist_ok=False)
    log=root/'run.log'
    manifest={k:str(v) if isinstance(v,Path) else v for k,v in vars(a).items()}
    manifest.update({'version':'DriftSort-0.3','python':sys.version,'platform':platform.platform(),
                     'processor':platform.processor() or os.environ.get('PROCESSOR_IDENTIFIER','unreported'),
                     'rustc':rustc or 'NOT MEASURED','start_time':time.strftime('%Y-%m-%dT%H:%M:%S%z'),
                     'warm_start':'human 28-comparator insertion network; 19-comparator reference is not a founder',
                     'goal':'at least 5% faster than every measured reference on all five held-out workloads; adjusted sign-tail threshold',
                     'source_sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in (ROOT/'sort').glob('*') if f.is_file()}})
    try:manifest['revision']=subprocess.check_output(['git','-C',ROOT,'rev-parse','HEAD'],text=True,stderr=subprocess.DEVNULL).strip()
    except (OSError,subprocess.CalledProcessError):manifest['revision']='unversioned'
    if rustc:manifest['rust_version']=subprocess.check_output([rustc,'--version','--verbose'],text=True)
    (root/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
    state={'phase':'Baseline','births':0,'rust':'Enabled' if rustc else 'NOT MEASURED — rustc absent',
           'message':'Building and measuring frozen reference routines before evolution.'}
    write_report(root,state)
    if not a.no_browser:webbrowser.open((root/'report.html').as_uri())
    print(f'RESULTS: {root}\nReport: {root / "report.html"}',flush=True)
    print('Rust: '+('enabled' if rustc else 'SKIPPED. No claim of beating Rust will be made.'),flush=True)
    try:
        build=root/'engine-build'
        execute(['cmake','-S',ROOT/'sort','-B',build,'-DCMAKE_BUILD_TYPE=Release'],log)
        execute(['cmake','--build',build,'--config','Release','--parallel','2'],log)
        execute(['ctest','--test-dir',build,'-C','Release','--output-on-failure'],log)
        engine=executable(build,'driftsort')
        baseline=root/'baseline'
        binary,rlib,variants=native_build(baseline,[],rustc,log)
        samples=benchmark(binary,rlib,baseline/'timings.csv',1001,a.rows,a.trials,log)
        state.update(metrics=metrics(samples),measurement_note='Initial frozen references; the same references are remeasured alongside every candidate.')
        write_report(root,state)
        resume=a.resume.resolve() if a.resume else None
        feedback=(resume.parent/'native-parents.txt') if resume and (resume.parent/'native-parents.txt').is_file() else None
        completed=0;all_screenings=[];previous_champion=None
        for round_number in range(1,a.rounds+1):
            n=a.births//a.rounds+(round_number<=a.births%a.rounds)
            phase=root/f'search-{round_number}'
            state.update(phase=f'Evolving {round_number}/{a.rounds}',message='Exact correctness and cost-proxy search. Native speed is measured at the next phase boundary.')
            write_report(root,state)
            cmd=[engine,'--births',n,'--seed',a.seed,'--report-every',min(100000,n),'--out',phase]
            if resume:cmd+=['--resume',resume]
            if feedback:cmd+=['--feedback',feedback]
            if a.no_modules:cmd+=['--no-modules']
            if a.no_drift:cmd+=['--no-drift']
            def progress(line):
                m=re.match(r'birth=(\d+)',line)
                if m:state['births']=int(m.group(1));write_report(root,state)
            execute(cmd,log,progress);completed+=n;resume=phase/'checkpoint.txt'
            chosen=candidates(phase/'candidates.tsv',a.finalists)
            if previous_champion and all(x['id']!=previous_champion['id'] for x in chosen):
                chosen.append(previous_champion)
            if not chosen:raise RuntimeError('No certified candidates; cannot benchmark or claim success.')
            state.update(phase=f'Native tournament {round_number}/{a.rounds}',message='Compiling candidates and comparing actual machine timings on matched workloads.')
            write_report(root,state)
            native=root/f'native-{round_number}'
            binary,rlib,variants=native_build(native,chosen,rustc,log)
            samples=benchmark(binary,rlib,native/'tuning.csv',11000+round_number,a.rows,a.trials,log)
            ranked=ranking(samples)
            winner=ranked[0]
            previous_champion=variants[winner]['specimen']
            state.update(winner=winner,chosen=variants[winner],metrics=metrics(samples),measurement_note='Tuning measurements. These are not the final held-out confirmation.')
            write_report(root,state)
            selected=[];ids=set()
            for name in ranked:
                sp=variants[name]['specimen']
                if sp['id'] not in ids:
                    selected.append(sp);ids.add(sp['id'])
                if len(selected)==4:break
            feedback=phase/'native-parents.txt'
            feedback.write_text(''.join(f"{s['id']} {s['parent']} {s['birth']} {s['genome']}\n" for s in selected),encoding='utf8')
            all_screenings.append({'phase':round_number,'winner':winner,'selected_parents':[s['id'] for s in selected]})
        # Freeze one tuning-selected finalist before touching the confirmation set.
        (root/'selection.json').write_text(json.dumps({'winner':winner,'specimen':variants[winner],'tournaments':all_screenings},indent=2),encoding='utf8')
        state.update(phase='Independent confirmation',message='The finalist is frozen. Fresh input seed and fresh timings; no selecting a different winner from these results.')
        write_report(root,state)
        confirm=benchmark(binary,rlib,root/'confirmation.csv',0xface0001,a.rows,a.confirm_trials,log,only=winner)
        verdict=decide(confirm,winner)
        (root/'verdict.json').write_text(json.dumps(verdict,indent=2),encoding='utf8')
        (root/'winner.genome').write_text(variants[winner]['specimen']['genome']+'\n',encoding='utf8')
        # Standalone routines, in addition to the complete audited native tournament sources.
        chosen_entry=variants[winner]
        (root/'winner.cpp').write_text('#include <algorithm>\n#include <cstdint>\nvoid drift_sort8(uint32_t (&v)[8]) {\n'+body(chosen_entry['flat'],chosen_entry['style'])+'\n}\n',encoding='utf8')
        (root/'winner.rs').write_text('#[allow(unused_mut,unused_assignments)]\npub fn drift_sort8(v: &mut [u32;8]) {\n'+body(chosen_entry['flat'],chosen_entry['style'],True)+'\n}\n',encoding='utf8')
        outcome='COMBINED GOAL MET on this machine/workload' if verdict['combined_cpp_rust_goal_met'] else 'C++ GOAL MET; combined Rust goal not met' if verdict['cpp_goal_met'] else 'GOAL NOT MET — keep the reference implementation'
        state.update(phase='Complete',message=outcome,verdict=outcome+'\n'+verdict['limitation'],metrics=metrics(confirm),
                     measurement_note='Held-out confirmation. Ratios use the fastest measured reference for each workload. Above 1x favors the evolved candidate.')
        write_report(root,state)
        print('\n'+outcome+f'\nFull results: {root}\nNext checkpoint: {resume}\n',flush=True)
    except BaseException as exc:
        state.update(phase='Stopped',message=str(exc) or 'Interrupted. Only completed-phase checkpoints are resumable.')
        write_report(root,state)
        raise

if __name__=='__main__':
    try:main()
    except KeyboardInterrupt:print('\nStopped. Completed phase checkpoints remain; the current unfinished phase is not resumable.',file=sys.stderr);sys.exit(130)
    except Exception as exc:print(f'error: {exc}',file=sys.stderr);sys.exit(1)
