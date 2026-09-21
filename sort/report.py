"""A self-contained local report. No server, packages, or generated illustrations."""
import html
import json
import os
import time
from pathlib import Path

STYLE='''body{font:16px system-ui;margin:0;background:#f4f6fa;color:#172a40}main{max-width:1160px;margin:auto;padding:36px}h1{font-size:38px;margin:12px 0}h2{font-size:23px}p{line-height:1.6}.eyebrow{letter-spacing:2px;text-transform:uppercase;font-size:12px;color:#456383}.cards{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}.card,section{background:white;border:1px solid #dbe2ed;border-radius:12px;padding:24px;margin:18px 0}.cards .card{margin:0}.number{font-size:30px;font-weight:700}small,.muted{color:#52657d}table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}th,td{text-align:left;padding:10px;border-bottom:1px solid #e4e8f0}th{font-size:13px}td{font-size:14px}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px;background:#eef2f8;padding:16px;border-radius:8px}input{padding:10px;width:min(90%,500px);font:inherit}button{padding:10px 15px;margin:8px 5px 8px 0;cursor:pointer;border:1px solid #c4cedd;background:white;border-radius:6px}.wires{display:flex;gap:8px}.wire{flex:1;text-align:center;background:#eef2f8;padding:12px 4px;font-size:20px;border-radius:6px}.active{background:#d9f1e9}.status{background:#e7edf8;padding:16px;border-left:4px solid #4466a4}.overflow{overflow:auto}@media(max-width:700px){main{padding:14px}h1{font-size:28px}.cards{grid-template-columns:1fr}.wires{display:grid;grid-template-columns:repeat(4,1fr)}.wire{font-size:13px;padding:10px 0}section{padding:15px}}'''

def replace_display_file(source, target):
    # Windows browser readers can briefly hold a file open without delete-sharing.
    # A display refresh must not abort the evolutionary run.
    for attempt in range(5):
        try:
            os.replace(source, target)
            return
        except PermissionError:
            time.sleep(0.05)
    print(f"Display refresh deferred: {target}", flush=True)

def write_report(root, state):
    root=Path(root)
    esc=lambda x:html.escape(str(x))
    metrics=state.get('metrics',[])
    table=''.join('<tr>'+''.join(f'<td>{esc(row.get(k,"—"))}</td>' for k in ('name','workload','ns','ratio'))+'</tr>' for row in metrics)
    chosen=state.get('chosen',{})
    data=json.dumps(chosen.get('flat',[])).replace('<','\\u003c')
    specimen=chosen.get('specimen',{})
    blocks=chosen.get('blocks',[])
    dialect=[]
    for i,block in enumerate(blocks):
        dialect.append('M'+str(i)+' = '+' '.join('M'+str(t-256) if t>=256 else ('CX','MIN','MAX','COPY')[t//64]+f'({(t//8)%8},{t%8})' for t in block))
    refresh='<meta http-equiv="refresh" content="15">' if state.get('phase')!='Complete' else ''
    text=f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">{refresh}<title>DriftSort — measured discovery</title><style>{STYLE}</style></head><body><main>
<div class="eyebrow">DriftVM / Sorting objective / Native code</div><h1>Can evolution beat the reference?</h1>
<p>Sort exactly eight unsigned 32-bit integers, preserving every value. Evolve a task-specific instruction vocabulary and program; compile it, check it, and measure it on this machine.</p>
<div class="status"><strong>{esc(state.get('phase','Starting'))}</strong> — {esc(state.get('message','Preparing the experiment.'))}</div>
<div class="cards"><div class="card"><small>Attempted offspring</small><div class="number">{int(state.get('births',0)):,}</div></div><div class="card"><small>Measured winner</small><div style="font-size:18px;font-weight:700;overflow-wrap:anywhere">{esc(state.get('winner','Not selected'))}</div></div><div class="card"><small>Rust comparison</small><div style="font-size:18px;font-weight:700">{esc(state.get('rust','Checking compiler'))}</div></div></div>
<section><h2>The pass condition</h2><p>Correct output first. Then at least <strong>5% faster than every tested reference on all five workloads</strong>, with the predeclared paired-win check. A frozen finalist is checked again using a separate input seed and fresh timing samples. Rust must be measured before the combined C++/Rust goal can pass.</p><p class="muted">This compares named compiled implementations on one CPU—not programming languages universally. Timing is batched throughput; copying inputs and checking outputs are outside the timer. Operation count is a search proxy, never a measured speedup.</p></section>
<section><h2>Native timing results</h2><p>{esc(state.get('measurement_note','Baselines are compiled and measured before evolution starts.'))}</p><div class="overflow"><table><thead><tr><th>Implementation</th><th>Workload</th><th>Median ns / sort</th><th>Speed vs fastest reference</th></tr></thead><tbody>{table or '<tr><td colspan="4">No measurements yet.</td></tr>'}</tbody></table></div></section>
<section><h2>Watch the selected program sort</h2><p>Numbers below are executed by the selected instruction stream, not a decorative animation. This single example is illustrative; the binary-input verification and native audits are recorded separately.</p><input id="values" aria-label="Eight unsigned integers" value="37, 12, 99, 0, 42, 7, 7, 255"><div><button id="reset">Load inputs</button><button id="step">Next instruction</button><button id="finish">Run to completion</button></div><div id="wires" class="wires"></div><p id="operation">No selected candidate yet.</p><pre id="dialect">{esc(chr(10).join(dialect) or 'No reusable blocks in the selected specimen. This is a valid outcome, not hidden.')}</pre><p class="muted">Parent {esc(specimen.get('parent','—'))}; birth {esc(specimen.get('birth','—'))}. Factoring and the primitive operations were supplied by us. The inherited block bodies and their use can mutate; their existence alone does not prove a useful invention.</p></section>
<section><h2>What is being preserved?</h2><p>Completed-phase checkpoints retain population, random state, recent novelty history, archived programs and measured breeding parents. Each exported candidate includes its complete language and flattened program. This sorting track does not record every extinct ancestor. It does not change your earlier Cambrian runs.</p><p>All raw timings, compiler output, generated C++/Rust, manifests and checkpoints are in this result folder. No file is uploaded anywhere.</p><pre>{esc(state.get('verdict','No performance verdict yet.'))}</pre></section>
</main><script>const code={data};let row=[],pc=0;function draw(active=[]){{document.getElementById('wires').innerHTML=row.map((v,i)=>'<div class="wire '+(active.includes(i)?'active':'')+'">'+v+'</div>').join('');}}function reset(){{const a=document.getElementById('values').value.split(',').map(x=>x.trim());if(a.length!==8||a.some(x=>!/^\\d+$/.test(x)||Number(x)>4294967295)){{document.getElementById('operation').textContent='Enter exactly eight integers in 0..4294967295.';return false;}}row=a.map(Number);pc=0;draw();document.getElementById('operation').textContent=code.length?'Ready: '+code.length+' expanded instructions.':'No selected candidate yet.';return true;}}function step(){{if(pc>=code.length)return;const t=code[pc++],k=Math.floor(t/64),a=Math.floor(t/8)%8,b=t%8,x=row[a],y=row[b];if(k===0){{row[a]=Math.min(x,y);row[b]=Math.max(x,y);}}else if(k===1)row[a]=Math.min(x,y);else if(k===2)row[a]=Math.max(x,y);else row[a]=y;draw([a,b]);document.getElementById('operation').textContent=pc+'/'+code.length+': '+['CX','MIN','MAX','COPY'][k]+'('+a+','+b+')'+(pc===code.length?' — completed':'');}}document.getElementById('reset').onclick=reset;document.getElementById('step').onclick=step;document.getElementById('finish').onclick=()=>{{if(reset())while(pc<code.length)step();}};reset();</script></body></html>'''
    tmp=root/'report.html.tmp';tmp.write_text(text,encoding='utf8');replace_display_file(tmp,root/'report.html')
    tmp=root/'status.json.tmp';tmp.write_text(json.dumps(state,indent=2),encoding='utf8');replace_display_file(tmp,root/'status.json')
