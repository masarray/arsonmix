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
    const out=s.mainOutputs?.length===2?s.mainOutputs:s.outputPorts||[];
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
  // G3: ONE floating inspector follows the selected source/AUX, no new
  // DSP graph, no heavyweight matrix, no layout shifts in the MIX strips.
  const sendEditor=node('section','arSendEditor');
  sendEditor.id='arSendEditor';sendEditor.hidden=true;
  sendEditor.setAttribute('role','dialog');sendEditor.setAttribute('aria-label','Focused channel AUX sends');
  document.body.appendChild(sendEditor);
  let sendFocus=null,sendAnchor=null,sendFeedback='';
  const gainOptions=[[0,'OFF'],[.125,'−18 dB'],[.25,'−12 dB'],[.5,'−6 dB'],[1,'0 dB']];
  const panOptions=[[-1,'L100'],[-.5,'L50'],[0,'CENTER'],[.5,'R50'],[1,'R100']];
  const sendValueOptions=(items,value)=>items.concat(
    items.some(([v])=>Math.abs(v-value)<1e-6)?[]:[[value,value===0?'OFF':(20*Math.log10(Math.max(1e-8,value))).toFixed(1)+' dB']])
    .map(([v,label])=>'<option value="'+v+'"'+(Math.abs(v-value)<1e-6?' selected':'')+'>'+label+'</option>').join('');
  const showDb=g=>g>0?(20*Math.log10(g)).toFixed(0)+' dB':'OFF';
  function positionSendEditor(){
    if(sendEditor.hidden||!sendAnchor?.isConnected)return;
    const rect=sendAnchor.getBoundingClientRect(),width=sendEditor.offsetWidth,height=sendEditor.offsetHeight;
    const x=Math.max(8,Math.min(window.innerWidth-width-8,rect.right+9));
    const y=Math.max(8,Math.min(window.innerHeight-height-8,rect.top-15));
    sendEditor.style.left=x+'px';sendEditor.style.top=y+'px';
  }
  function closeSendEditor(){sendEditor.hidden=true;sendFocus=null;sendAnchor=null;}
  function drawSendEditor(){
    if(!sendFocus||sendEditor.hidden)return;
    const {from,bus}=sendFocus,s=route();
    if(!s.active||!s.ready||s.epoch!==sendFocus.epoch){closeSendEditor();return;}
    const aux=s.draftAux?.[bus]||{enabled:false,gain:1,main:true,mute:false};
    const sends=(s.draftSends||[]).filter(x=>x.from===from&&x.to===10+bus);
    const off=s.pending;
    const header='<header><div><small>CHANNEL '+(from+1)+' · INTERNAL SEND</small><strong>AUX '+(bus+1)+'</strong></div><button type="button" data-send-close aria-label="Close sends editor">×</button></header>';
    const auxPanel='<div class="arSendReturn"><label><input type="checkbox" data-send-return="enabled"'+(aux.enabled?' checked':'')+'> RETURN ON</label>'+
      '<label>RETURN <select data-send-return="gain">'+sendValueOptions(gainOptions,Number(aux.gain)||0)+'</select></label>'+
      '<label><input type="checkbox" data-send-return="main"'+(aux.main?' checked':'')+'> MAIN</label>'+
      '<label><input type="checkbox" data-send-return="mute"'+(aux.mute?' checked':'')+'> MUTE</label></div>';
    let lines='';
    for(const tap of ['PRE','POST']){
      const item=sends.find(x=>x.tap===tap);
      if(!item){
        lines+='<div class="arSendEditLine"><strong>'+tap+'</strong><button class="btn-180" data-send-create="'+tap+'"'+
           (off||!aux.enabled?' disabled':'')+'>＋ ADD '+tap+'</button></div>';continue;
      }
      lines+='<div class="arSendEditLine" data-send-tap="'+tap+'"><strong>'+tap+'</strong>'+
        '<label><input type="checkbox" data-send-enable="'+tap+'"'+(item.gain>0?' checked':'')+
        (off?' disabled':'')+'> ON</label>'+
        '<label>LEVEL <select data-send-gain="'+tap+'"'+(off?' disabled':'')+'>'+
          sendValueOptions(gainOptions,Number(item.gain))+'</select></label>'+
        '<label>PAN <select data-send-pan="'+tap+'"'+(off?' disabled':'')+'>'+
          panOptions.map(([v,label])=>'<option value="'+v+'"'+(Math.abs(Number(item.pan||0)-v)<1e-6?' selected':'')+'>'+label+'</option>').join('')+
          '</select></label><button class="btn-180" data-send-remove="'+tap+'"'+(off?' disabled':'')+'>REMOVE</button></div>';
    }
    const accepted=s.acceptedRoute?.sends||[];
    const liveSignature=JSON.stringify([
      accepted.filter(x=>x.from===from&&x.to===10+bus),
      s.acceptedRoute?.auxes?.[bus]||null
    ]);
    const draftSignature=JSON.stringify([sends,aux]);
    sendEditor.innerHTML=header+'<p class="arSendNotice">PRE = after FX, before fader · POST = after fader. Changes stay DRAFT until native ACK.</p>'+
      auxPanel+'<div class="arSendEditRows">'+lines+'</div><footer><span id="arSendFeedback" role="status">'+
      (sendFeedback||(s.pending?'NATIVE APPLY PENDING':liveSignature!==draftSignature?'DRAFT · NOT LIVE':'NATIVE LIVE / UNCHANGED'))+
      '</span><button type="button" class="btn-180" data-send-revert'+(off?' disabled':'')+'>REVERT</button>'+
      '<button type="button" class="btn-180 arSendApply" data-send-apply'+(off||s.monitor?' disabled':'')+
      '>APPLY · MUTED</button></footer>';
    sendEditor.querySelectorAll('input,select').forEach(x=>{if(off)x.disabled=true;});
    positionSendEditor();
  }
  function editSend(from,bus,anchor){
    const s=route();if(!isNative()||!s.ready||s.pending)return;
    sendFocus={from,bus,epoch:s.epoch};sendAnchor=anchor;sendFeedback='';
    sendEditor.hidden=false;drawSendEditor();
  }
  function sendMutation(fn){
    const result=fn();
    sendFeedback=result?.ok?result.message:result?.error||'Routing controller unavailable';
    refresh();drawSendEditor();
  }

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
      bank.innerHTML='<div class="arRouteBank"><span class="arBankTitle">PHYSICAL INPUT</span><select class="arBankSource" aria-label="Physical channel input source"></select><span class="arBankTitle">OUTPUT DESTINATION</span><span class="arBankOutput">MAIN · UNKNOWN</span><div class="arBankFormatRow"><select class="arBankChannelFormat" aria-label="Independent channel processing format" title="Processing MONO / STEREO independent from hardware input"><option value="MONO">CH M</option><option value="STEREO">CH L/R</option></select><span class="arBankStage">NATIVE PATCH REQUIRED</span></div><button class="btn-180 arBankPatch" data-mix-patch="input">OPEN INPUT PATCH</button></div><div class="arSendBank"><span class="arBankTitle">AUX SENDS · PRE / POST</span><div class="arSendSummary"></div><button class="btn-180 arBankPatch" data-mix-patch="internal">EDIT AUX SENDS</button></div>';
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
  const channelFormat=r=>['MONO','STEREO'].includes(r?.channelFormat)?
    r.channelFormat:(['BUS','STEREO'].includes(r?.mode)?'STEREO':'MONO');
  function channelState(s,i){return s.draftRoutes?.[i]||s.acceptedRoute?.channels?.[i]||{mode:'NONE',channelFormat:'MONO',left:-1,right:-1};}
  function refreshStrip(strip,s){
    const i=Number(strip.dataset.strip),r=channelState(s,i);
    const source=strip.querySelector('.arBankSource'),output=strip.querySelector('.arBankOutput');
    if(!source||!output)return;
    const current=valueFor(r);
    populate(source,portsFor(s,r),current);
    source.dataset.channel=String(i);source.disabled=!isNative()||!s.ready||!!s.pending;
    const format=strip.querySelector('.arBankChannelFormat');
    if(format){format.value=channelFormat(r);format.dataset.channel=String(i);
      format.disabled=!isNative()||!s.ready||!!s.pending;}
    output.textContent=r.mode==='NONE'?'UNPATCHED · NO INPUT':r.main?outputLabel(s):'MAIN OFF · SEND PATH ONLY';
    const accepted=s.acceptedRoute?.channels?.[i],
      dirty=current!==valueFor(accepted)||channelFormat(r)!==channelFormat(accepted);
    const stage=strip.querySelector('.arBankStage');
    stage.textContent=!s.ready?'NO LIVE DEVICE':dirty?'DRAFT · NOT APPLIED':r.mode==='NONE'?'UNPATCHED · SILENT':'LIVE · NATIVE ACK';
    stage.dataset.state=!s.ready?'offline':dirty?'draft':r.mode==='NONE'?'offline':'live';
    // Four native AUX destinations are always visible, even when empty.
    // Render only after a topology change, never on meter telemetry.
    const slots=Array.from({length:4},(_,bus)=>{
      const taps=(s.draftSends||[]).filter(e=>e.from===i&&e.to===10+bus);
      const enabled=!!s.draftAux?.[bus]?.enabled;
      const label=taps.length?taps.map(e=>e.tap+' '+showDb(Number(e.gain))).join(' / '):
        (enabled?'＋ ADD SEND':'AUX OFF');
      const accepted=(s.acceptedRoute?.sends||[]).filter(e=>e.from===i&&e.to===10+bus);
      const savedAux=s.acceptedRoute?.auxes?.[bus];
      const changed=JSON.stringify([taps,s.draftAux?.[bus]])!==
                    JSON.stringify([accepted,savedAux]);
      return {bus,label,enabled,state:changed?'draft':taps.length?'live':enabled?'ready':'off'};
    });
    const sig=JSON.stringify(slots);
    const holder=strip.querySelector('.arSendSummary');
    if(holder&&holder.dataset.signature!==sig){
      const buttons=slots.map(item=>{
        const b=node('button','arSendChip',item.label);
        b.type='button';b.dataset.sendFocus=String(item.bus);
        b.title='CH '+(i+1)+' → AUX '+(item.bus+1)+' · edit PRE/POST sends';
        b.setAttribute('aria-label',b.title);
        b.dataset.enabled=item.enabled?'true':'false';
        b.dataset.state=item.state;
        b.setAttribute('aria-label',b.title+' · '+item.state);
        b.prepend(node('b','arSendDest','AUX'+(item.bus+1)));
        return b;
      });
      holder.replaceChildren(...buttons);holder.dataset.signature=sig;
    }
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
    if(!sendEditor.hidden)drawSendEditor();
  }
  function refreshIO(){
    const s=route(),id=el('arIOIdentity'),body=el('arIOBody');
    const status=(lastStatus.driver||'NO DEVICE')+' · '+(Number(lastStatus.rate)||0)+' Hz · '+(Number(lastStatus.buffer)||0)+' samples';
    if(id.textContent!==status)id.textContent=status;
    overlay.querySelectorAll('[data-io-tab]').forEach(b=>{b.classList.toggle('active',b.dataset.ioTab===ioTab);b.setAttribute('aria-selected',String(b.dataset.ioTab===ioTab));});
    overlay.querySelector('[data-mix-apply]').disabled=!isNative()||!s.ready||s.monitor||s.pending;
    const signature=ioTab+'|'+JSON.stringify([s.epoch,s.revision,s.pending,s.draftRoutes,s.outputPorts,s.mainOutputs,s.draftMainOutputs,s.ports,deviceInventory]);
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
      body.appendChild(node('p','arIOHint','MAIN stereo output is now a real native patch. Select two verified physical pins and APPLY while OUTPUT is MUTED; other pins remain SILENT.'));
      const ports=Array.isArray(s.outputPorts)?s.outputPorts:[];
      const accepted=s.mainOutputs?.length===2?s.mainOutputs:ports.slice(0,2);
      const draft=s.draftMainOutputs?.length===2?s.draftMainOutputs:accepted;
      for(const [i,label] of ['MAIN LEFT','MAIN RIGHT'].entries()){
        const row=node('label','arIOOutputRow');
        const selector=node('select','arIOMainOutput');
        selector.dataset.side=String(i);
        selector.setAttribute('aria-label',label+' destination');
        for(const p of ports)if(Number.isInteger(p)&&p>=0){
          const option=node('option','','HW OUT '+(p+1));
          option.value=String(p);option.selected=p===draft[i];
          selector.appendChild(option);
        }
        selector.disabled=!isNative()||!s.ready||!!s.pending||ports.length<2;
        row.append(node('strong','',label),selector);
        body.appendChild(row);
      }
      body.appendChild(node('p','arIOHint','LIVE MAIN: '+(accepted.length===2?'HW '+(accepted[0]+1)+'/'+(accepted[1]+1):'UNAVAILABLE')+'. Remaining output channels stay silent. MONITOR/Matrix independent hardware buses are the next MS3 gate.'));
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
    if(e.target.matches('.arIOMainOutput')){
      const s=route(),selectors=[...overlay.querySelectorAll('.arIOMainOutput')];
      if(selectors.length!==2)return;
      const pins=selectors.map(x=>Number(x.value));
      const result=window.__arMixStageMainOutputs?.(pins[0],pins[1]);
      if(!result?.ok){el('arIOFeedback').textContent=result?.error||'Invalid MAIN output';}
      else el('arIOFeedback').textContent=result.message;
      lastSignature='';refresh();return;
    }
    if(e.target.matches('.arBankSource,.arIOSource'))changeSource(e.target);
    if(e.target.matches('.arBankChannelFormat')){
      const response=window.__arMixStageChannelFormat?.(
        Number(e.target.dataset.channel),e.target.value);
      if(!response?.ok){
        const message=response?.error||'Native format change unavailable';
        el('arIOFeedback').textContent=message;
        if(typeof window.flash==='function')window.flash(message);
      }
      lastSignature='';refresh();
    }
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
    if(b.dataset.mixMode){e.preventDefault();e.stopImmediatePropagation();closeSendEditor();setMode(b.dataset.mixMode);return;}
    if(b.dataset.sendFocus!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      editSend(Number(b.closest('[data-strip]')?.dataset.strip),Number(b.dataset.sendFocus),b);return;
    }
    if(b.dataset.sendClose!==undefined){e.preventDefault();closeSendEditor();return;}
    if(b.dataset.sendCreate!==undefined||b.dataset.sendRemove!==undefined){
      e.preventDefault();if(!sendFocus)return;
      const tap=b.dataset.sendCreate||b.dataset.sendRemove;
      sendMutation(()=>window.__arMixStageAuxSendEdit?.({...sendFocus,tap,
        action:b.dataset.sendCreate!==undefined?'create':'remove'}));return;
    }
    if(b.dataset.sendRevert!==undefined){e.preventDefault();sendMutation(()=>window.__arMixRevertMixerDraft?.());return;}
    if(b.dataset.sendApply!==undefined){
      e.preventDefault();const result=window.__arMixApplyMixerDraft?.();
      sendFeedback=result?.ok?'SUBMITTED · WAIT FOR NATIVE ACK':result?.error||'APPLY failed';
      drawSendEditor();return;
    }
    if(b.dataset.ioClose!==undefined){e.preventDefault();e.stopImmediatePropagation();closeIO();return;}
    if(b.dataset.ioTab){e.preventDefault();e.stopImmediatePropagation();ioTab=b.dataset.ioTab;lastSignature='';refreshIO();return;}
    if(b.dataset.ioAdvanced!==undefined){e.preventDefault();e.stopImmediatePropagation();closeIO();backend?.emitEvent?.('arMixAction',{action:'audioSetup'});return;}
    if(b.dataset.mixPatch){e.preventDefault();e.stopImmediatePropagation();const strip=b.closest('[data-strip]');window.__arMixOpenChannelPatch?.(Number(strip?.dataset.strip)||0,b.dataset.mixPatch);return;}
    if(b.dataset.mixApply!==undefined){e.preventDefault();e.stopImmediatePropagation();const response=window.__arMixApplyMixerDraft?.();if(response&&!response.ok)el('arIOFeedback').textContent=response.error;refresh();return;}
  },true);
  document.addEventListener('change',e=>{
    if(sendEditor.hidden||!sendFocus||!sendEditor.contains(e.target))return;
    const input=e.target;
    if(input.dataset.sendReturn!==undefined){
      const field=input.dataset.sendReturn;
      sendMutation(()=>window.__arMixStageAuxReturnEdit?.(sendFocus.bus,field,
        field==='gain'?Number(input.value):input.checked));
    }else {
      const field=input.dataset.sendGain!==undefined?'gain':
        input.dataset.sendPan!==undefined?'pan':
        input.dataset.sendEnable!==undefined?'enable':null;
      if(!field)return;
      const tap=input.dataset.sendGain||input.dataset.sendPan||input.dataset.sendEnable;
      sendMutation(()=>window.__arMixStageAuxSendEdit?.({...sendFocus,tap,action:field,
        value:field==='enable'?input.checked:Number(input.value)}));
    }
  },true);
  document.addEventListener('keydown',e=>{
    if(e.key!=='Escape')return;
    if(!sendEditor.hidden){closeSendEditor();e.stopImmediatePropagation();}
    else if(!overlay.hidden){closeIO();e.stopImmediatePropagation();}
  },true);
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
  backend?.addEventListener?.('arMixRoutingLabAck',ack=>{
    lastSignature='';
    const message=ack?.ok?'NATIVE ACK · SENDS/ROUTING LIVE':'REJECTED · '+(ack?.error||'Native route unchanged');
    el('arIOFeedback').textContent=message;
    sendFeedback=message;
    refresh();
  });
  window.addEventListener('arMixRoutingDraftChanged',()=>{lastSignature='';refresh();});
  window.addEventListener('resize',()=>positionSendEditor());
  document.addEventListener('pointerdown',e=>{
    if(!sendEditor.hidden&&!sendEditor.contains(e.target)&&!e.target.closest?.('[data-send-focus]')){
      closeSendEditor();
    }
  },true);
  setMode('FX');
})();
