'use strict';
const $=id=>document.getElementById(id), V=DriftVM;
const colors=['#329b9b','#79a87c','#8baacb','#adbe82','#a592c1','#cf9b64','#bb7c95','#588b7a','#c1ab70','#7b91b9'];
const format=n=>Number(n||0).toLocaleString(), esc=s=>String(s??'').replace(/[&<>"']/g,x=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[x]));
const maskNames=m=>V.tasks.filter((_,i)=>Number(m)&(1<<i));
let latest=null, frozen=false, selected=null, event=null, side='child', tab='live', trace=null, code=null, timer=null, story=null;
function tell(message){$('error').textContent=message;$('error').hidden=!message;}
function stop(){if(timer)clearInterval(timer);timer=null;$('play').textContent='Play steps';}
function select(specimen,witness=null){stop();selected=specimen;event=witness;side='child';displaySpecimen();}
function displaySpecimen(){
 if(!selected)return;const x=side==='parent'&&event?.ancestor?event.ancestor:selected;
 try{code=V.decode(x.genome);}catch(e){tell('Specimen could not be decoded: '+e.message);return;}
 $('specimen-body').hidden=false;$('specimen-title').textContent=(side==='parent'?'Parent ':'Program ')+x.id;
 $('specimen-meta').textContent=`Born at ${format(x.birth)} · lineage depth ${format(x.depth)} · parent ${x.parent||'none'} · ${x.pool||'archived'} pool`;
 $('sidebuttons').hidden=!event?.ancestor;$('parentbtn').classList.toggle('chosen',side==='parent');$('childbtn').classList.toggle('chosen',side==='child');
 $('witness').hidden=!event;
 if(event){const parentTasks=maskNames(event.parent_verified_mask);$('witness').textContent=event.teaching?
  'Replaying your recorded MAX → MIN transition. '+($('reduced').checked?'The short forms are human reductions, separately verified after evolution.':'These are the original evolved child and its reconstructed parent.'):
  `${event.task} certified on ${format(event.checked)} inputs. Parent: ${event.ancestor?(parentTasks.join(', ')||'no named task verified'):'not available in this record'}. ${selected.change||''} ${event.admitted===false?'The discovery child was not admitted to the population.':''}`;}
 $('program').innerHTML=code.program.map((t,i)=>`<span class="token ${t>=32?'module':''}" data-top="${i}" title="${t>=32?'Expand block M'+(t-32):V.names.length&&code.language[t].map(op=>V.names[op]).join(' → ')}">${t>=32?'M'+(t-32):t}</span>`).join('');
 $('codestats').textContent=`${code.program.length} top-level tokens → ${code.flat.length} primitive instructions · block depth ${code.depth}`;
 $('blocks').innerHTML=code.blocks.map((b,i)=>`<div class="block-definition"><b>M${i}</b> = ${b.map(t=>t>=32?'M'+(t-32):t).join(' · ')}<small>${code.uses[i]} structural reference${code.uses[i]===1?'':'s'} in expanded program</small></div>`).join('');
 if(!code.blocks.length)$('blocks').innerHTML='<small class="fine">No evolved code blocks in this specimen.</small>';
 runInputs();
}
function runInputs(){stop();try{trace=V.run(code,Number($('inputA').value),Number($('inputB').value),true);tell('');}catch(e){tell(e.message);return;}
 $('answer').textContent=trace.output;const task=event?V.tasks.indexOf(side==='parent'?maskNames(event.parent_verified_mask)[0]:event.task):-1;
 $('expected').textContent=task>=0?`Expected ${V.tasks[task]}: ${V.target(task,Number($('inputA').value),Number($('inputB').value))} · one example, not a proof`:'This is one input pair, not a certificate.';
 $('step').max=Math.max(0,trace.steps.length-1);$('step').value=0;
 $('trace').innerHTML=trace.steps.map((s,i)=>`<tr data-step="${i}"><td>${i+1}</td><td>${s.pc}: ${s.code}</td><td>${s.op}</td><td>${s.before.x} → ${s.after.x}</td><td>${s.before.y} → ${s.after.y}</td><td>${s.after.last??'—'}</td></tr>`).join('');showStep();
}
function showStep(){if(!trace)return;let index=Number($('step').value),s=trace.steps[index];if(!s)return;
 $('stepcount').textContent=`${index+1} / ${trace.steps.length}`;$('stepop').textContent=s.op;$('stepnote').textContent=s.note;
 $('origin').textContent=`Primitive instruction ${s.pc}, opcode ${s.code}, micro-operation ${s.slot+1}/4${s.origin.path.length?' · inside '+s.origin.path.map(i=>'M'+i).join(' → '):''}`;
 $('regx').textContent=s.after.x;$('regy').textContent=s.after.y;$('emit').textContent=s.after.last??'—';
 $('bitsx').textContent=s.after.x.toString(2).padStart(8,'0');$('bitsy').textContent=s.after.y.toString(2).padStart(8,'0');
 document.querySelectorAll('.token').forEach(n=>n.classList.toggle('highlight',Number(n.dataset.top)===s.origin.top));
 document.querySelectorAll('#trace tr').forEach(n=>n.classList.toggle('current',Number(n.dataset.step)===index));
 $('previous').disabled=index===0;$('next').disabled=index===trace.steps.length-1;
}
function renderState(s){latest=s;if(frozen||!s)return;
 if(s.error){tell(s.error);return;}tell('');
 $('status').textContent=s.complete?'Completed · results preserved':s.process_exit!=null?'Process stopped · last snapshot':s.waiting?'Waiting for first snapshot':s.observer_only?'Reading snapshots · process not managed':'Running · snapshot updates';
 if(s.process_exit!=null&&s.process_exit!==0)tell('The simulator exited with code '+s.process_exit+'. Check the terminal. The latest completed snapshot is still shown.');
 if(s.waiting)return;
 $('runlabel').textContent=`${s.version} · seed ${s.seed} · revision ${s.revision||'unrecorded'}${s.module_mode===false?' · modules disabled':''}`;
 $('path').textContent='Local results: '+(s.run_path||'');
 const known=(s.tasks||[]).filter(x=>x.verified).length;
 $('verified').textContent=known+' / 10';$('births').textContent=format(s.birth);
 $('birthsub').textContent=`${s.total?((s.birth/s.total)*100).toFixed(1):'0'}% of ${format(s.total)} attempts · ${format(s.accepted)} admitted`;
 $('blockcount').textContent=format(s.block_bearers);$('reuse').textContent=format(s.reuse_bearers);
 $('blocksub').textContent=`Of ${(s.organisms||[]).length} living programs · max structural depth ${s.max_block_depth||0}`;
 $('livecount').textContent=(s.organisms||[]).length+' alive';
 const descriptions={performance:'Compete on screened computational tasks',novelty:'Preserve uncommon probe outputs',drift:'Random replacement, no fitness comparison'};
 $('pools').innerHTML=['performance','novelty','drift'].map(pool=>{const members=s.organisms.filter(x=>x.pool===pool);return `<div class="pool"><div class="pool-heading"><b>${pool[0].toUpperCase()+pool.slice(1)} <span>· ${members.length}</span></b><span>${descriptions[pool]}</span></div><div class="grid">${members.map(x=>{const task=V.tasks.findIndex((_,i)=>x.candidate_mask&(1<<i));return `<button class="cell ${x.blocks?'block':''} ${selected?.id===x.id&&tab==='live'?'selected':''}" style="--tile:${task<0?'#dce4e3':colors[task]}" data-id="${esc(x.id)}" aria-label="Program ${esc(x.id)}, ${task<0?'no task candidate':V.tasks[task]}, ${x.blocks||0} blocks" title="${esc(x.id)} · ${task<0?'unclassified':V.tasks[task]} · ${x.blocks||0} blocks"></button>`;}).join('')}</div></div>`;}).join('');
 $('pools').querySelectorAll('button').forEach(n=>n.onclick=()=>{select(s.organisms.find(x=>x.id===n.dataset.id));$('microscope').scrollIntoView({behavior:'smooth',block:'start'});});
 $('legend').innerHTML=V.tasks.map((t,i)=>`<span><i style="background:${colors[i]}"></i>${t}</span>`).join('')+'<span><i style="background:#dce4e3"></i>Unclassified</span>';
 const discoveries=(s.discoveries||[]).sort((a,b)=>b.child.birth-a.child.birth);
 $('feed').innerHTML=discoveries.length?discoveries.map((d,i)=>`<button class="discovery" data-index="${i}"><span class="badge">✓</span><span><b>${esc(d.task)} verified</b><small>${d.child.birth===0?'Present in a random founder':'Parent '+esc(d.child.parent)+' → child '+esc(d.child.id)}</small><small>${d.child.blocks||0} code blocks · 65,536 / 65,536 inputs</small></span><time>${format(d.child.birth)}</time></button>`).join(''):'<p class="empty">No certified discovery yet. That is a valid experimental outcome.</p>';
 $('feed').querySelectorAll('button').forEach(n=>n.onclick=()=>{const d=discoveries[Number(n.dataset.index)];select(d.child,d);$('microscope').scrollIntoView({behavior:'smooth'});});
 $('exploration').textContent=format(s.admitted_behaviors)+' distinct admitted probe outputs';
 const h=s.history||[];const max=Math.max(1,s.total||s.birth);let points='';
 h.forEach((r,i)=>{let x=45+910*Number(r.birth)/max,y=120-10*Number(r.verified_tasks||0);if(i)points+=' H'+x;points+=(i?' V':'M'+x+',')+y;});
 $('timeline').innerHTML=`<line x1="45" x2="955" y1="120" y2="120" stroke="#dce2dd"/><line x1="45" x2="955" y1="20" y2="20" stroke="#edf0ec"/><text x="12" y="124">0</text><text x="6" y="24">10</text><text x="45" y="144">0 births</text><text x="955" y="144" text-anchor="end">${format(max)} births</text><path d="${points}" stroke="#156d5d" fill="none" stroke-width="2.5"/>`;
 if(!selected&&tab==='live'&&s.organisms.length){const specimen=s.organisms.find(x=>x.blocks)||s.organisms[0];select(specimen);}
}
async function poll(){try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);renderState(await r.json());}catch(e){$('status').textContent='Disconnected · view preserved';tell('Cannot read the local experiment. Keep the launcher terminal open. '+e.message);}}
async function showStory(){if(!story){const r=await fetch('/story.json');if(!r.ok)throw Error('Story fixture missing');story=await r.json();}
 const part=$('reduced').checked?'reduced':'original',d=structuredClone(story[part]);d.teaching=true;select(d.child,d);}
$('pause').onclick=()=>{frozen=!frozen;$('pause').textContent=frozen?'Resume view':'Pause view';$('status').textContent=frozen?'View paused · engine unaffected':latest?.complete?'Completed':'Running';if(!frozen&&latest)renderState(latest);};
document.querySelectorAll('.tab').forEach(n=>n.onclick=async()=>{tab=n.dataset.tab;document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('active',b===n));$('live').hidden=tab!=='live';$('story').hidden=tab!=='story';stop();if(tab==='story'){try{await showStory();}catch(e){tell(e.message);}}else{selected=null;event=null;if(latest)renderState(latest);}});
$('reduced').onchange=()=>showStory().catch(e=>tell(e.message));
$('parentbtn').onclick=()=>{side='parent';displaySpecimen();};$('childbtn').onclick=()=>{side='child';displaySpecimen();};
$('execute').onclick=runInputs;$('step').oninput=()=>{stop();showStep();};
$('previous').onclick=()=>{stop();$('step').value=Math.max(0,Number($('step').value)-1);showStep();};
$('next').onclick=()=>{stop();$('step').value=Math.min(trace.steps.length-1,Number($('step').value)+1);showStep();};
$('play').onclick=()=>{if(timer){stop();return;}if(!trace)return;if(Number($('step').value)>=trace.steps.length-1)$('step').value=0;$('play').textContent='Pause steps';timer=setInterval(()=>{let i=Number($('step').value);if(i>=trace.steps.length-1){stop();return;}$('step').value=i+1;showStep();},450);};
poll();setInterval(poll,1200);
