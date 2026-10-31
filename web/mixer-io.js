// ArSonMix G1/G2 operator console: one presentation of monitor.js's native routing controller.
// UI data is a snapshot; only monitor.js can stage/apply the device-bound graph.
(() => {
  'use strict';
  const backend=window.__JUCE__?.backend;
  const mixer=document.getElementById('mixer');
  const host=document.querySelector('.workbench');
  if(!mixer||!host)return;
  let mode='FX', lastStatus={}, deviceInventory=null, ioTab='device', lastSignature='', focusedInput=2;
  let monitorSendsExpanded=false;
  // DEVICE1: staged selections are UI-only; only positive native ACK changes active hardware.
  let deviceDraft=null,deviceBusy=false,deviceNote='',deviceRequest='',deviceRequestSeq=0,lastDeviceRefresh=0,lastDeviceRescan=0,applyMutePending=false,applyAfterDevice=null,applyBaseEpoch=0;
  let deviceMatrixView='inputs',deviceMatrixWaitEpoch=null; // Never display old hardware pins after an APPLY ACK.
  // Pending SOURCE ACK is a UI projection only; monitor.js owns the native route.
  let quickSourcePending=null;
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

  // M1 named logical inputs: PRESENTATION aliases for verified native hardware
  // pins. Every choice still commits through the single native route authority.
  // Profile is bound to backend + capture device + the entire active pin/name
  // signature; never remap silently when the driver or enabled pins change.
  const aliasStorePrefix='armix:named-source:v1:';
  let aliasCacheKey='',aliasCache=[];
  const sourceIdentity=s=>{
    const d=deviceInventory?.active,pins=portList(s),enabled=d?.enabledInputs;
    if(!isNative()||!s.ready||!d?.open||!d.backend||!d.captureDevice||
       !Array.isArray(enabled)||!pins.length||pins.length!==enabled.length)return '';
    const p=pins.map(x=>x.port).sort((a,b)=>a-b);
    const e=enabled.map(Number).sort((a,b)=>a-b);
    if(!p.every((v,i)=>v===e[i]))return '';
    const names=Array.isArray(d.inputNames)?d.inputNames:[];
    if(!pins.every(x=>names[x.port]===x.label))return '';
    // Device names are NOT guaranteed stable operating-system IDs; on any
    // changed label/pin list, the prior aliases are deliberately unavailable.
    return aliasStorePrefix+encodeURIComponent(JSON.stringify([
      d.backend,d.captureDevice,pins.map(x=>[x.port,x.label]).sort((a,b)=>a[0]-b[0])]));
  };
  const validAlias=(a,pins)=>{
    const used=new Set(pins.map(x=>x.port));
    return !!a&&typeof a.name==='string'&&a.name.length>0&&a.name.length<=32&&
      (a.mode==='MONO'||a.mode==='STEREO')&&
      Number.isInteger(a.left)&&used.has(a.left)&&
      (a.mode==='MONO'?(a.right===-1):
        Number.isInteger(a.right)&&a.left!==a.right&&used.has(a.right));
  };
  const sourceAliases=s=>{
    const key=sourceIdentity(s);
    if(key!==aliasCacheKey){
      aliasCacheKey=key;aliasCache=[];
      if(key)try {
        const value=JSON.parse(localStorage.getItem(key)||'[]');
        if(Array.isArray(value))aliasCache=value.slice(0,32).filter(x=>validAlias(x,portList(s)));
      }catch{} // blocked storage does not interrupt native routing
    }
    return key?aliasCache:[];
  };
  const saveSourceAliases=(s,items)=>{
    const key=sourceIdentity(s);
    if(!key)return false;
    try {
      localStorage.setItem(key,JSON.stringify(items));
      aliasCacheKey=key;aliasCache=items;
      return true;
    }catch{return false;}
  };
  const aliasValue=a=>a.mode+':'+a.left+(a.mode==='STEREO'?':'+a.right:'');
  const namedRoute=(s,r)=>sourceAliases(s).find(a=>aliasValue(a)===valueFor(r));

  // DEVICE4: named software inputs are aliases for verified physical ports,
  // NOT new streams. Only SOURCE2/native ACK can make them audible on channels.
  let softwareEdit=null,softwarePage=0,directMatrixOpen=false;
  const softwarePageSize=8;
  function softwareSay(message){
    if(ioTab==='device')deviceNote=message;
    matrixSay(message);
  }
  function beginSoftwareInput(mode,index=-1){
    const s=route(),key=sourceIdentity(s),items=sourceAliases(s);
    if(!hardwareReady(s)||!deviceSameEndpoint(deviceDraft)||deviceDirty()||!key)
      return softwareSay('Select and ACK a verified audio interface before editing software inputs.');
    if(index===-1&&items.length>=32)return softwareSay('32 named inputs maximum per device.');
    const prior=index>=0?items[index]:null;
    if(index>=0&&!prior)return softwareSay('Software input no longer exists.');
    const names=new Set(items.map(x=>x.name.toLowerCase()));
    let number=1;while(names.has(('Input '+number).toLowerCase()))number++;
    softwareEdit={key,index,originalName:prior?.name||'',name:prior?.name||'Input '+number,
      mode:prior?.mode||mode,left:prior?.left??-1,right:prior?.right??-1,side:'L'};
    softwareSay('SOFTWARE INPUT DRAFT · map M or L/R then SAVE I/O.');
    lastSignature='';refreshIO();
  }
  function saveSoftwareInput(){
    const s=route(),d=softwareEdit,items=sourceAliases(s);
    if(!d||d.key!==sourceIdentity(s)||!hardwareReady(s)||!deviceSameEndpoint(deviceDraft)||deviceDirty())
      return softwareSay('Device changed or is pending. Review enabled hardware pins.');
    const value={name:d.name.trim(),mode:d.mode,left:d.left,
      right:d.mode==='MONO'?-1:d.right};
    if(!validAlias(value,portList(s)))
      return softwareSay('INCOMPLETE M/L/R · provide a name and valid distinct driver pins.');
    const old=d.index<0?-1:items.findIndex(x=>x.name===d.originalName);
    if(d.index>=0&&old<0)return softwareSay('Software input changed elsewhere; reopen to edit.');
    if(items.some((x,i)=>i!==old&&x.name.toLowerCase()===value.name.toLowerCase()))
      return softwareSay('Software input name already exists on this device.');
    const next=items.slice();
    if(old<0)next.push(value);else next[old]=value;
    if(!saveSourceAliases(s,next))return softwareSay('Device profile save failed (storage unavailable).');
    softwareEdit=null;
    softwareSay('SOFTWARE INPUT SAVED · choose it in a mixer SOURCE; sound is unchanged.');
    if(!sourceChooser.hidden)renderSourceChooser();
    refreshFocus(s);lastSignature='';refreshIO();
  }
  function assignSoftwarePin(index,pin){
    const s=route(),items=sourceAliases(s);
    if(!hardwareReady(s)||!portList(s).some(p=>p.port===pin)||deviceDirty())
      return softwareSay('Hardware pin not verified. Review DEVICE / DRIVER.');
    if(!softwareEdit||softwareEdit.index!==index)beginSoftwareInput(items[index]?.mode,index);
    const d=softwareEdit;
    if(!d||d.key!==sourceIdentity(s))return;
    if(d.mode==='MONO'){d.left=pin;d.right=-1;}
    else {
      if(d.side==='L'&&d.right===pin||d.side==='R'&&d.left===pin)
        return softwareSay('STEREO L/R must use two different hardware pins.');
      if(d.side==='L'){d.left=pin;d.side='R';}
      else {d.right=pin;d.side='L';}
    }
    softwareSay('SOFTWARE I/O DRAFT · SAVE I/O to publish this named input.');
    lastSignature='';refreshIO();
  }
  function makeSoftwareInputs(s){
    const ports=portList(s),items=sourceAliases(s),key=sourceIdentity(s);
    if(softwareEdit?.key!==key)softwareEdit=null;
    const section=node('section','arIOSoftware');
    section.setAttribute('aria-label','Named software audio input mapping');
    const head=node('div','arIOSoftwareHead'),intro=node('div','');
    intro.append(node('strong','','SOFTWARE INPUTS · M / L / R'),
      node('small','','Name your verified interface inputs, then assign them to mixer SOURCE.'));
    const actions=node('div','arIOSoftwareActions');
    for(const [mode,label] of [['MONO','+ ADD MONO'],['STEREO','+ ADD STEREO']]){
      const b=node('button','btn-180',label);b.type='button';b.dataset.ioSoftwareAdd=mode;
      b.disabled=!key||!hardwareReady(s)||deviceDirty()||items.length>=32;
      actions.appendChild(b);
    }
    head.append(intro,actions);section.appendChild(head);
    const maxPage=Math.max(0,Math.ceil(ports.length/softwarePageSize)-1);
    softwarePage=Math.min(softwarePage,maxPage);
    const shown=ports.slice(softwarePage*softwarePageSize,(softwarePage+1)*softwarePageSize);
    const pager=node('div','arIOSoftwarePager');
    const prev=node('button','arIOPageButton','‹');prev.type='button';
    prev.dataset.ioSoftwarePage='-1';prev.disabled=softwarePage===0;
    const next=node('button','arIOPageButton','›');next.type='button';
    next.dataset.ioSoftwarePage='1';next.disabled=softwarePage>=maxPage;
    pager.append(prev,node('span','',ports.length?(softwarePage*softwarePageSize+1)+'–'+
      (softwarePage*softwarePageSize+shown.length)+' / '+ports.length+' PORTS':'NO VERIFIED INPUTS'),next);
    section.appendChild(pager);
    if(!key)section.appendChild(node('p','arIOSoftwareHint',
      'No verified capture pins. Enable actual driver inputs and wait for native ACK.'));
    const viewport=node('div','arIOCrossScroll arIOSoftwareScroll');
    const grid=node('div','arIOMatrix arIOSoftwareGrid');grid.setAttribute('role','grid');
    grid.setAttribute('aria-label','Software inputs to real hardware pins');
    grid.style.gridTemplateColumns='minmax(220px,1fr) 75px'+
      (shown.length?' repeat('+shown.length+',minmax(47px,1fr))':'');
    grid.append(node('div','arIOMatrixCorner','SOFTWARE INPUT / NAME'),
      node('div','arIOMatrixCorner','FORMAT'));
    for(const p of shown){
      const h=node('div','arIOPortTitle');
      h.append(node('span','arIOPortFriendly',portName(p)),
        node('b','arIOPortIndex',String(p.port+1)));
      h.title=portName(p)+' · actual hardware input '+(p.port+1);
      h.setAttribute('role','columnheader');grid.appendChild(h);
    }
    const rows=items.map((a,index)=>({a,index}));
    if(softwareEdit?.index===-1)rows.push({a:softwareEdit,index:-1});
    if(!rows.length)section.appendChild(node('p','arIOSoftwareHint',
      'Add Mono or Stereo, click the hardware M/L/R cells, and SAVE I/O. Direct CH patch remains available below.'));
    for(const {a,index} of rows){
      const draft=softwareEdit?.index===index&&softwareEdit.key===key;
      const value=draft?softwareEdit:a;
      const label=node('div','arIOMatrixRowName arIOSoftwareName');
      if(draft){
        const field=node('input','arIOSoftwareNameEdit');field.type='text';
        field.maxLength=32;field.value=value.name;
        field.dataset.ioSoftwareName='1';field.setAttribute('aria-label','Software input name');
        label.appendChild(field);
        for(const [act,word] of [['save','SAVE'],['cancel','CANCEL']]){
          const b=node('button','arIOSoftwareMini',word);b.type='button';
          b.dataset.ioSoftwareAction=act;label.appendChild(b);
        }
      }else{
        const name=node('strong','',a.name);name.title=a.name;label.appendChild(name);
        const controls=node('div','arIOSoftwareRowActions');
        for(const [act,word] of [['edit','EDIT'],['remove','REMOVE'],
          ['use','ASSIGN CH'+String(focusedInput+1).padStart(2,'0')]]){
          const b=node('button','arIOSoftwareMini',word);b.type='button';
          b.dataset.ioSoftwareAction=act;b.dataset.ioSoftwareIndex=String(index);
          b.disabled=!key||!hardwareReady(s)||deviceDirty()||!!s.pending;
          controls.appendChild(b);
        }
        label.appendChild(controls);
        const usedBy=(s.acceptedRoute?.channels||[]).map((r,i)=>
          valueFor(r)===aliasValue(a)?'CH'+String(i+1).padStart(2,'0'):null).filter(Boolean);
        label.appendChild(node('small','arIOSoftwareUsedBy',
          usedBy.length?'LIVE ON '+usedBy.join(', '):'NOT ASSIGNED'));
      }
      const format=node('div','arIOMatrixFormat arIOSoftwareFormat');
      format.appendChild(node('strong','',value.mode));
      if(draft&&value.mode==='STEREO'){
        const sides=node('div','arIOMatrixSides');
        for(const side of ['L','R']){
          const b=node('button','arIOSideButton',side);b.type='button';
          b.dataset.ioSoftwareSide=side;
          b.setAttribute('aria-pressed',String(value.side===side));
          sides.appendChild(b);
        }
        format.appendChild(sides);
      }else format.appendChild(node('small','',value.mode==='MONO'?'M':'L / R'));
      grid.append(label,format);
      for(const p of shown){
        const pin=p.port;
        const assigned=value.mode==='MONO'&&value.left===pin?'M':
          value.mode==='STEREO'&&value.left===pin?'L':
          value.mode==='STEREO'&&value.right===pin?'R':'';
        const cell=node('button','arIOMatrixCell',assigned||'·');cell.type='button';
        cell.dataset.ioSoftwarePin=String(pin);cell.dataset.ioSoftwareIndex=String(index);
        cell.dataset.assigned=String(!!assigned);cell.dataset.stage=draft?'draft':'saved';
        cell.setAttribute('aria-pressed',String(!!assigned));
        cell.setAttribute('aria-label',value.name+' · '+portName(p)+(assigned?' mapped '+assigned:' unmapped'));
        cell.disabled=!key||!hardwareReady(s)||deviceDirty()||!!s.pending;
        grid.appendChild(cell);
      }
    }
    viewport.appendChild(grid);section.appendChild(viewport);
    section.appendChild(node('p','arIOMatrixLegend',
      'M = mono · L/R = stereo · Blue = saved name · Amber = unsaved draft. SAVE I/O stores metadata; choosing SOURCE requires real native ACK.'));
    return section;
  }

  const outputLabel=s=>{
    const out=s.mainOutputs?.length===2?s.mainOutputs:s.outputPorts||[];
    return s.ready&&Number.isInteger(out[0])&&Number.isInteger(out[1])?
      'MAIN → HW '+(out[0]+1)+'/'+(out[1]+1):'MAIN → NO HW OUTPUT';
  };
  const toolbar=node('div','arMixerBankBar');
  toolbar.innerHTML='<div class="arMixerBankModes" role="group" aria-label="Mixer channel bank"><button type="button" class="btn-180 active" data-mix-mode="FX" aria-pressed="true">FX</button><button type="button" class="btn-180" data-mix-mode="ROUTE" aria-pressed="false">ROUTE</button><button type="button" class="btn-180" data-mix-mode="SENDS" aria-pressed="false">SENDS</button></div><div class="arMixerBankInfo" id="arMixerBankInfo" role="status">NO HARDWARE · PREVIEW</div><button type="button" class="btn-180 arMixApply" data-mix-apply title="MUTE OUTPUT first; native ACK required">APPLY</button><button type="button" class="btn-180 arMixIoOpen" data-io-open>AUDIO I/O</button>';
  host.insertBefore(toolbar,mixer);
  // One genuinely selected-channel journey, NOT another DSP or source registry.
  // Four compact steps show ACKNOWLEDGED state, never stage a hidden graph.
  const focusFlow=node('div','arChannelSignalFlow');
  focusFlow.setAttribute('aria-label','Selected live mixer channel signal flow');
  focusFlow.innerHTML='<span class="arFocusIndex" data-focus-channel>CH 01</span>'+
    '<button type="button" data-focus-step="source" title="Select verified physical input">SOURCE <span data-focus-source>UNPATCHED</span></button>'+
    '<button type="button" data-focus-step="fx" title="Open existing native insert rack">FX <span data-focus-fx>0 INSERTS</span></button>'+
    '<button type="button" data-focus-step="sends" title="Open channel AUX sends">SEND <span data-focus-sends>NO AUX</span></button>'+
    '<button type="button" data-focus-step="output" title="Open verified output routing">OUT <span data-focus-out>NO OUTPUT</span></button>';
  // mixer-layout.js moves the whole toolbar into the LEFT RAIL.
  // The selected channel overview is therefore a separate compact WORKBENCH
  // row; placing it in toolbar would jam 4 steps into 168px of sidebar.
  host.insertBefore(focusFlow,mixer);
  let focusIndex=0,focusSignature='';
  const focusSet=(q,value)=>{
    const target=focusFlow.querySelector(q);
    if(target&&target.textContent!==value)target.textContent=value;
  };
  const selectedInputChannel=()=>{
    const chosen=mixer.querySelector('.strip[data-strip].selected');
    const i=chosen?Number(chosen.dataset.strip):focusIndex;
    return Number.isInteger(i)&&i>=0&&i<10?i:0;
  };
  function refreshFocus(s){
    const i=selectedInputChannel(),r=s.acceptedRoute?.channels?.[i];
    const ack=!!s.ready&&!!s.active&&!!r&&Number(s.acceptedRoute.deviceEpoch)===Number(s.epoch);
    const driverPins=portList(s);
    const names=id=>driverPins.find(p=>p.port===id)?.label;
    let source='UNPATCHED';
    if(ack&&r.mode==='MONO')
      source=names(r.left)||'PIN '+(Number(r.left)+1)+' · VERIFY';
    if(ack&&r.mode==='STEREO')
      source=(names(r.left)||'L'+(Number(r.left)+1))+' / '+
        (names(r.right)||'R'+(Number(r.right)+1));
    if(ack&&r.mode==='BUS')source='INTERNAL BUS';
    if(ack&&r.mode!=='NONE'&&r.mode!=='BUS')source=namedRoute(s,r)?.name||source;
    if(!ack)source='NO NATIVE ACK';
    const routed=ack?lastStatus.dryRoutingLab?.fxInstances?.[i]:null;
    const inserts=Array.isArray(routed)?routed.length:0;
    const legacy=i===0?Math.max(0,Number(lastStatus.chainCount)||0):0;
    const fx=(inserts+legacy)+' VST3'+(legacy&&inserts?' · SPLIT OWNERS':'');
    const sends=ack&&Array.isArray(s.acceptedRoute.sends)?
      s.acceptedRoute.sends.filter(x=>x.from===i&&x.to>=10&&x.to<14):[];
    const sendsText=sends.length?sends.map(x=>'AUX'+(x.to-9)+' '+x.tap).join(' / '):'NO AUX';
    const outputs=s.mainOutputs?.length===2?s.mainOutputs:[];
    const dest=ack&&r.main&&outputs.length===2?
      'MAIN HW '+(outputs[0]+1)+'/'+(outputs[1]+1):
      ack&&r.main?'MAIN · NO PIN':ack?'SEND ONLY':'NO OUTPUT';
    const sig=JSON.stringify([i,source,fx,sendsText,dest,ack,s.pending]);
    if(sig!==focusSignature){
      focusIndex=i;focusSignature=sig;
      focusSet('[data-focus-channel]','CH '+String(i+1).padStart(2,'0'));
      focusSet('[data-focus-source]',source);
      focusSet('[data-focus-fx]',fx);
      focusSet('[data-focus-sends]',sendsText);
      focusSet('[data-focus-out]',dest);
      focusFlow.dataset.nativeState=ack&&!s.pending?'accepted':'unverified';
    }
  }
  // SOURCE2: compact, directly actionable source chooser on the selected strip.
  // No second routing owner, no invented virtual port, no browser DSP.
  const sourceChooser=node('div','arSourceChooser');
  sourceChooser.hidden=true;
  sourceChooser.setAttribute('role','dialog');
  sourceChooser.setAttribute('aria-label','Choose verified channel input source');
  sourceChooser.innerHTML='<div class="arSourceChooserTop"><span data-source-title>INPUT SOURCE</span><button type="button" class="btn-180" data-source-close aria-label="Close input source chooser">×</button></div><input class="arSourceSearch" data-source-search type="search" placeholder="Find named or hardware source..." aria-label="Search verified sources"><div class="arSourceChooserChoices" data-source-choices></div><details class="arSourceAliasManager"><summary>MANAGE NAMED INPUTS</summary><div class="arSourceAliasFields"><input data-source-alias-name maxlength="32" placeholder="Name: Mic 1 / Virtual L-R" aria-label="New source name"><select data-source-alias-mode aria-label="Source format"><option value="MONO">MONO</option><option value="STEREO">STEREO</option></select><select data-source-alias-left aria-label="Input left or mono pin"></select><select data-source-alias-right aria-label="Input right pin"></select><button type="button" class="btn-180" data-source-alias-save>SAVE SOURCE</button></div><small data-source-alias-help>Requires a verified device inventory; routes still require native ACK.</small></details><div class="arSourceChooserNote" data-source-note role="status"></div>';
  host.insertBefore(sourceChooser,mixer);
  let sourceChooserChannel=-1;
  const sourceChoices=sourceChooser.querySelector('[data-source-choices]');
  const sourceNote=sourceChooser.querySelector('[data-source-note]');
  const aliasNameInput=sourceChooser.querySelector('[data-source-alias-name]');
  const aliasLeft=sourceChooser.querySelector('[data-source-alias-left]');
  const aliasRight=sourceChooser.querySelector('[data-source-alias-right]');
  const aliasMode=sourceChooser.querySelector('[data-source-alias-mode]');
  const aliasSearch=sourceChooser.querySelector('[data-source-search]');
  const closeSourceChooser=()=>{
    sourceChooser.hidden=true;sourceChooserChannel=-1;
  };
  const renderSourceChooser=()=>{
    if(sourceChooser.hidden||sourceChooserChannel<0)return;
    const s=route(),r=s.acceptedRoute?.channels?.[sourceChooserChannel];
    sourceChooser.querySelector('[data-source-title]').textContent=
      'CH '+String(sourceChooserChannel+1).padStart(2,'0')+' · VERIFIED SOURCE';
    sourceChoices.replaceChildren();
    const pins=portList(s);
    const values=portsFor(s,r||{mode:'NONE',left:-1,right:-1});
    const current=valueFor(r);
    const aliases=sourceAliases(s),search=aliasSearch.value.trim().toLowerCase();
    const matched=title=>!search||title.toLowerCase().includes(search);
    if(aliases.length){
      for(let i=0;i<aliases.length;++i){
        const a=aliases[i],value=aliasValue(a),label=a.name+' · '+a.mode+' · '+
          (a.left+1)+(a.mode==='STEREO'?'/'+(a.right+1):'');
        if(!matched(label))continue;
        const line=node('div','arSourceAliasRow');
        const button=node('button','arSourceChoice',label);
        button.type='button';button.dataset.sourceChoice=value;
        button.setAttribute('aria-pressed',String(value===current));
        button.disabled=!!s.pending||!!quickSourcePending||!s.ready;
        line.appendChild(button);
        const remove=node('button','arSourceAliasRemove','×');
        remove.type='button';remove.dataset.sourceAliasRemove=String(i);
        remove.title='Remove only this name, not hardware routing';
        line.appendChild(remove);sourceChoices.appendChild(line);
      }
    }
    for(const [value,title] of values){
      if(!matched(title))continue;
      if(value!=='NONE'&&value!=='CUSTOM'&&
         !value.startsWith('MONO:')&&!value.startsWith('STEREO:'))continue;
      // CUSTOM is a pointer to Advanced Patch, never an invented pin.
      const button=node('button','arSourceChoice',title);
      button.type='button';button.dataset.sourceChoice=value;
      button.setAttribute('aria-pressed',String(value===current));
      button.disabled=!!s.pending||!!quickSourcePending||!s.ready;
      sourceChoices.appendChild(button);
    }
    const advanced=node('button','arSourceAdvanced','ADVANCED PATCH · NONADJACENT L/R');
    advanced.type='button';advanced.dataset.sourceAdvanced='';
    if(matched('ADVANCED PATCH'))sourceChoices.appendChild(advanced);
    const previousLeft=aliasLeft.value,previousRight=aliasRight.value;
    const pinOptions=pins.map(p=>[String(p.port),portName(p)]);
    populate(aliasLeft,pinOptions,previousLeft||String(pins[0]?.port??''));
    populate(aliasRight,pinOptions,previousRight||String(pins[1]?.port??pins[0]?.port??''));
    aliasRight.disabled=aliasMode.value!=='STEREO';
    const identified=!!sourceIdentity(s);
    sourceChooser.querySelector('[data-source-alias-save]').disabled=!identified;
    sourceChooser.querySelector('[data-source-alias-help]').textContent=identified?
      'Saved for this verified device and enabled-pin layout · native ACK required':
      'Awaiting actual driver inventory; naming disabled until hardware is verified';
    sourceNote.textContent=!s.ready?'No verified native driver input ports':
      s.monitor?'Same-format physical input changes use native 64-sample crossfade · new patch or format requires MUTE':
      'Choose verified source; native ACK required before it is LIVE';
  };
  const openSourceChooser=channel=>{
    if(!Number.isInteger(channel)||channel<0||channel>=10)return;
    sourceChooserChannel=channel;sourceChooser.hidden=false;
    if(!sourceIdentity(route()))deviceRefresh(); // cached JUCE readback, not a vendor rescan
    renderSourceChooser();
  };
  sourceChooser.addEventListener('input',event=>{
    if(event.target===aliasSearch)renderSourceChooser();
  });
  aliasMode.addEventListener('change',()=>{aliasRight.disabled=aliasMode.value!=='STEREO';});
  sourceChooser.addEventListener('click',event=>{
    const remove=event.target.closest('[data-source-alias-remove]');
    if(remove){
      const s=route(),items=sourceAliases(s).slice(),index=Number(remove.dataset.sourceAliasRemove);
      if(Number.isInteger(index)&&index>=0&&index<items.length&&
         saveSourceAliases(s,items.filter((_,i)=>i!==index))){
        sourceNote.textContent='SOURCE NAME REMOVED · live hardware route unchanged';
        renderSourceChooser();refreshFocus(s);
      }return;
    }
    if(event.target.closest('[data-source-alias-save]')){
      const s=route(),a={name:aliasNameInput.value.trim(),mode:aliasMode.value,
        left:Number(aliasLeft.value),right:aliasMode.value==='MONO'?-1:Number(aliasRight.value)};
      if(!validAlias(a,portList(s))){sourceNote.textContent='INVALID NAME OR HARDWARE L/R PAIR';return;}
      const items=sourceAliases(s).filter(x=>x.name.toLowerCase()!==a.name.toLowerCase());
      if(items.length>=32){sourceNote.textContent='32 NAMED SOURCES MAXIMUM';return;}
      if(saveSourceAliases(s,[...items,a])){
        aliasNameInput.value='';aliasSearch.value='';renderSourceChooser();refreshFocus(s);
        sourceNote.textContent='NAMED SOURCE SAVED · choose it to request native routing';
      }else sourceNote.textContent='DEVICE/LOCAL STORAGE UNAVAILABLE · not saved';
      return;
    }
    if(event.target.closest('[data-source-close]')){closeSourceChooser();return;}
    if(event.target.closest('[data-source-advanced]')){
      const ch=sourceChooserChannel;closeSourceChooser();openIO('inputs',ch);return;
    }
    const button=event.target.closest('[data-source-choice]');
    if(!button||sourceChooserChannel<0)return;
    const [mode,a,b]=button.dataset.sourceChoice.split(':');
    if(!['NONE','MONO','STEREO'].includes(mode))return;
    const channel=sourceChooserChannel;
    const outcome=window.__arMixCommitSelectedSource?.(
      channel,mode,mode==='NONE'?-1:Number(a),mode==='STEREO'?Number(b):-1);
    sourceNote.textContent=outcome?.ok?
      outcome.pending?'SOURCE PENDING · WAIT NATIVE ACK':
        'SOURCE ALREADY ACTIVE · NATIVE ACK':
      outcome?.error||'Native source route rejected';
    if(!outcome?.ok)return;
    if(outcome.pending)quickSourcePending={
      channel,epoch:outcome.epoch,value:button.dataset.sourceChoice,live:!!outcome.live};
    renderSourceChooser();
    sourceNote.textContent=outcome.pending?
      outcome.live?'LIVE SOURCE PENDING · 64-SAMPLE RAMP · WAIT NATIVE ACK':
        'MUTED SOURCE APPLY PENDING · WAIT NATIVE ACK':
      'SOURCE ALREADY ACKNOWLEDGED';
  });
  // MS5: explicit Sends-on-Faders control, scoped to a single real AUX return.
  // MAIN fader DOM and all audio controls remain resident and unmodified.
  const sofControls=node('div','arSofControls');
  sofControls.innerHTML='<select data-sof-bus aria-label="Sends on Faders AUX destination"><option value="0">AUX 1</option><option value="1">AUX 2</option><option value="2">AUX 3</option><option value="3">AUX 4</option></select><button type="button" class="btn-180" data-sof-toggle aria-pressed="false">SEND FADERS</button><label class="arAuxMasterInline" title="Native AUX return gain — live, after AUX inserts">AUX MASTER <input type="range" data-aux-master min="-60" max="6" step=".5" value="0" aria-label="Selected AUX master return level"><output data-aux-master-read>0.0 dB</output></label>';
  sofControls.hidden=true;toolbar.appendChild(sofControls);
  const overlay=node('div','arIOOverlay');
  overlay.id='arIOOverlay';overlay.hidden=true;
  overlay.innerHTML='<section class="arIOWindow" role="dialog" aria-modal="true" aria-label="ArSonMix hardware audio I/O"><header><div><small>ArSonMiX · AUDIO HARDWARE</small><h2>Audio Setup · Windows Devices & Routing</h2></div><button type="button" class="btn-180" data-io-close aria-label="Close Audio I/O">CLOSE</button></header><div class="arIOTop" id="arIOIdentity"></div><nav role="tablist" aria-label="Audio I/O views"><button class="btn-180 active" role="tab" data-io-tab="device">DEVICE / DRIVER</button><button class="btn-180" role="tab" data-io-tab="inputs">INPUT ROUTING</button><button class="btn-180" role="tab" data-io-tab="outputs">OUTPUT ROUTING</button></nav><div id="arIOBody" class="arIOBody"></div><footer><span id="arIOFeedback" role="status">Hardware changes require native ACK. Output stays MUTED on APPLY.</span><button type="button" class="btn-180" data-io-revert>REVERT DRAFT</button><button type="button" class="btn-180" data-mix-apply>APPLY PATCH</button><button type="button" class="btn-180" data-io-internal>ADVANCED BUS / FX</button><button type="button" class="btn-180" data-io-device-revert hidden>REVERT DEVICE</button><button type="button" class="btn-180 arIODeviceApply" data-io-device-apply hidden>APPLY DEVICE</button></footer></section>';
  document.body.appendChild(overlay);
  const audioPath=node('div','arIOAudioPath');audioPath.id='arIOAudioPath';
  audioPath.innerHTML='<span data-audio-path-input>CAPTURE · WAIT</span><span data-audio-path-route>ROUTE · WAIT</span><span data-audio-path-output>OUTPUT · WAIT</span><button type="button" class="btn-180" data-io-signal-check>OPEN SIGNAL CHECK</button><small data-audio-path-reason role="status"></small>';
  overlay.querySelector('.arIOWindow').insertBefore(audioPath,el('arIOBody'));
  function refreshAudioPath(){
    const d=window.__arMixAudioPathView?.(lastStatus);
    if(!d)return;
    for(const [key,value] of [['input',d.input],['route',d.route],['output',d.output],['reason',d.message]]){
      const n=audioPath.querySelector('[data-audio-path-'+key+']');
      if(n&&n.textContent!==value)n.textContent=value;
    }
  }
  // G3: ONE floating inspector follows the selected source/AUX, no new
  // DSP graph, no heavyweight matrix, no layout shifts in the MIX strips.
  const sendEditor=node('section','arSendEditor');
  sendEditor.id='arSendEditor';sendEditor.hidden=true;
  sendEditor.setAttribute('role','dialog');sendEditor.setAttribute('aria-label','Focused channel AUX sends');
  document.body.appendChild(sendEditor);
  let sendFocus=null,sendAnchor=null,sendFeedback='';
  let sofBus=-1,sofEpoch=0,sofMessage='',sofDrag=null;
  const sofDb=g=>g<=0?-60:Math.max(-60,Math.min(0,20*Math.log10(g)));
  const sofGain=db=>db<=-60?0:Math.min(1,Math.pow(10,Math.min(0,db)/20));
  const sofRead=g=>g<=0?'OFF':sofDb(g).toFixed(1)+' dB';
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
    sendEditor.innerHTML=header+'<p class="arSendNotice">PRE and structural edits: MUTE + APPLY · existing POST LEVEL and AUX RETURN LEVEL: realtime native ACK.</p>'+
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
  function sendEditorLiveReady(focus){
    const s=route();
    return !!focus&&isNative()&&s.active&&s.ready&&!s.pending&&
      s.epoch===focus.epoch&&!s.draftActive&&
      !!s.acceptedRoute?.auxes?.[focus.bus]?.enabled;
  }
  function sendMutation(fn){
    const result=fn();
    sendFeedback=result?.ok?result.message:result?.error||'Routing controller unavailable';
    refresh();drawSendEditor();
  }

  // M1: one native POST send, one in-flight ACK per edge, coalesced continuous
  // drag input (not a per-pointer DOM rerender, FX action or structural APPLY).
  function leaveSof(){
    if(sofDrag){document.removeEventListener('pointermove',sofMove);
      sofDrag.slider.classList.remove('is-dragging');sofDrag=null;}
    sofBus=-1;sofEpoch=0;sofMessage='';
    mixer.removeAttribute('data-sof-active');
    sofControls.querySelector('[data-sof-bus]').disabled=false;
    sofControls.querySelector('[data-sof-toggle]').setAttribute('aria-pressed','false');
  }
  function sofExisting(s,channel){
    return (s.draftSends||[]).find(x=>x.from===channel&&x.to===10+sofBus&&x.tap==='POST');
  }
  function sofPaintLayer(layer,gain){
    const g=Number.isFinite(gain)?Math.max(0,Math.min(1,gain)):0;
    const slider=layer.querySelector('.arSofFader');
    slider.style.setProperty('--sof-position',(100*(sofDb(g)+60)/60).toFixed(2)+'%');
    slider.setAttribute('aria-valuenow',sofDb(g).toFixed(1));
    slider.setAttribute('aria-valuetext',sofRead(g));
    // A CSS pseudo-readout survives native meter/control textContent updates.
    const read=layer.closest('[data-strip]')?.querySelector('.db-display');
    if(read)read.dataset.sofText=sofRead(g);
  }
  function sofStatus(message){
    sofMessage=message;
    const status=el('arMixerBankInfo');
    if(status)status.textContent=message;
  }
  function sofRefresh(s){
    sofControls.hidden=mode!=='SENDS';
    const selector=sofControls.querySelector('[data-sof-bus]');
    const toggle=sofControls.querySelector('[data-sof-toggle]');
    const auxLevel=sofControls.querySelector('[data-aux-master]');
    const auxRead=sofControls.querySelector('[data-aux-master-read]');
    const busSelected=Number(selector.value);
    const acceptedBus=s.acceptedRoute?.auxes?.[busSelected];
    const enabled=!!acceptedBus?.enabled&&!s.pending&&s.ready&&isNative();
    const currentGain=Number(acceptedBus?.gain);
    const db=Number.isFinite(currentGain)&&currentGain>0
      ?Math.max(-60,Math.min(6,20*Math.log10(currentGain))):-60;
    auxLevel.disabled=!enabled;
    if(document.activeElement!==auxLevel)auxLevel.value=String(db);
    const readText=enabled?(db<=-60?'OFF':db.toFixed(1)+' dB'):'AUX OFF';
    if(auxRead.textContent!==readText)auxRead.textContent=readText;
    if(sofBus>=0&&(!isNative()||!s.ready||!s.active||s.epoch!==sofEpoch)){
      leaveSof();sofStatus('SEND FADERS EXITED · DEVICE CHANGED');
    }
    const active=sofBus>=0&&mode==='SENDS';
    mixer.dataset.sofActive=String(active);
    selector.disabled=active;
    toggle.textContent=active?'EXIT · AUX '+(sofBus+1):'SEND FADERS';
    toggle.setAttribute('aria-pressed',String(active));
    for(const strip of mixer.querySelectorAll('.strip[data-strip]')){
      const ch=Number(strip.dataset.strip),zone=strip.querySelector('.fader-zone');
      if(!zone)continue;
      let layer=zone.querySelector('.arSofLayer');
      if(active&&!layer){
        layer=node('div','arSofLayer');
        layer.innerHTML='<small class="arSofTarget"></small><button type="button" class="arSofAdd" data-sof-add title="Stage native POST send">＋ POST</button><div class="arSofFader" data-sof-fader role="slider" tabindex="0" aria-orientation="vertical" aria-valuemin="-60" aria-valuemax="0"><span class="arSofThumb" aria-hidden="true"></span></div>';
        zone.appendChild(layer);
      }
      if(layer)layer.hidden=!active;
      const original=zone.querySelector('[data-fader]');
      if(original){
        if(active){original.setAttribute('tabindex','-1');original.setAttribute('aria-hidden','true');}
        else{original.setAttribute('tabindex','0');original.removeAttribute('aria-hidden');}
      }
      const db=strip.querySelector('.db-display');
      if(db&&!active)delete db.dataset.sofText;
      if(!active||!layer)continue;
      layer.querySelector('.arSofTarget').textContent='AUX '+(sofBus+1)+' · POST';
      const item=sofExisting(s,ch);
      const add=layer.querySelector('[data-sof-add]'),slider=layer.querySelector('[data-sof-fader]');
      add.hidden=!!item;slider.hidden=!item;
      add.disabled=!!s.pending||!s.draftAux?.[sofBus]?.enabled;
      add.title=!s.draftAux?.[sofBus]?.enabled?'Enable AUX '+(sofBus+1)+' return in SENDS first':'Stage POST send for CH '+(ch+1);
      slider.setAttribute('aria-label','Channel '+(ch+1)+' POST send to AUX '+(sofBus+1));
      slider.setAttribute('aria-disabled',String(!!s.pending));
      if(item&&(!sofDrag||sofDrag.ch!==ch))sofPaintLayer(layer,Number(item.gain));
      else if(!item&&db)db.dataset.sofText='NO SEND';
    }
    if(active&&!sofMessage){
      const aux=s.draftAux?.[sofBus];
      const text=aux?.enabled?'AUX '+(sofBus+1)+' · LIVE POST LEVEL (ACK) · NEW SEND REQUIRES APPLY':'AUX '+(sofBus+1)+' RETURN OFF · enable in SENDS';
      el('arMixerBankInfo').textContent=text;
    }else if(active&&sofMessage)el('arMixerBankInfo').textContent=sofMessage;
  }
  function sofToggle(){
    if(sofBus>=0){leaveSof();refresh();return;}
    const s=route(),bus=Number(sofControls.querySelector('[data-sof-bus]').value);
    if(mode!=='SENDS'||!isNative()||!s.ready||!s.active||s.pending||!Number.isInteger(bus)||bus<0||bus>=4){
      sofStatus('SELECT DIRECT DEVICE · SENDS MODE · NO APPLY PENDING');return;
    }
    if(!s.draftAux?.[bus]?.enabled){
      sofStatus('ENABLE AUX '+(bus+1)+' RETURN IN SENDS BEFORE ENTERING');return;
    }
    closeSendEditor();sofBus=bus;sofEpoch=s.epoch;sofMessage='';
    refresh();
  }
  function sofStage(channel,action,value){
    const s=route();
    if(sofBus<0||mode!=='SENDS'||s.epoch!==sofEpoch||!s.ready||s.pending){
      sofStatus('SEND EDIT BLOCKED · STALE DEVICE / APPLY PENDING');return false;
    }
    // Ordinary level changes remain LIVE; new edges change topology.
    const r=action==='gain'
      ?window.__arMixSetLiveAuxSendGain?.({from:channel,bus:sofBus,gain:value})
      :window.__arMixStageAuxSendEdit?.({from:channel,bus:sofBus,tap:'POST',action});
    sofMessage=r?.ok?(action==='gain'?'AUX '+(sofBus+1)+' · NATIVE PARAM REQUEST · WAIT ACK':
      'AUX '+(sofBus+1)+' · NEW POST DRAFT · MUTE + APPLY'):
      r?.error||'Native send unavailable';
    refresh();return !!r?.ok;
  }
  function sofFinish(event,cancel=false){
    const d=sofDrag;
    if(!d||d.pointerId!==event.pointerId)return;
    sofDrag=null;
    document.removeEventListener('pointermove',sofMove);
    d.slider.classList.remove('is-dragging');
    if(cancel&&d.sent)sofStage(d.ch,'gain',+sofGain(d.startDb).toFixed(7));
    else if(!cancel&&Math.abs(d.db-d.startDb)>0.005)
      sofStage(d.ch,'gain',+sofGain(d.db).toFixed(7));
    else refresh();
  }
  function sofMove(event){
    const d=sofDrag;
    if(!d||d.pointerId!==event.pointerId)return;
    d.db=Math.max(-60,Math.min(0,d.startDb+(d.startY-event.clientY)*60/d.travel));
    if(d.slider.isConnected)sofPaintLayer(d.slider.closest('.arSofLayer'),sofGain(d.db));
    // Native ACK backpressure keeps at most one pending IPC command. While
    // dragging, only the latest target is retained; the audio DSP de-zippers.
    const now=performance.now();
    if(Math.abs(d.db-d.lastSentDb)>=.25 && now-d.lastSentAt>=25){
      const response=window.__arMixSetLiveAuxSendGain?.({
        from:d.ch,bus:sofBus,gain:+sofGain(d.db).toFixed(7)});
      if(response?.ok){d.sent=true;d.lastSentDb=d.db;d.lastSentAt=now;}
      else if(response?.error)sofStatus(response.error);
    }
  }
  document.addEventListener('pointerdown',event=>{
    const slider=event.target.closest?.('[data-sof-fader]');
    if(!slider||sofBus<0||sofDrag)return;
    const ch=Number(slider.closest('[data-strip]')?.dataset.strip),s=route(),item=sofExisting(s,ch);
    if(!item||s.pending||s.epoch!==sofEpoch)return;
    const rect=slider.getBoundingClientRect();
    sofDrag={ch,slider,pointerId:event.pointerId,startY:event.clientY,
      travel:Math.max(24,rect.height-18),startDb:sofDb(item.gain),db:sofDb(item.gain),
      lastSentDb:sofDb(item.gain),lastSentAt:0,sent:false};
    slider.classList.add('is-dragging');
    slider.setPointerCapture?.(event.pointerId);
    document.addEventListener('pointermove',sofMove,{passive:true});
    event.preventDefault();
  },true);
  document.addEventListener('pointerup',event=>sofFinish(event),true);
  document.addEventListener('pointercancel',event=>sofFinish(event,true),true);
  document.addEventListener('keydown',event=>{
    const slider=event.target.closest?.('[data-sof-fader]');
    if(!slider||sofBus<0)return;
    const step={ArrowUp:.5,ArrowDown:-.5,PageUp:5,PageDown:-5};
    if(!(event.key in step)&&!['Home','End'].includes(event.key))return;
    event.preventDefault();event.stopImmediatePropagation();
    const ch=Number(slider.closest('[data-strip]')?.dataset.strip),item=sofExisting(route(),ch);
    if(!item)return;
    const db=event.key==='Home'?-60:event.key==='End'?0:sofDb(item.gain)+step[event.key];
    sofStage(ch,'gain',+sofGain(Math.max(-60,Math.min(0,db))).toFixed(7));
  },true);
  document.addEventListener('click',event=>{
    if(sofBus>=0&&event.target.closest?.('[data-sof-fader]')){
      event.stopImmediatePropagation();event.preventDefault();
    }
  },true);
  function openIO(tab='device',channel=null){
    document.getElementById('arRouteClose')?.click();
    if(['inputs','outputs','device'].includes(tab))ioTab=tab;
    if(Number.isInteger(channel)&&channel>=0&&channel<10)focusedInput=channel;
    if(overlay.hidden)advancedMatrixOpen=false;
    overlay.hidden=false;lastSignature='';refreshIO();
    if(isNative())deviceRefresh();
    if(Number.isInteger(channel))overlay.querySelector('[data-io-quick-row="'+channel+'"]')?.scrollIntoView({block:'nearest'});
  }
  window.__arMixOpenAudioIO=openIO;
  function closeIO(){overlay.hidden=true;matrixDraft.clear();matrixSide.clear();matrixMessage='';}
  function setMode(next){
    if(!['FX','ROUTE','SENDS'].includes(next))return;
    if(next!=='SENDS'&&sofBus>=0)leaveSof();
    mode=next;mixer.dataset.ioMode=mode;
    toolbar.querySelectorAll('[data-mix-mode]').forEach(b=>{const selected=b.dataset.mixMode===mode;b.classList.toggle('active',selected);b.setAttribute('aria-pressed',String(selected));});
    refresh();
  }
  function decorate(){
    for(const strip of mixer.querySelectorAll('.strip[data-strip]')){
      if(strip.querySelector('.arChannelBank'))continue;
      const bank=node('div','arChannelBank');
      bank.innerHTML='<div class="arRouteBank"><span class="arBankTitle">INPUT SOURCE · NATIVE</span><select class="arBankSource" aria-label="Physical channel input source"></select><span class="arBankTitle">OUTPUT DESTINATION</span><span class="arBankOutput">MAIN · UNKNOWN</span><div class="arBankFormatRow"><select class="arBankChannelFormat" aria-label="Independent channel processing format" title="Processing MONO / STEREO independent from hardware input"><option value="MONO">CH M</option><option value="STEREO">CH L/R</option></select><span class="arBankStage">NATIVE PATCH REQUIRED</span></div><button class="btn-180 arBankPatch" data-mix-patch="input">OPEN INPUT PATCH</button></div><div class="arSendBank"><span class="arBankTitle">AUX SENDS · PRE / POST</span><div class="arSendSummary"></div><button class="btn-180 arBankPatch" data-mix-patch="internal">EDIT AUX SENDS</button></div>';
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
    source.dataset.channel=String(i);source.disabled=!isNative()||!s.ready||!!s.pending||!!quickSourcePending;
    const format=strip.querySelector('.arBankChannelFormat');
    if(format){format.value=channelFormat(r);format.dataset.channel=String(i);
      format.disabled=!isNative()||!s.ready||!!s.pending;}
    output.textContent=r.mode==='NONE'?'UNPATCHED · NO INPUT':r.main?outputLabel(s):'MAIN OFF · SEND PATH ONLY';
    const accepted=s.acceptedRoute?.channels?.[i],
      dirty=current!==valueFor(accepted)||channelFormat(r)!==channelFormat(accepted);
    const stage=strip.querySelector('.arBankStage');
    const sourcePending=quickSourcePending?.channel===i&&quickSourcePending.epoch===s.epoch;
    stage.textContent=!s.ready?'NO LIVE DEVICE':sourcePending?'PENDING · NATIVE ACK':dirty?'DRAFT · NOT APPLIED':r.mode==='NONE'?'UNPATCHED · SILENT':'LIVE · NATIVE ACK';
    stage.dataset.state=!s.ready?'offline':sourcePending?'draft':dirty?'draft':r.mode==='NONE'?'offline':'live';
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
    toolbar.querySelector('[data-mix-apply]').disabled=!isNative()||!s.ready||!!s.pending||!!s.monitor||(!overlay.hidden&&matrixDraft.size>0);
    for(const strip of mixer.querySelectorAll('.strip[data-strip]'))refreshStrip(strip,s);
    if(!overlay.hidden)refreshIO();
    if(!sendEditor.hidden)drawSendEditor();
    sofRefresh(s);
    refreshFocus(s);
  }

  // IO-MATRIX: reusable presentation of the existing native 10-channel graph.
  // Never publish a second graph, guess a missing pin, or stage incomplete L/R.
  const matrixDraft=new Map(), matrixSide=new Map();
  let advancedMatrixOpen=false;
  const matrixPage={inputs:0,outputs:0}, matrixPageSize=8;
  let matrixEpoch=-1, matrixMessage='';
  const hardwareReady=s=>isNative()&&s.ready&&s.active&&!s.pending;
  function matrixPorts(s,kind){
    const raw=kind==='inputs'?portList(s).map(p=>p.port):(s.outputPorts||[]);
    return [...new Set(raw.filter(p=>Number.isInteger(p)&&p>=0))];
  }
  function matrixSay(message){
    matrixMessage=message;
    const feedback=el('arIOFeedback');
    if(feedback)feedback.textContent=message;
  }
  function matrixResetIfDeviceChanged(s){
    if(matrixEpoch===s.epoch)return;
    const old=matrixEpoch;
    matrixEpoch=s.epoch;matrixDraft.clear();matrixSide.clear();
    softwareEdit=null;softwarePage=0;
    matrixPage.inputs=0;matrixPage.outputs=0;
    if(old!==-1)matrixSay('Device generation changed · review physical pins before APPLY.');
  }
  function matrixLogicalName(i){
    return document.querySelector('#mixer [data-strip="'+i+'"] .strip-caption')?.textContent?.trim()||
      'Input '+(i+1);
  }
  function matrixInputState(s,i){
    const r=channelState(s,i),pending=matrixDraft.get(i);
    return pending&&pending.epoch===s.epoch?pending:r;
  }

  // The operator-first source patch is only a controller for monitor.js.
  // Mono and stereo can use ANY driver-reported enabled capture pins.
  function makeQuickInputs(s){
    const section=node('section','arIOQuickPatch');
    section.setAttribute('aria-label','Route audio interface input to mixer channel');
    const title=node('header','arIOQuickHeader');
    const words=node('div','');
    words.append(node('strong','','1 · INTERFACE INPUT → MIXER CHANNEL'),
      node('small','','Choose a channel, mono/stereo source and actual driver input(s). STAGE → MUTE → APPLY.'));
    const jump=node('select','arIOJump');
    jump.dataset.ioJump='1';jump.setAttribute('aria-label','Find mixer channel');
    for(let i=0;i<10;i++){
      const opt=node('option','','CH '+String(i+1).padStart(2,'0')+' · '+matrixLogicalName(i));
      opt.value=String(i);opt.selected=i===focusedInput;jump.appendChild(opt);
    }
    title.append(words,jump);section.appendChild(title);
    const verified=portList(s);
    section.appendChild(node('p','arIOQuickGuide',verified.length?
      verified.length+' driver-reported capture pins · physical and virtual inputs appear only if enabled by this driver.' :
      'NO INPUT PINS · select an audio interface in DEVICE / DRIVER to enable routing.'));
    const headings=node('div','arIOQuickColumns');
    for(const label of ['MIX CHANNEL','SOURCE','INPUT / LEFT','RIGHT','NATIVE STATE'])headings.appendChild(node('span','',label));
    section.appendChild(headings);
    function sourceSelect(channel,side,value){
      const select=node('select','arIOQuickPin');
      select.dataset.ioQuickPin=side;select.setAttribute('aria-label','CH '+(channel+1)+' '+side+' input port');
      const blank=node('option','','Choose input…');blank.value='-1';select.appendChild(blank);
      for(const pin of verified){
        const option=node('option','','IN '+(pin.port+1)+' · '+portName(pin));
        option.value=String(pin.port);option.selected=pin.port===value;select.appendChild(option);
      }
      return select;
    }
    for(let i=0;i<10;i++){
      const r=channelState(s,i),accepted=s.acceptedRoute?.channels?.[i];
      const row=node('div','arIOQuickRow');row.dataset.ioQuickRow=String(i);
      row.dataset.state=!s.ready?'offline':valueFor(r)!==valueFor(accepted)?'draft':
        r.mode==='NONE'?'empty':'live';
      const identity=node('div','arIOQuickIdentity');
      identity.append(node('strong','','CH '+String(i+1).padStart(2,'0')),node('small','',matrixLogicalName(i)));
      const mode=node('select','arIOQuickMode');mode.dataset.ioQuickMode='1';
      mode.setAttribute('aria-label','CH '+(i+1)+' physical input type');
      for(const pair of [['NONE','NO INPUT'],['MONO','MONO'],['STEREO','STEREO'],
        ...(r.mode==='BUS'?[['BUS','INTERNAL BUS']]:[])]){
        const option=node('option','',pair[1]);option.value=pair[0];option.selected=r.mode===pair[0];mode.appendChild(option);
      }
      const left=sourceSelect(i,'left',r.left),right=sourceSelect(i,'right',r.right);
      left.disabled=!hardwareReady(s)||r.mode==='NONE'||r.mode==='BUS';
      right.disabled=!hardwareReady(s)||r.mode!=='STEREO';
      mode.disabled=!hardwareReady(s)||r.mode==='BUS';
      const action=node('div','arIOQuickAction');
      const stage=node('button','btn-180 arIOQuickStage','STAGE');
      stage.dataset.ioQuickStage='1';stage.type='button';
      stage.disabled=!hardwareReady(s)||r.mode==='BUS';
      action.append(stage,node('small','arIOQuickState',r.mode==='BUS'?'INTERNAL BUS':
        !s.ready?'NO DEVICE':row.dataset.state==='draft'?'DRAFT · NOT LIVE':
        row.dataset.state==='live'?'LIVE · ACK':'UNPATCHED'));
      row.append(identity,mode,left,right,action);section.appendChild(row);
    }
    section.appendChild(node('p','arIOQuickFooter','STAGE is not audible. MUTE output → APPLY PATCH → wait for NATIVE ACK to make the assignment live.'));
    return section;
  }
  function stageQuickInput(button){
    const row=button.closest('[data-io-quick-row]'),channel=Number(row?.dataset.ioQuickRow);
    const s=route(),pins=new Set(matrixPorts(s,'inputs'));
    if(!row||!Number.isInteger(channel)||channel<0||channel>=10||!hardwareReady(s))
      return matrixSay('Choose an active driver and wait for any previous APPLY.');
    const mode=row.querySelector('[data-io-quick-mode]').value,
      left=Number(row.querySelector('[data-io-quick-pin="left"]').value),
      right=Number(row.querySelector('[data-io-quick-pin="right"]').value);
    if(mode==='MONO'&&!pins.has(left))return matrixSay('CH '+(channel+1)+' · choose a valid mono input.');
    if(mode==='STEREO'&&(!pins.has(left)||!pins.has(right)||left===right))
      return matrixSay('CH '+(channel+1)+' · choose two DISTINCT driver-reported L/R inputs.');
    if(!['NONE','MONO','STEREO'].includes(mode))return matrixSay('Use ADVANCED BUS / FX for internal sources.');
    focusedInput=channel;matrixDraft.delete(channel);
    const result=window.__arMixStageChannelInput?.(channel,mode,mode==='NONE'?-1:left,mode==='STEREO'?right:-1);
    matrixSay(result?.ok?'CH '+(channel+1)+' · '+(mode==='NONE'?'INPUT REMOVED':
      mode==='MONO'?'MONO INPUT '+(left+1):'STEREO INPUT '+(left+1)+'/'+(right+1))+
      ' → DRAFT · MUTE OUTPUT → APPLY PATCH → NATIVE ACK':result?.error||'Native input patch unavailable');
    lastSignature='';refresh();
  }
  function makeMatrix(s,kind){
    const ports=matrixPorts(s,kind),maxPage=Math.max(0,Math.ceil(ports.length/matrixPageSize)-1);
    matrixPage[kind]=Math.min(maxPage,Math.max(0,matrixPage[kind]));
    const page=matrixPage[kind],shown=ports.slice(page*matrixPageSize,(page+1)*matrixPageSize);
    const wrap=node('section','arIOCrosspatch');
    wrap.setAttribute('aria-label',kind==='inputs'?'Physical input crosspatch':'Physical output crosspatch');
    const controls=node('div','arIOMatrixToolbar');
    const headline=node('strong','',kind==='inputs'?'ADVANCED INPUT CROSSPOINTS · M / L / R':'HARDWARE OUTPUT CROSSPOINTS · MAIN / MONITOR');
    const pager=node('div','arIOPager');
    const prev=node('button','arIOPageButton','‹');prev.type='button';prev.dataset.ioPage='-1';
    prev.disabled=page===0;prev.setAttribute('aria-label','Previous physical port bank');
    const next=node('button','arIOPageButton','›');next.type='button';next.dataset.ioPage='1';
    next.disabled=page===maxPage;next.setAttribute('aria-label','Next physical port bank');
    pager.append(prev,node('span','',ports.length?(page*matrixPageSize+1)+'–'+(page*matrixPageSize+shown.length)+' / '+ports.length+' PORTS':'NO ACTIVE PORTS'),next);
    controls.append(headline,pager);wrap.appendChild(controls);
    const viewport=node('div','arIOCrossScroll');
    const grid=node('div','arIOMatrix');
    grid.style.gridTemplateColumns='minmax(166px,1fr) 96px'+(shown.length?' repeat('+shown.length+',minmax(39px,1fr))':'');
    grid.setAttribute('role','grid');
    grid.setAttribute('aria-label',kind==='inputs'?'Logical input to hardware patch':'Logical main output to hardware patch');
    grid.append(node('div','arIOMatrixCorner','LOGICAL BUS / CHANNEL'),node('div','arIOMatrixCorner','SOURCE'));
    for(const pin of shown){
      const p=portList(s).find(x=>x.port===pin);
      const h=node('div','arIOPortTitle');
      h.append(node('span','arIOPortFriendly',kind==='inputs'?(p?.label||'Input '+(pin+1)):'HW OUT '+(pin+1)),
        node('b','arIOPortIndex',String(pin+1)));
      h.title=(kind==='inputs'?(p?.label||'Input '+(pin+1)):'Output '+(pin+1))+' · verified driver pin';
      h.setAttribute('role','columnheader');
      grid.appendChild(h);
    }
    const rowCount=kind==='inputs'?10:(s.draftMonitorOutputs?.length===2?4:2);
    for(let i=0;i<rowCount;i++){
      const r=kind==='inputs'?matrixInputState(s,i):null;
      const draft=kind==='inputs'?channelState(s,i):null;
      const label=node('div','arIOMatrixRowName');
      const monRow=kind==='outputs'&&i>=2;
      label.append(node('strong','',kind==='inputs'?'CH '+String(i+1).padStart(2,'0'):
        (monRow?'MONITOR ':'MAIN ')+(i%2===0?'LEFT':'RIGHT')),
        node('small','',kind==='inputs'?matrixLogicalName(i):monRow?'POST MIX':'STEREO MAIN'));
      if(kind==='inputs'&&r?.mode==='STEREO'){
        const sides=node('div','arIOMatrixSides');
        for(const side of ['L','R']){
          const b=node('button','arIOSideButton',side);b.type='button';b.dataset.ioMatrixSide=side;b.dataset.ioMatrixRow=String(i);
          b.setAttribute('aria-pressed',String((matrixSide.get(i)||'L')===side));
          b.title='Choose '+side+' pin to patch';
          b.disabled=!hardwareReady(s);
          sides.appendChild(b);
        }
        label.appendChild(sides);
      }
      const format=node('div','arIOMatrixFormat');
      if(kind==='inputs'){
        const select=node('select','arIOMatrixMode');
        select.dataset.ioRowMode=String(i);select.setAttribute('aria-label','CH '+(i+1)+' physical source mode');
        for(const [v,name] of [['NONE','—'],['MONO','MONO'],['STEREO','STEREO'],...(r?.mode==='BUS'?[['BUS','BUS']]:[])]){
          const o=node('option','',name);o.value=v;o.selected=r?.mode===v;select.appendChild(o);
        }
        select.disabled=!hardwareReady(s)||r?.mode==='BUS';
        format.appendChild(select);
        format.appendChild(node('small','',r?.mode==='STEREO'?'CLICK L OR R':r?.mode==='BUS'?'BUS · ROUTING':(draft?.channelFormat==='STEREO'?'CH L/R':'CH M')));
      }else{
        format.append(node('strong','',i%2===0?'LEFT':'RIGHT'),node('small','',monRow?'MON BUS':'HW OUTPUT'));
      }
      grid.append(label,format);
      for(const pin of shown){
        const v=kind==='inputs'?r:null;
        const assigned=kind==='inputs'?(v?.mode==='MONO'&&v?.left===pin?'M':
          v?.mode==='STEREO'&&v?.left===pin?'L':v?.mode==='STEREO'&&v?.right===pin?'R':''):
          ((monRow?s.draftMonitorOutputs:(s.draftMainOutputs?.length===2?
             s.draftMainOutputs:s.mainOutputs||[]))[i%2]===pin?(i%2===0?'L':'R'):'');
        const button=node('button','arIOMatrixCell',assigned||'·');
        button.type='button';button.dataset.ioMatrixPin=String(pin);
        button.dataset.ioMatrixRow=String(i);button.dataset.ioMatrixKind=kind;
        button.dataset.assigned=assigned?'true':'false';
        const accepted=kind==='inputs'?s.acceptedRoute?.channels?.[i]:null;
        const live=kind==='inputs'?(accepted?.mode===v?.mode&&accepted?.left===v?.left&&accepted?.right===v?.right):
          ((monRow?s.monitorOutputs:s.mainOutputs||[])[i%2]===pin);
        button.dataset.stage=live?'live':'draft';
        button.setAttribute('role','gridcell');
        button.setAttribute('aria-label',(kind==='inputs'?'Channel '+(i+1):(monRow?'MONITOR ':'MAIN ')+(i%2===0?'left':'right'))+
          ' hardware pin '+(pin+1)+(assigned?' assigned '+assigned:' not assigned'));
        button.setAttribute('aria-pressed',String(!!assigned));
        button.title=assigned?assigned+' · HW '+(pin+1):'Patch HW '+(pin+1);
        button.disabled=!hardwareReady(s)||kind==='inputs'&&v?.mode==='BUS';
        grid.appendChild(button);
      }
    }
    viewport.appendChild(grid);wrap.appendChild(viewport);
    wrap.appendChild(node('p','arIOMatrixLegend','M = mono · L/R = stereo pin assignment · Blue = draft · Green = native live · Empty = unpatched'));
    if(!ports.length)wrap.appendChild(node('p','arIOHint','No verified hardware pins. Select an ASIO / WASAPI device under DEVICE.'));
    return wrap;
  }
  function matrixInputPin(row,pin){
    const s=route(),ports=matrixPorts(s,'inputs');
    if(!hardwareReady(s)||!ports.includes(pin))return matrixSay('Input hardware pin is unavailable or APPLY pending.');
    const r=matrixInputState(s,row);
    if(r?.mode==='BUS')return matrixSay('BUS receiver must be patched in the full ROUTING workspace.');
    const mode=r?.mode==='STEREO'?'STEREO':'MONO';
    if(mode==='MONO'){
      matrixDraft.delete(row);
      const result=window.__arMixStageChannelInput?.(row,'MONO',pin,-1);
      matrixSay(result?.ok?'INPUT DRAFT · CH '+(row+1)+' M → HW '+(pin+1)+' · MUTE + APPLY':result?.error||'Input patch rejected');
    }else{
      const side=matrixSide.get(row)||'L';
      const candidate={mode:'STEREO',epoch:s.epoch,left:Number.isInteger(r?.left)?r.left:-1,
        right:Number.isInteger(r?.right)?r.right:-1};
      candidate[side==='L'?'left':'right']=pin;
      if(candidate.left===candidate.right){
        matrixSay('STEREO L and R cannot use the same hardware pin.');
        return;
      }
      if(candidate.left>=0&&candidate.right>=0){
        const result=window.__arMixStageChannelInput?.(row,'STEREO',candidate.left,candidate.right);
        if(result?.ok){matrixDraft.delete(row);matrixSide.set(row,side==='L'?'R':'L');}
        matrixSay(result?.ok?'STEREO DRAFT · CH '+(row+1)+' L/R → HW '+(candidate.left+1)+'/'+(candidate.right+1):
          result?.error||'Stereo input patch rejected');
      }else{
        matrixDraft.set(row,candidate);matrixSide.set(row,side==='L'?'R':'L');
        matrixSay('CH '+(row+1)+' stereo selection incomplete · choose the '+(side==='L'?'RIGHT':'LEFT')+' pin before APPLY.');
      }
    }
    lastSignature='';refresh();
  }
  function matrixModeChange(select){
    const row=Number(select.dataset.ioRowMode),mode=select.value,s=route(),r=channelState(s,row);
    if(!hardwareReady(s))return matrixSay('Choose a verified DIRECT device first.');
    if(mode==='BUS')return;
    if(mode==='NONE'){
      matrixDraft.delete(row);
      const result=window.__arMixStageChannelInput?.(row,'NONE',-1,-1);
      matrixSay(result?.ok?'CH '+(row+1)+' unpatched in DRAFT · MUTE + APPLY':result?.error||'Failed to clear input');
    }else if(mode==='MONO'&&matrixPorts(s,'inputs').includes(r.left)){
      matrixDraft.delete(row);
      const result=window.__arMixStageChannelInput?.(row,'MONO',r.left,-1);
      matrixSay(result?.ok?'MONO DRAFT · CH '+(row+1)+' · MUTE + APPLY':result?.error||'Input patch rejected');
    }else if(mode==='STEREO'&&r.mode==='STEREO'){
      matrixDraft.delete(row);
      matrixSay('STEREO · choose L or R then a verified hardware pin.');
    }else{
      matrixDraft.set(row,{epoch:s.epoch,mode,left:matrixPorts(s,'inputs').includes(r.left)?r.left:-1,right:-1});
      matrixSide.set(row,mode==='STEREO'&&r.left>=0?'R':'L');
      matrixSay('CH '+(row+1)+' · select '+(mode==='STEREO'?'two different L/R pins':'a mono pin')+' in the matrix. No incomplete route is applied.');
    }
    lastSignature='';refresh();
  }
  // Both physical buses belong to the SAME native routing transaction.
  function matrixOutputPin(row,pin){
    const s=route(),ports=matrixPorts(s,'outputs'),mon=row>=2,side=row%2;
    if(!hardwareReady(s)||!ports.includes(pin))
      return matrixSay('Output pin unavailable or native APPLY pending.');
    const chosen=mon?s.draftMonitorOutputs:s.draftMainOutputs?.length===2?s.draftMainOutputs:
      s.mainOutputs?.length===2?s.mainOutputs:ports.slice(0,2);
    if(!Array.isArray(chosen)||chosen.length!==2)
      return matrixSay('Enable a verified stereo output pair first.');
    const pair=chosen.slice(),old=pair[side];
    if(old===pin)return matrixSay('This output is already assigned.');
    pair[side]=pin;
    if(pair[0]===pair[1])pair[1-side]=old; // atomic L/R swap
    if(pair[0]===pair[1])return matrixSay('Different output pins required.');
    const result=mon?window.__arMixStageMonitorPatch?.(pair[0],pair[1]):
      window.__arMixStageMainOutputs?.(pair[0],pair[1]);
    matrixSay(result?.ok?(mon?'MONITOR':'MAIN')+' DRAFT · HW '+
      (pair[0]+1)+'/'+(pair[1]+1)+' · MUTE + APPLY':result?.error||'Output patch rejected');
    lastSignature='';refresh();
  }
  function stageMonitorEnable(enabled){
    const s=route(),ports=matrixPorts(s,'outputs');
    if(!hardwareReady(s))return matrixSay('Select a verified DIRECT device.');
    const main=s.draftMainOutputs?.length===2?s.draftMainOutputs:
      s.mainOutputs?.length===2?s.mainOutputs:ports.slice(0,2);
    const free=ports.filter(pin=>!main.includes(pin));
    // Only real unused pins are suggested; they stay DRAFT until ACK.
    const result=enabled?free.length>=2&&window.__arMixStageMonitorPatch?.(free[0],free[1]):
      window.__arMixStageMonitorPatch?.(-1,-1);
    matrixSay(result?.ok?(enabled?'MONITOR DRAFT ENABLED · review pins and POST sends':
      'MONITOR DRAFT DISABLED · native APPLY required'):result?.error||
      'MONITOR needs two verified unused output pins.');
    lastSignature='';refresh();
  }

  const deviceType=name=>(deviceInventory?.types||[]).find(t=>t.name===name);
  const deviceSameEndpoint=d=>!!d&&!!deviceInventory?.active?.open&&
    deviceInventory.active.backend===d.backend&&
    deviceInventory.active.captureDevice===d.captureDevice&&
    deviceInventory.active.playbackDevice===d.playbackDevice;
  function makeDeviceDraft(){
    const active=deviceInventory?.active||{},types=deviceInventory?.types||[];
    // DEVICE3: NEVER replace an already-open endpoint with the first entry
    // just because JUCE exposes a different input/output device name list.
    // This is essential for WASAPI capture CABLE Output -> Realtek playback.
    const backend=active.backend||types.find(t=>/ASIO/i.test(t.name))?.name||
      types.find(t=>/Windows Audio|WASAPI/i.test(t.name))?.name||types[0]?.name||'';
    const type=deviceType(backend);
    const hasActive=!!active.open&&active.backend===backend;
    const playback=hasActive?active.playbackDevice||'' :type?.outputs?.[0]||'';
    const capture=hasActive?active.captureDevice||'' :
      type?.separate?type?.inputs?.[0]||'':playback;
    return {backend,captureDevice:capture,playbackDevice:playback,
      sampleRate:hasActive&&active.rate?Number(active.rate):0,
      bufferSize:hasActive&&active.buffer?Number(active.buffer):0,
      enabledInputs:hasActive?[...(active.enabledInputs||[])]:null,
      enabledOutputs:hasActive?[...(active.enabledOutputs||[])]:null};
  }
  function selectDeviceBackend(name){
    if(deviceBusy||!isNative())return;
    const t=deviceType(name);if(!t||deviceDraft?.backend===name)return;
    const playback=t.outputs?.[0]||'',capture=t.separate?t.inputs?.[0]||'':playback;
    const a=deviceInventory?.active||{};
    deviceDraft=(a.backend===name&&a.open)?makeDeviceDraft():
      {backend:name,captureDevice:capture,playbackDevice:playback,
        sampleRate:0,bufferSize:0,enabledInputs:null,enabledOutputs:null};
    deviceNote='DRIVER DRAFT · output unchanged until APPLY DEVICE';
    lastSignature='';refreshIO();
  }
  const pinsEqual=(a,b)=>Array.isArray(a)&&Array.isArray(b)&&
    a.length===b.length&&a.every((v,i)=>v===b[i]);
  function deviceDirty(){
    const d=deviceDraft,a=deviceInventory?.active;
    return !!d&&(!a?.open||a.authorized===false||d.backend!==a.backend||d.captureDevice!==a.captureDevice||
      d.playbackDevice!==a.playbackDevice||
      (d.sampleRate>0&&d.sampleRate!==Number(a.rate))||
      (d.bufferSize>0&&d.bufferSize!==Number(a.buffer))||
      (deviceSameEndpoint(d)&&
        (!pinsEqual(d.enabledInputs,a.enabledInputs)||!pinsEqual(d.enabledOutputs,a.enabledOutputs))));
  }
  function deviceRefresh(rescan=false){
    if(!isNative()||deviceBusy)return;
    const now=Date.now();
    if(rescan){
      if(now-lastDeviceRescan<1000)return; // one vendor scan per user action burst
      lastDeviceRescan=now;
      deviceNote='SCANNING DRIVER LIST · output must be MUTED';
    }else{
      if(now-lastDeviceRefresh<300)return;
      lastDeviceRefresh=now;
    }
    backend.emitEvent('arMixAction',{
      action:rescan?'audioDeviceRefresh':'audioDeviceInventory'
    });
  }
  // P0 MAONO: the stage/ACK boundary must be understandable in the SAME
  // dialog. A blue ASIO selection is never mistaken for an opened ASIO
  // device, and a disabled APPLY must explain the exact safety blocker.
  const deviceSwitchBlocker=s=>{
    if(!isNative())return 'NATIVE APP REQUIRED · browser preview cannot open hardware';
    if(deviceBusy)return 'DEVICE SWITCH IN PROGRESS · wait for native readback';
    if(!deviceDraft||!deviceInventory?.active)return 'REFRESH DEVICE INVENTORY';
    if(lastStatus.secondaryOutputArmed)return 'STEP 1 · DISARM SECONDARY STREAM in SIGNAL CHECK before changing driver';
    // APPLY performs native MUTE first if the output is armed.
    if(s?.pending)return 'WAIT · finish native routing APPLY/ACK before changing driver';
    if(!deviceDraft.backend||!deviceDraft.captureDevice||!deviceDraft.playbackDevice)
      return 'SELECT A VALID DUPLEX AUDIO INTERFACE';
    return '';
  };
  const devicePinNextStep=()=>{
    const a=deviceInventory?.active||{};
    if(!a.open||a.authorized===false)return 'SELECT ASIO/WINDOWS AUDIO, then APPLY DEVICE';
    const maono=/maono/i.test(String(a.captureDevice||'')+' '+String(a.playbackDevice||''));
    return /ASIO/i.test(a.backend||'')&&maono?
      'ASIO OPEN · expand HARDWARE PIN ENABLE, select the actual Maono MIC or VC In 3+4 ports, then APPLY DEVICE again. Next select CH input sources, APPLY.':
      'DEVICE OPEN · verify enabled INPUT/OUTPUT pins, choose CH input sources, APPLY.';
  };
  function deviceApply(){
    if(!isNative()||deviceBusy||!deviceDraft||!deviceInventory?.active)return;
    const d=deviceDraft,a=deviceInventory.active;
    const blocked=deviceSwitchBlocker(route());
    if(blocked){deviceNote=blocked;lastSignature='';refreshIO();return;}
    if(!deviceDirty()){
      const result=window.__arMixApplyAndActivate?.();
      deviceNote=result?.ok?'APPLY ACCEPTED · NATIVE ACK PENDING':'APPLY REJECTED · '+(result?.error||'Route not ready');
      lastSignature='';refreshIO();return;
    }
    if(lastStatus.monitor){
      applyMutePending=true;
      backend.emitEvent('arMixAction',{action:'monitor',value:false});
      deviceNote='APPLY · WAITING FOR HARDWARE MUTE ACK';return;
    }
    sendDeviceRequest();
  }
  function sendDeviceRequest(){
    if(!deviceDirty()||deviceBusy)return;
    const d=deviceDraft,a=deviceInventory.active;
    applyBaseEpoch=Number(route().epoch)||0;
    // Changing hardware is a structural transaction, not a fader parameter.
    deviceRequest='device-'+Date.now()+'-'+(++deviceRequestSeq);
    deviceBusy=true;deviceNote='APPLYING · OUTPUT MUTED · waiting for native readback';
    backend.emitEvent('arMixAction',{action:'audioDeviceApply',requestId:deviceRequest,
      expectedDeviceGeneration:Number(a.deviceGeneration)||0,
      config:{backend:d.backend,captureDevice:d.captureDevice,
        playbackDevice:d.playbackDevice,sampleRate:d.sampleRate,bufferSize:d.bufferSize,
        enabledInputs:Array.isArray(d.enabledInputs)?d.enabledInputs:[],
        enabledOutputs:Array.isArray(d.enabledOutputs)?d.enabledOutputs:[]}});
    lastSignature='';refreshIO();
  }
  function deviceField(label,control){
    const group=node('label','arIODeviceField');
    group.append(node('span','',label),control);return group;
  }
  function deviceSelect(cls,value,options,disabled=false){
    const field=node('select',cls);
    for(const [v,label] of options){
      const opt=node('option','',label);opt.value=String(v);field.appendChild(opt);
    }
    if(value!==''&&!options.some(([v])=>String(v)===String(value))){
      const unmatched=node('option','','CURRENT · '+value+' (not in current driver list)');
      unmatched.value=String(value);field.appendChild(unmatched);
    }
    field.value=String(value??'');
    field.disabled=disabled||!options.length;
    return field;
  }
  function renderCustomDevice(body){
    const a=deviceInventory?.active||{},types=deviceInventory?.types||[];
    if(!deviceDraft&&(types.length||a.open))deviceDraft=makeDeviceDraft();
    const d=deviceDraft,selected=deviceType(d?.backend);
    const panel=node('section','arIORealDeviceSetup arIODeviceControl');
    const activeLine=node('div','arIODeviceActiveLine');
    const activeText=node('div','');
    activeText.append(
      node('span','arIODeviceMicro','CURRENT AUDIO DEVICE · NATIVE READBACK'),
      node('strong','',a.open&&a.authorized!==false?
        a.backend+' · '+a.captureDevice+' → '+a.playbackDevice:
        'NO AUTHORIZED AUDIO DEVICE'),
      node('span','',a.open?Number(a.rate||0).toLocaleString('en-US')+' Hz · '+a.buffer+
        ' samples · '+(lastStatus.monitor?'OUTPUT ARMED':'OUTPUT MUTED'):
        'Select a driver and audio interface below'));
    activeLine.append(activeText,node('span','arIODeviceActiveFlag',
      a.open&&a.authorized!==false?'ACTIVE':'NOT READY'));
    panel.appendChild(activeLine);
    // Operator-first First Sound action: the existing monitor.js controller
    // stages the real native route then independently ARM-ACKs hardware.
    const quick=window.__arMixFastAudioState?.()||{};
    const start=node('button','btn-180 arIODeviceStartAudio',
      'MUTE OUTPUT');
    start.type='button';start.dataset.ioStartAudio='1';start.hidden=!lastStatus.monitor;
    start.disabled=!isNative()||(!lastStatus.monitor&&(
      deviceBusy||!!quick.pending||!a.open||!deviceSameEndpoint(deviceDraft)||
      deviceDirty()||!hardwareReady(route())));
    start.setAttribute('aria-label',lastStatus.monitor?'Mute physical output':
      'Start verified live routing and arm hardware output');
    activeLine.appendChild(start);
    const accepted=route().acceptedRoute?.channels||[];
    const used=accepted.map((r,i)=>r.mode==='MONO'||r.mode==='STEREO'?
      'CH'+String(i+1).padStart(2,'0'):null).filter(Boolean);
    const firstSound=node('div','arIODeviceFirstSound',quick.message||(
      lastStatus.monitor?'OUTPUT ARMED · '+used.join(' + ')+' → MAIN → HARDWARE':
      used.length?used.join(' + ')+' → MAIN ready · APPLY to hear sound':
      'FIRST SOUND · APPLY maps verified input pins, routes and arms MAIN'));
    panel.appendChild(firstSound);

    if(!types.length&&!a.open)
      panel.appendChild(node('p','arIODeviceEmpty',
        'No reported Windows audio devices. Refresh driver list while all outputs are MUTED.'));
    if(d){
      const choose=node('div','arIODeviceChoose');
      const driver=node('div','arIODeviceDriverGroup');
      driver.appendChild(node('span','arIODeviceMicro','AUDIO SYSTEM'));
      const toggle=node('div','arIODeviceDriverToggle');
      toggle.setAttribute('role','group');
      toggle.setAttribute('aria-label','Audio system: ASIO or Windows Audio');
      const asio=types.find(t=>/^ASIO$/i.test(t.name));
      const windows=types.find(t=>/^(Windows Audio|WASAPI)$/i.test(t.name));
      for(const [label,type] of [['ASIO',asio],['WINDOWS AUDIO',windows]]){
        const button=node('button','arIODeviceDriverButton',label);
        button.type='button';button.dataset.ioBackendSelect=type?.name||'';
        button.disabled=deviceBusy||!isNative()||!type;
        button.setAttribute('aria-pressed',String(!!type&&d.backend===type.name));
        button.title=type?'Choose '+type.name+' · changes are staged':'This backend is not installed';
        toggle.appendChild(button);
      }
      driver.appendChild(toggle);choose.appendChild(driver);
      const chosen=selected||{inputs:[],outputs:[],separate:false};
      const interfaceGroup=node('div','arIODeviceInterfaceGroup');
      interfaceGroup.appendChild(deviceField(chosen.separate?'PLAYBACK DEVICE':'AUDIO INTERFACE',
        deviceSelect('arIODevicePlayback',d.playbackDevice,
          (chosen.outputs||[]).map(n=>[n,n]),deviceBusy||!isNative())));
      if(chosen.separate){
        interfaceGroup.appendChild(deviceField('CAPTURE DEVICE',
          deviceSelect('arIODeviceCapture',d.captureDevice,
            (chosen.inputs||[]).map(n=>[n,n]),deviceBusy||!isNative())));
      }else{
        // Keep the unified duplex field in the DOM for native/smoke parity;
        // the user should not see two misleading ASIO selectors.
        const linked=node('input','arIODeviceCapture');linked.type='hidden';
        linked.value=d.captureDevice;linked.disabled=true;interfaceGroup.appendChild(linked);
      }
      choose.appendChild(interfaceGroup);panel.appendChild(choose);
      if(types.some(t=>t.name!==asio?.name&&t.name!==windows?.name)){
        const other=node('details','arIODeviceOther');
        other.appendChild(node('summary','','OTHER AUDIO BACKENDS'));
        other.appendChild(deviceField('BACKEND',
          deviceSelect('arIODeviceBackend',d.backend,types.map(t=>[t.name,t.name]),
            deviceBusy||!isNative())));
        panel.appendChild(other);
      }else{
        const hidden=node('input','arIODeviceBackend');hidden.type='hidden';
        hidden.value=d.backend;panel.appendChild(hidden);
      }
      const same=deviceSameEndpoint(d),advanced=node('details','arIODeviceAdvanced');
      advanced.appendChild(node('summary','','BUFFER / SAMPLE RATE / HARDWARE PIN ENABLE'));
      const fields=node('div','arIODeviceFields');
      const rateOpts=[[0,'AUTO · driver default']],bufferOpts=[[0,'AUTO · driver default']];
      if(same){
        for(const v of [...new Set([...(a.rates||[]),a.rate].filter(v=>Number(v)>0))])
          rateOpts.push([Number(v),Number(v)/1000+' kHz']);
        for(const v of [...new Set([...(a.buffers||[]),a.buffer].filter(v=>Number(v)>0))])
          bufferOpts.push([Number(v),v+' samples']);
      }
      fields.appendChild(deviceField('SAMPLE RATE',
        deviceSelect('arIODeviceRate',d.sampleRate,rateOpts,deviceBusy||!isNative())));
      fields.appendChild(deviceField('BUFFER / PERIOD',
        deviceSelect('arIODeviceBuffer',d.bufferSize,bufferOpts,deviceBusy||!isNative())));
      advanced.appendChild(fields);
      const pins=node('div','arIODevicePins');
      for(const [direction,names,selectedPins] of [
        ['INPUT',same?a.inputNames:[],d.enabledInputs],
        ['OUTPUT',same?a.outputNames:[],d.enabledOutputs]]){
        const side=node('div','arIODevicePinGroup');
        side.appendChild(node('strong','',direction+' · DRIVER PORT ENABLE'));
        if(!same||!names?.length)
          side.appendChild(node('small','',
            'Available after this device is opened and acknowledged by JUCE'));
        else names.forEach((name,index)=>{
          const lbl=node('label','arIODevicePinRow');
          const cb=node('input','arIODevicePin');cb.type='checkbox';
          cb.dataset.devicePinDirection=direction;cb.value=String(index);
          cb.checked=Array.isArray(selectedPins)&&selectedPins.includes(index);
          cb.disabled=deviceBusy||!isNative();
          lbl.append(cb,node('span','',String(index+1).padStart(2,'0')+' · '+name));
          side.appendChild(lbl);
        });
        pins.appendChild(side);
      }
      advanced.appendChild(pins);panel.appendChild(advanced);
    }
    const actions=node('div','arIODeviceActions');
    const refresh=node('button','btn-180 arIODeviceRefresh','REFRESH DEVICES');
    refresh.type='button';refresh.dataset.ioRefreshDevice='1';
    refresh.disabled=deviceBusy||!isNative();
    actions.appendChild(refresh);
    if(d&&!deviceSameEndpoint(d))
      actions.appendChild(node('span','arIODevicePending',
        'ASIO DEVICE SELECTED · NOT ACTIVE · current engine still uses '+String(a.backend||'NO DEVICE')));
    // No extra buttons or heavy cards: one state-derived instruction beside
    // the driver selector, independent from a stale transient feedback note.
    if(d&&deviceDirty())
      actions.appendChild(node('span','arIODevicePending arIODeviceNextStep',
        deviceSwitchBlocker(route())||'STEP 2 · CLICK APPLY DEVICE · physical output stays MUTED'));
    else if(a.open)
      actions.appendChild(node('span','arIODevicePending arIODeviceNextStep',
        devicePinNextStep()));
    panel.appendChild(actions);body.appendChild(panel);

    const matrixSection=node('section','arIODevicePatchMatrix');
    const heading=node('div','arIODeviceMatrixHeading');
    heading.appendChild(node('strong','','AUDIO I/O · REAL PORT MATRIX'));
    const tabs=node('div','arIODeviceMatrixSwitch');
    for(const [value,label] of [['inputs','INPUTS'],['outputs','OUTPUTS']]){
      const b=node('button','arIODeviceMatrixTab',label);b.type='button';
      b.dataset.ioMatrixView=value;
      b.setAttribute('aria-pressed',String(deviceMatrixView===value));
      tabs.appendChild(b);
    }
    heading.appendChild(tabs);matrixSection.appendChild(heading);
    const live=!!d&&deviceSameEndpoint(d)&&a.authorized!==false;
    const s=route();
    if(deviceMatrixWaitEpoch!==null&&s.epoch!==deviceMatrixWaitEpoch&&s.ready&&s.active)
      deviceMatrixWaitEpoch=null;
    if(!live){
      matrixSection.appendChild(node('p','arIODeviceMatrixEmpty',
        'APPLY DEVICE first. The M/L/R matrix will show only pins actually enabled and reported by the new driver.'));
    }else if(!hardwareReady(s)||deviceMatrixWaitEpoch!==null){
      matrixSection.appendChild(node('p','arIODeviceMatrixEmpty',
        'Waiting for the native routing session and verified hardware ports. No virtual ports are invented.'));
    }else{
      if(deviceMatrixView==='inputs'){
        matrixSection.appendChild(makeSoftwareInputs(s));
        const direct=node('details','arIODeviceDirectMatrix');
        direct.open=directMatrixOpen;
        direct.addEventListener('toggle',()=>{directMatrixOpen=direct.open;});
        direct.appendChild(node('summary','','DIRECT MIXER CHANNEL PATCH · CH01–10'));
        direct.appendChild(makeMatrix(s,deviceMatrixView));
        matrixSection.appendChild(direct);
      }else matrixSection.appendChild(makeMatrix(s,deviceMatrixView));
      matrixSection.appendChild(node('p','arIODeviceMatrixNote',
        deviceMatrixView==='inputs'?
        'Named inputs are reusable SOURCES. APPLY builds a verified initial route; only native ACK confirms sound routing.':
        'MAIN / MONITOR outputs use muted APPLY PATCH and native ACK.'));
    }
    body.appendChild(matrixSection);
  }

  function refreshIO(){
    const s=route(),id=el('arIOIdentity'),body=el('arIOBody');
    matrixResetIfDeviceChanged(s);
    refreshAudioPath();
    const status=(lastStatus.driver||'NO DEVICE')+' · '+(Number(lastStatus.rate)||0)+' Hz · '+(Number(lastStatus.buffer)||0)+' samples';
    if(id.textContent!==status)id.textContent=status;
    overlay.querySelectorAll('[data-io-tab]').forEach(b=>{
      const active=b.dataset.ioTab===ioTab;b.classList.toggle('active',active);
      b.setAttribute('aria-selected',String(active));
    });
    overlay.dataset.activeTab=ioTab;
    const matrixInDevice=ioTab==='device'&&deviceSameEndpoint(deviceDraft)&&hardwareReady(s);
    overlay.querySelector('[data-mix-apply]').hidden=ioTab==='device';
    overlay.querySelector('[data-io-revert]').hidden=ioTab==='device'&&!matrixInDevice;
    overlay.querySelector('[data-io-internal]').hidden=ioTab==='device';
    overlay.querySelector('[data-io-device-apply]').hidden=ioTab!=='device';
    overlay.querySelector('[data-io-device-revert]').hidden=ioTab!=='device';
    const deviceBlocker=deviceSwitchBlocker(s);
    overlay.querySelector('[data-io-device-apply]').disabled=!!deviceBlocker||deviceBusy||applyMutePending||!!window.__arMixFastAudioState?.().pending||(!deviceDirty()&&(!hardwareReady(s)||!!lastStatus.monitor));
    overlay.querySelector('[data-io-device-apply]').textContent=deviceBusy||applyMutePending?'APPLYING…':'APPLY';
    overlay.querySelector('[data-io-device-apply]').title=deviceBlocker||
      (deviceDirty()?'Commit selected device then verify physical pin readback':'No pending device edit');
    overlay.querySelector('[data-io-device-revert]').disabled=deviceBusy||!deviceDirty();
    overlay.querySelector('[data-mix-apply]').disabled=!hardwareReady(s)||!!matrixDraft.size||
      (ioTab==='device'&&deviceDirty());
    overlay.querySelector('[data-io-revert]').disabled=!isNative()||s.pending;
    const signature=ioTab+'|'+JSON.stringify([s.epoch,s.revision,s.pending,s.draftRoutes,
      s.outputPorts,s.mainOutputs,s.draftMainOutputs,s.monitorOutputs,s.draftMonitorOutputs,s.monitorSends,s.draftMonitorSends,s.ports,deviceInventory,deviceDraft,deviceBusy,deviceNote,matrixPage[ioTab==='device'?deviceMatrixView:ioTab],deviceMatrixView,monitorSendsExpanded,
      [...matrixDraft],[...matrixSide],softwareEdit,softwarePage,sourceIdentity(s),sourceAliases(s),
      !!lastStatus.monitor,window.__arMixFastAudioState?.()]);
    if(lastSignature===signature)return;
    lastSignature=signature;
    // Preserves two-step stereo L/R selection across each native DRAFT render.
    // Closing the advanced matrix mid-edit made its crosspoints inaccessible.
    const previous=body.querySelector('.arIOAdvancedMatrix');
    if(previous)advancedMatrixOpen=previous.open;
    const previousDirect=body.querySelector('.arIODeviceDirectMatrix');
    if(previousDirect)directMatrixOpen=previousDirect.open;
    body.replaceChildren();
    if(ioTab!=='device'){
      const identity=node('div','arIOHardwareHeader');
      const art=node('span','arIOHardwareIcon','I/O');art.setAttribute('aria-hidden','true');
      const textWrap=node('div','arIOHardwareText');
      textWrap.append(node('small','','AUDIO INTERFACE · DRIVER REPORTED'),
        node('strong','',lastStatus.driver||'NO ACTIVE DRIVER'),
        node('span','',ioTab==='inputs'
          ?matrixPorts(s,'inputs').length+' verified inputs · map to logical mixer channels'
          :matrixPorts(s,'outputs').length+' verified outputs · patch only supported physical buses'));
      identity.append(art,textWrap);body.appendChild(identity);
      if(ioTab==='inputs'){
        body.appendChild(makeQuickInputs(s));
        const advanced=node('details','arIOAdvancedMatrix');
        advanced.open=advancedMatrixOpen;
        advanced.appendChild(node('summary','','ADVANCED M / L / R CROSSPOINT · SAME NATIVE ROUTING'));
        advanced.appendChild(makeMatrix(s,ioTab));
        body.appendChild(advanced);
      }else body.appendChild(makeMatrix(s,ioTab));
    }
    if(ioTab==='inputs'){
      body.appendChild(node('p','arIOHint','MAONO PS22 virtual inputs appear here ONLY if its selected driver enumerates them as capture ports. Configure MAONO Routing Center and select the appropriate PS22 ASIO/recording endpoint in DEVICE / DRIVER. Separate Windows capture endpoints cannot be silently combined into one device. Processing MONO/STEREO is independent of physical source mode.'));
    }else if(ioTab==='outputs'){
      const monitorEnabled=s.draftMonitorOutputs?.length===2;
      const monitorControl=node('div','arIOMonitorControls');
      const toggle=node('button','btn-180',monitorEnabled?'DISABLE MONITOR':'ENABLE MONITOR · DRAFT');
      toggle.type='button';toggle.dataset.ioMonitorToggle=monitorEnabled?'off':'on';
      toggle.disabled=!hardwareReady(s)||(!monitorEnabled&&matrixPorts(s,'outputs').length<4);
      monitorControl.append(toggle,node('span','',monitorEnabled?
        'HW '+(s.draftMonitorOutputs[0]+1)+'/'+(s.draftMonitorOutputs[1]+1)+' · separate POST mix':
        'Needs two unused hardware output pins · native APPLY required'));
      body.appendChild(monitorControl);
      if(monitorEnabled){
        const fold=node('details','arIOMonitorSends');fold.open=monitorSendsExpanded;
        fold.appendChild(node('summary','','MONITOR SENDS · CH01–10 + AUX1–4 · POST-FADER'));
        const gains=s.draftMonitorSends?.length===14?s.draftMonitorSends:Array(14).fill(0);
        const rows=node('div','arIOMonitorSendGrid');
        for(let i=0;i<14;i++){
          const row=node('label','arIOMonitorSendRow');
          row.appendChild(node('span','',i<10?'CH '+String(i+1).padStart(2,'0'):'AUX '+(i-9)));
          const select=node('select','arIOMonitorSend');select.dataset.lane=String(i);
          select.setAttribute('aria-label','Monitor POST gain '+(i<10?'CH '+(i+1):'AUX '+(i-9)));
          const options=[[0,'OFF'],[.125,'−18 dB'],[.25,'−12 dB'],[.5,'−6 dB'],[1,'0 dB']];
          if(!options.some(([value])=>value===gains[i]))options.push([gains[i],'CUSTOM']);
          for(const [value,name] of options){
            const o=node('option','',name);o.value=String(value);o.selected=value===gains[i];select.appendChild(o);
          }
          select.disabled=!hardwareReady(s);row.appendChild(select);rows.appendChild(row);
        }
        fold.appendChild(rows);fold.addEventListener('toggle',()=>{monitorSendsExpanded=fold.open});
        body.appendChild(fold);
      }
      const monitorLive=s.monitorOutputs?.length===2?s.monitorOutputs:[];
      body.appendChild(node('p','arIOHint','LIVE MONITOR: '+
        (monitorLive.length===2?'HW '+(monitorLive[0]+1)+'/'+(monitorLive[1]+1):'DISABLED')+
        ' · Independent POST-fader mix; all changes require MUTE → APPLY → ACK.'));
      const details=node('details','arIOQuickMap');
      details.appendChild(node('summary','','QUICK OUTPUT SELECTION · ACCESSIBLE ALTERNATIVE'));
      const accepted=s.mainOutputs?.length===2?s.mainOutputs:[];
      const draft=s.draftMainOutputs?.length===2?s.draftMainOutputs:accepted;
      const ports=matrixPorts(s,'outputs');
      for(const [i,label] of ['MAIN LEFT','MAIN RIGHT'].entries()){
        const row=node('label','arIOOutputRow');
        const selector=node('select','arIOMainOutput');
        selector.dataset.side=String(i);
        selector.setAttribute('aria-label',label+' destination');
        for(const p of ports){
          const option=node('option','','HW OUT '+(p+1));
          option.value=String(p);option.selected=p===draft[i];selector.appendChild(option);
        }
        selector.disabled=!hardwareReady(s)||ports.length<2;
        row.append(node('strong','',label),selector);details.appendChild(row);
      }
      body.appendChild(details);
      body.appendChild(node('p','arIOHint','LIVE MAIN: '+(accepted.length===2?'HW '+(accepted[0]+1)+'/'+(accepted[1]+1):'UNAVAILABLE')+
        ' · Only native-ACKed MAIN/MONITOR pairs can carry sound. AUX1–4 are internal stereo FX returns, not physical output sockets.'));
    }else{
      renderCustomDevice(body);
    }
    const hint=!isNative()?'PREVIEW ONLY · no native hardware':
      s.monitor?'MUTE OUTPUT BEFORE APPLY':s.pending?'WAITING FOR NATIVE ACK':
      s.ready?'E'+s.epoch+' · Native APPLY required for input edits':'CHOOSE A VERIFIED DIRECT DEVICE';
    el('arIOFeedback').textContent=(ioTab==='device'?deviceNote:matrixMessage)||hint;
  }
  function changeSource(select){
    const channel=Number(select.dataset.channel);
    const value=select.value,parts=value.split(':');
    const mode=parts[0],left=mode==='NONE'?-1:Number(parts[1]),right=mode==='STEREO'?Number(parts[2]):-1;
    const direct=select.classList.contains('arBankSource');
    const response=direct?window.__arMixCommitSelectedSource?.(channel,mode,left,right):
      window.__arMixStageChannelInput?.(channel,mode,left,right);
    if(response?.pending)quickSourcePending={channel,epoch:response.epoch,
      value:valueFor({mode,left,right}),live:!!response.live};
    if(!response?.ok){
      const message=response?.error||'Native INPUT PATCH unavailable';
      el('arIOFeedback').textContent=message;
      if(typeof window.flash==='function')window.flash(message);
    }
    lastSignature='';refresh();
  }
  document.addEventListener('change',e=>{
    if(e.target.matches('[data-io-quick-mode]')){
      const row=e.target.closest('[data-io-quick-row]'),mode=e.target.value;
      const left=row.querySelector('[data-io-quick-pin="left"]'),
        right=row.querySelector('[data-io-quick-pin="right"]');
      left.disabled=mode==='NONE'||mode==='BUS';
      right.disabled=mode!=='STEREO';
      if(mode!=='STEREO')right.value='-1';
      return;
    }
    if(e.target.matches('[data-io-jump]')){
      focusedInput=Number(e.target.value);
      overlay.querySelector('[data-io-quick-row="'+focusedInput+'"]')?.scrollIntoView({block:'nearest'});
      return;
    }
    if(e.target.matches('.arIOMatrixMode')){matrixModeChange(e.target);return;}
    if(e.target.matches('.arIOMonitorSend')){
      const result=window.__arMixStageMonitorSend?.(Number(e.target.dataset.lane),Number(e.target.value));
      matrixSay(result?.ok?'MONITOR SEND DRAFT · APPLY required':result?.error||'MONITOR send rejected');
      lastSignature='';refresh();return;
    }
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
  // AUX MASTER is a real continuous parameter as well; input and release
  // share the same bounded native ACK/coalescing controller.
  const sendAuxMaster=event=>{
    if(!event.target.matches('[data-aux-master]'))return;
    const bus=Number(sofControls.querySelector('[data-sof-bus]').value);
    const db=Number(event.target.value);
    const gain=db<=-60?0:Math.pow(10,db/20);
    const answer=window.__arMixSetLiveAuxMasterGain?.(bus,+gain.toFixed(7));
    if(!answer?.ok)sofMessage=answer?.error||'AUX master unavailable';
    const read=sofControls.querySelector('[data-aux-master-read]');
    if(read)read.textContent=db<=-60?'OFF':db.toFixed(1)+' dB';
  };
  sofControls.addEventListener('input',sendAuxMaster);
  sofControls.addEventListener('change',event=>{
    if(event.target.matches('[data-sof-bus]')){sofMessage='';refresh();return;}
    if(!event.target.matches('[data-aux-master]'))return;
    sendAuxMaster(event);
  });
  // Channel focus is intentionally lightweight: point into existing MIX
  // modes and native controller, never create a parallel mixer/FX model.
  focusFlow.addEventListener('click',event=>{
    const button=event.target.closest('[data-focus-step]');
    if(!button)return;
    event.preventDefault();
    const index=selectedInputChannel();
    const strip=mixer.querySelector('[data-strip="'+index+'"]');
    const step=button.dataset.focusStep;
    if(step==='source'){
      if(!route().ready)openIO('device');
      else openSourceChooser(index);
    } else if(step==='fx'){
      setMode('FX');
      if(typeof chooseChannel==='function')chooseChannel(index);
      strip?.querySelector('.rack')?.scrollIntoView?.({block:'nearest',inline:'nearest'});
    } else if(step==='sends'){
      setMode('SENDS');
      const sends=route().acceptedRoute?.sends||[];
      const found=sends.find(x=>x.from===index&&x.to>=10&&x.to<14);
      editSend(index,found?found.to-10:0,button);
    } else if(step==='output')openIO('outputs',index);
  });
  document.addEventListener('keydown',event=>{
    if(event.key==='Escape'&&!sourceChooser.hidden)closeSourceChooser();
  },true);
  document.addEventListener('click',event=>{
    if(event.target.closest?.('#arChannelFocus,[data-focus-step]'))return;
    const strip=event.target.closest?.('.strip[data-strip],[data-choose],[data-chanselect]');
    if(strip)refreshFocus(route());
  });
  // The legacy native VST3 bridge registers document capture before this module.
  // Window capture intercepts only AUDIO SETUP, without changing FX/Orbit ownership.
  window.addEventListener('click',e=>{
    const setup=e.target.closest?.('[data-native-action="audioSetup"]');
    if(!setup)return;
    e.preventDefault();e.stopImmediatePropagation();openIO('device');
  },true);
  document.addEventListener('click',e=>{
    const b=e.target.closest('button');
    if(!b)return;
    if(b.dataset.nativeAction==='audioSetup'){e.preventDefault();e.stopImmediatePropagation();openIO('device');return;}
    if(b.dataset.ioOpen!==undefined){e.preventDefault();e.stopImmediatePropagation();openIO('inputs');return;}
    if(b.dataset.sofToggle!==undefined){e.preventDefault();e.stopImmediatePropagation();sofToggle();return;}
    if(b.dataset.sofAdd!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      const ch=Number(b.closest('[data-strip]')?.dataset.strip);
      if(sofBus>=0&&Number.isInteger(ch))sofStage(ch,'create');
      return;
    }
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
    if(b.dataset.ioQuickStage!==undefined){
      e.preventDefault();e.stopImmediatePropagation();stageQuickInput(b);return;
    }
    if(b.dataset.ioInternal!==undefined){
      e.preventDefault();e.stopImmediatePropagation();closeIO();window.__arMixOpenRoutingLab?.();return;
    }
    if(b.dataset.ioStartAudio!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      const result=window.__arMixStartLiveAudio?.();
      deviceNote=result?.ok?'START AUDIO requested · native ACK required':
        'START REJECTED · '+(result?.error||'Native device not ready');
      lastSignature='';refreshIO();return;
    }
    if(b.dataset.ioSoftwareAdd){
      e.preventDefault();e.stopImmediatePropagation();beginSoftwareInput(b.dataset.ioSoftwareAdd);return;
    }
    if(b.dataset.ioSoftwarePage!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      softwarePage=Math.max(0,softwarePage+Number(b.dataset.ioSoftwarePage));
      lastSignature='';refreshIO();return;
    }
    if(b.dataset.ioSoftwareSide){
      e.preventDefault();e.stopImmediatePropagation();
      if(softwareEdit){softwareEdit.side=b.dataset.ioSoftwareSide;lastSignature='';refreshIO();}
      return;
    }
    if(b.dataset.ioSoftwarePin!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      assignSoftwarePin(Number(b.dataset.ioSoftwareIndex),Number(b.dataset.ioSoftwarePin));return;
    }
    if(b.dataset.ioSoftwareAction){
      e.preventDefault();e.stopImmediatePropagation();
      const s=route(),items=sourceAliases(s),index=Number(b.dataset.ioSoftwareIndex);
      const action=b.dataset.ioSoftwareAction;
      if(action==='save'){saveSoftwareInput();return;}
      if(action==='cancel'){
        softwareEdit=null;softwareSay('SOFTWARE I/O DRAFT CANCELLED · native audio unchanged');
      }else if(!hardwareReady(s)||deviceDirty()||!deviceSameEndpoint(deviceDraft)||!sourceIdentity(s)){
        softwareSay('Hardware not ready; software input unchanged.');
      }else if(!Number.isInteger(index)||index<0||index>=items.length){
        softwareSay('Software input no longer exists.');
      }else if(action==='edit'){beginSoftwareInput(items[index].mode,index);return;}
      else if(action==='remove'){
        if(saveSourceAliases(s,items.filter((_,i)=>i!==index))){
          softwareEdit=null;softwareSay('NAME REMOVED · existing native channel routes unchanged.');
          if(!sourceChooser.hidden)renderSourceChooser();refreshFocus(s);
        }else softwareSay('Could not update device-specific source names.');
      }else if(action==='use'){
        const alias=items[index],result=window.__arMixCommitSelectedSource?.(
          focusedInput,alias.mode,alias.left,alias.mode==='STEREO'?alias.right:-1);
        if(result?.pending)quickSourcePending={
          channel:focusedInput,epoch:result.epoch,value:aliasValue(alias),live:!!result.live};
        softwareSay(result?.ok?(result.pending?
          'CH '+(focusedInput+1)+' SOURCE PENDING · WAIT NATIVE ACK':
          'SOURCE ALREADY ACKNOWLEDGED'):
          result?.error||'Native source switch rejected');
      }
      lastSignature='';refresh();return;
    }
    if(b.dataset.ioMatrixPin!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      const row=Number(b.dataset.ioMatrixRow),pin=Number(b.dataset.ioMatrixPin);
      if(b.dataset.ioMatrixKind==='inputs')matrixInputPin(row,pin);
      else matrixOutputPin(row,pin);
      return;
    }
    if(b.dataset.ioMatrixSide!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      matrixSide.set(Number(b.dataset.ioMatrixRow),b.dataset.ioMatrixSide);
      lastSignature='';refreshIO();return;
    }
    if(b.dataset.ioPage!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      const pageKind=ioTab==='device'?deviceMatrixView:ioTab;
      matrixPage[pageKind]=Math.max(0,matrixPage[pageKind]+Number(b.dataset.ioPage));
      lastSignature='';refreshIO();return;
    }
    if(b.dataset.ioRevert!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      const result=window.__arMixRevertMixerDraft?.();
      if(result?.ok){matrixDraft.clear();matrixSide.clear();}
      matrixSay(result?.ok?'DRAFT REVERTED · native route unchanged':result?.error||'Cannot revert while APPLY is pending');
      lastSignature='';refresh();return;
    }
    if(b.dataset.ioMonitorToggle!==undefined){
      e.preventDefault();e.stopImmediatePropagation();stageMonitorEnable(b.dataset.ioMonitorToggle==='on');return;
    }
    if(b.dataset.ioSignalCheck!==undefined){
      e.preventDefault();e.stopImmediatePropagation();closeIO();
      document.getElementById('arSignalOpen')?.click();return;
    }
    if(b.dataset.ioClose!==undefined){e.preventDefault();e.stopImmediatePropagation();closeIO();return;}
    if(b.dataset.ioTab){e.preventDefault();e.stopImmediatePropagation();ioTab=b.dataset.ioTab;matrixMessage='';lastSignature='';refreshIO();return;}
    if(b.dataset.ioMatrixView){
      e.preventDefault();e.stopImmediatePropagation();
      deviceMatrixView=b.dataset.ioMatrixView;
      lastSignature='';refreshIO();return;
    }
    if(b.dataset.ioBackendSelect!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      if(b.dataset.ioBackendSelect)selectDeviceBackend(b.dataset.ioBackendSelect);
      return;
    }
    if(b.dataset.ioRefreshDevice!==undefined){
      e.preventDefault();e.stopImmediatePropagation();deviceRefresh(true);return;
    }
    if(b.dataset.ioDeviceApply!==undefined){
      e.preventDefault();e.stopImmediatePropagation();deviceApply();return;
    }
    if(b.dataset.ioDeviceRevert!==undefined){
      e.preventDefault();e.stopImmediatePropagation();
      if(!deviceBusy){deviceDraft=makeDeviceDraft();deviceNote='DEVICE DRAFT REVERTED';lastSignature='';refreshIO();}
      return;
    }
    if(b.dataset.mixPatch){
      e.preventDefault();e.stopImmediatePropagation();
      const strip=b.closest('[data-strip]'),channel=Number(strip?.dataset.strip)||0;
      if(b.dataset.mixPatch==='input')openIO('inputs',channel);
      else window.__arMixOpenChannelPatch?.(channel,b.dataset.mixPatch);
      return;
    }
    if(b.dataset.mixApply!==undefined){e.preventDefault();e.stopImmediatePropagation();const response=window.__arMixApplyAndActivate?.();if(response&&!response.ok)el('arIOFeedback').textContent=response.error;refresh();return;}
  },true);
  document.addEventListener('input',e=>{
    if(e.target.matches('.arIOSoftwareNameEdit')&&softwareEdit)
      softwareEdit.name=e.target.value;
  },true);
  document.addEventListener('change',e=>{
    const target=e.target;
    if(overlay.hidden||ioTab!=='device'||deviceBusy||!deviceDraft)return;
    if(target.matches('.arIODeviceBackend')){
      selectDeviceBackend(target.value);return;
    }else if(target.matches('.arIODevicePlayback')){
      deviceDraft.playbackDevice=target.value;
      if(!deviceType(deviceDraft.backend)?.separate)deviceDraft.captureDevice=target.value;
    }else if(target.matches('.arIODeviceCapture')){
      deviceDraft.captureDevice=target.value;
    }else if(target.matches('.arIODeviceRate')){
      deviceDraft.sampleRate=Number(target.value)||0;
    }else if(target.matches('.arIODeviceBuffer')){
      deviceDraft.bufferSize=Number(target.value)||0;
    }else if(target.matches('.arIODevicePin')){
      const key=target.dataset.devicePinDirection==='INPUT'?'enabledInputs':'enabledOutputs';
      const set=new Set(deviceDraft[key]||[]),pin=Number(target.value);
      if(target.checked)set.add(pin);else set.delete(pin);
      deviceDraft[key]=[...set].sort((a,b)=>a-b);
    }else return;
    deviceNote='DEVICE DRAFT · changes are not live until native APPLY';
    lastSignature='';refreshIO();
  },true);
  document.addEventListener('change',e=>{
    if(sendEditor.hidden||!sendFocus||!sendEditor.contains(e.target))return;
    const input=e.target;
    if(input.dataset.sendReturn!==undefined){
      const field=input.dataset.sendReturn;
      // Existing accepted AUX master gain is a live parameter; the
      // return enable/main/mute controls still alter structural topology.
      if(field==='gain' && sendEditorLiveReady(sendFocus)){
        sendMutation(()=>window.__arMixSetLiveAuxMasterGain?.(
          sendFocus.bus,Number(input.value)));
      }else sendMutation(()=>window.__arMixStageAuxReturnEdit?.(
        sendFocus.bus,field,field==='gain'?Number(input.value):input.checked));
    }else {
      const field=input.dataset.sendGain!==undefined?'gain':
        input.dataset.sendPan!==undefined?'pan':
        input.dataset.sendEnable!==undefined?'enable':null;
      if(!field)return;
      const tap=input.dataset.sendGain||input.dataset.sendPan||input.dataset.sendEnable;
      // The same existing native send parameter authority used by SEND
      // FADERS serves the focused editor. No second routing controller.
      if(field==='gain'&&tap==='POST'&&sendEditorLiveReady(sendFocus) &&
         (route().acceptedRoute?.sends||[]).some(x=>x.from===sendFocus.from&&
           x.to===10+sendFocus.bus&&x.tap==='POST')){
        sendMutation(()=>window.__arMixSetLiveAuxSendGain?.({
          from:sendFocus.from,bus:sendFocus.bus,gain:Number(input.value)}));
      }else sendMutation(()=>window.__arMixStageAuxSendEdit?.(
        {...sendFocus,tap,action:field,
          value:field==='enable'?input.checked:Number(input.value)}));
    }
  },true);
  document.addEventListener('keydown',e=>{
    if(e.key!=='Escape')return;
    if(!sendEditor.hidden){closeSendEditor();e.stopImmediatePropagation();}
    else if(!overlay.hidden){closeIO();e.stopImmediatePropagation();}
  },true);
  const observer=new MutationObserver(records=>{
    if(records.some(r=>r.type==='childList'))refresh();
    if(records.some(r=>r.attributeName==='class'))
      focusFlow.hidden=mixer.classList.contains('hidden');
  });
  observer.observe(mixer,{childList:true,attributes:true,attributeFilter:['class']});
  focusFlow.hidden=mixer.classList.contains('hidden');
  backend?.addEventListener?.('arMixStatus',d=>{
    lastStatus=d;
    if(applyAfterDevice && Number(d?.deviceGeneration)===applyAfterDevice.generation && Number(d?.dryRoutingLab?.epoch)>applyAfterDevice.epoch && d?.dryRoutingLab?.ready && d?.dryRoutingLab?.active){
      applyAfterDevice=null;
      const result=window.__arMixApplyAndActivate?.();
      deviceNote=result?.ok?'APPLY · NATIVE ROUTE/ARM ACK PENDING':'APPLY REJECTED · '+(result?.error||'Source not ready');
    }
    // 12Hz native telemetry: update only when topology, epoch or safety changes.
    const s=d.dryRoutingLab||{},key=JSON.stringify([s.epoch,s.revision,s.ready,s.ports,s.outputPorts,s.acceptedRoute,d.monitor,d.masterMute]);
    if(window.__armixIoStatusKey!==key){window.__armixIoStatusKey=key;lastSignature='';refresh();}
    if(!overlay.hidden){
      refreshAudioPath(); // P0 text-only 12Hz; no matrix DOM churn
      const identity=(d.driver||'')+'/'+(d.rate||0)+'/'+(d.buffer||0);
      if(overlay.dataset.deviceIdentity!==identity){overlay.dataset.deviceIdentity=identity;refreshIO();}
    }
  });
  backend?.addEventListener?.('arMixAudioDeviceInventory',d=>{
    const hadDraft=deviceDirty();
    deviceInventory=d;
    if(!hadDraft&&!deviceBusy)deviceDraft=makeDeviceDraft();
    if(!sourceChooser.hidden){renderSourceChooser();refreshFocus(route());}
    if(d?.scanState==='SCANNED')
      deviceNote='DEVICE LIST REFRESHED · '+Number(d.scanDurationMs||0).toFixed(0)+' ms · output remains MUTED';
    else if(d?.scanState==='BLOCKED_ARMED')
      deviceNote='REFRESH REJECTED · MUTE MAIN/OUTPUT1 and finish audio transitions first';
    else if(d?.scanState==='SCAN_ERROR')
      deviceNote='DRIVER SCAN FAILED · current device remains unchanged';
    // CACHED is a non-disruptive snapshot; do not erase pending operator notes.
    lastSignature='';if(!overlay.hidden)refreshIO();
  });
  backend?.addEventListener?.('arMixAudioArmAck',ack=>{ if(applyMutePending){ applyMutePending=false; if(ack?.ok&&!ack.armed){lastStatus.monitor=false;sendDeviceRequest();}else deviceNote='APPLY MUTE REJECTED'; } });
  backend?.addEventListener?.('arMixAudioDeviceApplyAck',ack=>{
    if(!deviceBusy||ack?.requestId!==deviceRequest)return;
    deviceBusy=false;deviceRequest='';
    const oldGeneration=Number(deviceInventory?.active?.deviceGeneration)||0;
    if(deviceInventory&&ack?.active)deviceInventory.active=ack.active;
    // A completed device APPLY can reopen hardware before the routing
    // controller has delivered its new epoch: no stale matrix crosspoints.
    if(ack?.active?.open&&Number(ack.active.deviceGeneration)!==oldGeneration)
      deviceMatrixWaitEpoch=route().epoch;
    deviceDraft=makeDeviceDraft();
    if(ack?.ok&&ack?.active?.open)applyAfterDevice={generation:Number(ack.active.deviceGeneration),epoch:applyBaseEpoch};
    deviceNote=ack?.ok?'DEVICE ACCEPTED · OUTPUT MUTED · '+devicePinNextStep():
      (ack?.state||'REJECTED')+' · '+(ack?.error||'Check driver and retry');
    // Same real routing controller sees the new device epoch via normal status.
    lastSignature='';if(!overlay.hidden)refreshIO();
  });
  backend?.addEventListener?.('arMixRoutingLabAck',ack=>{
    if(quickSourcePending){
      const requested=quickSourcePending;
      quickSourcePending=null;
      if(ack?.ok!==true)window.__arMixRevertMixerDraft?.();
      else if(valueFor(ack?.state?.acceptedRoute?.channels?.[requested.channel])!==requested.value)
        matrixMessage='SOURCE ACK MISMATCH · inspect native accepted route';
    }
    lastSignature='';
    const message=ack?.ok?'NATIVE ACK · SENDS/ROUTING LIVE':'REJECTED · '+(ack?.error||'Native route unchanged');
    matrixMessage=message;
    el('arIOFeedback').textContent=message;
    sendFeedback=message;
    refresh();
    if(!sourceChooser.hidden){
      sourceNote.textContent=ack?.ok?'NATIVE SOURCE ACK · OUTPUT REMAINS MUTED':
        'SOURCE REJECTED · '+(ack?.error||'Route unchanged');
      renderSourceChooser();
    }
  });
  window.addEventListener('arMixSourceSwitchResult',event=>{
    const result=event.detail||{};
    if(quickSourcePending?.live&&quickSourcePending.channel===result.channel)
      quickSourcePending=null;
    const s=route();
    lastSignature='';refresh();
    if(!sourceChooser.hidden){
      renderSourceChooser();
      sourceNote.textContent=result.ok?
        'SOURCE ACK · 64-SAMPLE INPUT CROSSFADE · FX AND BUS UNCHANGED':
        'SOURCE REJECTED · '+(result.error||'Verified route unchanged');
    }
  });
  window.addEventListener('arMixRoutingDraftChanged',()=>{lastSignature='';refresh();});
  window.addEventListener('arMixFastAudioResult',event=>{
    deviceNote=event.detail?.message||'START AUDIO';
    lastSignature='';if(!overlay.hidden)refreshIO();
  });
  window.addEventListener('arMixAuxMasterGainResult',event=>{
    const ack=event.detail||{};
    sofMessage=ack.ok?'AUX '+(ack.bus+1)+' MASTER · LIVE NATIVE ACK':
      'AUX MASTER REJECTED · '+(ack.error||'Stale device');
    refresh();
  });
  window.addEventListener('arMixAuxSendGainResult',event=>{
    const a=event.detail||{};
    if(sofBus>=0&&a.bus===sofBus){
      sofMessage=a.ok?'AUX '+(sofBus+1)+' · LIVE LEVEL ACK · MAIN FADER UNCHANGED':
        'LIVE SEND REJECTED · '+(a.error||'Review device and route');
      refresh();
    }
  });
  window.addEventListener('resize',()=>positionSendEditor());
  document.addEventListener('pointerdown',e=>{
    if(!sendEditor.hidden&&!sendEditor.contains(e.target)&&!e.target.closest?.('[data-send-focus]')){
      closeSendEditor();
    }
  },true);
  setMode('FX');
})();
