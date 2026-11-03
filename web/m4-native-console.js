// SPDX-License-Identifier: AGPL-3.0-only
// Native M4 physical-console compatibility projection.
// No WebAudio, no fabricated ChannelIds, no alternate DSP, no browser-owned
// source of truth. Universal V6 graph/capabilities remain untouched.
(() => {
  'use strict';
  const api=window.__JUCE__?.backend;
  const root=document.getElementById('arV6Console');
  if(!api?.addEventListener||!api?.emitEvent||!root)return;
  const validDecimal=v=>typeof v==='string'&&/^(0|[1-9][0-9]*)$/.test(v)&&
    BigInt(v)<=18446744073709551615n;
  const h=(tag,cls,text)=>{
    const n=document.createElement(tag);
    if(cls)n.className=cls;
    if(text!==undefined)n.textContent=String(text);
    return n;
  };
  const host=h('section','arM4Host');
  host.id='arM4NativeConsole';
  host.setAttribute('aria-label','Native M4 physical mixer · verified route');
  host.hidden=true;
  const header=h('header','arM4Header');
  const identity=h('div','arM4Identity');
  identity.append(h('span','arM4Eyebrow','ARSONMIX · M4 NATIVE / PHYSICAL PCM'),
    h('span','arM4Device','WAITING FOR ACCEPTED HARDWARE'));
  const state=h('span','arM4State','NOT READY');
  const openSetup=h('button','arV6Button','AUDIO SETUP');
  openSetup.type='button';openSetup.setAttribute('aria-label','Open native audio setup');
  const patch=h('button','arV6Button','ROUTING MATRIX');
  patch.type='button';patch.setAttribute('aria-label','Open native routing matrix');
  header.append(identity,state,openSetup,patch);
  const notice=h('div','arM4Notice');
  notice.setAttribute('role','status');
  notice.textContent='Native route readback only · no ChannelId universal cutover';
  const scroll=h('div','arM4Scroll');
  scroll.setAttribute('aria-label','Ten real native M4 input strips');
  host.append(header,notice,scroll);
  root.append(host);
  let accepted=null,signature='',manualMatrix=false,seq=0;
  const pending=new Map(),pendingByLane=new Map();
  const prefix='m4:'+Math.random().toString(36).slice(2)+':'+Date.now().toString(36);
  const isConnected=s=>{
    const d=s?.device,legacy=s?.legacyM4,c=s?.graph?.capabilities;
    if(s?.schemaVersion!==6||s.productionCallbackLive!==false||
       !d||!legacy||!c||s.graph.channels?.length!==0||
       c.supportedOps?.length!==0||!validDecimal(d.epoch)||
       !validDecimal(legacy.routingRevision)||d.epoch==='0'||
       legacy.controlPlane!=='LEGACY_M4_ORDINAL'||
       legacy.active!==true||legacy.routeAccepted!==true||
       !Array.isArray(legacy.acceptedChannels)||legacy.acceptedChannels.length!==10||
       !Array.isArray(legacy.channelControls)||legacy.channelControls.length!==10||
       !Array.isArray(legacy.supportedOps)||!Array.isArray(d.inputEndpoints)||
       !Array.isArray(d.enabledInputs)||!Array.isArray(d.enabledOutputs)||
       d.enabledOutputs.length<2||!validDecimal(s.graph.revision)||
       !validDecimal(s.graph.rosterRevision))return false;
    const allowed=new Set(['SET_FADER','SET_PAN','SET_MUTE','SET_SOLO','SET_SOURCE',
      'SET_PROCESSING_FORMAT','SET_PRIMARY_OUT','SET_AUX_FADER',
      'SET_SEND_LEVEL','SET_INSERT_BYPASS','OPEN_INSERT_EDITOR','SET_MONITOR_LEVEL']);
    if(legacy.supportedOps.some(op=>!allowed.has(op))||
       new Set(legacy.supportedOps).size!==legacy.supportedOps.length)return false;
    for(let i=0;i<10;++i){
      const row=legacy.acceptedChannels[i],ctl=legacy.channelControls[i];
      if(!row||!['NONE','BUS','MONO','STEREO'].includes(row.mode)||
         !Number.isInteger(row.left)||!Number.isInteger(row.right)||
         !ctl||!Number.isFinite(ctl.db)||!Number.isFinite(ctl.pan)||
         typeof ctl.mute!=='boolean'||typeof ctl.solo!=='boolean')return false;
      if(['MONO','STEREO'].includes(row.mode)&&
         !d.enabledInputs.includes(row.left))return false;
      if(row.mode==='STEREO'&&(!d.enabledInputs.includes(row.right)||
         row.right===row.left))return false;
    }
    return true;
  };
  const enabled=op=>accepted?.legacyM4?.supportedOps?.includes(op)===true;
  const panelVisible=()=>{
    const on=!!accepted&&!manualMatrix;
    host.hidden=!on;root.classList.toggle('m4-physical-active',on);
    if(on)document.documentElement.classList.add('ar-v6-active');
  };
  const currentValue=(s,lane,op,insertId)=>{
    const c=s.legacyM4.channelControls[lane-1];
    if(op==='SET_FADER')return c.db;
    if(op==='SET_PAN')return c.pan;
    if(op==='SET_MUTE')return c.mute;
    if(op==='SET_SOLO')return c.solo;
    if(op==='SET_SOURCE'){
      const row=s.legacyM4.acceptedChannels[lane-1];
      return row.mode==='MONO'?('in:'+row.left):'';
    }
    if(op==='SET_INSERT_BYPASS'){
      const rack=s.legacyM4.fxInstances?.[lane-1];
      return rack?.find(f=>f.instanceId===insertId)?.bypassed;
    }
    return undefined;
  };
  const matches=(request,s)=>{
    const actual=currentValue(s,request.lane,request.op,request.insertId);
    return typeof actual==='number'
      ? Math.abs(actual-request.expected)<=0.11
      : actual===request.expected;
  };
  const unlock=(request)=>{
    pending.delete(request.id);
    if(pendingByLane.get(request.lane)===request.id)pendingByLane.delete(request.lane);
  };
  const requestSnapshot=()=>api.emitEvent('arMixConsoleSubscribe',
    {protocol:'v6',requester:prefix});
  const submit=(lane,op,payload,expected)=>{
    if(!accepted||!enabled(op)||pendingByLane.has(lane))return false;
    const s=accepted,id=prefix+':'+(++seq);
    const command={requestId:id,deviceEpoch:s.device.epoch,
      baseGraphRevision:s.graph.revision,
      baseRosterRevision:s.graph.rosterRevision,
      legacyRoutingRevision:s.legacyM4.routingRevision,
      // THIS IS EXPLICITLY A LEGACY M4 ORDINAL (1..10), not a ChannelId.
      targetChannelId:String(lane),operation:op,payload};
    const record={id,lane,op,expected,epoch:s.device.epoch,
      legacyRevision:s.legacyM4.routingRevision,insertId:payload.insertId,acked:false,readsAfterAck:0};
    pending.set(id,record);pendingByLane.set(lane,id);
    notice.textContent='PENDING · CH '+String(lane).padStart(2,'0')+
      ' · '+op+' · awaiting native ACK + PCM readback';
    refresh();
    api.emitEvent('arMixConsoleIntent',command);
    return true;
  };
  const field=(label,control)=>{
    const wrap=h('label','arM4Field');
    wrap.append(h('span','arM4FieldLabel',label),control);
    return wrap;
  };
  function makeStrip(lane,row,ctl,s){
    const article=h('article','arV6Strip arM4Strip');
    article.dataset.m4Lane=String(lane);
    article.setAttribute('aria-label','Native channel '+lane);
    const head=h('div','arV6StripHead');
    head.append(h('span','arV6Role','INPUT · NATIVE M4'),
      h('strong','arV6Name','CH '+String(lane).padStart(2,'0')));
    const upper=h('div','arM4Upper');
    const src=h('select','arV6Select');
    src.dataset.m4Op='SET_SOURCE';
    src.setAttribute('aria-label','CH '+lane+' physical input (mono)');
    const sourceOptions=[{id:'',label:row.mode==='STEREO'?'STEREO · MATRIX':row.mode==='BUS'?'BUS · MATRIX':'UNPATCHED'}];
    for(const p of s.device.inputEndpoints){
      if(p.active===true&&s.device.enabledInputs.includes(p.port))
        sourceOptions.push({id:p.id,label:p.label||('IN '+(p.port+1))});
    }
    for(const option of sourceOptions){
      const node=h('option','',option.label);node.value=option.id;src.append(node);
    }
    src.value=row.mode==='MONO'?'in:'+row.left:'';
    src.disabled=row.mode==='STEREO'||row.mode==='BUS'||!enabled('SET_SOURCE');
    upper.append(field('INPUT',src));
    const fx=h('div','arM4Fx');
    fx.append(h('span','arV6SectionTitle','REAL VST3 · ACCEPTED INSTANCES'));
    const racks=s.legacyM4.fxInstances;
    const chain=Array.isArray(racks)?racks[lane-1]:null;
    if(Array.isArray(chain)&&chain.length){
      for(const insert of chain){
        if(typeof insert.instanceId!=='string'||!insert.instanceId)continue;
        const line=h('div','arM4FxRow');
        line.append(h('span','arM4FxName',insert.catalogId||'VST3'));
        const bypass=h('button','arV6Button',insert.bypassed?'OFF':'ON');
        bypass.type='button';bypass.dataset.m4Op='SET_INSERT_BYPASS';
        bypass.dataset.insertId=insert.instanceId;
        bypass.dataset.bypassed=String(!!insert.bypassed);
        bypass.disabled=!enabled('SET_INSERT_BYPASS');
        bypass.setAttribute('aria-label','Toggle native VST3 bypass');
        const edit=h('button','arV6Button','EDIT');edit.type='button';
        edit.dataset.m4Op='OPEN_INSERT_EDITOR';edit.dataset.insertId=insert.instanceId;
        edit.disabled=!enabled('OPEN_INSERT_EDITOR');
        line.append(bypass,edit);fx.append(line);
      }
    }else fx.append(h('span','arM4Empty','No native insert'));
    upper.append(fx);
    const controls=h('div','arM4Controls');
    const pan=h('input','arV6PanRange');pan.type='range';
    pan.min='-100';pan.max='100';pan.step='1';
    pan.dataset.m4Op='SET_PAN';pan.value=String(Math.round(ctl.pan));
    pan.setAttribute('aria-label','CH '+lane+' native PAN');
    const mute=h('button','arV6Button',ctl.mute?'M ON':'M');
    mute.type='button';mute.dataset.m4Op='SET_MUTE';
    mute.setAttribute('aria-label','CH '+lane+' native MUTE');
    const solo=h('button','arV6Button',ctl.solo?'S ON':'S');
    solo.type='button';solo.dataset.m4Op='SET_SOLO';
    solo.setAttribute('aria-label','CH '+lane+' native SOLO');
    controls.append(field('PAN',pan),mute,solo);
    const lower=h('div','arM4Lower');
    const fader=h('input','arV6Fader');fader.type='range';
    fader.min='-60';fader.max='10';fader.step='.1';
    fader.dataset.m4Op='SET_FADER';fader.value=String(ctl.db);
    fader.setAttribute('aria-label','CH '+lane+' native fader dB');
    const db=h('span','arM4Db',Number(ctl.db).toFixed(1)+' dB');
    const meter=h('div','arM4Meter');
    const fillL=h('span','arM4MeterL');
    const fillR=h('span','arM4MeterR');
    meter.append(fillL,fillR);
    lower.append(fader,db,meter);
    const foot=h('div','arM4Footer',row.mode+' · M4 ORDINAL '+lane);
    article.append(head,upper,controls,lower,foot);
    return article;
  }
  const stableStructure=s=>JSON.stringify([s.device.epoch,s.legacyM4.routingRevision,
    s.legacyM4.acceptedChannels.map(x=>[x.mode,x.left,x.right]),
    s.legacyM4.fxInstances||[],s.device.inputEndpoints]);
  function render(){
    scroll.replaceChildren();
    for(let i=0;i<10;++i)scroll.append(makeStrip(
      i+1,accepted.legacyM4.acceptedChannels[i],
      accepted.legacyM4.channelControls[i],accepted));
  }
  function refresh(){
    if(!accepted)return;
    state.textContent=accepted.device.outputArmed?'M4 PCM · ARMED':'M4 PCM · DISARMED';
    state.dataset.armed=String(accepted.device.outputArmed);
    const device=identity.querySelector('.arM4Device');
    device.textContent=[accepted.device.backend,accepted.device.sampleRate?
      accepted.device.sampleRate+' Hz':'RATE —',accepted.device.bufferFrames?
      accepted.device.bufferFrames+' frames':'BUFFER —',
      'REV '+accepted.legacyM4.routingRevision].join(' · ');
    for(const el of scroll.children){
      const lane=Number(el.dataset.m4Lane),c=accepted.legacyM4.channelControls[lane-1];
      const locked=pendingByLane.has(lane);
      for(const control of el.querySelectorAll('[data-m4-op]')){
        const op=control.dataset.m4Op;
        control.disabled=locked||!enabled(op)||
          (op==='SET_SOURCE'&&['STEREO','BUS'].includes(
            accepted.legacyM4.acceptedChannels[lane-1].mode));
        if(locked)continue;
        if(op==='SET_FADER')control.value=String(c.db);
        if(op==='SET_PAN')control.value=String(c.pan);
        if(op==='SET_MUTE')control.textContent=c.mute?'M ON':'M';
        if(op==='SET_SOLO')control.textContent=c.solo?'S ON':'S';
      }
      const label=el.querySelector('.arM4Db');
      if(label)label.textContent=Number(c.db).toFixed(1)+' dB';
      const peaksL=accepted.legacyM4.channelPeaksL||[];
      const peaksR=accepted.legacyM4.channelPeaksR||[];
      for(const [side,levels] of [['L',peaksL],['R',peaksR]]){
        const value=Number(levels[lane-1]);
        const dB=Number.isFinite(value)&&value>0?20*Math.log10(value):-60;
        const fill=el.querySelector('.arM4Meter'+side);
        if(fill)fill.style.height=Math.max(0,Math.min(100,(dB+60)*100/60))+'%';
      }
    }
  }
  function receiveSnapshot(s){
    if(!isConnected(s)){
      accepted=null;signature='';
      pending.clear();pendingByLane.clear();
      host.hidden=true;root.classList.remove('m4-physical-active');
      if(s?.productionCallbackLive!==true)
        document.documentElement.classList.remove('ar-v6-active');
      return;
    }
    if(accepted){
      if(BigInt(s.device.epoch)<BigInt(accepted.device.epoch))return;
      if(s.device.epoch===accepted.device.epoch&&
         BigInt(s.legacyM4.routingRevision)<BigInt(accepted.legacyM4.routingRevision))return;
      if(s.device.epoch!==accepted.device.epoch){
        pending.clear();pendingByLane.clear();
        notice.textContent='DEVICE CHANGED · native M4 control transactions invalidated';
      }
    }
    accepted=s;
    const next=stableStructure(s);
    if(next!==signature){signature=next;render();}
    for(const req of pending.values()){
      if(req.epoch!==s.device.epoch||
         BigInt(s.legacyM4.routingRevision)<BigInt(req.legacyRevision)){
        unlock(req);continue;
      }
      if(req.acked&&matches(req,s)){
        unlock(req);
        notice.textContent='ACCEPTED · CH '+String(req.lane).padStart(2,'0')+
          ' · native PCM control confirmed by subsequent snapshot';
      }else if(req.acked&&++req.readsAfterAck>=5){
        // Bounded fail-closed: never lock a strip forever if the vendor
        // rejected/normalized a target after ACK without matching readback.
        unlock(req);
        notice.textContent='READBACK MISMATCH · native target not confirmed';
      }
    }
    refresh();panelVisible();
  }
  function receiveAck(a){
    const ack=a?.ack||a,req=pending.get(ack?.requestId);
    if(!req)return;
    if(!validDecimal(ack.deviceEpoch)||ack.deviceEpoch!==req.epoch||
       !validDecimal(ack.graphRevision)||!validDecimal(ack.rosterRevision)||
       !['ACCEPTED','REJECTED','REQUIRES_DEVICE_CHANGE'].includes(ack.status)){
      unlock(req);notice.textContent='INVALID ACK · keeping previous native state';
      refresh();requestSnapshot();return;
    }
    if(ack.status!=='ACCEPTED'){
      unlock(req);
      notice.textContent='NATIVE REJECTED · '+String(ack.errorCode||ack.status).slice(0,90);
    }else req.acked=true;
    refresh();requestSnapshot();
  }
  scroll.addEventListener('change',e=>{
    const node=e.target.closest('[data-m4-op]'),article=node?.closest('[data-m4-lane]');
    if(!node||!article||node.disabled||!accepted)return;
    const lane=Number(article.dataset.m4Lane),op=node.dataset.m4Op;
    if(op==='SET_FADER'){
      const v=Number(node.value);
      if(Number.isFinite(v)&&v>=-60&&v<=10)submit(lane,op,{valueDb:v},v);
    }else if(op==='SET_PAN'){
      const v=Number(node.value);
      if(Number.isFinite(v)&&v>=-100&&v<=100)submit(lane,op,{value:v},v);
    }else if(op==='SET_SOURCE'){
      const chosen=accepted.device.inputEndpoints.find(p=>p.id===node.value&&
        p.active===true&&accepted.device.enabledInputs.includes(p.port));
      if(chosen)submit(lane,op,{portId:chosen.id,sourceFormat:'MONO'},chosen.id);
    }
  });
  scroll.addEventListener('click',e=>{
    const node=e.target.closest('button[data-m4-op]');
    const article=node?.closest('[data-m4-lane]');
    if(!node||!article||node.disabled||!accepted)return;
    const lane=Number(article.dataset.m4Lane),op=node.dataset.m4Op;
    if(op==='SET_MUTE'||op==='SET_SOLO'){
      const current=currentValue(accepted,lane,op);
      submit(lane,op,{value:!current},!current);
    }else if(op==='SET_INSERT_BYPASS'||op==='OPEN_INSERT_EDITOR'){
      const token=node.dataset.insertId;
      const chain=accepted.legacyM4.fxInstances?.[lane-1]||[];
      const fx=chain.find(x=>x.instanceId===token);
      if(!fx)return;
      const bypass=!fx.bypassed;
      submit(lane,op,op==='SET_INSERT_BYPASS'
        ?{insertId:token,bypassed:bypass}:{insertId:token},
        op==='SET_INSERT_BYPASS'?bypass:undefined);
    }
  });
  openSetup.addEventListener('click',()=>document.getElementById('audioSetupBtn')?.click());
  patch.addEventListener('click',()=>{
    manualMatrix=true;panelVisible();
    const back=document.getElementById('arV6Return');if(back)back.hidden=false;
    window.__arMixOpenRoutingLab?.();
  });
  document.getElementById('arV6Return')?.addEventListener('click',()=>{
    manualMatrix=false;panelVisible();
  });
  api.addEventListener('arMixConsoleSnapshot',receiveSnapshot);
  api.addEventListener('arMixConsoleAck',receiveAck);
  requestSnapshot(); // Register first, then force exact native readback.
  window.__arMixM4NativeReadOnly=()=>({live:!!accepted,epoch:accepted?.device.epoch||null,
    legacyRoutingRevision:accepted?.legacyM4.routingRevision||null,
    pendingCount:pending.size,controlPlane:accepted?.legacyM4.controlPlane||null});
})();
