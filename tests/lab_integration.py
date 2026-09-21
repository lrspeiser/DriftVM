import csv
import importlib.util
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading
import urllib.request
import urllib.error
from http.server import ThreadingHTTPServer

ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('lab',ROOT/'scripts/lab.py')
lab=importlib.util.module_from_spec(spec);spec.loader.exec_module(lab)
exe,tests=sys.argv[1:]
def check(condition,message):
    if not condition:raise AssertionError(message)
def execute(args,ok=True):
    p=subprocess.run([exe]+args,capture_output=True,text=True)
    check((p.returncode==0)==ok,p.stdout+p.stderr)
    return p
with tempfile.TemporaryDirectory() as td:
    root=pathlib.Path(td)
    for name,flags in [('one',[]),('two',[]),('control',['--no-modules','--no-drift'])]:
        execute(['--births','1500','--population','32','--seed','1','--report-every','500','--out',str(root/name)]+flags)
    a=lab.Store(root/'one').read();b=lab.Store(root/'two').read();c=lab.Store(root/'control').read()
    check(a['complete'] and a['birth']==1500,'incomplete run')
    check(a['organisms']==b['organisms'],'seed not deterministic')
    check((root/'one/lineage-modules.tsv').read_bytes()==(root/'two/lineage-modules.tsv').read_bytes(),'lineage not deterministic')
    check(all(x['blocks']==0 and x['pool']!='drift' for x in c['organisms']),'ablation not applied')
    for organism in a['organisms']:
        saved=root/(organism['id']+'.genome')
        lab.replay(root/'one/lineage-modules.tsv',organism['id'],saved)
        check(saved.read_text().splitlines()[1]==organism['genome'],'replay mismatch')
    before=(root/'one/config.txt').read_bytes()
    execute(['--out',str(root/'one')],False)
    check((root/'one/config.txt').read_bytes()==before,'old output overwritten')
    for args in [['--births','-1'],['--report-every','0'],['--population','0'],['--births','oops']]:execute(args,False)
    # A partial writer file must never displace a complete snapshot.
    (root/'one/state-999999999999.json.tmp').write_text('{')
    check(lab.Store(root/'one').read()['complete'],'partial snapshot used')
    server=ThreadingHTTPServer(('127.0.0.1',0),lab.handler(lab.Store(root/'one')))
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    url='http://127.0.0.1:'+str(server.server_port)
    for path in ['/','/api/state','/vm.js','/story.json']:
        with urllib.request.urlopen(url+path) as r:check(r.status==200,'HTTP failed')
    for path,headers,expected in [('/../src/core.hpp',{},404),('/api/state',{'Host':'evil.example'},403),('/api/state',{'Origin':'http://evil.example'},403)]:
        try:urllib.request.urlopen(urllib.request.Request(url+path,headers=headers));raise AssertionError('unsafe route accepted')
        except urllib.error.HTTPError as e:check(e.code==expected,'wrong rejection')
    server.shutdown();server.server_close();thread.join()
    # Archive compatibility without claiming synthetic test data are user results.
    old=root/'old';old.mkdir();(old/'discoveries').mkdir()
    (old/'config.txt').write_text('version=Cambrian-0.1\nbirths=0\nseed=1\n')
    sample=a['organisms'][0];genome=sample['genome'].split('|')[0]
    # Old specimens have no modules; use the original case-study instead.
    genome=json.loads((ROOT/'examples/min-transition.json').read_text())['original']['child']['genome']
    (old/'population-0.tsv').write_text('id\tparent\tbirth\tdepth\tpool\tcandidate_mask\tgenome\n1\t0\t0\t0\tperformance\t64\t'+genome+'\n')
    (old/'summary.txt').write_text('test fixture')
    check(lab.Store(old).read()['organisms'][0]['genome']==genome,'old archive incompatible')
    node=shutil.which('node')
    if node:
        vectors=subprocess.check_output([tests,'--vectors'],text=True)
        program="const fs=require('fs'),v=require(process.argv[1]);let n=0;for(const line of fs.readFileSync(0,'utf8').trim().split('\\n')){const [g,a,b,out]=line.split('\\t');if(v.run(v.decode(g),+a,+b).output!==+out)throw Error('VM disagreement at '+n);n++;}console.log(n+' cross-VM cases passed');"
        subprocess.run([node,'-e',program,str(ROOT/'web/vm.js')],input=vectors,text=True,check=True)
    else:print('Node unavailable: browser-VM cross-check skipped, not claimed as passed.')
print('PASS: deterministic runs, controls, all final lineage replay, safe outputs, loopback HTTP, archived-run parsing, browser VM cross-check when Node is present.')
