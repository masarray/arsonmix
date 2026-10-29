// ArSonMix G1/G2 operator console: one presentation of monitor.js's native routing controller.
// UI data is a snapshot; only monitor.js can stage/apply the device-bound graph.
(() => {
  'use strict';
  const backend=window.__JUCE__?.backend;
  const mixer=document.getElementById('mixer');
  const host=document.querySelector('.workbench');
  if(!mixer||!host)return;
  let mode='FX', lastStatus={}, deviceInventory=null, ioTab='inputs', lastSignature='';
  const el=id=>document.getElementById(id);
  const node=(tag,cls,text)=>{const n=document.createElement(tag);if(cls)n.className=cls;if(text!==undefined)n.textContent=text;return n;};
  const route=()=>window.__arMixRouteView?.()||{ports:[],outputPorts:[],draftRoutes:[],draftSends:[],draftAux:[],ready:false,epoch:0,monitor:false};
  const isNative=()=>!!backend?.emitEvent;
  const portName=p=>String(p.label||('INPUT '+(p.port+1)));
  const portList=s=>Array.isArray(s.ports)?s.ports.filter(p=>Number.isInteger(p.port)&&p.port>=0):[];
  const valueFor=r=>r?.mode==='MONO'?'MONO:'+r.left:r?.mode==='STEREO'?'STEREO:'+r.left+':'+r.right:'NONE';
  const portsFor=(s,select)=>{
    const current=valueFor(select);
    const options=[['NONE','UNPATCHED']];
    const ports=portList(s),set=new Set(ports.map(p=>p.port));
    for(const p of ports)options.push(['MONO:'+p.port,'M · '+portName(p)]);
    for(const p of ports)if(set.has(p.port+1))options.push(['STEREO:'+p.port+':'+(p.port+1),'L/R · IN '+(p.port+1)+'/'+(p.port+2)]);
    if(current!=='NONE'&&!options.some(v=>v[0]===current))options.push([current,'CUSTOM L/R · PATCH']);
    return options;
  };
  const outputLabel=s=>{
    const out=s.outputPorts||[];
    return s.ready&&Number.isInteger(out[0])&&Number.isInteger(out[1])?
      'MAIN → HW '+(out[0]+1)+'/'+(out[1]+1):'MAIN → NO HW OUTPUT';
  };
  const toolbar=node('div','arMixerBankBar');
  toolbar.innerHTML='<div class="arMixerBankModes" role="group" aria-label="Mixer channel bank"><button type="button" class="btn-180 active" data-mix-mode="FX" aria-pressed="true">FX</button><button type="button" class="btn-180" data-mix-mode="ROUTE" aria-pressed="false">ROUTE</button><button type="button" class="btn-180" data-mix-mode="SENDS" aria-pressed="false">SENDS</button></div><div class="arMixerBankInfo" id="arMixerBankInfo" role="status">NO HARDWARE · PREVIEW</div><button type="button" class="btn-180 arMixApply" data-mix-apply title="MUTE OUTPUT first; native ACK required">APPLY</button><button type="button" class="btn-180 arMixIoOpen" data-io-open>AUDIO I/O</button>';
  host.insertBefore(toolbar,mixer);
  const overlay=node('div','arIOOverlay');
  overlay.id='arIOOverlay';overlay.hidden=true;
  overlay.innerHTML='<section class="arIOWindow" role="dialog" aria-modal="true" aria-label="ArSonMix hardware audio I/O"><header><div><small>ARSONMIX · AUDIO HARDWARE</small><h2>Audio I/O Setup</h2></div><button type="button" class="btn-180" data-io-close aria-label="Close Audio I/O">CLOSE</button></header><div class="arIOTop" id="arIOIdentity"></div><nav role="tablist" aria-label="Audio I/O views"><button class="btn-180 active" role="tab" data-io-tab="inputs">INPUTS</button><button class="btn-180" role="tab" data-io-tab="outputs">OUTPUTS</button><button class="btn-180" role="tab" data-io-tab="device">DEVICE</button></nav><div id="arIOBody" class="arIOBody"></div><footer><span id="arIOFeedback" role="status">Hardware changes require native ACK. Output stays MUTED on APPLY.</span><button type="button" class="btn-180" data-mix-apply>APPLY PATCH</button><button type="button" class="btn-180" data-io-advanced>DRIVER SETTINGS</button></footer></section>';
  document.body.appendChild(overlay);
  function openIO(){document.getElementById('arRouteClose')?.click();overlay.hidden=false;refreshIO();if(isNative())backend.emitEvent('arMixAction',{action:'audioDeviceInventory'});}
  function closeIO(){overlay.hidden=true;}
  function setMode(next){
    if(!['FX','ROUTE','SENDS'].includes(next))return;
    mode=next;mixer.dataset.ioMode=mode;
    toolbar.querySelectorAll('[data-mix-mode]').forEach(b=>{const selected=b.dataset.mixMode===mode;b.classList.toggle('active',selected);b.setAttribute('aria-pressed',String(selected));});
    refresh();
  }
  function decorate(){
    for(const strip of mixer.querySelectorAll('.strip[data-strip]')){
      if(strip.querySelector('.arChannelBank'))continue;
      const bank=node('div','arChannelBank');
      bank.innerHTML='<div class="arRouteBank"><span class="arBankTitle">PHYSICAL INPUT</span><select class="arBankSource" aria-label="Physical channel input source"></select><span class="arBankTitle">OUTPUT DESTINATION</span><span class="arBankOutput">MAIN · UNKNOWN</span><span class="arBankStage">NATIVE PATCH REQUIRED</span><button class="btn-180 arBankPatch" data-mix-patch="input">OPEN INPUT PATCH</button></div><div class="arSendBank"><span class="arBankTitle">AUX SENDS · PRE / POST</span><div class="arSendSummary"></div><button class="btn-180 arBankPatch" data-mix-patch="internal">EDIT AUX SENDS</button></div>';
      strip.querySelector('.rack')?.insertAdjacentElement('afterend',bank);
    }
  }
  function populate(select,options,current){
    const signature=JSON.stringify(options);
    if(select.dataset.options!==signature){
      const nodes=options.map(([value,label])=>new Option(label,value));
      select.replaceChildren(...nodes);select.dataset.options=signature;
    }
    if(select.value!==current)select.value=current;
  }
  function channelState(s,i){return s.draftRoutes?.[i]||s.acceptedRoute?.channels?.[i]||{mode:'NONE',left:-1,right:-1};}
  function refreshStrip(strip,s){
    const i=Number(strip.dataset.strip),r=channelState(s,i);
    const source=strip.querySelector('.arBankSource'),output=strip.querySelector('.arBankOutput');
    if(!source||!output)return;
    const current=valueFor(r);
    populate(source,portsFor(s,r),current);
    source.dataset.channel=String(i);source.disabled=!isNative()||!s.ready||!!s.pending;
    output.textContent=outputLabel(s);
    const accepted=s.acceptedRoute?.channels?.[i],dirty=current!==valueFor(accepted);
    const stage=strip.querySelector('.arBankStage');
    stage.textContent=!s.ready?'NO LIVE DEVICE':dirty?'DRAFT · NOT APPLIED':r.mode==='NONE'?'UNPATCHED · SILENT':'LIVE · NATIVE ACK';
    stage.dataset.state=!s.ready?'offline':dirty?'draft':r.mode==='NONE'?'offline':'live';
    const sends=(s.draftSends||[]).filter(e=>e.from===i&&e.to>=10&&e.to<14);
    const holder=strip.querySelector('.arSendSummary');
    const caption=sends.length?sends.slice(0,4).map(e=>'AUX'+(e.to-9)+' '+e.tap+' '+(e.gain<=0?'−∞':(20*Math.log10(e.gain)).toFixed(0)+'dB')):['NO AUX SENDS'];
    const sig=caption.join('|');
    if(holder.dataset.signature!==sig){holder.replaceChildren(...caption.map(c=>node('span','arSendChip',c)));holder.dataset.signature=sig;}
  }
  function refresh(){
    decorate();const s=route();
    const info=el('arMixerBankInfo');
    const caption=isNative()?
      (s.ready?'E'+s.epoch+' / R'+(s.revision||0)+' · '+portList(s).length+' IN · '+(s.outputPorts?.length||0)+' OUT · '+(s.monitor?'OUTPUT ARMED':'OUTPUT MUTED'):'NO VERIFIED DEVICE · OUTPUT MUTED'):
      'WEB PREVIEW · NO LIVE AUDIO';
    if(info.textContent!==caption)info.textContent=caption;
    toolbar.querySelector('[data-mix-apply]').disabled=!isNative()||!s.ready||!!s.pending||!!s.monitor;
    for(const strip of mixer.querySelectorAll('.strip[data-strip]'))refreshStrip(strip,s);
    if(!overlay.hidden)refreshIO();
  }
  function refreshIO(){
    const s=route(),id=el('arIOIdentity'),body=el('arIOBody');
    const status=(lastStatus.driver||'NO DEVICE')+' · '+(Number(lastStatus.rate)||0)+' Hz · '+(Number(lastStatus.buffer)||0)+' samples';
    if(id.textContent!==status)id.textContent=status;
    overlay.querySelectorAll('[data-io-tab]').forEach(b=>{b.classList.toggle('active',b.dataset.ioTab===ioTab);b.setAttribute('aria-selected',String(b.dataset.ioTab===ioTab));});
    overlay.querySelector('[data-mix-apply]').disabled=!isNative()||!s.ready||s.monitor||s.pending;
    const signature=ioTab+'|'+JSON.stringify([s.epoch,s.revision,s.pending,s.draftRoutes,s.outputPorts,s.ports,deviceInventory]);
    if(lastSignature===signature)return;
    lastSignature=signature;body.replaceChildren();
    if(ioTab==='inputs'){
      const desc=node('p','arIOHint',portList(s).length+' verified active input pins. Assign MONO (M) or STEREO (L/R); APPLY only while output MUTED.');
      body.appendChild(desc);
      const grid=node('div','arIOInputGrid');
      grid.append(node('strong','arIOGridLabel','LOGICAL CHANNEL'),node('strong','arIOGridLabel','PHYSICAL SOURCE'));
      for(let i=0;i<10;i++){
        const r=channelState(s,i);
        const logicalName=document.querySelector('#mixer [data-strip="'+i+'"] .strip-caption')?.textContent?.trim()||'INPUT';
        grid.appendChild(node('span','arIOChannelName','CH '+String(i+1).padStart(2,'0')+' · '+logicalName));
        const select=node('select','arIOSource');
        select.dataset.channel=String(i);select.setAttribute('aria-label','CH '+(i+1)+' physical input');
        populate(select,portsFor(s,r),valueFor(r));select.disabled=!isNative()||!s.ready||!!s.pending;
        grid.appendChild(select);
      }
      body.appendChild(grid);
      body.appendChild(node('p','arIOHint','For non-adjacent stereo L/R or BUS, open the full Routing patch matrix.'));
    }else if(ioTab==='outputs'){
      body.appendChild(node('p','arIOHint','Physical MAIN L/R readback from the current device. Independent AUX / monitor hardware output is not yet supported.'));
      const ports=Array.isArray(s.outputPorts)?s.outputPorts:[];
      for(const [i,label] of ['MAIN LEFT','MAIN RIGHT'].entries()){
        const p=ports[i];
        const row=node('div','arIOOutputRow');
        row.append(node('strong','',label),node('span','',s.ready&&Number.isInteger(p)&&p>=0?'HW OUT '+(p+1)+' · READ ONLY':'NOT AVAILABLE · NO VERIFIED OUTPUT'));
        body.appendChild(row);
      }
      body.appendChild(node('p','arIOHint','AUX1–4 are internal stereo FX returns, not physical output sockets.'));
    }else{
      body.appendChild(node('p','arIOHint','Actual driver types and endpoint names reported by JUCE. To change rate, buffer, or endpoint safely, use DRIVER SETTINGS; output is muted during reconfiguration.'));
      if(!deviceInventory?.types?.length)body.appendChild(node('p','arIOHint','No device inventory yet · select a verified Windows audio device.'));
      for(const type of deviceInventory?.types||[]){
        const card=node('div','arIODeviceType');
        card.append(node('strong','',type.name+(type.active?' · CURRENT':'')));
        card.appendChild(node('span','','IN: '+(type.inputs?.join(', ')||'NONE')));
        card.appendChild(node('span','','OUT: '+(type.outputs?.join(', ')||'NONE')));
        body.appendChild(card);
      }
    }
    el('arIOFeedback').textContent=!isNative()?'PREVIEW ONLY · no native hardware':
      s.monitor?'MUTE OUTPUT BEFORE APPLY':
      s.pending?'WAITING FOR NATIVE ACK':
      s.ready?'E'+s.epoch+' · Native APPLY required for input edits':'CHOOSE A VERIFIED DIRECT DEVICE';
  }
  function changeSource(select){
    const channel=Number(select.dataset.channel);
    const value=select.value,parts=value.split(':');
    const mode=parts[0],left=mode==='NONE'?-1:Number(parts[1]),right=mode==='STEREO'?Number(parts[2]):-1;
    const response=window.__arMixStageChannelInput?.(channel,mode,left,right);
    if(!response?.ok){
      const message=response?.error||'Native INPUT PATCH unavailable';
      el('arIOFeedback').textContent=message;
      if(typeof window.flash==='function')window.flash(message);
    }
    lastSignature='';refresh();
  }
  document.addEventListener('change',e=>{
    if(e.target.matches('.arBankSource,.arIOSource'))changeSource(e.target);
  },true);
  // The legacy native VST3 bridge registers document capture before this module.
  // Window capture intercepts only AUDIO SETUP, without changing FX/Orbit ownership.
  window.addEventListener('click',e=>{
    const setup=e.target.closest?.('[data-native-action="audioSetup"]');
    if(!setup)return;
    e.preventDefault();e.stopImmediatePropagation();openIO();
  },true);
  document.addEventListener('click',e=>{
    const b=e.target.closest('button');
    if(!b)return;
    if(b.dataset.nativeAction==='audioSetup'||b.dataset.ioOpen!==undefined){e.preventDefault();e.stopImmediatePropagation();openIO();return;}
    if(b.dataset.mixMode){e.preventDefault();e.stopImmediatePropagation();setMode(b.dataset.mixMode);return;}
    if(b.dataset.ioClose!==undefined){e.preventDefault();e.stopImmediatePropagation();closeIO();return;}
    if(b.dataset.ioTab){e.preventDefault();e.stopImmediatePropagation();ioTab=b.dataset.ioTab;lastSignature='';refreshIO();return;}
    if(b.dataset.ioAdvanced!==undefined){e.preventDefault();e.stopImmediatePropagation();closeIO();backend?.emitEvent?.('arMixAction',{action:'audioSetup'});return;}
    if(b.dataset.mixPatch){e.preventDefault();e.stopImmediatePropagation();const strip=b.closest('[data-strip]');window.__arMixOpenChannelPatch?.(Number(strip?.dataset.strip)||0,b.dataset.mixPatch);return;}
    if(b.dataset.mixApply!==undefined){e.preventDefault();e.stopImmediatePropagation();const response=window.__arMixApplyMixerDraft?.();if(response&&!response.ok)el('arIOFeedback').textContent=response.error;refresh();return;}
  },true);
  document.addEventListener('keydown',e=>{if(e.key==='Escape'&&!overlay.hidden){closeIO();e.stopImmediatePropagation();}},true);
  const observer=new MutationObserver(records=>{if(records.some(r=>r.type==='childList'))refresh();});
  observer.observe(mixer,{childList:true});
  backend?.addEventListener?.('arMixStatus',d=>{
    lastStatus=d;
    // 12Hz native telemetry: update only when topology, epoch or safety changes.
    const s=d.dryRoutingLab||{},key=JSON.stringify([s.epoch,s.revision,s.ready,s.ports,s.outputPorts,s.acceptedRoute,d.monitor]);
    if(window.__armixIoStatusKey!==key){window.__armixIoStatusKey=key;lastSignature='';refresh();}
    if(!overlay.hidden){const identity=(d.driver||'')+'/'+(d.rate||0)+'/'+(d.buffer||0);if(overlay.dataset.deviceIdentity!==identity){overlay.dataset.deviceIdentity=identity;refreshIO();}}
  });
  backend?.addEventListener?.('arMixAudioDeviceInventory',d=>{deviceInventory=d;lastSignature='';if(!overlay.hidden)refreshIO();});
  backend?.addEventListener?.('arMixRoutingLabAck',ack=>{lastSignature='';el('arIOFeedback').textContent=ack?.ok?'NATIVE ACK · INPUT PATCH LIVE':'REJECTED · '+(ack?.error||'Routing unchanged');refresh();});
  window.addEventListener('arMixRoutingDraftChanged',()=>{lastSignature='';refresh();});
  setMode('FX');
})();
