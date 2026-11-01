// ArSonMix mixer-shell coordinator. Presentation only: no routing or DSP state.
// Mounted after mixer-io.js so all native controllers and delegated handlers remain owners.
(() => {
  'use strict';
  const rail=document.querySelector('#app .left-rail');
  const shell=document.querySelector('#app .workbench');
  const bank=document.querySelector('.arMixerBankBar');
  const nav=document.querySelector('.toolbar-actions .nav');
  if(!rail||!shell||!bank||!nav)return;
  if(rail.querySelector('.arRailModeSection'))return;

  // The existing mixer-io module retains bank, mode, APPLY and I/O references.
  // Move the WHOLE bank, not its buttons (its listeners query bank descendants).
  const modeSection=document.createElement('section');
  modeSection.className='arRailModeSection';
  modeSection.setAttribute('aria-label','Mixer layer modes');
  const heading=document.createElement('div');
  heading.className='arRailHeading arRailContextHeading';
  const modeTitle=document.createElement('span');modeTitle.textContent='STRIP VIEW';
  const activeStrip=document.createElement('span');activeStrip.className='arRailFocus';
  heading.append(modeTitle,activeStrip);
  const syncStripContext=()=>{
    const selected=rail.querySelector('#channelList [data-choose].selected');
    const id=Number(selected?.dataset.choose);
    const name=selected?.lastElementChild?.textContent?.trim()||'';
    const label=selected&&Number.isInteger(id)?'CH '+String(id+1).padStart(2,'0')+' · '+name:'MAIN';
    if(activeStrip.textContent!==label){activeStrip.textContent=label;activeStrip.title=label;}
  };
  window.addEventListener('arMixChannelFocus',syncStripContext);
  modeSection.append(heading,bank);
  rail.insertBefore(modeSection,document.getElementById('channelList'));
  bank.setAttribute('aria-label','Channel processing and routing mode');

  const actions=document.createElement('section');
  actions.className='arRailUtilities';
  actions.setAttribute('aria-label','Console utilities');
  const utilitiesTitle=document.createElement('div');
  utilitiesTitle.className='arRailHeading';utilitiesTitle.textContent='UTILITIES';
  const grid=document.createElement('div');grid.className='arRailUtilityGrid';
  const selectors=['[data-my-sounds]','[data-plugin-manager]','#audioSetupBtn','#arSignalOpen'];
  for(const selector of selectors){
    const button=nav.querySelector(selector);
    if(!button)continue;
    grid.appendChild(button);
  }
  actions.append(utilitiesTitle,grid);
  const footer=rail.querySelector('.rail-input-footer');
  rail.insertBefore(actions,footer);
  // Only true navigable workspaces stay in the top segmented tabs.
  const names={'MIX':'MIXER','FX RACK':'FX RACK','SIGNAL FLOW':'PATCH'};
  for(const button of nav.querySelectorAll('[data-view]')){
    const label=button.querySelector('span');
    if(label&&names[button.dataset.view])label.textContent=names[button.dataset.view];
    button.setAttribute('aria-label',names[button.dataset.view]||button.textContent.trim());
  }
  nav.setAttribute('aria-label','Workspace tabs');
  for(const button of nav.querySelectorAll('[data-view]')){
    button.setAttribute('aria-current',button.classList.contains('selected')?'page':'false');
    button.title=(button.title?button.title+' · ':'')+
      ({'MIX':'Alt+1','FX RACK':'Alt+2','SIGNAL FLOW':'Alt+3'}[button.dataset.view]||'');
  }
  syncStripContext();
  document.querySelector('.toolbar-actions')?.classList.add('arSegmentedToolbar');

  // Bank modes are operational even when the full FX/PATCH workspace is open.
  // Existing app.js remains the sole owner of view switching.
  window.addEventListener('click',e=>{
    if(!e.target.closest?.('.arRailModeSection [data-mix-mode]'))return;
    if(document.getElementById('mixer')?.classList.contains('hidden'))
      nav.querySelector('[data-view="MIX"]')?.click();
  },true);
  // Adaptive bank paging of real input strips only. MAIN remains sticky.
  const mixer=document.getElementById('mixer');
  if(mixer&&!document.getElementById('arMixChannelPager')){
    const pager=document.createElement('div');pager.id='arMixChannelPager';pager.className='arMixerChannelPager';
    const prev=document.createElement('button');prev.type='button';prev.className='btn-180';prev.textContent='‹';
    prev.setAttribute('aria-label','Previous mixer channel bank');
    const label=document.createElement('span');label.className='arMixerPagerLabel';
    const next=document.createElement('button');next.type='button';next.className='btn-180';next.textContent='›';
    next.setAttribute('aria-label','Next mixer channel bank');
    pager.append(prev,label,next);nav.parentElement?.append(pager);
    const strips=()=>[...mixer.querySelectorAll('[data-strip]')];
    const pageSize=()=>{
      const first=strips()[0];if(!first)return 1;
      const master=document.getElementById('masterLeft')?.getBoundingClientRect().width||0;
      return Math.max(1,Math.floor((mixer.clientWidth-master)/(first.getBoundingClientRect().width||1)));
    };
    const firstVisible=()=>Math.max(0,strips().findIndex(s=>s.getBoundingClientRect().right>mixer.getBoundingClientRect().left+4));
    const refresh=()=>{
      const all=strips(),at=firstVisible(),count=pageSize();
      label.textContent=all.length?'CH '+String(at+1).padStart(2,'0')+'–'+
        String(Math.min(all.length,at+count)).padStart(2,'0')+' / '+all.length:'NO CHANNELS';
      prev.disabled=!at;next.disabled=!all.length||at+count>=all.length;
    };
    const jump=dir=>{
      const all=strips();if(!all.length)return;
      const index=Math.max(0,Math.min(all.length-1,firstVisible()+dir*pageSize()));
      mixer.scrollLeft+=all[index].getBoundingClientRect().left-mixer.getBoundingClientRect().left;
      refresh();
    };
    prev.addEventListener('click',()=>jump(-1));next.addEventListener('click',()=>jump(1));
    mixer.addEventListener('scroll',refresh,{passive:true});
    if(typeof ResizeObserver==='function')new ResizeObserver(refresh).observe(mixer);
    refresh();
  }
  // Studio-like channel selection and FX navigation are presentation-only.
  const channelList=rail.querySelector('#channelList');
  channelList?.addEventListener('keydown',event=>{
    const current=event.target.closest?.('[data-choose]');
    if(!current)return;
    if(event.key==='Enter'||event.key===' '){
      event.preventDefault();current.click();return;
    }
    if(event.key!=='ArrowDown'&&event.key!=='ArrowUp')return;
    const rows=Array.from(channelList.querySelectorAll('[data-choose]'));
    const currentIndex=rows.indexOf(current);
    if(currentIndex<0)return;
    event.preventDefault();
    const next=rows[Math.min(rows.length-1,Math.max(0,currentIndex+(event.key==='ArrowDown'?1:-1)))];
    next?.focus();next?.click();
  });
  channelList?.addEventListener('dblclick',event=>{
    if(!event.target.closest?.('[data-choose]'))return;
    nav.querySelector('[data-view="FX RACK"]')?.click();
  });
  // No audio/routing writes, idle pointer listeners, or periodic DOM work.
  window.addEventListener('keydown',event=>{
    if(!event.altKey||event.ctrlKey||event.metaKey||event.shiftKey||event.repeat||event.defaultPrevented)return;
    const target=event.target;
    if(target?.closest?.('input,textarea,select,[contenteditable],[role="dialog"]'))return;
    if(document.querySelector('.modal:not(.hidden),[role="dialog"].open'))return;
    const tabs={'Digit1':'MIX','Digit2':'FX RACK','Digit3':'SIGNAL FLOW'};
    const layers={'KeyF':'FX','KeyR':'ROUTE','KeyS':'SENDS'};
    const view=tabs[event.code],layer=layers[event.code];
    if(!view&&!layer)return;
    event.preventDefault();
    if(view)nav.querySelector('[data-view="'+view+'"]')?.click();
    else rail.querySelector('[data-mix-mode="'+layer+'"]')?.click();
  });
  for(const button of rail.querySelectorAll('[data-mix-mode]')){
    button.title=(button.title?button.title+' · ':'')+'Alt+'+button.dataset.mixMode[0];
  }
  document.documentElement.dataset.arMixerShell='rail-modes';
})();


/* ARSONMIX V6 OPERATOR CONSOLE — native accepted projection only.
   The legacy M4 console remains available until the native v6 publisher exists.
   No WebAudio, DSP, routing solver, or fabricated ACK lives in this module. */
(() => {
  'use strict';
  const mount=document.querySelector('#app .workbench');
  const backend=window.__JUCE__?.backend;
  if(!mount||!document.getElementById('mixer')||document.getElementById('arV6Console'))return;
  const native=!!(backend&&typeof backend.emitEvent==='function'&&typeof backend.addEventListener==='function');
  const h=(tag,cls,text)=>{
    const n=document.createElement(tag);
    if(cls)n.className=cls;
    if(text!==undefined)n.textContent=String(text);
    return n;
  };
  const button=(label,attrs={})=>{
    const n=h('button','arV6Button',label);n.type='button';
    for(const [key,value] of Object.entries(attrs))n.setAttribute(key,String(value));
    return n;
  };
  const clamp=(n,a,b)=>Math.max(a,Math.min(b,n));
  const clean=s=>String(s==null?'—':s);
  const byId=new Map();
  const pending=new Map();
  const pendingTargets=new Set();
  const awaitingSnapshot=new Map();
  let snapshotSerial=0;
  const operationCaps={
    SET_SOURCE:'SET_SOURCE',SET_PROCESSING_FORMAT:'SET_PROCESSING_FORMAT',
    SET_PRIMARY_OUT:'SET_PRIMARY_OUT',ADD_INSERT:'ADD_INSERT',
    PICK_INSERT_FILE:'PICK_INSERT_FILE',SET_INSERT_BYPASS:'SET_INSERT_BYPASS',
    OPEN_INSERT_EDITOR:'OPEN_INSERT_EDITOR',MOVE_INSERT:'MOVE_INSERT',
    ADD_SEND:'ADD_SEND',SET_SEND_TAP:'SET_SEND_TAP',SET_SEND_LEVEL:'SET_SEND_LEVEL',
    SET_FADER:'SET_FADER',SET_PAN:'SET_PAN',SET_MUTE:'SET_MUTE',
    SET_SOLO:'SET_SOLO',CREATE_CHANNEL:'CREATE_CHANNEL'
  };
  let accepted=null,acceptedNative=false,modeSend='',lastStructure='',requestSeq=0;
  let draggingFx=null;
  // Numeric decimal wire identifiers are required by integration/ConsoleNativeContractV6.h.
  // Never downcast native 64-bit ChannelId, device epoch, or graph revision.
  const decimal=v=>typeof v==='string'&&/^(0|[1-9][0-9]*)$/.test(v)?
    v:(Number.isSafeInteger(v)&&v>=0?String(v):null);
  const ge=(a,b)=>a!==null&&b!==null&&BigInt(a)>=BigInt(b);
  const instance='v6';
  let feedback='';
  const root=h('section','arV6Console');root.id='arV6Console';
  root.setAttribute('aria-label','ArSonMix universal operator console');
  const top=h('div','arV6Top');
  const identification=h('div','arV6Identification');
  const kicker=h('span','arV6Kicker','ARSONMIX / UNIVERSAL LIVE CONSOLE');
  const deviceText=h('span','arV6Device','NO NATIVE CONSOLE SNAPSHOT');
  const modeBadge=h('span','arV6State','WAITING FOR NATIVE');
  identification.append(kicker,deviceText,modeBadge);
  const tools=h('div','arV6Tools');
  const bankPrev=button('‹',{'aria-label':'Previous channel bank'});
  const bankLabel=h('span','arV6BankLabel','—');
  const bankNext=button('›',{'aria-label':'Next channel bank'});
  const sendMode=h('select','arV6Select arV6SendMode');sendMode.setAttribute('aria-label','Sends on faders target');
  const roleSelect=h('select','arV6Select arV6RoleSelect');
  roleSelect.setAttribute('aria-label','Native new channel type');
  for(const type of ['MONO','STEREO','GROUP','FX_RETURN','MONITOR']){
    const option=h('option','',type);option.value=type;roleSelect.append(option);
  }
  const add=button('+ CHANNEL',{'data-op':'CREATE_CHANNEL'});
  const patch=button('PATCH',{'aria-label':'Open advanced patch'});
  const back=button('V6 CONSOLE',{'aria-label':'Return to operator console'});
  back.id='arV6Return';back.hidden=true;
  tools.append(bankPrev,bankLabel,bankNext,sendMode,roleSelect,add,patch);
  top.append(identification,tools);
  const message=h('div','arV6Message');message.setAttribute('role','status');
  const body=h('div','arV6Body');
  const lanes=h('div','arV6Lanes');lanes.id='arV6Lanes';lanes.setAttribute('aria-label','Channel banks');
  const main=h('div','arV6Main');main.id='arV6Main';main.setAttribute('aria-label','Pinned main output');
  body.append(lanes,main);
  root.append(top,message,body);
  mount.append(root);
  document.querySelector('.toolbar-actions')?.append(back);
  const setVisible=v=>{
    document.documentElement.classList.toggle('ar-v6-active',!!v);
    back.hidden=!!v;
  };
  patch.addEventListener('click',()=>{
    setVisible(false);
    document.querySelector('[data-view="SIGNAL FLOW"]')?.click();
  });
  back.addEventListener('click',()=>{
    document.querySelector('[data-view="MIX"]')?.click();setVisible(true);
  });
  function supported(op){
    const caps=accepted?.graph?.capabilities||{};
    // Absence of explicit native advertised operation support means disabled.
    return acceptedNative && accepted?.productionCallbackLive===true &&
      Array.isArray(caps.supportedOps) && caps.supportedOps.includes(operationCaps[op]);
  }
  function can(op,id){
    if(!supported(op))return false;
    if(op==='CREATE_CHANNEL')return !!accepted.graph.capabilities.dynamicChannels && !pendingTargets.has('@roster');
    if(op==='ADD_INSERT'&&!accepted?.catalog?.plugins?.length)return false;
    if(!id)return true;
    const ch=byId.get(id);
    return !!ch && !pendingTargets.has(id) &&
      (!Array.isArray(ch.allowedOps)||ch.allowedOps.includes(op));
  }
  function showFeedback(text){
    feedback=clean(text);message.textContent=feedback;
    message.hidden=!feedback;
  }
  function requestSnapshot(){
    if(native)backend.emitEvent('arMixConsoleSubscribe',{protocol:'v6',requester:instance});
  }
  function sendIntent(op,id,payload){
    if(!can(op,id)){showFeedback('UNAVAILABLE · Native capability or channel authorization missing');return false}
    const requestId=String(++requestSeq);
    const command={requestId,deviceEpoch:accepted.device.epoch,
      baseGraphRevision:accepted.graph.revision,
      baseRosterRevision:accepted.graph.rosterRevision,
      operation:op,targetChannelId:id||null,payload:payload||{}};
    pending.set(requestId,{op,id,baseRevision:command.baseGraphRevision,epoch:command.deviceEpoch,
      snapshotSerial});
    if(id)pendingTargets.add(id);
    else if(op==='CREATE_CHANNEL')pendingTargets.add('@roster');
    showFeedback('PENDING · '+op+' · awaiting native ACK');
    refreshDisabled();
    backend.emitEvent('arMixConsoleIntent',command);
    return true;
  }
  function receiveAck(data){
    const ack=data?.ack||data;
    if(!ack||typeof ack.requestId!=='string')return;
    const intent=pending.get(ack.requestId);if(!intent)return;
    pending.delete(ack.requestId);
    const target=intent.id||'@roster';
    if(ack.status==='ACCEPTED'||ack.status==='acceptedGraph'){
      // A realtime ACK can keep the revision. Require a fresh readback, not
      // only a positive ACK. All revisions stay decimal strings.
      const requiredRevision=decimal(ack.graphRevision??ack.newRevision)??intent.baseRevision;
      if(snapshotSerial>intent.snapshotSerial&&accepted?.device?.epoch===intent.epoch&&
         ge(accepted?.graph?.revision,requiredRevision)){
        pendingTargets.delete(target);
      }else{
        awaitingSnapshot.set(ack.requestId,{...intent,requiredRevision});
      }
      showFeedback('ACCEPTED · '+intent.op+' · synchronizing native snapshot');
      requestSnapshot();
    }else{
      pendingTargets.delete(target);
      showFeedback((ack.status==='REQUIRES_DEVICE_CHANGE'?'REQUIRES DEVICE · ':'REJECTED · ')+
        clean(ack.errorCode||ack.message||'Native rejected request')+' · accepted audio preserved');
      requestSnapshot();
      refreshAcceptedControls();
    }
    refreshDisabled();
  }
  function validSnapshot(s){
    if(!s||typeof s!=='object'||s.schemaVersion!==6||!s.device||!s.graph)return false;
    if(decimal(s.device.epoch)===null||decimal(s.graph.revision)===null||
      decimal(s.graph.rosterRevision)===null)return false;
    if(!Array.isArray(s.graph.channels)||!s.graph.capabilities)return false;
    const ids=new Set();
    for(const c of s.graph.channels){
      // JS numeric conversion corrupts 64-bit identities: reject such a wire.
      if(!c||typeof c.id!=='string'||!c.id||ids.has(c.id))return false;
      ids.add(c.id);
    }
    return true;
  }
  function normalizeSnapshot(payload){
    const raw=payload?.snapshot||payload;
    if(!raw||typeof raw!=='object'||raw.schemaVersion!==6)return null;
    // The new main integration/ConsoleNativeContractV6.h readback has a
    // flat, control-thread-only shape. It does NOT assert physical LIVE.
    if(raw.schemaVersion===6&&!raw.graph&&Array.isArray(raw.channels)){
      const caps=raw.capabilities||{};
      return {schemaVersion:6,productionCallbackLive:raw.productionCallbackLive===true &&
        caps.callbackOwnsChannelIds===true,
        device:{epoch:decimal(raw.deviceEpoch),backend:'NATIVE GRAPH READBACK',
          sampleRate:null,bufferFrames:null,outputArmed:false,
          inputEndpoints:[],outputEndpoints:[]},
        graph:{revision:decimal(raw.graphRevision),
          rosterRevision:decimal(raw.rosterRevision),
          channels:raw.channels.map(ch=>({...ch,
            role:clean(ch.role).toUpperCase(),
            processingFormat:clean(ch.processingFormat).toUpperCase(),
            source:null,primaryOut:ch.primaryOutId||null,
            inserts:[],controls:{},
            sends:(ch.sends||[]).map(s=>({
              targetId:s.destinationId,tap:s.preFader?'PRE':'POST',name:s.destinationId
            }))})),
          destinations:[],capabilities:{dynamicChannels:!!caps.addChannel,
            groups:!!caps.group,fxReturns:!!caps.fxReturn,monitor:!!caps.monitor,
            sendsOnFaders:false,supportedOps:[]}},
        telemetry:{rawCapturePresent:false,channelMeterRmsPeak:{}}};
    }
    if(!raw.device||!raw.graph)return null;
    const caps=raw.graph.capabilities||{};
    return {...raw,schemaVersion:6,
      productionCallbackLive:raw.productionCallbackLive===true&&caps.callbackOwnsChannelIds===true,
      device:{...raw.device,epoch:decimal(raw.device.epoch)},
      graph:{...raw.graph,revision:decimal(raw.graph.revision),
        rosterRevision:decimal(raw.graph.rosterRevision??raw.graph.revision)}};
  }
  function receiveSnapshot(data,fromNative){
    const s=normalizeSnapshot(data);
    if(!validSnapshot(s)){
      if(fromNative)showFeedback('REJECTED SNAPSHOT · Invalid native epoch, revision or ChannelId');
      return false;
    }
    if(acceptedNative&&!fromNative)return false;
    if(fromNative&&acceptedNative&&accepted&&
      BigInt(s.device.epoch)<BigInt(accepted.device.epoch))return false;
    if(fromNative&&acceptedNative&&accepted&&s.device.epoch===accepted.device.epoch&&
      BigInt(s.graph.revision)<BigInt(accepted.graph.revision))return false;
    const priorEpoch=acceptedNative?accepted?.device?.epoch:null;
    const structure=s.device.epoch+':'+s.graph.revision+':'+s.graph.channels.map(c=>c.id).join('|');
    accepted=s;acceptedNative=!!fromNative;
    snapshotSerial++;
    byId.clear();
    for(const c of s.graph.channels)byId.set(c.id,c);
    if(fromNative&&priorEpoch!==null&&priorEpoch!==s.device.epoch){
      // Device replacement invalidates all old intents and controls; never
      // replay them into an unrelated callback/port inventory.
      pending.clear();pendingTargets.clear();awaitingSnapshot.clear();
      showFeedback('DEVICE CHANGED · native source and destination inventory refreshed');
    }
    for(const [requestId,task] of awaitingSnapshot){
      if(task.epoch===s.device.epoch&&snapshotSerial>task.snapshotSerial&&
        ge(s.graph.revision,task.requiredRevision)){
        awaitingSnapshot.delete(requestId);
        pendingTargets.delete(task.id||'@roster');
      }
    }
    if(structure!==lastStructure||root.dataset.demo!==String(!fromNative)){
      lastStructure=structure;render();
    }else{
      refreshAcceptedControls();refreshDisabled();updateHeader();
    }
    root.dataset.demo=String(!fromNative);
    // Integration readback is intentionally not a production callback.
    // It must never replace the working M4 console until callback cutover.
    if(!fromNative||s.productionCallbackLive===true)setVisible(true);
    return true;
  }
  const makeDemo=()=>{
    const names=['Lead Vocal','Guest Mic','Music L','Music R','Speech','Stream','Backing','Guitar','Keys','Talkback'];
    const channels=names.map((name,i)=>({
      id:'DEMO:'+String(i+1),role:'INPUT',name,color:'#4e8ba4',processingFormat:i===2||i===3?'STEREO':'MONO',
      source:null,primaryOut:null,inserts:[],sends:[],controls:{faderDb:0,pan:0,mute:false,solo:false}
    }));
    channels.push({id:'DEMO:MAIN',role:'MAIN',name:'MAIN',processingFormat:'STEREO',
      inserts:[],sends:[],controls:{faderDb:0,pan:0,mute:false,solo:false}});
    return {schemaVersion:6,productionCallbackLive:false,
      device:{epoch:'0',backend:'BROWSER DEMO',sampleRate:null,bufferFrames:null,outputArmed:false,
      inputEndpoints:[],outputEndpoints:[]},graph:{revision:'0',rosterRevision:'0',channels,destinations:[],
      capabilities:{dynamicChannels:false,groups:false,fxReturns:false,sendsOnFaders:false,supportedOps:[]}},
      telemetry:{rawCapturePresent:false,channelMeterRmsPeak:{}}};
  };
  function options(select,items,current,emptyLabel){
    select.replaceChildren();
    const zero=h('option','',emptyLabel);zero.value='';select.append(zero);
    let found=!current;
    for(const item of items){
      if(!item||typeof item.id!=='string'||!item.id)continue;
      const opt=h('option','',item.name||item.label||item.id);
      opt.value=item.id;opt.disabled=item.available===false||item.active===false;
      if(current===item.id){opt.selected=true;found=true}
      select.append(opt);
    }
    if(!found){
      const missing=h('option','','MISSING · '+current);missing.value=current;
      missing.selected=true;missing.disabled=true;select.append(missing);
    }
    if(!current)select.value='';
    return select;
  }
  function selectField(label,op,id,items,current,placeholder,extra){
    const row=h('label','arV6Field');
    row.append(h('span','arV6FieldName',label));
    const sel=options(h('select','arV6Select'),items,current,placeholder);
    sel.dataset.op=op;sel.dataset.id=id;
    if(extra)for(const [key,val] of Object.entries(extra))sel.dataset[key]=val;
    sel.disabled=!can(op,id);
    row.append(sel);return row;
  }
  function fxSection(ch){
    const section=h('div','arV6Fx');
    section.append(h('span','arV6SectionTitle','VST3 INSERTS'));
    const list=Array.isArray(ch.inserts)?ch.inserts:[];
    for(const fx of list){
      const row=h('div','arV6FxSlot');
      row.dataset.fxId=clean(fx.id);row.dataset.id=ch.id;
      row.draggable=can('MOVE_INSERT',ch.id);
      row.append(button(fx.name||fx.pluginName||'VST3',{'data-op':'OPEN_INSERT_EDITOR',
        'data-id':ch.id,'data-native-insert-id':clean(fx.id)}));
      const bypass=button(fx.bypassed?'OFF':'ON',{'data-op':'SET_INSERT_BYPASS',
        'data-id':ch.id,'data-native-insert-id':clean(fx.id),'aria-pressed':String(!fx.bypassed)});
      bypass.classList.toggle('isOff',!!fx.bypassed);row.append(bypass);
      section.append(row);
    }
    const filePicker=supported('PICK_INSERT_FILE');
    if(!filePicker&&supported('ADD_INSERT')&&accepted?.catalog?.plugins?.length){
      const catalog=accepted.catalog.plugins.filter(p=>typeof p.id==='string'&&p.id);
      const chooser=options(h('select','arV6Select arV6PluginChoice'),catalog,'','SELECT NATIVE VST3');
      chooser.dataset.pluginChoice=ch.id;section.append(chooser);
    }
    const addFx=button(filePicker?'+ OPEN VST3 FILE':'+ VST3',
      {'data-op':filePicker?'PICK_INSERT_FILE':'ADD_INSERT','data-id':ch.id});
    addFx.classList.add('arV6AddFx');section.append(addFx);
    return section;
  }
  function sendSection(ch){
    const section=h('div','arV6Sends');
    section.append(h('span','arV6SectionTitle','SEND'));
    const sends=Array.isArray(ch.sends)?ch.sends:[];
    const targets=(accepted.graph.destinations||[]).filter(x=>x.acceptsSend===true);
    for(const send of sends){
      const line=h('div','arV6Send');line.dataset.sendId=clean(send.id);
      line.append(h('span','arV6SendName',clean(send.name||send.targetId)));
      const toggle=button(send.tap==='PRE'?'PRE':'POST',{'data-op':'SET_SEND_TAP',
        'data-id':ch.id,'data-send':clean(send.id)});
      line.append(toggle);
      section.append(line);
    }
    const select=selectField('+ TO','ADD_SEND',ch.id,targets,'','NO NATIVE SEND BUS');
    section.append(select);return section;
  }
  function strip(ch,pinned=false){
    const role=clean(ch.role).toUpperCase();
    const elStrip=h('article','arV6Strip'+(pinned?' arV6Master':''));
    elStrip.dataset.channelId=ch.id;
    const head=h('div','arV6StripHead');
    const roleName=h('span','arV6Role',role);
    const name=h('strong','arV6Name',ch.name||ch.id);name.title=ch.id;
    head.append(roleName,name);elStrip.append(head);
    const sourceSection=h('div','arV6Upper');
    const sources=(accepted.device.inputEndpoints||[]).filter(p=>p.active!==false && p.available!==false);
    if(role==='INPUT')sourceSection.append(selectField('IN','SET_SOURCE',ch.id,sources,
      ch.source?.portId||ch.source?.id||'','NO VERIFIED INPUT'));
    else sourceSection.append(h('div','arV6Field arV6FixedField','IN   ·   '+role));
    const formats=['MONO','STEREO'].map(s=>({id:s,name:s}));
    sourceSection.append(selectField('M/ST','SET_PROCESSING_FORMAT',ch.id,formats,
      ch.processingFormat||'','NATIVE FORMAT'));
    sourceSection.append(fxSection(ch),sendSection(ch));
    const outs=(accepted.graph.destinations||[]).concat(accepted.device.outputEndpoints||[]);
    sourceSection.append(selectField('OUT','SET_PRIMARY_OUT',ch.id,outs,
      ch.primaryOut?.id||ch.primaryOut||'','NO VERIFIED OUT'));
    elStrip.append(sourceSection);
    const controls=h('div','arV6Controls');
    const pan=h('label','arV6Pan');pan.append(h('span','','PAN'));
    const panRange=h('input','arV6PanRange');panRange.type='range';
    panRange.min='-100';panRange.max='100';panRange.step='1';
    panRange.value=String(Number(ch.controls?.pan)||0);
    panRange.dataset.op='SET_PAN';panRange.dataset.id=ch.id;
    pan.append(panRange);controls.append(pan);
    controls.append(button('M',{'data-op':'SET_MUTE','data-id':ch.id,
      'aria-pressed':String(!!ch.controls?.mute)}));
    controls.append(button('S',{'data-op':'SET_SOLO','data-id':ch.id,
      'aria-pressed':String(!!ch.controls?.solo)}));
    elStrip.append(controls);
    const lower=h('div','arV6Lower');
    const meter=h('div','arV6Meter');meter.dataset.meterId=ch.id;
    meter.setAttribute('aria-label','Native channel meter: '+(ch.name||ch.id));
    for(let n=0;n<22;n++)meter.append(h('i','arV6Led'));
    const range=h('input','arV6Fader');
    range.type='range';range.min='-60';range.max='12';range.step='.1';
    const faderVal=Number(ch.controls?.faderDb);
    range.value=String(Number.isFinite(faderVal)?clamp(faderVal,-60,12):0);
    range.dataset.op='SET_FADER';range.dataset.id=ch.id;
    range.setAttribute('aria-label',(ch.name||ch.id)+' fader dB');
    const lowerLabel=h('span','arV6Readout',(Number.isFinite(faderVal)?faderVal.toFixed(1):'—')+' dB');
    lower.append(meter,range,lowerLabel);elStrip.append(lower);
    const status=h('div','arV6StripStatus');
    status.dataset.statusId=ch.id;status.textContent=channelState(ch);
    elStrip.append(status);
    return elStrip;
  }
  function channelState(ch){
    if(!acceptedNative)return 'DEMO / NO AUDIO';
    if(accepted?.productionCallbackLive!==true)return 'NATIVE READBACK / NOT LIVE';
    if(ch.controls?.mute)return 'MUTED';
    if(!ch.source && clean(ch.role).toUpperCase()==='INPUT')return 'SILENT / NO SOURCE';
    if(!accepted.device.outputArmed)return 'OUTPUT DISARMED';
    const t=accepted.telemetry||{};
    // Do not infer LIVE from a port label, snapshot or demo LED alone.
    if(t.rawCapturePresent!==true)return 'NO VERIFIED PCM';
    return 'ACCEPTED / AUDIO ARMED';
  }
  function updateHeader(){
    const d=accepted?.device||{},g=accepted?.graph||{};
    deviceText.textContent=(d.backend||'NO DEVICE')+'  ·  '+
      (d.sampleRate?Math.round(d.sampleRate)+' Hz':'— Hz')+' / '+
      (d.bufferFrames||'—')+' frames  ·  GRAPH '+(g.revision??'—');
    modeBadge.textContent=!acceptedNative?'DEMO · NOT CONNECTED':
      accepted?.productionCallbackLive!==true?'NATIVE READBACK · NOT LIVE':
      d.outputArmed?'NATIVE ACCEPTED · ARMED':'NATIVE ACCEPTED · OUTPUT DISARMED';
    root.dataset.demo=String(!acceptedNative);
    root.dataset.armed=String(acceptedNative&&accepted?.productionCallbackLive===true&&!!d.outputArmed);
  }
  function render(){
    if(!accepted)return;
    updateHeader();
    lanes.replaceChildren();main.replaceChildren();
    const channels=accepted.graph.channels;
    for(const ch of channels){
      if(clean(ch.role).toUpperCase()==='MAIN')main.append(strip(ch,true));
      else lanes.append(strip(ch));
    }
    if(!main.children.length){
      main.append(h('div','arV6NoMaster','MAIN NOT PUBLISHED BY NATIVE'));
    }
    const destinations=(accepted.graph.destinations||[]).filter(x=>x.acceptsSend===true);
    options(sendMode,destinations,modeSend,'MAIN FADERS');
    const cap=accepted.graph.capabilities||{};
    sendMode.disabled=!acceptedNative||!cap.sendsOnFaders;
    add.disabled=!can('CREATE_CHANNEL');
    refreshDisabled();syncFaderMode();refreshMeters();refreshBanks();
  }
  function refreshDisabled(){
    if(!accepted)return;
    for(const node of root.querySelectorAll('[data-op]')){
      const op=node.dataset.op,id=node.dataset.id||null;
      node.disabled=!can(op,id);
    }
    add.disabled=!can('CREATE_CHANNEL');
    const caps=accepted.graph.capabilities||{};
    roleSelect.disabled=!can('CREATE_CHANNEL');
    for(const option of roleSelect.options){
      option.disabled=(option.value==='GROUP'&&!caps.groups)||
        (option.value==='FX_RETURN'&&!caps.fxReturns)||
        (option.value==='MONITOR'&&!caps.monitor);
    }
    sendMode.disabled=!acceptedNative||!caps.sendsOnFaders;
    for(const n of root.querySelectorAll('.arV6Fader')){
      if(modeSend){
        const ch=byId.get(n.dataset.id);
        const hasSend=(ch?.sends||[]).some(s=>s.targetId===modeSend);
        n.disabled=!hasSend||!can('SET_SEND_LEVEL',n.dataset.id);
      }
    }
  }
  function refreshAcceptedControls(){
    for(const strip of root.querySelectorAll('[data-channel-id]')){
      const ch=byId.get(strip.dataset.channelId);
      if(!ch)continue;
      const choose=(op,value)=>{
        const field=strip.querySelector('select[data-op="'+op+'"]');
        if(!field)return;
        const actual=typeof value==='string'?value:'';
        if([...field.options].some(o=>o.value===actual))field.value=actual;
      };
      choose('SET_SOURCE',ch.source?.portId||ch.source?.id||'');
      choose('SET_PROCESSING_FORMAT',ch.processingFormat||'');
      choose('SET_PRIMARY_OUT',ch.primaryOut?.id||(typeof ch.primaryOut==='string'?ch.primaryOut:''));
      choose('ADD_SEND','');
      const pan=strip.querySelector('[data-op="SET_PAN"]');
      if(pan)pan.value=String(Number(ch.controls?.pan)||0);
      for(const op of ['SET_MUTE','SET_SOLO']){
        const n=strip.querySelector('button[data-op="'+op+'"]');
        if(n)n.setAttribute('aria-pressed',String(!!ch.controls?.[op==='SET_MUTE'?'mute':'solo']));
      }
      for(const node of strip.querySelectorAll('.arV6FxSlot')){
        const fx=(ch.inserts||[]).find(x=>String(x.id)===node.dataset.fxId);
        const power=node.querySelector('[data-op="SET_INSERT_BYPASS"]');
        if(fx&&power){
          power.textContent=fx.bypassed?'OFF':'ON';
          power.classList.toggle('isOff',!!fx.bypassed);
          power.setAttribute('aria-pressed',String(!fx.bypassed));
        }
      }
      for(const node of strip.querySelectorAll('.arV6Send')){
        const send=(ch.sends||[]).find(x=>String(x.id)===node.dataset.sendId);
        const tap=node.querySelector('[data-op="SET_SEND_TAP"]');
        if(send&&tap)tap.textContent=send.tap==='PRE'?'PRE':'POST';
      }
    }
    syncFaderMode();refreshMeters();
  }
  function syncFaderMode(){
    for(const range of root.querySelectorAll('.arV6Fader')){
      const ch=byId.get(range.dataset.id);
      const send=modeSend?(ch?.sends||[]).find(s=>s.targetId===modeSend):null;
      const db=Number(modeSend?send?.levelDb:ch?.controls?.faderDb);
      range.value=String(Number.isFinite(db)?clamp(db,-60,12):0);
      const text=range.parentElement?.querySelector('.arV6Readout');
      if(text)text.textContent=(Number.isFinite(db)?db.toFixed(1):'—')+' dB';
    }
  }
  function refreshMeters(){
    if(!accepted)return;
    const meters=acceptedNative?accepted.telemetry?.channelMeterRmsPeak||{}:{};
    for(const meter of root.querySelectorAll('[data-meter-id]')){
      const id=meter.dataset.meterId;
      const m=meters[id]||{};
      const peak=Math.max(0,Number(m.peakLeft)||0,Number(m.peakRight)||0,Number(m.peak)||0);
      const lit=clamp(Math.ceil(22*clamp(peak,0,1)),0,22);
      if(meter.dataset.lit===String(lit))continue;
      meter.dataset.lit=String(lit);
      const leds=meter.children;
      for(let i=0;i<leds.length;i++)leds[i].classList.toggle('lit',i<lit);
    }
    for(const node of root.querySelectorAll('[data-status-id]')){
      const ch=byId.get(node.dataset.statusId);
      if(ch)node.textContent=channelState(ch);
    }
  }
  function refreshBanks(){
    const total=lanes.children.length;
    const child=lanes.querySelector('.arV6Strip');
    const width=child?.getBoundingClientRect().width||172;
    const perPage=Math.max(1,Math.floor(lanes.clientWidth/width));
    const at=Math.max(0,Math.round(lanes.scrollLeft/width));
    bankLabel.textContent=total?(at+1)+'–'+Math.min(total,at+perPage)+' / '+total:'NO CHANNELS';
    bankPrev.disabled=at===0;bankNext.disabled=at+perPage>=total;
  }
  function jump(dir){
    const first=lanes.querySelector('.arV6Strip');
    if(!first)return;
    const perPage=Math.max(1,Math.floor(lanes.clientWidth/first.getBoundingClientRect().width));
    lanes.scrollBy({left:dir*perPage*first.getBoundingClientRect().width,behavior:'auto'});
    refreshBanks();
  }
  bankPrev.addEventListener('click',()=>jump(-1));
  bankNext.addEventListener('click',()=>jump(1));
  lanes.addEventListener('scroll',refreshBanks,{passive:true});
  if(typeof ResizeObserver==='function')new ResizeObserver(refreshBanks).observe(lanes);
  sendMode.addEventListener('change',()=>{modeSend=sendMode.value;
    syncFaderMode();refreshDisabled();
    showFeedback(modeSend?
      'SENDS ON FADERS · native sends only · select MAIN FADERS to return':'MAIN FADERS · source gains unchanged');
  });
  // Reorder is one native structural intent; DOM/PCM order never changes on drop.
  root.addEventListener('dragstart',event=>{
    const node=event.target.closest?.('.arV6FxSlot');
    if(!node||!node.draggable||!can('MOVE_INSERT',node.dataset.id)){
      event.preventDefault();return;
    }
    draggingFx={id:node.dataset.id,insertId:node.dataset.fxId};
    event.dataTransfer?.setData('text/plain',draggingFx.insertId);
    if(event.dataTransfer)event.dataTransfer.effectAllowed='move';
  });
  root.addEventListener('dragover',event=>{
    const node=event.target.closest?.('.arV6FxSlot');
    if(draggingFx&&node&&draggingFx.id===node.dataset.id&&
      draggingFx.insertId!==node.dataset.fxId)event.preventDefault();
  });
  root.addEventListener('drop',event=>{
    const node=event.target.closest?.('.arV6FxSlot'),source=draggingFx;
    draggingFx=null;
    if(!source||!node||source.id!==node.dataset.id||
      source.insertId===node.dataset.fxId)return;
    event.preventDefault();
    sendIntent('MOVE_INSERT',source.id,
      {insertId:source.insertId,beforeInsertId:node.dataset.fxId});
  });
  root.addEventListener('dragend',()=>{draggingFx=null});
  root.addEventListener('click',event=>{
    const n=event.target.closest('button[data-op]');
    if(!n||n.disabled)return;
    const op=n.dataset.op,id=n.dataset.id||null,ch=byId.get(id);
    if(op==='CREATE_CHANNEL'){
      // Native must allocate ChannelId; never append a fake strip on ACK.
      const role=roleSelect.value;
      const caps=accepted.graph.capabilities||{};
      if(!['MONO','STEREO','GROUP','FX_RETURN','MONITOR'].includes(role))return;
      if(role==='GROUP'&&!caps.groups||role==='FX_RETURN'&&!caps.fxReturns||
        role==='MONITOR'&&!caps.monitor){showFeedback('UNAVAILABLE · Native role not accepted');return}
      sendIntent(op,null,{role});return;
    }
    if(!ch)return;
    if(op==='SET_MUTE')sendIntent(op,id,{value:!ch.controls?.mute});
    else if(op==='SET_SOLO')sendIntent(op,id,{value:!ch.controls?.solo});
    else if(op==='SET_INSERT_BYPASS'){
      const fx=(ch.inserts||[]).find(x=>String(x.id)===n.dataset.nativeInsertId);
      if(fx)sendIntent(op,id,{insertId:fx.id,bypassed:!fx.bypassed});
    }
    else if(op==='OPEN_INSERT_EDITOR')sendIntent(op,id,{insertId:n.dataset.nativeInsertId});
    else if(op==='SET_SEND_TAP'){
      const send=(ch.sends||[]).find(x=>String(x.id)===n.dataset.send);
      if(send)sendIntent(op,id,{sendId:send.id,tap:send.tap==='PRE'?'POST':'PRE'});
    }
    else if(op==='PICK_INSERT_FILE')sendIntent(op,id,{});
    else if(op==='ADD_INSERT'){
      const catalog=accepted?.catalog?.plugins||[];
      const pluginId=n.closest('.arV6Fx')?.querySelector('[data-plugin-choice]')?.value;
      if(catalog.some(p=>p.id===pluginId))sendIntent(op,id,{pluginId});
      else showFeedback('UNAVAILABLE · Select a verified native VST3 first');
    }
  });
  root.addEventListener('change',event=>{
    const n=event.target.closest('[data-op]');
    if(!n||n.disabled)return;
    const id=n.dataset.id,op=n.dataset.op,ch=byId.get(id);
    if(!ch)return;
    if(op==='SET_SOURCE'&&n.value){
      const endpoint=(accepted.device.inputEndpoints||[]).find(e=>e.id===n.value&&e.available!==false&&e.active!==false);
      if(endpoint)sendIntent(op,id,{portId:endpoint.id});
    }else if(op==='SET_PRIMARY_OUT'&&n.value)sendIntent(op,id,{destinationId:n.value});
    else if(op==='SET_PROCESSING_FORMAT')sendIntent(op,id,{format:n.value});
    else if(op==='ADD_SEND'&&n.value)sendIntent(op,id,{targetId:n.value,tap:'POST',levelDb:0});
    else if(op==='SET_PAN')sendIntent(op,id,{value:Number(n.value)});
    else if(op==='SET_FADER'){
      if(modeSend){
        const send=(ch.sends||[]).find(s=>s.targetId===modeSend);
        if(send)sendIntent('SET_SEND_LEVEL',id,{sendId:send.id,levelDb:Number(n.value)});
        else showFeedback('UNAVAILABLE · No native send on this channel');
      }else sendIntent(op,id,{valueDb:Number(n.value)});
    }
    // Restore only accepted controls without remounting meters or fader nodes.
    if(accepted){refreshAcceptedControls();refreshDisabled();}
  });
  if(native){
    backend.addEventListener('arMixConsoleSnapshot',s=>receiveSnapshot(s,true));
    backend.addEventListener('arMixConsoleAck',receiveAck);
    requestSnapshot();
  }else if(new URLSearchParams(location.search).get('console')==='v6'){
    // The opt-in browser preview is visibly DEMO with every mutation disabled.
    // Existing baseline preview without ?console=v6 remains backward compatible.
    receiveSnapshot(makeDemo(),false);
    showFeedback('BROWSER DEMO · UI layout only · no PCM, hardware, installed VST3 or LIVE routes');
  }
  // Host/browser acceptance harness: dispatch a native envelope explicitly;
  // no public function may forge a native acceptance in a real JUCE session.
  if(!native){
    window.addEventListener('arMixV6Fixture',e=>receiveSnapshot(e.detail?.snapshot||e.detail,false));
  }
  window.__arMixV6ReadOnly=()=>({
    nativePublisher:acceptedNative,revision:accepted?.graph?.revision??null,
    epoch:accepted?.device?.epoch??null,visibleChannelIds:[...byId.keys()],
    pendingCount:pending.size,feedback
  });
})();
