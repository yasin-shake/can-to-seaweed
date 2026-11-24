'use strict';
const $ = id => document.getElementById(id);
const paths = {
  wave:'M2 13h4l3-8 4 15 3-11 2 4h4', trend:'M3 18l6-7 4 3 8-10 M15 4h6v6',
  sliders:'M4 6h5m5 0h6M4 12h10m5 0h1M4 18h2m5 0h9 M9 4v4m5 2v4m-8 2v4',
  fault:'M12 3L2 20h20L12 3z M12 9v5m0 2v1', layers:'M3 8l9-5 9 5-9 5z M3 12l9 5 9-5 M3 16l9 5 9-5',
  settings:'M4 5h16v14H4z M8 9h8m-8 5h3m3 0h2', logs:'M5 3h14v18H5z M8 8h8m-8 4h8m-8 4h5',
  disk:'M4 4h13l3 3v13H4z M8 4v6h8V4 M8 20v-6h8v6', reset:'M4 10a8 8 0 1 1 1 7 M4 4v6h6',
  menu:'M3 6h18M3 12h18M3 18h18', close:'M5 5l14 14M19 5L5 19'
};
for (const el of document.querySelectorAll('[data-icon]')) {
  el.insertAdjacentHTML('afterbegin','<svg class="icon" aria-hidden="true" viewBox="0 0 24 24"><path d="'+paths[el.dataset.icon]+'"/></svg>');
}
const esc = v => String(v ?? '').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const number = (v,digits=4) => typeof v==='number' ? v.toLocaleString(undefined,{maximumFractionDigits:digits}) : v;
const keyOf = s => [s.sa,s.pgn,s.name].join('/');
const unitOf = s => s.unit?.toLowerCase()==='rpm' ? 'RPM' : s.unit || '';
const valueOf = s => s?.status || (s?.value == null ? 'No reading' : String(number(s.value,s.name==='Latitude'?7:4)));
const reading = s => esc(valueOf(s)) + (unitOf(s)?'<small>'+esc(unitOf(s))+'</small>':'') +
  (s.age>5?'<span class="stale">Last seen '+esc(number(Math.floor(s.age)))+' s ago</span>':'');
const identifier = s => '<code class="ident">'+esc(s.name)+'</code>';
const pointID = s => 'SPN '+(s.spn||'—')+' · PGN '+s.pgn+' · '+s.sa;
const fmtTime = ts => new Date(ts*1000).toLocaleTimeString([], {hour12:false});
const palette=['#007e71','#3469ab','#ad652a','#795da5','#427e53','#287fa2'];
function stored(key,fallback){try{return JSON.parse(localStorage.getItem('ecu.v2.'+key)) ?? fallback;}catch{return fallback;}}
function save(key,value){try{localStorage.setItem('ecu.v2.'+key,JSON.stringify(value));}catch{}}
const pages={telemetry:'Live telemetry',controls:'Signal controls',faults:'Fault diagnostics',inspector:'Data inspector',s3:'S3 monitor',settings:'Settings',logs:'Activity log'};
let statusData=null, catalog=[], catalogReady=false, live=new Map(), known=new Map(), page='telemetry', inspectTab='signals';
let selection=stored('signals',null), draft=[], seconds=Number(stored('seconds',60)), settingsDirty=false;
if (!Array.isArray(selection)) selection=null;
if (![30,60,300].includes(seconds)) seconds=60;
$('smooth').checked=stored('smooth',true);
let chartKeys=[], chartData=null, controlSignature='', lastSession='', editChannel='';
function route(){
  const requested=location.hash.slice(1);
  page=Object.hasOwn(pages,requested)?requested:'telemetry';
  if(page==='controls' && statusData && !statusData.simulator_controls) page='telemetry';
  for(const el of document.querySelectorAll('[data-page]')) el.hidden=el.dataset.page!==page;
  for(const a of document.querySelectorAll('nav a')) {
    if(a.hash==='#'+page) a.setAttribute('aria-current','page'); else a.removeAttribute('aria-current');
  }
  $('title').textContent=pages[page]; $('crumb').textContent=page==='telemetry'?'Telemetry':pages[page];
  document.title=pages[page]+' · ECU Monitor'; document.body.classList.remove('menuOpen');
  $('menu').setAttribute('aria-expanded','false'); window.scrollTo(0,0);
  if(statusData) render();
  if(page==='controls' && editChannel){
    $('controlQuery').value='';
    filterControls();
    const input=$('value-'+editChannel);
    if(input){input.focus();input.select();}
  }
  editChannel='';
}
window.addEventListener('hashchange',route);
$('menu').onclick=()=>{$('menu').setAttribute('aria-expanded',document.body.classList.toggle('menuOpen'));};
document.querySelector('nav').addEventListener('click',()=>{document.body.classList.remove('menuOpen');$('menu').setAttribute('aria-expanded','false');});
async function request(url,body){
  const options=body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)};
  const r=await fetch(url,options);
  const json=await r.json();
  if(!r.ok) throw Error(json.error||'Request failed');
  return json;
}
function report(el,message,error=false){el.textContent=message;el.classList.toggle('error',error);}
async function command(cmd,msg,success){
  try{await request('/api/simulator',{command:cmd});report(msg,success);}
  catch(e){report(msg,e.message,true);}
}
function table(id,headers,rows,empty){
  $(id).innerHTML=rows.length?'<div class="tablewrap"><table><thead><tr>'+headers.map(h=>'<th scope="col">'+esc(h)+'</th>').join('')+'</tr></thead><tbody>'+rows.map(r=>'<tr>'+r.map(c=>'<td>'+c+'</td>').join('')+'</tr>').join('')+'</tbody></table></div>':'<p class="empty">'+esc(empty)+'</p>';
}
function details(id,values){
  $(id).innerHTML=Object.entries(values).map(([k,v])=>'<dt>'+esc(k)+'</dt><dd>'+esc(v)+'</dd>').join('');
}
function defaults(){
  const available=[...known.values()].filter(s=>live.get(keyOf(s))?.numeric);
  const priority=[190,110,100,168,177,96,171,84,102,94];
  const rank=s=>priority.includes(s.spn)?priority.indexOf(s.spn):priority.length;
  return available.sort((a,b)=>rank(a)-rank(b)).slice(0,6).map(keyOf);
}
function windowButtons(){
  for(const b of $('window').querySelectorAll('button')) b.setAttribute('aria-pressed',Number(b.dataset.seconds)===seconds);
}
$('window').onclick=e=>{const b=e.target.closest('[data-seconds]');if(b){seconds=Number(b.dataset.seconds);save('seconds',seconds);windowButtons();updateTrends().catch(showError);}};
$('smooth').onchange=()=>{save('smooth',$('smooth').checked);drawCharts();};
function trendKeys(){return (selection??defaults()).filter(k=>known.has(k)).slice(0,12);}
function chartCards(){
  const keys=trendKeys();
  if(keys.join('|')!==chartKeys.join('|')){
    chartKeys=keys;chartData=null;
    $('charts').innerHTML=keys.map((k,i)=>{
      const s=known.get(k), channel=catalog.find(c=>keyOf(c)===k);
      return '<article class="chart" style="--series:'+palette[i%palette.length]+'"><div class="signalhead"><h3><i class="channelDot"></i>'+esc(s.label)+'</h3></div>'+identifier(s)+'<div class="reading"></div><canvas class="plot" role="img" aria-label="'+esc(s.name+' · '+s.label+' trend in '+unitOf(s))+'"></canvas><div class="chartfoot"><span>'+esc(pointID(s))+'</span>'+(channel?'<button data-edit="'+esc(channel.channel)+'">Adjust value ↗</button>':'')+'</div></article>';
    }).join('');
  }
  [...$('charts').children].forEach((card,i)=>{card.querySelector('.reading').innerHTML=reading(known.get(keys[i]));});
  $('chartSummary').textContent=keys.length+' selected · '+($('smooth').checked?'smoothed trend':'sampled readings');
  $('emptyCharts').hidden=keys.length>0;
  $('emptyCharts').textContent=selection?'Choose signals to display their trends.':'Waiting for decoded telemetry…';
}
$('charts').onclick=e=>{
  const b=e.target.closest('[data-edit]');if(!b)return;
  editChannel=b.dataset.edit;
  location.hash='controls';
};
let trendVersion=0;
async function updateTrends(){
  if(page!=='telemetry'||!chartKeys.length)return;
  const version=++trendVersion, keys=chartKeys.join('|'), params=new URLSearchParams({seconds});
  chartKeys.forEach(k=>params.append('signal',k));
  const result=await request('/api/trends?'+params);
  if(version===trendVersion && page==='telemetry' && chartKeys.join('|')===keys){
    chartData={...result,seconds:Number(params.get('seconds'))};drawCharts();
  }
}
function drawChart(canvas,points,now,span,color,smooth){
  const box=canvas.getBoundingClientRect();if(box.width<10)return;
  const w=box.width,h=box.height, dpr=window.devicePixelRatio||1;
  canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);
  const ctx=canvas.getContext('2d');ctx.scale(dpr,dpr);ctx.font='10px Consolas, monospace';ctx.lineWidth=1;
  const vals=points.map(p=>p[1]).filter(Number.isFinite);
  let lo=vals.length?Math.min(...vals):0, hi=vals.length?Math.max(...vals):1;
  const pad=hi===lo?Math.max(Math.abs(lo)*.025,.1):(hi-lo)*.1;
  lo-=pad;hi+=pad;
  const axis=v=>Math.abs(v)>=1e5?v.toExponential(1):String(Number(v.toPrecision(4)));
  const left=Math.max(34,...[lo,(lo+hi)/2,hi].map(v=>ctx.measureText(axis(v)).width+7));
  const right=w-6, top=8, bottom=h-22;
  const x=t=>left+(t-now+span)/span*(right-left), y=v=>bottom-(v-lo)/(hi-lo)*(bottom-top);
  ctx.fillStyle='#5a7079';ctx.strokeStyle='#e0e8e9';ctx.textAlign='right';
  for(let i=0;i<3;i++){
    const v=lo+(hi-lo)*i/2,yp=y(v);
    ctx.beginPath();ctx.setLineDash([3,4]);ctx.moveTo(left,yp);ctx.lineTo(right,yp);ctx.stroke();
    if(vals.length)ctx.fillText(axis(v),left-6,yp+3);
  }
  ctx.setLineDash([]);ctx.textAlign='left';ctx.fillText('−'+span+' s',left,h-5);
  ctx.textAlign='right';ctx.fillText(fmtTime(now),right,h-5);
  if(!vals.length){ctx.textAlign='center';ctx.fillText('No samples in this window',(left+right)/2,(top+bottom)/2);return;}
  // Time-weighted EMA; reset at missing readings and gaps. The headline is the latest raw value.
  let segment=[],previous=null,ema=0;
  function stroke(){
    if(!segment.length)return;
    ctx.beginPath();ctx.moveTo(...segment[0]);
    for(let i=1;i<segment.length;i++){
      const [x0,y0]=segment[i-1],[x1,y1]=segment[i];
      if(smooth){const mid=(x0+x1)/2;ctx.bezierCurveTo(mid,y0,mid,y1,x1,y1);}
      else ctx.lineTo(x1,y1);
    }
    ctx.strokeStyle=color;ctx.lineWidth=2;ctx.stroke();
    const end=segment.at(-1);ctx.lineTo(end[0],bottom);ctx.lineTo(segment[0][0],bottom);ctx.closePath();
    const fill=ctx.createLinearGradient(0,top,0,bottom);fill.addColorStop(0,color+'32');fill.addColorStop(1,color+'04');ctx.fillStyle=fill;ctx.fill();
    ctx.beginPath();ctx.fillStyle=color;ctx.arc(...end,2.5,0,2*Math.PI);ctx.fill();segment=[];
  }
  for(const [t,v] of points){
    if(!Number.isFinite(v)||previous!==null&&t-previous>5){stroke();previous=null;}
    if(!Number.isFinite(v))continue;
    ema=previous===null?v:ema+(1-Math.exp(-(t-previous)/3))*(v-ema);
    segment.push([x(t),y(smooth?ema:v)]);previous=t;
  }
  stroke();
}
function drawCharts(){
  if(!chartData||page!=='telemetry')return;
  [...$('charts').children].forEach((card,i)=>drawChart(card.querySelector('canvas'),chartData.series[chartKeys[i]]||[],chartData.now,chartData.seconds,palette[i%palette.length],$('smooth').checked));
}
window.addEventListener('resize',drawCharts);
function matches(s,q){return [s.name,s.label,s.spn,s.pgn,s.sa,unitOf(s)].join(' ').toLowerCase().includes(q.trim().toLowerCase());}
function renderChoices(){
  const options=[...known.values()].filter(s=>(s.numeric||s.channel)&&matches(s,$('pickQuery').value)).sort((a,b)=>a.label.localeCompare(b.label));
  $('choices').innerHTML=options.map(s=>{const k=keyOf(s);return '<label class="choice"><input type="checkbox" value="'+esc(k)+'" '+(draft.includes(k)?'checked':'')+'><span><strong>'+esc(s.label)+'</strong>'+identifier(s)+'<small>'+esc(pointID(s))+'</small></span><span>'+esc(valueOf(s)+' '+unitOf(s))+'</span></label>';}).join('')||'<p class="empty">No matching signals.</p>';
  $('selectionCount').textContent=draft.length+' of 12 selected';
}
$('choose').onclick=()=>{draft=trendKeys().slice();$('pickQuery').value='';renderChoices();$('picker').showModal();$('pickQuery').focus();};
$('pickQuery').oninput=renderChoices;
$('choices').onchange=e=>{const input=e.target,k=input.value;if(input.checked && draft.length===12){input.checked=false;$('selectionCount').textContent='12 selected · remove one to add another';return;}
  draft=input.checked?[...draft,k]:draft.filter(v=>v!==k);$('selectionCount').textContent=draft.length+' of 12 selected';};
$('cancelPick').onclick=()=>$('picker').close();
$('defaults').onclick=()=>{draft=defaults();renderChoices();};
$('pickerForm').onsubmit=e=>{e.preventDefault();selection=draft.slice();save('signals',selection);$('picker').close();chartCards();updateTrends().catch(showError);};

function buildControls(){
  const signature=catalog.map(keyOf).join('|');
  if(signature===controlSignature)return;
  controlSignature=signature;
  const order=['RPM','SPEED','COOLANT','OILPRESS','BOOST','VOLT','TRANSOIL','FUEL','AMBIENT'];
  const rank=c=>order.includes(c.channel)?order.indexOf(c.channel):order.length;
  $('controls').innerHTML=catalog.slice().sort((a,b)=>rank(a)-rank(b)||a.label.localeCompare(b.label)).map(c=>{
    const id=esc(c.channel),unit=esc(unitOf(c)),measured=live.get(keyOf(c))?.value;
    const start=Number.isFinite(measured)?measured:'';
    const targetLabel=unit?'Target value ('+unit+')':'Target value';
    return '<form class="ctrl" data-channel="'+id+'"><h3>'+esc(c.label)+'</h3>'+identifier(c)+'<div class="reading"></div><span class="muted">Decoded value · '+esc(pointID(c))+'</span><input type="range" aria-label="'+esc(c.label)+' target" min="'+c.min+'" max="'+c.max+'" step="'+c.step+'" value="'+start+'"><div class="rangeEnds"><span>'+c.min+' '+unit+'</span><span>'+c.max+' '+unit+'</span></div><div class="entry"><label>'+targetLabel+'<input id="value-'+id+'" name="target" type="number" min="'+c.min+'" max="'+c.max+'" step="any" placeholder="Enter value" value="'+start+'" required></label><button class="primary">Apply</button></div><output aria-live="polite"></output></form>';
  }).join('');
  filterControls();
}
function filterControls(){
  const query=$('controlQuery').value;
  const visible=new Set(catalog.filter(c=>matches(c,query)).map(c=>c.channel));
  for(const form of $('controls').children)form.hidden=!visible.has(form.dataset.channel);
  $('noControls').hidden=visible.size>0;
}
$('controlQuery').oninput=filterControls;
$('controls').oninput=e=>{
  const form=e.target.closest('form');if(!form)return;
  const target=form.elements.target,slider=form.querySelector('[type=range]');
  if(e.target===slider)target.value=slider.value;else if(target.value!=='' && target.validity.valid)slider.value=target.value;
};
$('controls').onsubmit=async e=>{
  e.preventDefault();const f=e.target,c=catalog.find(s=>s.channel===f.dataset.channel),value=f.elements.target.value;
  const btn=f.querySelector('button');btn.disabled=true;
  await command('SET '+c.channel+' '+value,f.querySelector('output'),'Target accepted · '+value+' '+unitOf(c));
  btn.disabled=false;
};
$('reset').onclick=()=>command('RESET',$('controlMsg'),'Baseline restored. Recurring faults cleared.').then(()=>{
  if(!$('controlMsg').classList.contains('error')){controlSignature='';renderControls();}
});
function renderControls(){
  buildControls();
  for(const f of $('controls').children){
    const c=catalog.find(v=>v.channel===f.dataset.channel);
    f.querySelector('.reading').innerHTML=reading(known.get(keyOf(c))||c);
  }
}
$('faultForm').onsubmit=e=>{
  e.preventDefault();const f=e.target.elements;
  command([f.mode.value,f.spn.value,f.fmi.value,f.oc.value].join(' '),$('faultMsg'),f.mode.value==='FAULT'?'Fault sent once.':'Fault added to the recurring list.');
};
$('clearFaults').onclick=()=>command('DTC CLEAR',$('faultMsg'),'Recurring fault list cleared.');
function renderInspector(){
  const s=statusData;
  $('inspectQuery').hidden=inspectTab!=='signals';
  for(const b of $('inspectTabs').children)b.setAttribute('aria-pressed',b.dataset.tab===inspectTab);
  if(inspectTab==='signals'){
    const rows=s.signals.filter(v=>matches(v,$('inspectQuery').value)).map(v=>[esc(v.label),'<code>'+esc(v.name)+'</code>',esc(valueOf(v)),esc(unitOf(v)||'—'),esc(v.spn||'—'),esc(v.pgn),esc(v.sa),esc(number(v.age))+' s']);
    table('inspectorTable',['Label','Decoded name','Value','Unit','SPN','PGN','Source','Age'],rows,'No matching decoded signals.');
  }else if(inspectTab==='frames'){
    table('inspectorTable',['Time','CAN ID','Priority','PGN','Source','Payload'],s.recent.map(f=>[esc(f.t),esc(f.id),esc(f.prio),esc(f.pgn),'0x'+f.sa.toString(16).padStart(2,'0'),'<code>'+esc(f.data)+'</code>']),'Waiting for frames.');
  }else{
    table('inspectorTable',['PGN','Source','Description','Length','Payload','Count'],s.undecoded.map(f=>[esc(f.pgn),esc(f.sa),esc(f.name),esc(f.length),'<code>'+esc(f.data)+'</code>',esc(f.count)]),'All received messages have a decoder.');
  }
}
$('inspectTabs').onclick=e=>{if(e.target.dataset.tab){inspectTab=e.target.dataset.tab;if(statusData)renderInspector();}};
$('inspectQuery').oninput=()=>{if(statusData)renderInspector();};
function renderSettings(){
  const s=statusData,form=$('settingsForm');
  if(!settingsDirty){
    for(const [k,v] of Object.entries(s.config)){
      const el=form.elements[k];if(!el)continue;
      el.value=String(v);
      if(s.limits[k]) [el.min,el.max]=s.limits[k];
    }
  }
  const se=s.serial, file=s.current_file, test=s.upload.test||{}, verified=s.upload.verified_mf4;
  const message=$('settingsMsg');
  if(test.ok && message.classList.contains('error') && message.textContent.includes('S3 connection test')){
    report(message,settingsDirty?'Connection test passed. Save settings to apply your changes.':'Connection test passed.');
  }
  const values={'Profile':s.simulator_controls?'ECU simulator':'Hardware logger','Device ID':s.device_id,
    'Input':se.port,'Input state':se.error||(se.connected?'Connected':'Disconnected'),
    'Throughput':number(se.bytes_per_sec)+' B/s','Malformed / oversized':se.bad_lines+' / '+se.oversize_lines,
    'PGN mismatches':se.pgn_mismatch,'Decoder':s.dbc?.name||'Built-in signals','Current MF4':file?.name||'Waiting for frames',
    'Current file frames':file?.frames??0,'Pending / uploaded':s.files.pending+' / '+s.files.uploaded,
    'Current session target':s.upload.target,
    'S3 connection test':test.ok?'Passed'+(test.checked_at?' · '+test.checked_at:''):
      (test.error?'Failed · '+test.error:'No pass recorded')+' · Run s3_test.py'+(s.simulator_controls?' --local':''),
    'Automatic uploads':s.config.upload_enabled?'On':'Off',
    'Upload state':s.config.upload_enabled?(s.upload.gate||s.upload.error||s.upload.state):'Paused in this profile',
    'Last automatic upload this run':s.upload.last||'—'};
  if(verified){
    values['Last verified MF4']=verified.uri;
    values['Verified at']=verified.verified_at;
    values['Verified bytes']=number(verified.bytes);
    values['SHA-256']=verified.sha256;
  }
  if(se.transport!=='tcp')values['Baud / utilization']=se.baud+' / '+se.utilization_pct+'%';
  values['Frames received']=number(s.frames.total);
  details('health',values);
}
function renderS3(){
  const s=statusData, u=s.upload, check=u.session_check, test=u.test||{};
  const runTest='Run s3_test.py'+(s.simulator_controls?' --local':'');
  const sessionId=n=>String(n).padStart(8,'0');
  let targetStatus=runTest;
  if(check?.error)targetStatus='Listing failed · '+check.error;
  else if(check && check.target_session!==s.session)targetStatus='Session changed since last check · '+runTest;
  else if(check?.target_is_empty && check.target_prefix_exists)targetStatus='Listed in S3 · No objects returned';
  else if(check)targetStatus=check.target_is_empty?'Empty at last check':'Already contained objects at last check';
  details('s3Session',{
    'Profile':s.simulator_controls?'ECU simulator':'Hardware logger',
    'Device ID':s.device_id,
    'Current target':u.target,
    'Target at last check':targetStatus,
    'Latest S3 session for this device':!check||check.error?'—':check.latest_session==null?'No sessions found':sessionId(check.latest_session),
    'Latest S3 session across matching profiles':check?.latest_profile_session==null?'—':sessionId(check.latest_profile_session),
    'Next session at check':check?.next_session==null?'—':sessionId(check.next_session),
    'Session checked at':check?.checked_at||'—'
  });
  const connection=test.ok?'Upload and read-back verified':test.error?
    (test.sent?'Upload accepted · read-back failed':'Check failed')+' · '+test.error:runTest;
  details('s3Queue',{
    'Automatic uploads':s.config.upload_enabled?(u.gate||u.state):'Off',
    'Pending files':number(s.files.pending),
    'Verified automatic uploads this run':number(s.files.uploaded),
    'Last retry error':u.error||'—',
    'Connection check':connection,
    'Connection checked at':test.checked_at||'—'
  });
  const rows=Object.entries(u.transfers||{}).map(([source,t])=>{
    const sent=t.sent==='uploaded'?'Accepted by S3':t.sent==='already_exists'?'Object already in S3':'Unconfirmed';
    const received=t.verified?'SHA-256 matched':t.sent?'Verification failed':'—';
    return [esc(source==='manual'?'Manual':'Automatic'),'<code>'+esc(t.uri)+'</code>',esc(sent),esc(received),
      esc(t.checked_at),esc(t.error||'—')];
  });
  table('s3Transfers',['Source','Object','Sent to S3','Read back from S3','Checked at (UTC)','Error'],rows,
    'Waiting for an automatic upload or an s3_test.py --verify-file check.');
  const verified=u.monitor_verified_mf4;
  details('s3Verified',verified?
    {'Object':verified.uri,'Verified at':verified.verified_at,'Bytes':number(verified.bytes),'SHA-256':verified.sha256}:
    {'Status':'Waiting for an MF4 read-back.'});
}
$('settingsForm').oninput=()=>{settingsDirty=true;report($('settingsMsg'),'Unsaved changes');};
$('settingsForm').onsubmit=async e=>{
  e.preventDefault();if(!statusData)return;
  const config={};for(const [k,v] of new FormData(e.target)){
    config[k]=statusData.limits[k]?Number(v):k==='upload_enabled'?v==='true':v;
  }
  try{await request('/api/config',config);Object.assign(statusData.config,config);settingsDirty=false;report($('settingsMsg'),'Settings saved.');}
  catch(err){report($('settingsMsg'),err.message,true);}
};
function render(){
  const s=statusData;
  if(page==='telemetry'){
    $('frameRate').textContent=number(s.frames.rate);$('signalCount').textContent=number(s.signals.length);
    $('fileCount').textContent=number(s.files.written);$('faultCount').textContent=number(s.dtc.total);chartCards();
  }else if(page==='controls')renderControls();
  else if(page==='faults'){
    $('faultControls').hidden=!s.simulator_controls;
    table('faultTable',['Last seen','Type','SPN','FMI','Source','OC','Events'],s.dtc.seen.map(d=>[esc(d.last),esc(d.kind),esc(d.spn)+' '+esc(d.name||''),esc(d.fmi)+' '+esc(d.fmi_text||''),esc(d.sa),esc(d.oc),esc(number(d.count))]),'No faults received this session.');
  }else if(page==='inspector')renderInspector();else if(page==='settings')renderSettings();else if(page==='s3')renderS3();
}
function showError(e){$('offline').textContent=e.message;$('offline').hidden=false;}
function setLink(online){
  const connected=online&&statusData.serial.connected;
  $('connection').textContent=!online?'Dashboard offline':connected?'Input connected':'Input disconnected';
  $('connection').classList.toggle('live',connected);$('sideDot').classList.toggle('on',connected);
}
async function poll(){
  try{
    const s=await request('/api/status');statusData=s;
    const session=s.device_id+'/'+s.session;
    if(lastSession!==session){lastSession=session;catalogReady=false;chartData=null;chartKeys=[];$('charts').replaceChildren();controlSignature='';}
    if(!catalogReady){catalog=s.simulator_controls?await request('/api/controls'):[];catalogReady=true;}
    live=new Map(s.signals.map(v=>[keyOf(v),v]));known=new Map(catalog.map(v=>[keyOf(v),v]));
    for(const [k,v] of live)known.set(k,{...known.get(k),...v});
    document.querySelector('a[href="#controls"]').hidden=!s.simulator_controls;
    if(page==='controls'&&!s.simulator_controls)route();
    $('sideDevice').textContent=s.device_id;$('sideSource').textContent=s.simulator_controls?'Software ECU':s.serial.port;
    $('session').textContent='Session '+String(s.session).padStart(4,'0');$('clock').textContent=fmtTime(Date.now()/1000);
    setLink(true);$('offline').hidden=true;render();
    if(page==='telemetry')await updateTrends();
    if(page==='logs'){
      const logs=await request('/api/logs');
      table('logTable',['Time','Level','Message'],logs.reverse().map(l=>[esc(l.t),esc(l.level),esc(l.msg)]),'No log entries yet.');
    }
  }catch(e){setLink(false);showError(Error('Live updates paused. '+e.message));}
  finally{setTimeout(poll,1000);}
}
windowButtons();route();poll();
