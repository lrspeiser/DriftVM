#!/usr/bin/env python3
"""Read-only, loopback-only viewer. Python standard library; no cloud service.

Launch a new engine with --launch EXE, or view an existing Cambrian-0.1/0.2 run.
The browser reads completed snapshots, never the growing multi-million-row lineage.
"""
from __future__ import annotations
import argparse
import csv
import io
import json
import re
import signal
import subprocess
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
TASKS = ['XOR','ADD','SUB','AND','OR','MAX','MIN','EQ','PARITY','ROTATE_XOR']


def text(path: Path, limit: int = 2_000_000) -> str:
    with path.open('rb') as f:
        data = f.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Viewer input exceeds safe size limit')
    return data.decode('utf-8-sig')


def tail_csv(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open('rb') as f:
        header = f.readline().decode('utf-8-sig').strip()
        start = f.tell()
        size = path.stat().st_size
        if size - start > 1_000_000:
            f.seek(size - 1_000_000)
            f.readline()  # discard partial leading record
        chunk = f.read(1_000_000)
    # A currently written final line is not a completed observation.
    data = chunk[:chunk.rfind(b'\n') + 1].decode('utf-8')
    return list(csv.DictReader(io.StringIO(header + '\n' + data)))[-5000:]


def config(run: Path) -> dict:
    p = run / 'config.txt'
    return dict(line.split('=', 1) for line in text(p).splitlines() if '=' in line) if p.exists() else {}


def old_state(run: Path) -> dict:
    cfg = config(run)
    history = tail_csv(run / 'progress.csv')
    snapshots = sorted(run.glob('population-*.tsv'), key=lambda p: int(p.stem.split('-')[1]), reverse=True)
    if not snapshots:
        return {'waiting': True, 'complete': False}
    p = snapshots[0]
    rows = list(csv.DictReader(io.StringIO(text(p, 8_000_000)), delimiter='\t'))
    organisms = []
    for r in rows:
        if not r.get('genome'):
            continue
        n = len(r['genome'].split(':')[0]) // 2
        organisms.append(dict(id=r['id'], parent=r['parent'], birth=int(r['birth']), depth=int(r['depth']),
            pool=r['pool'], candidate_mask=int(r['candidate_mask']), encoded=n, expanded=n,
            blocks=0, block_depth=0, reused_blocks=0, genome=r['genome'], change='Archived population specimen'))
    discoveries = []
    for r in tail_csv(run / 'discoveries.csv'):
        task = r['task']
        if task not in TASKS:
            continue
        gp = run / 'discoveries' / (task + '.genome')
        try:
            genome = text(gp).splitlines()[1]
        except (OSError, IndexError):
            continue
        child = dict(id=r['id'],parent=r['parent'],birth=int(r['birth']),depth=int(r['depth']),pool='archived',
            genome=genome,blocks=0,change='See the saved discovery text for the original mutation.')
        discoveries.append(dict(task=task,checked=int(r['checked_inputs']),child=child,ancestor=None,parent_verified_mask=0))
    by_task = {x['task']: x for x in discoveries}
    final = tail_csv(run / 'final-verification.csv')
    tasks = [dict(name=t,verified=t in by_task,first_birth=by_task[t]['child']['birth'] if t in by_task else None,
        carriers=sum(bool(x['candidate_mask'] & (1 << i)) for x in organisms),
        final_verified=sum(r.get('task') == t and r.get('pass') == '1' for r in final)) for i,t in enumerate(TASKS)]
    last = history[-1] if history else {}
    return dict(version=cfg.get('version','Cambrian-0.1')+' / archived',revision=cfg.get('revision','unknown'),
        seed=int(cfg.get('seed',1)),birth=int(p.stem.split('-')[1]),total=int(cfg.get('births',0)),
        complete=(run/'summary.txt').exists(),accepted=int(last.get('accepted',0)),
        admitted_behaviors=int(last.get('admitted_behaviors',0)),live_behaviors=int(last.get('live_behaviors',0)),
        module_mode=False,block_bearers=0,reuse_bearers=0,max_block_depth=0,organisms=organisms,tasks=tasks,
        discoveries=discoveries,history=history)


class Store:
    def __init__(self, run: Path, process=None):
        self.run, self.process, self.cached, self.stamp = run.resolve(), process, None, None

    def read(self) -> dict:
        files = sorted(self.run.glob('state-*.json'), reverse=True)
        if files:
            for p in files[:2]:
                try:
                    stamp = (p.name, p.stat().st_mtime_ns)
                    if self.stamp != stamp:
                        data = json.loads(text(p, 12_000_000))
                        data['history'] = tail_csv(self.run/'progress.csv')
                        data['discoveries'] = []
                        for task in TASKS:
                            specimen = self.run/'discoveries'/(task+'.json')
                            if specimen.exists():
                                try:
                                    data['discoveries'].append(json.loads(text(specimen)))
                                except (OSError, ValueError):
                                    pass  # not complete yet; retried at next snapshot
                        self.cached, self.stamp = data, stamp
                    break
                except (OSError, ValueError):
                    continue
        elif (self.run/'config.txt').exists():
            try:
                self.cached = old_state(self.run)
            except (OSError, ValueError, KeyError):
                pass
        data = dict(self.cached or {'waiting':True,'complete':False})
        data['run_path'] = str(self.run)
        data['observer_only'] = self.process is None
        if self.process:
            data['process_exit'] = self.process.poll()
        return data


def handler(store: Store):
    routes = {'/':'index.html','/style.css':'style.css','/vm.js':'vm.js','/app.js':'app.js'}
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            expected = {'127.0.0.1:'+str(self.server.server_port),'localhost:'+str(self.server.server_port)}
            if self.headers.get('Host') not in expected:
                self.send_error(403, 'Loopback host required')
                return
            origin = self.headers.get('Origin')
            if origin and origin not in {'http://'+h for h in expected}:
                self.send_error(403, 'Same-origin requests only')
                return
            path = urlsplit(self.path).path
            try:
                if path == '/api/state':
                    body = json.dumps(store.read(), allow_nan=False).encode()
                    mime = 'application/json'
                elif path == '/story.json':
                    body = (ROOT/'examples'/'min-transition.json').read_bytes()
                    mime = 'application/json'
                elif path in routes:
                    body = (ROOT/'web'/routes[path]).read_bytes()
                    mime = 'text/html' if path == '/' else 'text/css' if path.endswith('.css') else 'text/javascript'
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header('Content-Type', mime+'; charset=utf-8')
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Cache-Control','no-store')
                self.send_header('X-Content-Type-Options','nosniff')
                self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; object-src 'none'")
                self.end_headers()
                self.wfile.write(body)
            except (OSError, ValueError):
                self.send_error(503, 'A complete snapshot is not available yet')
    return Handler


def replay(lineage: Path, wanted: str, destination: Path):
    """Reconstruct an admitted v0.2 genome; memory bounded by living population."""
    live = {}
    with lineage.open(encoding='utf-8-sig',newline='') as f:
        for r in csv.DictReader(f, delimiter='\t'):
            kind, data = r['record'].split(' ',1)
            parent = live.get(r['parent'])
            if r['parent'] != '0' and parent is None:
                raise ValueError('Missing living parent '+r['parent'])
            if kind == 'G':
                genome = data
            elif kind == 'P' and parent is not None:
                genome = data+':'+parent.split(':',1)[1]
            else:
                raise ValueError('Invalid lineage record')
            live[r['id']] = genome
            if r['replaced'] != '0':
                if r['replaced'] not in live:
                    raise ValueError('Missing replaced organism')
                del live[r['replaced']]
            if r['id'] == wanted:
                with destination.open('x',encoding='utf-8',newline='\n') as out:
                    out.write('DRIFTVM_MODULES_1\n'+genome+'\n')
                return
    raise ValueError('ID is not an admitted organism in this log')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--run',type=Path)
    p.add_argument('--launch',type=Path)
    p.add_argument('--births',type=int,default=10000000)
    p.add_argument('--seed',type=int,default=1)
    p.add_argument('--no-modules',action='store_true')
    p.add_argument('--no-drift',action='store_true')
    p.add_argument('--port',type=int,default=0,help='0 chooses an unused local port')
    p.add_argument('--open',action='store_true')
    p.add_argument('--replay',type=Path)
    p.add_argument('--id')
    p.add_argument('--save',type=Path)
    a=p.parse_args()
    if a.replay:
        if not a.id or not a.save:p.error('--replay requires --id and --save')
        replay(a.replay,a.id,a.save)
        return
    if a.run is None:p.error('--run is required')
    if not 0<=a.port<=65535:p.error('Invalid port')
    if a.launch and a.run.exists():p.error('A new experiment requires a new output directory')
    if not a.launch and not a.run.is_dir():p.error('Run directory does not exist')
    store=Store(a.run)
    server=ThreadingHTTPServer(('127.0.0.1',a.port),handler(store))
    server.daemon_threads=True
    process=None
    try:
        if a.launch:
            cmd=[str(a.launch.resolve()),'--out',str(a.run.resolve()),'--births',str(a.births),'--seed',str(a.seed),'--report-every','10000']
            if a.no_modules:cmd+=['--no-modules']
            if a.no_drift:cmd+=['--no-drift']
            kwargs={'creationflags':subprocess.CREATE_NEW_PROCESS_GROUP} if sys.platform=='win32' else {'start_new_session':True}
            process=subprocess.Popen(cmd,**kwargs)
            store.process=process
        url='http://127.0.0.1:'+str(server.server_port)
        print('Evolution Lab: '+url,flush=True)
        print('Keep this terminal open. Ctrl+C closes the viewer and stops its child experiment. No resume checkpoint is created.',flush=True)
        if a.open:webbrowser.open(url)
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nClosing local lab.',flush=True)
    finally:
        server.server_close()
        if process and process.poll() is None:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()

if __name__=='__main__':
    try:main()
    except (OSError,ValueError) as e:sys.exit('Lab error: '+str(e))
