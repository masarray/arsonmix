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
  // MAIN lives in the original #mixer DOM (app.js still owns its native
  // meter/FX/fader). In CONSOLE it is viewport-fixed, not a false sticky
  // child at the end of a horizontally scrolling 10-strip flex track.
  // Geometry is recomputed ONLY on resize, not on pointer/meter/scroll.
  if(mixer){
    const syncMainDock=()=>{
      const rect=mixer.getBoundingClientRect();
      // Actual MASTER includes the original 2px rail edge and padding;
      // do not assume its CSS flex-basis equals its rendered outer width.
      const width=mixer.querySelector('#masterLeft')?.getBoundingClientRect().width||
        (window.innerWidth<=1250?148:155);
      mixer.style.setProperty('--ar-main-dock-left',Math.round(rect.right-width)+'px');
      mixer.style.setProperty('--ar-main-dock-top',Math.round(rect.top)+'px');
      mixer.style.setProperty('--ar-main-dock-height',Math.round(rect.height)+'px');
    };
    if(typeof ResizeObserver==='function')new ResizeObserver(syncMainDock).observe(mixer);
    window.addEventListener('resize',syncMainDock,{passive:true});
    syncMainDock();
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
  function paintPan(slider,value){
    const n=clamp(Number(value)||0,-100,100),pos=(n+100)/2;
    slider.style.setProperty('--v6-pan-start',Math.min(50,pos)+'%');
    slider.style.setProperty('--v6-pan-end',Math.max(50,pos)+'%');
    slider.setAttribute('aria-valuetext',n===0?'Center':n<0?'L '+Math.abs(n):'R '+n);
  }
  const clean=s=>String(s==null?'—':s);
  const byId=new Map();
  // Selection is display state only, with immutable native ChannelId keys.
  const selectedIds=new Set();let selectionAnchor=null;
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
    SET_SOLO:'SET_SOLO',SET_TRIM:'SET_TRIM',SET_CUE:'SET_CUE',
    SET_LINK_GROUP:'SET_LINK_GROUP',ROUTE_SELECTED_TO_GROUP:'ROUTE_SELECTED_TO_GROUP',
    RENAME_CHANNEL:'RENAME_CHANNEL',CREATE_CHANNEL:'CREATE_CHANNEL'
  };
  let accepted=null,acceptedNative=false,modeSend='',lastStructure='',requestSeq=0;
  let draggingFx=null;
  // Numeric decimal wire identifiers are required by integration/ConsoleNativeContractV6.h.
  // Never downcast native 64-bit ChannelId, device epoch, or graph revision.
  // Wire uint64 identities are canonical decimal STRINGS, not JS numbers.
  // Reject overflow and noncanonical aliases before BigInt comparisons.
  const decimal=v=>typeof v==='string'&&/^(0|[1-9][0-9]*)$/.test(v)&&
    BigInt(v)<=18446744073709551615n?v:null;
  const ge=(a,b)=>a!==null&&b!==null&&BigInt(a)>=BigInt(b);
  // A reconnect must not replay the prior editor's request IDs. Retain a
  // single session prefix for this WebView lifetime; each new WebView gets
  // a cryptographically random prefix when secure randomness is available.
  const instance=(()=>{
    const bytes=new Uint8Array(16);
    try{
      window.crypto.getRandomValues(bytes);
      return 'gui-'+Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
    }catch{
      return 'gui-'+Date.now().toString(36)+'-'+
        Math.random().toString(36).slice(2,14);
    }
  })();
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
  const deviceButton=button('DEVICE',{'aria-expanded':'false','aria-controls':'arV6DevicePanel',
    'aria-label':'Verified native audio device inventory'});
  const pluginsButton=button('VST3 F8',{'aria-expanded':'false',
    'aria-controls':'arV6PluginPanel','aria-label':'Native Plugin Manager F8'});
  const patch=button('PATCH',{'aria-label':'Open advanced patch'});
  const narrow=button('NARROW',{'aria-pressed':'false','aria-label':'Toggle narrow channel bank'});
  const roleFilter=h('select','arV6Select arV6Filter');
  roleFilter.setAttribute('aria-label','Filter visible mixer channels');
  const selectionCount=button('0 SEL',{'aria-label':'Selected channels and native group actions',
    'aria-controls':'arV6SelectionPanel','aria-expanded':'false'});
  selectionCount.classList.add('arV6SelectionCount');
  for(const [value,label] of [['ALL','ALL'],['INPUT','INPUT'],['GROUP','BUS'],
    ['FX_RETURN','FX RTN'],['AUX','AUX'],['MONITOR','MON'],['SOLO','SOLO']]){
    const option=h('option','',label);option.value=value;roleFilter.append(option);
  }
  const back=button('V6 CONSOLE',{'aria-label':'Return to operator console'});
  back.id='arV6Return';back.hidden=true;
  tools.append(bankPrev,bankLabel,bankNext,roleFilter,selectionCount,
    sendMode,roleSelect,add,deviceButton,pluginsButton,narrow,patch);
  top.append(identification,tools);
  const message=h('div','arV6Message');message.setAttribute('role','status');
  const devicePanel=h('section','arV6DevicePanel');
  devicePanel.id='arV6DevicePanel';devicePanel.hidden=true;
  devicePanel.setAttribute('aria-label','Read-only native audio device inventory');
  const deviceHead=h('div','arV6DevicePanelHead');
  const deviceClose=button('×',{'aria-label':'Close native device inventory'});
  deviceHead.append(h('span','','VERIFIED DEVICE · NATIVE READBACK'),deviceClose);
  const deviceRows=h('div','arV6DeviceRows');
  devicePanel.append(deviceHead,deviceRows);
  const pluginPanel=h('section','arV6PluginPanel');
  pluginPanel.id='arV6PluginPanel';pluginPanel.hidden=true;
  pluginPanel.setAttribute('aria-label','Native scanned VST3 catalog');
  const pluginHead=h('div','arV6DevicePanelHead');
  const pluginClose=button('×',{'aria-label':'Close native Plugin Manager'});
  pluginHead.append(h('span','','NATIVE VST3 · PLUGIN MANAGER'),pluginClose);
  const pluginTarget=h('div','arV6PluginTarget');
  const pluginSearch=h('input','arV6PluginSearch');pluginSearch.type='search';
  pluginSearch.placeholder='Search native scanned plugins...';
  pluginSearch.setAttribute('aria-label','Search native scanned VST3 catalog');
  const pluginResults=h('div','arV6PluginResults');
  pluginPanel.append(pluginHead,pluginTarget,pluginSearch,pluginResults);
  // A small operator drawer differentiates relative fader links from actual
  // serial Group-Bus summing. Both are SINGLE native graph transactions.
  const selectionPanel=h('section','arV6SelectionPanel');
  selectionPanel.id='arV6SelectionPanel';selectionPanel.hidden=true;
  selectionPanel.setAttribute('aria-label','Native multi-channel operator actions');
  const selectionHead=h('div','arV6DevicePanelHead');
  const selectionClose=button('×',{'aria-label':'Close selected channel actions'});
  selectionHead.append(h('span','','SELECTED · LIVE MIXING'),selectionClose);
  const selectionSummary=h('div','arV6SelectionSummary');
  const selectionActions=h('div','arV6SelectionActions');
  const linkFaders=button('LINK FADERS',{'data-op':'SET_LINK_GROUP',
    'aria-label':'Link selected faders using native relative control group'});
  const unlinkFaders=button('UNLINK',{'data-op':'SET_LINK_GROUP',
    'aria-label':'Unlink selected faders using native relative control group'});
  const routeBus=h('select','arV6Select arV6GroupTarget');
  routeBus.setAttribute('aria-label','Select actual native summing Group Bus');
  const routeSelected=button('ROUTE TO GROUP BUS',{'data-op':'ROUTE_SELECTED_TO_GROUP',
    'aria-label':'Route selected channels to native summing Group Bus'});
  const linkRow=h('div','arV6LinkActions');linkRow.append(linkFaders,unlinkFaders);
  const busRow=h('div','arV6BusActions');busRow.append(routeBus,routeSelected);
  selectionActions.append(linkRow,busRow);
  const selectionNote=h('p','arV6DeviceNote',
    'LINK FADERS moves controls relatively. GROUP BUS sums audio serially. These are different native operations; UI never mixes or reroutes PCM.');
  selectionPanel.append(selectionHead,selectionSummary,selectionActions,selectionNote);
  const body=h('div','arV6Body');
  const sectionRail=h('aside','arV6SectionRail');
  sectionRail.setAttribute('aria-label','Synchronized mixer section collapse');
  sectionRail.append(h('span','arV6RailCell arV6RailHeader','▦'));
  for(const [section,text,label] of [
    ['io','I/O','Collapse or expand native input and output rows'],
    ['fx','FX','Collapse or expand VST3 insert section'],
    ['send','SND','Collapse or expand send section']]){
    const control=button(text,{'data-section':section,'aria-expanded':'true','aria-label':label});
    control.classList.add('arV6RailCell','arV6RailToggle');
    sectionRail.append(control);
  }
  sectionRail.append(h('span','arV6RailCell arV6RailPan','PAN'));
  sectionRail.append(h('span','arV6RailCell arV6RailFader','dB'));
  sectionRail.append(h('span','arV6RailCell arV6RailStatus','•'));
  const lanes=h('div','arV6Lanes');lanes.id='arV6Lanes';lanes.setAttribute('aria-label','Channel banks');
  const main=h('div','arV6Main');main.id='arV6Main';main.setAttribute('aria-label','Pinned main output');
  body.append(sectionRail,lanes,main);
  root.append(top,message,devicePanel,pluginPanel,selectionPanel,body);
  mount.append(root);
  sectionRail.addEventListener('click',event=>{
    const control=event.target.closest('[data-section]');
    if(!control)return;
    const key='collapse'+control.dataset.section;
    const collapsed=root.dataset[key]!=='true';
    root.dataset[key]=String(collapsed);
    control.setAttribute('aria-expanded',String(!collapsed));
    control.classList.toggle('isCollapsed',collapsed);
    // Shared CSS variables resize every strip and rail atomically.
    // No channel topology change and no fader/meter DOM remount.
    refreshBanks();
  });
  roleFilter.addEventListener('change',()=>applyPresentation());
  selectionCount.addEventListener('click',()=>{
    selectionPanel.hidden=!selectionPanel.hidden;
    selectionCount.setAttribute('aria-expanded',String(!selectionPanel.hidden));
    if(!selectionPanel.hidden)refreshSelectionActions();
  });
  selectionClose.addEventListener('click',()=>{
    selectionPanel.hidden=true;selectionCount.setAttribute('aria-expanded','false');
  });
  function selectedNativeIds(){
    return [...selectedIds].filter(id=>byId.has(id)&&byId.get(id).role!=='MAIN');
  }
  function refreshSelectionActions(){
    selectionCount.textContent=selectedIds.size+' SEL';
    if(selectionPanel.hidden)return;
    const ids=selectedNativeIds(),channels=ids.map(id=>byId.get(id));
    const names=channels.map(c=>c.name).join(' · ');
    selectionSummary.textContent=names||'Select channel headers using Ctrl / Shift';
    const ready=ids.length>=2&&ids.length<=64&&!pendingTargets.has('@selection')&&
      ids.every(id=>can('SET_LINK_GROUP',id));
    linkFaders.disabled=!ready||!can('SET_LINK_GROUP',null);
    const current=channels[0]?.linkGroupId;
    unlinkFaders.disabled=!ready||typeof current!=='string'||!current||
      !channels.every(c=>c.linkGroupId===current);
    const buses=(accepted?.graph?.channels||[]).filter(ch=>
      ch.role==='GROUP'&&!selectedIds.has(ch.id)&&
      (accepted.graph.destinations||[]).some(dst=>dst.id===ch.id&&
        dst.acceptsPrimary!==false));
    const signature=buses.map(ch=>ch.id+'|'+ch.name).join(',');
    if(routeBus.dataset.signature!==signature){
      const value=routeBus.value;
      routeBus.replaceChildren();
      const prompt=h('option','','SELECT NATIVE BUS');prompt.value='';routeBus.append(prompt);
      for(const bus of buses){
        const option=h('option','',bus.name);option.value=bus.id;routeBus.append(option);
      }
      routeBus.value=buses.some(ch=>ch.id===value)?value:'';
      routeBus.dataset.signature=signature;
    }
    const canRoute=ids.length>0&&ids.length<=64&&
      (accepted?.graph?.capabilities?.groups===true)&&
      channels.every(c=>['INPUT','AUX','FX_RETURN'].includes(c.role)&&
        can('ROUTE_SELECTED_TO_GROUP',c.id))&&
      can('ROUTE_SELECTED_TO_GROUP',null);
    routeBus.disabled=!canRoute||!buses.length;
    routeSelected.disabled=!canRoute||!buses.some(ch=>ch.id===routeBus.value);
  }
  routeBus.addEventListener('change',refreshSelectionActions);
  const sendSelectionAction=linked=>{
    const ids=selectedNativeIds();
    if(ids.length<2||ids.length>64||linkFaders.disabled&&linked||
       unlinkFaders.disabled&&!linked)return;
    sendIntent('SET_LINK_GROUP',null,{channelIds:ids,linked},ids);
  };
  linkFaders.addEventListener('click',()=>sendSelectionAction(true));
  unlinkFaders.addEventListener('click',()=>sendSelectionAction(false));
  routeSelected.addEventListener('click',()=>{
    const ids=selectedNativeIds(),destinationId=routeBus.value;
    if(routeSelected.disabled||!ids.length||ids.length>64||
       !byId.has(destinationId)||byId.get(destinationId).role!=='GROUP')return;
    sendIntent('ROUTE_SELECTED_TO_GROUP',null,
      {channelIds:ids,destinationId},ids);
  });
  deviceButton.addEventListener('click',()=>{
    devicePanel.hidden=!devicePanel.hidden;
    deviceButton.setAttribute('aria-expanded',String(!devicePanel.hidden));
    if(!devicePanel.hidden)refreshDevicePanel();
  });
  deviceClose.addEventListener('click',()=>{
    devicePanel.hidden=true;deviceButton.setAttribute('aria-expanded','false');
  });
  function closePluginManager(){
    pluginPanel.hidden=true;pluginsButton.setAttribute('aria-expanded','false');
  }
  function openPluginManager(id){
    if(id&&byId.has(id)&&byId.get(id).role!=='MAIN'){
      selectedIds.clear();selectedIds.add(id);selectionAnchor=id;
      updateSelection();
    }
    devicePanel.hidden=true;deviceButton.setAttribute('aria-expanded','false');
    pluginPanel.hidden=false;pluginsButton.setAttribute('aria-expanded','true');
    refreshPluginCatalog();pluginSearch.focus();
  }
  pluginsButton.addEventListener('click',()=>{
    if(pluginPanel.hidden)openPluginManager();
    else closePluginManager();
  });
  pluginClose.addEventListener('click',closePluginManager);
  pluginSearch.addEventListener('input',refreshPluginCatalog);
  pluginResults.addEventListener('click',event=>{
    const node=event.target.closest('button[data-plugin-id]');
    if(!node||node.disabled||selectedIds.size!==1)return;
    const channelId=[...selectedIds][0];
    const catalog=accepted?.catalog?.plugins||[];
    const entry=catalog.find(p=>p?.id===node.dataset.pluginId);
    if(entry&&can('ADD_INSERT',channelId)&&
       sendIntent('ADD_INSERT',channelId,{pluginId:entry.id}))closePluginManager();
  });
  document.addEventListener('keydown',event=>{
    if(!document.documentElement.classList.contains('ar-v6-active'))return;
    if(event.key==='Escape'&&!selectionPanel.hidden){
      event.preventDefault();selectionPanel.hidden=true;
      selectionCount.setAttribute('aria-expanded','false');
    }else if(event.key==='Escape'&&!pluginPanel.hidden){
      event.preventDefault();closePluginManager();
    }else if(event.key==='F8'&&!event.repeat){
      event.preventDefault();openPluginManager();
    }
  });
  narrow.addEventListener('click',()=>{
    const compact=root.dataset.narrow!=='true';
    root.dataset.narrow=String(compact);
    narrow.setAttribute('aria-pressed',String(compact));
    narrow.textContent=compact?'WIDE':'NARROW';
    refreshBanks(); // Resize only; never create a native channel or remount faders.
  });
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
  function refreshDevicePanel(){
    if(devicePanel.hidden)return;
    deviceRows.replaceChildren();
    const d=acceptedNative?accepted?.device:null;
    if(!d){deviceRows.append(h('p','','No verified native device published.'));return;}
    const addRow=(label,value)=>{
      const row=h('div','arV6DeviceRow');
      row.append(h('span','',label),h('strong','',value));deviceRows.append(row);
    };
    addRow('BACKEND',d.backend||'—');
    addRow('INPUT DEVICE',d.captureDevice||'NONE');
    addRow('OUTPUT DEVICE',d.playbackDevice||'NONE');
    addRow('CLOCK',Number.isFinite(d.sampleRate)?d.sampleRate+' Hz / '+
      d.bufferFrames+' frames':'UNVERIFIED');
    addRow('OUTPUT ARM',d.outputArmed?'ARMED · acoustics unverified':'DISARMED');
    for(const [label,ports,mask] of [
      ['INPUT PINS',d.inputEndpoints,d.enabledInputs],
      ['OUTPUT PINS',d.outputEndpoints,d.enabledOutputs]]){
      const verified=ports.filter(p=>p.active===true&&mask.includes(p.port));
      addRow(label,verified.length?verified.map(p=>p.label+' (port '+p.port+
        ', callback '+p.callback+')').join(', '):'NO VERIFIED ACTIVE PIN');
    }
    deviceRows.append(h('p','arV6DeviceNote',
      'Read-only: device changes require a genuine native SET_DEVICE transaction; no guessed hardware pin or automatic audio reroute.'));
  }
  function refreshPluginCatalog(){
    if(pluginPanel.hidden)return;
    pluginResults.replaceChildren();
    const ids=[...selectedIds].filter(id=>byId.has(id));
    const id=ids.length===1?ids[0]:null;
    pluginTarget.textContent=id?
      'TARGET · '+byId.get(id).name+' · '+(can('ADD_INSERT',id)?
        'NATIVE LOAD AVAILABLE':'NATIVE LOAD NOT AVAILABLE'):
      'Select exactly ONE channel header to load a VST3';
    const catalog=acceptedNative&&Array.isArray(accepted?.catalog?.plugins)?
      accepted.catalog.plugins.filter(p=>p&&typeof p.id==='string'&&p.id&&
        typeof p.name==='string'&&p.name):[];
    const search=pluginSearch.value.trim().toLocaleLowerCase();
    const results=catalog.filter(p=>(p.name+' '+(p.vendor||'')+' '+(p.category||''))
      .toLocaleLowerCase().includes(search));
    if(!results.length)pluginResults.append(h('p','arV6DeviceNote',
      catalog.length?'No scanned plugins match the search':
        'Native plugin catalog not published. No simulated plugins available.'));
    for(const p of results.slice(0,100)){
      const node=button(p.name+' · '+(typeof p.vendor==='string'?p.vendor:'VST3'),
        {'data-plugin-id':p.id});
      node.disabled=!id||!can('ADD_INSERT',id);
      node.classList.add('arV6PluginResult');
      pluginResults.append(node);
    }
  }
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
    if(!id)return !pendingTargets.has('@selection');
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
  function sendIntent(op,id,payload,affectedIds=[]){
    if(!can(op,id)||affectedIds.length>64||
       affectedIds.some(channelId=>!can(op,channelId))){
      showFeedback('UNAVAILABLE · Native capability, selection lock or channel authorization missing');
      return false;
    }
    const requestId=instance+':'+String(++requestSeq);
    const command={requestId,deviceEpoch:accepted.device.epoch,
      baseGraphRevision:accepted.graph.revision,
      baseRosterRevision:accepted.graph.rosterRevision,
      operation:op,targetChannelId:id||null,payload:payload||{}};
    const targets=id?[id]:affectedIds.length?['@selection',...affectedIds]:
      op==='CREATE_CHANNEL'?['@roster']:[];
    pending.set(requestId,{op,id,targets,baseRevision:command.baseGraphRevision,
      baseRosterRevision:command.baseRosterRevision,epoch:command.deviceEpoch,snapshotSerial});
    for(const target of targets)pendingTargets.add(target);
    showFeedback('PENDING · '+op+' · awaiting native ACK');
    refreshDisabled();
    backend.emitEvent('arMixConsoleIntent',command);
    return true;
  }
  function receiveAck(data){
    const a=data?.ack||data;
    if(!a||typeof a.requestId!=='string')return;
    const task=pending.get(a.requestId);if(!task)return;
    const epoch=decimal(a.deviceEpoch),rev=decimal(a.graphRevision),roster=decimal(a.rosterRevision);
    if(!['ACCEPTED','REJECTED','REQUIRES_DEVICE_CHANGE'].includes(a.status)||
       epoch===null||rev===null||roster===null||epoch!==task.epoch||
       typeof a.errorCode!=='string'){
      showFeedback('INVALID NATIVE ACK · accepted route unchanged');return;
    }
    pending.delete(a.requestId);
    const unlock=()=>{for(const target of task.targets||[])pendingTargets.delete(target)};
    if(a.status==='ACCEPTED'){
      if(BigInt(rev)<BigInt(task.baseRevision)||
         BigInt(roster)<BigInt(task.baseRosterRevision)){
        unlock();
        showFeedback('INVALID ACK REVISION · accepted route unchanged');
      }else{
        const wait={...task,requiredRevision:rev,requiredRoster:roster};
        if(ackMatches(wait))unlock();
        else awaitingSnapshot.set(a.requestId,wait);
        showFeedback('ACCEPTED · '+task.op+' · awaiting exact native graph + roster');
      }
      requestSnapshot();
    }else{
      unlock();
      showFeedback((a.status==='REQUIRES_DEVICE_CHANGE'?'REQUIRES DEVICE · ':'REJECTED · ')+
        clean(a.errorCode||'INVALID REJECTION')+' · previous accepted route preserved');
      requestSnapshot();refreshAcceptedControls();
    }
    refreshDisabled();
  }
  function ackMatches(wait){
    return acceptedNative&&snapshotSerial>wait.snapshotSerial&&
      accepted?.device?.epoch===wait.epoch&&
      accepted?.graph?.revision===wait.requiredRevision&&
      accepted?.graph?.rosterRevision===wait.requiredRoster;
  }
  function validSnapshot(s){
    // Mirror the required v8 producer wire: never infer physical masks,
    // capabilities, PCM or accepted ChannelIds from browser/M4 fallback.
    if(!s||s.schemaVersion!==6||typeof s.productionCallbackLive!=='boolean'||
       !s.device||!s.graph||!s.telemetry)return false;
    const d=s.device,g=s.graph,c=g.capabilities,t=s.telemetry;
    if(decimal(d.epoch)===null||decimal(g.revision)===null||
       decimal(g.rosterRevision)===null||typeof d.backend!=='string'||
       typeof d.captureDevice!=='string'||typeof d.playbackDevice!=='string'||
       typeof d.outputArmed!=='boolean'||!Array.isArray(d.inputEndpoints)||
       !Array.isArray(d.outputEndpoints)||!Array.isArray(d.enabledInputs)||
       !Array.isArray(d.enabledOutputs)||!Array.isArray(g.channels)||
       !Array.isArray(g.destinations)||!c||!Array.isArray(c.supportedOps))
      return false;
    const pins=a=>a.every(v=>Number.isSafeInteger(v)&&v>=0&&v<=255)&&
      new Set(a).size===a.length;
    if(!pins(d.enabledInputs)||!pins(d.enabledOutputs))return false;
    for(const [type,endpoints,mask] of [['in',d.inputEndpoints,d.enabledInputs],
      ['out',d.outputEndpoints,d.enabledOutputs]]){
      const ids=new Set(),cb=new Set();
      for(const e of endpoints){
        if(!e||!mask.includes(e.port)||e.id!==type+':'+e.port||
           !Number.isSafeInteger(e.callback)||e.callback<0||
           typeof e.label!=='string'||!e.label||typeof e.active!=='boolean'||
           ids.has(e.id)||cb.has(e.callback))return false;
        ids.add(e.id);cb.add(e.callback);
      }
    }
    for(const key of ['callbackOwnsChannelIds','fullVendorFxMigration','showAndOutputParity',
      'physicalCaptureMapped','dynamicChannels','groups','fxReturns','monitor','sendsOnFaders'])
      if(typeof c[key]!=='boolean')return false;
    if(c.supportedOps.some(op=>typeof op!=='string'||!/^[A-Z_]+$/.test(op))||
       new Set(c.supportedOps).size!==c.supportedOps.length)return false;
    if(s.productionCallbackLive){
      if(d.epoch==='0'||!c.callbackOwnsChannelIds||!c.fullVendorFxMigration||
         !c.showAndOutputParity||!c.physicalCaptureMapped)return false;
    }else if(g.channels.length||c.supportedOps.length)return false;
    const ids=new Set(),roles=['INPUT','GROUP','FX_RETURN','MONITOR','MAIN','AUX'];
    for(const ch of g.channels){
      if(!ch||decimal(ch.id)===null||ch.id==='0'||ids.has(ch.id)||
         !roles.includes(ch.role)||!['MONO','STEREO'].includes(ch.processingFormat)||
         typeof ch.name!=='string'||!Array.isArray(ch.inserts)||!Array.isArray(ch.sends))return false;
      ids.add(ch.id);
    }
    for(const ch of g.channels){
      if(ch.primaryOutId!==undefined&&ch.primaryOutId!==null){
        const to=ch.primaryOutId;
        const physical=d.outputEndpoints.some(p=>p.id===to&&p.active===true&&
          d.enabledOutputs.includes(p.port));
        if(to===ch.id||!ids.has(to)&&!(physical&&['MAIN','MONITOR'].includes(ch.role)))
          return false;
      }
      if(ch.source){
        const x=ch.source;
        if(ch.role!=='INPUT'||typeof x.portId!=='string'||
           !Array.isArray(x.physicalPins)||!pins(x.physicalPins)||
           ![1,2].includes(x.physicalPins.length)||
           !x.physicalPins.every(p=>d.enabledInputs.includes(p)&&
             d.inputEndpoints.some(e=>e.port===p&&e.active))||
           !d.inputEndpoints.some(p=>p.id===x.portId&&p.active&&
             x.physicalPins.includes(p.port)))return false;
      }
      for(const send of ch.sends)
        if(!ids.has(send.targetId)||send.targetId===ch.id||
           !['PRE','POST'].includes(send.tap))return false;
      if(ch.role==='GROUP'&&!c.groups||ch.role==='FX_RETURN'&&!c.fxReturns||
         ch.role==='MONITOR'&&!c.monitor)return false;
    }
    if(s.productionCallbackLive&&g.channels.filter(ch=>ch.role==='MAIN').length!==1)return false;
    // Reject source/primary bus cycles before allowing the operator to act.
    const graphIds=new Map(g.channels.map(ch=>[ch.id,ch]));
    for(const ch of g.channels){
      let current=ch,visited=new Set();
      while(current?.primaryOutId){
        if(visited.has(current.id))return false;
        visited.add(current.id);
        current=graphIds.get(current.primaryOutId);
      }
    }
    if(g.destinations.some(dst=>!dst||!ids.has(dst.id)))return false;
    if(typeof t.rawCapturePresent!=='boolean'||typeof t.mainPcmPresent!=='boolean'||
       typeof t.monitorPcmPresent!=='boolean'||typeof t.rawCapturePeak!=='number'||
       !Number.isFinite(t.rawCapturePeak)||t.rawCapturePeak<0)return false;
    return true;
  }
  function normalizeSnapshot(payload){
    const raw=payload?.snapshot||payload;
    // Flat M4 readback is not the universal v8 device inventory.
    return raw&&typeof raw==='object'&&raw.schemaVersion===6?raw:null;
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
    if(fromNative&&acceptedNative&&accepted&&s.device.epoch===accepted.device.epoch){
      if(BigInt(s.graph.revision)<BigInt(accepted.graph.revision)||
         BigInt(s.graph.rosterRevision)<BigInt(accepted.graph.rosterRevision))
        return false;
      // Device backend/clock/pins may not change silently under the same epoch.
      // A hardware rebind needs an admitted new native device epoch.
      const hardware=d=>JSON.stringify([d.backend,d.captureDevice,d.playbackDevice,
        d.sampleRate,d.bufferFrames,d.inputEndpoints,d.outputEndpoints,
        d.enabledInputs,d.enabledOutputs]);
      if(hardware(s.device)!==hardware(accepted.device)){
        showFeedback('REJECTED DEVICE READBACK · epoch unchanged; retained verified pins');
        return false;
      }
    }
    const priorEpoch=acceptedNative?accepted?.device?.epoch:null;
    const structure=s.device.epoch+':'+s.graph.rosterRevision+':'+
      s.graph.channels.map(c=>[c.id,c.role,c.processingFormat,c.primaryOutId,
        (c.inserts||[]).map(f=>f.id).join(','),(c.sends||[]).map(x=>x.id).join(',')].join(':')).join('|')+
      ':'+s.graph.capabilities.supportedOps.join('|');
    accepted=s;acceptedNative=!!fromNative;
    snapshotSerial++;
    byId.clear();
    for(const c of s.graph.channels)byId.set(c.id,c);
    if(fromNative&&priorEpoch!==null&&priorEpoch!==s.device.epoch){
      // Device replacement invalidates all old intents and controls; never
      // replay them into an unrelated callback/port inventory.
      pending.clear();pendingTargets.clear();awaitingSnapshot.clear();
      selectedIds.clear();selectionAnchor=null;
      showFeedback('DEVICE CHANGED · native source and destination inventory refreshed');
    }
    for(const [requestId,task] of awaitingSnapshot){
      if(ackMatches(task)){
        awaitingSnapshot.delete(requestId);
        for(const target of task.targets||[])pendingTargets.delete(target);
      }
    }
    if(structure!==lastStructure||root.dataset.demo!==String(!fromNative)){
      lastStructure=structure;render();
    }else{
      refreshAcceptedControls();refreshDisabled();updateHeader();
    }
    root.dataset.demo=String(!fromNative);
    refreshDevicePanel();refreshPluginCatalog();
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
  function legalOutputs(ch){
    const destinations=(accepted.graph.destinations||[]).filter(dst=>{
      const target=byId.get(dst.id);
      return target&&target.id!==ch.id&&['MAIN','GROUP'].includes(target.role)&&
        dst.acceptsPrimary!==false;
    });
    // SOURCE channels may only route through native buses, not skip to a
    // physical output. The MAIN/MONITOR roles can select verified render pins.
    if(ch.role==='MAIN'||ch.role==='MONITOR')
      return (accepted.device.outputEndpoints||[]).filter(p=>
        p.active===true&&accepted.device.enabledOutputs.includes(p.port));
    return destinations;
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
    const addFx=button(filePicker?'+ OPEN VST3 FILE':'+ VST3',
      filePicker?{'data-op':'PICK_INSERT_FILE','data-id':ch.id}:
        {'data-ui':'OPEN_PLUGIN_MANAGER','data-id':ch.id});
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
      const gain=h('label','arV6SendGain');
      const db=Number(send.levelDb),verified=Number.isFinite(db);
      const slider=h('input','arV6SendLevel');
      slider.type='range';slider.min='-60';slider.max='12';slider.step='.5';
      slider.value=String(verified?clamp(db,-60,12):0);
      slider.dataset.op='SET_SEND_LEVEL';slider.dataset.id=ch.id;
      slider.dataset.send=clean(send.id);
      slider.dataset.hasAcceptedLevel=String(verified);
      slider.setAttribute('aria-label',(ch.name||ch.id)+' '+(send.name||send.targetId)+' send gain dB');
      slider.disabled=!can('SET_SEND_LEVEL',ch.id)||!verified;
      gain.append(slider,h('output','arV6SendDb',verified?db.toFixed(1)+' dB':'—'));
      line.append(gain);section.append(line);
    }
    const select=selectField('+ TO','ADD_SEND',ch.id,targets,'','NO NATIVE SEND BUS');
    section.append(select);return section;
  }
  const roleLabel=ch=>clean(ch.role).toUpperCase()+
    (typeof ch.linkGroupId==='string'&&ch.linkGroupId?
      ' · LINK '+ch.linkGroupId.slice(0,6):'');
  function strip(ch,pinned=false){
    const role=clean(ch.role).toUpperCase();
    const elStrip=h('article','arV6Strip'+(pinned?' arV6Master':''));
    elStrip.dataset.channelId=ch.id;
    const head=h('div','arV6StripHead');
    const roleName=h('span','arV6Role',roleLabel(ch));
    const name=h('strong','arV6Name',ch.name||ch.id);name.title=ch.id;
    head.append(roleName,name);elStrip.append(head);
    elStrip.setAttribute('aria-selected',String(selectedIds.has(ch.id)));
    const sourceSection=h('div','arV6Upper');
    const routeSection=h('div','arV6Route');
    const sources=(accepted.device.inputEndpoints||[]).filter(p=>p.active===true && accepted.device.enabledInputs.includes(p.port));
    if(role==='INPUT')routeSection.append(selectField('IN','SET_SOURCE',ch.id,sources,
      ch.source?.portId||ch.source?.id||'','NO VERIFIED INPUT'));
    else routeSection.append(h('div','arV6Field arV6FixedField','IN   ·   '+role));
    const outs=legalOutputs(ch);
    routeSection.append(selectField('OUT','SET_PRIMARY_OUT',ch.id,outs,
      ch.primaryOutId||ch.primaryOut?.id||ch.primaryOut||'','NO VERIFIED OUT'));
    const formats=['MONO','STEREO'].map(s=>({id:s,name:s}));
    routeSection.append(selectField('M/ST','SET_PROCESSING_FORMAT',ch.id,formats,
      ch.processingFormat||'','NATIVE FORMAT'));
    sourceSection.append(routeSection,fxSection(ch),sendSection(ch));
    elStrip.append(sourceSection);
    const controls=h('div','arV6Controls');
    const pan=h('label','arV6Pan');pan.append(h('span','','PAN'));
    const panRange=h('input','arV6PanRange');panRange.type='range';
    panRange.min='-100';panRange.max='100';panRange.step='1';
    panRange.value=String(Number(ch.controls?.pan)||0);
    paintPan(panRange,panRange.value);
    panRange.dataset.op='SET_PAN';panRange.dataset.id=ch.id;
    pan.append(panRange);controls.append(pan);
    controls.append(button('M',{'data-op':'SET_MUTE','data-id':ch.id,
      'aria-pressed':String(!!ch.controls?.mute)}));
    controls.append(button('S',{'data-op':'SET_SOLO','data-id':ch.id,
      'aria-pressed':String(!!ch.controls?.solo)}));
    const cueKnown=typeof ch.controls?.cue==='boolean'&&role!=='MAIN';
    const cue=button('C',{'data-op':'SET_CUE','data-id':ch.id,
      'aria-pressed':String(ch.controls?.cue===true),
      'aria-label':(ch.name||ch.id)+' native monitor Cue/PFL'});
    cue.dataset.acceptedKnown=String(cueKnown);cue.disabled=!cueKnown||!can('SET_CUE',ch.id);
    controls.append(cue);
    const trimKnown=typeof ch.controls?.trimDb==='number'&&Number.isFinite(ch.controls.trimDb);
    const trimRow=h('label','arV6Trim');trimRow.append(h('span','','TRIM'));
    const trim=h('input','arV6TrimLevel');trim.type='range';
    trim.min='-24';trim.max='24';trim.step='.5';
    trim.value=String(trimKnown?clamp(ch.controls.trimDb,-24,24):0);
    trim.dataset.op='SET_TRIM';trim.dataset.id=ch.id;
    trim.dataset.acceptedKnown=String(trimKnown);
    trim.setAttribute('aria-label',(ch.name||ch.id)+' digital input trim dB');
    trim.disabled=!trimKnown||!can('SET_TRIM',ch.id);
    trimRow.append(trim,h('output','arV6TrimDb',
      trimKnown?ch.controls.trimDb.toFixed(1):'—'));
    controls.append(trimRow);elStrip.append(controls);
    const lower=h('div','arV6Lower');
    const meter=h('div','arV6Meter');meter.dataset.meterId=ch.id;
    meter.dataset.format=ch.processingFormat||'MONO';
    meter.setAttribute('aria-label','Native L/R PCM meter: '+(ch.name||ch.id));
    for(const side of ['L','R']){
      const lane=h('div','arV6MeterLane');
      lane.dataset.side=side;
      for(let n=0;n<22;n++)lane.append(h('i','arV6Led'));
      meter.append(lane);
    }
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
    if(t.rawCapturePresent!==true)return 'NO RAW CAPTURE PCM';
    if(t.mainPcmPresent!==true)return 'RAW PCM · MAIN PCM UNVERIFIED';
    return 'MAIN PCM VERIFIED · OUTPUT ARMED';
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
  function updateSelection(){
    for(const strip of root.querySelectorAll('[data-channel-id]')){
      const selected=selectedIds.has(strip.dataset.channelId);
      strip.classList.toggle('isSelected',selected);
      strip.setAttribute('aria-selected',String(selected));
    }
    refreshSelectionActions();
    refreshPluginCatalog();
  }
  function selectChannel(id,event){
    const channel=byId.get(id);
    if(!channel||channel.role==='MAIN')return;
    const visible=[...lanes.querySelectorAll('.arV6Strip:not([hidden])')]
      .map(strip=>strip.dataset.channelId);
    if(!visible.includes(id))return;
    if(event.shiftKey&&selectionAnchor&&visible.includes(selectionAnchor)){
      if(!event.ctrlKey&&!event.metaKey)selectedIds.clear();
      const a=visible.indexOf(selectionAnchor),b=visible.indexOf(id);
      for(const x of visible.slice(Math.min(a,b),Math.max(a,b)+1))selectedIds.add(x);
    }else if(event.ctrlKey||event.metaKey){
      if(selectedIds.has(id))selectedIds.delete(id);else selectedIds.add(id);
      selectionAnchor=id;
    }else{
      selectedIds.clear();selectedIds.add(id);selectionAnchor=id;
    }
    updateSelection(); // No DSP or ConsoleIntent involved.
  }
  function applyPresentation(){
    const value=roleFilter.value;
    for(const strip of lanes.querySelectorAll('.arV6Strip')){
      const ch=byId.get(strip.dataset.channelId);
      const match=ch&&(value==='ALL'||value==='SOLO'&&ch.controls?.solo===true||
        ch.role===value);
      strip.hidden=!match;
    }
    // Presentational filters cannot alter native roster nor hide pinned MAIN.
    if(lanes.querySelector('.arV6Strip:not([hidden])')===null)lanes.scrollLeft=0;
    updateSelection();refreshBanks();
  }
  function render(){
    if(!accepted)return;
    updateHeader();
    lanes.replaceChildren();main.replaceChildren();
    const channels=accepted.graph.channels;
    for(const id of selectedIds)if(!byId.has(id))selectedIds.delete(id);
    if(selectionAnchor&&!byId.has(selectionAnchor))selectionAnchor=null;
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
    refreshDisabled();syncFaderMode();refreshMeters();applyPresentation();
  }
  function refreshDisabled(){
    if(!accepted)return;
    for(const node of root.querySelectorAll('[data-op]')){
      const op=node.dataset.op,id=node.dataset.id||null;
      node.disabled=!can(op,id)||
        (op==='SET_SEND_LEVEL'&&node.dataset.hasAcceptedLevel!=='true')||
        ((op==='SET_TRIM'||op==='SET_CUE')&&node.dataset.acceptedKnown!=='true');
    }
    add.disabled=!can('CREATE_CHANNEL');
    refreshSelectionActions();
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
      const nameNode=strip.querySelector('.arV6Name');
      if(nameNode&&nameNode.textContent!==ch.name)nameNode.textContent=ch.name;
      const roleNode=strip.querySelector('.arV6Role');
      if(roleNode&&roleNode.textContent!==roleLabel(ch))
        roleNode.textContent=roleLabel(ch);
      const choose=(op,value)=>{
        const field=strip.querySelector('select[data-op="'+op+'"]');
        if(!field)return;
        const actual=typeof value==='string'?value:'';
        if([...field.options].some(o=>o.value===actual))field.value=actual;
      };
      choose('SET_SOURCE',ch.source?.portId||ch.source?.id||'');
      choose('SET_PROCESSING_FORMAT',ch.processingFormat||'');
      choose('SET_PRIMARY_OUT',ch.primaryOutId||ch.primaryOut?.id||(typeof ch.primaryOut==='string'?ch.primaryOut:''));
      choose('ADD_SEND','');
      const pan=strip.querySelector('[data-op="SET_PAN"]');
      if(pan){
        pan.value=String(Number(ch.controls?.pan)||0);
        paintPan(pan,pan.value);
      }
      const meter=strip.querySelector('[data-meter-id]');
      if(meter)meter.dataset.format=ch.processingFormat||'MONO';
      for(const op of ['SET_MUTE','SET_SOLO','SET_CUE']){
        const n=strip.querySelector('button[data-op="'+op+'"]');
        if(n)n.setAttribute('aria-pressed',String(!!ch.controls?.[
          op==='SET_MUTE'?'mute':op==='SET_SOLO'?'solo':'cue']));
        if(n&&op==='SET_CUE')n.dataset.acceptedKnown=String(
          typeof ch.controls?.cue==='boolean'&&ch.role!=='MAIN');
      }
      const trim=strip.querySelector('[data-op="SET_TRIM"]');
      if(trim){
        const known=typeof ch.controls?.trimDb==='number'&&Number.isFinite(ch.controls.trimDb);
        trim.dataset.acceptedKnown=String(known);
        trim.value=String(known?clamp(ch.controls.trimDb,-24,24):0);
        const readout=strip.querySelector('.arV6TrimDb');
        if(readout)readout.textContent=known?ch.controls.trimDb.toFixed(1):'—';
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
        const gain=node.querySelector('.arV6SendLevel');
        if(gain){
          const db=Number(send?.levelDb),verified=!!send&&Number.isFinite(db);
          gain.dataset.hasAcceptedLevel=String(verified);
          gain.value=String(verified?clamp(db,-60,12):0);
          const out=node.querySelector('.arV6SendDb');
          if(out)out.textContent=verified?db.toFixed(1)+' dB':'—';
        }
      }
    }
    syncFaderMode();refreshMeters();applyPresentation();
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
    const incoming=acceptedNative?accepted.telemetry?.channelMeterRmsPeak:null;
    const meters=Array.isArray(incoming)?
      Object.fromEntries(incoming.filter(x=>x&&typeof x.channelId==='string')
        .map(x=>[x.channelId,x])):
      incoming&&typeof incoming==='object'?incoming:{};
    const ledsForPeak=peak=>{
      const p=Number(peak)||0;
      if(p<=0)return 0;
      // Native peaks are linear PCM amplitudes, quantized on a -60..0 dBFS
      // ladder. Static green/yellow/red segments do not move or recolor.
      return clamp(Math.ceil(22*(60+Math.max(-60,20*Math.log10(p)))/60),0,22);
    };
    for(const meter of root.querySelectorAll('[data-meter-id]')){
      const ch=byId.get(meter.dataset.meterId),m=meters[meter.dataset.meterId]||{};
      const mono=ch?.processingFormat==='MONO';
      meter.dataset.format=mono?'MONO':'STEREO';
      for(const lane of meter.children){
        const side=lane.dataset.side;
        // Never clone one aggregate maximum into both L/R: a missing
        // stereo side is unknown/silent, NOT a simulated dual-mono reading.
        const value=side==='L'?(m.peakLeft??(mono?m.peak:0)):m.peakRight;
        const lit=mono&&side==='R'?0:ledsForPeak(value);
        if(lane.dataset.lit===String(lit))continue;
        lane.dataset.lit=String(lit);
        for(let i=0;i<lane.children.length;i++)lane.children[i].classList.toggle('lit',i<lit);
      }
    }
    for(const node of root.querySelectorAll('[data-status-id]')){
      const ch=byId.get(node.dataset.statusId);
      if(ch)node.textContent=channelState(ch);
    }
  }
  function refreshBanks(){
    const visible=[...lanes.querySelectorAll('.arV6Strip:not([hidden])')];
    const total=visible.length;
    const child=visible[0];
    const width=child?.getBoundingClientRect().width||172;
    const perPage=Math.max(1,Math.floor(lanes.clientWidth/width));
    const at=Math.max(0,Math.round(lanes.scrollLeft/width));
    bankLabel.textContent=total?(at+1)+'–'+Math.min(total,at+perPage)+' / '+total:'NO CHANNELS';
    bankPrev.disabled=at===0;bankNext.disabled=at+perPage>=total;
  }
  function jump(dir){
    const first=lanes.querySelector('.arV6Strip:not([hidden])');
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
    else if(op==='SET_CUE'&&typeof ch.controls?.cue==='boolean')
      sendIntent(op,id,{value:!ch.controls.cue});
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
    else if(op==='ADD_INSERT')
      showFeedback('Native plugin selection requires the verified Plugin Manager');
  });
  root.addEventListener('click',event=>{
    const launcher=event.target.closest('button[data-ui="OPEN_PLUGIN_MANAGER"]');
    if(launcher){openPluginManager(launcher.dataset.id);return;}
    const strip=event.target.closest('.arV6Strip');
    if(!strip||event.target.closest('button,input,select,[draggable]'))return;
    selectChannel(strip.dataset.channelId,event);
  });
  // Inline rename never mutates accepted name. Only native ACK + matching
  // snapshot changes the channel header; Escape/blur only cancels local input.
  function beginRename(nameNode){
    const strip=nameNode?.closest('.arV6Strip'),id=strip?.dataset.channelId;
    const ch=byId.get(id);
    if(!ch||!can('RENAME_CHANNEL',id)){
      showFeedback('RENAME UNAVAILABLE · native RENAME_CHANNEL not installed');return;
    }
    if(strip.querySelector('.arV6Rename'))return;
    const input=h('input','arV6Rename');
    input.type='text';input.maxLength=40;input.value=ch.name;
    input.setAttribute('aria-label','Rename '+ch.name+' using native ChannelId');
    const finish=commit=>{
      if(!input.isConnected)return;
      const newName=input.value.trim();
      input.replaceWith(nameNode);
      if(commit&&newName&&newName.length<=40&&newName!==ch.name&&
         !/[\\x00-\\x1f\\x7f]/.test(newName))
        sendIntent('RENAME_CHANNEL',id,{name:newName});
    };
    input.addEventListener('keydown',event=>{
      if(event.key==='Enter'){event.preventDefault();finish(true)}
      if(event.key==='Escape'){event.preventDefault();finish(false)}
    });
    input.addEventListener('blur',()=>finish(false),{once:true});
    nameNode.replaceWith(input);input.focus();input.select();
  }
  root.addEventListener('dblclick',event=>{
    const n=event.target.closest('.arV6PanRange');
    if(n&&!n.disabled){sendIntent('SET_PAN',n.dataset.id,{value:0});return;}
    const name=event.target.closest('.arV6Name');
    if(name)beginRename(name);
  });
  root.addEventListener('change',event=>{
    const n=event.target.closest('[data-op]');
    if(!n||n.disabled)return;
    const id=n.dataset.id,op=n.dataset.op,ch=byId.get(id);
    if(!ch)return;
    if(op==='SET_SOURCE'&&n.value){
      const endpoint=(accepted.device.inputEndpoints||[]).find(e=>e.id===n.value&&e.available!==false&&e.active!==false);
      if(endpoint)sendIntent(op,id,{portId:endpoint.id});
    }else if(op==='SET_PRIMARY_OUT'&&n.value){
      if(legalOutputs(ch).some(p=>p.id===n.value))
        sendIntent(op,id,{destinationId:n.value});
    }
    else if(op==='SET_PROCESSING_FORMAT')sendIntent(op,id,{format:n.value});
    else if(op==='ADD_SEND'&&n.value)sendIntent(op,id,{targetId:n.value,tap:'POST',levelDb:0});
    else if(op==='SET_PAN'){
      const value=Number(n.value),snapped=Math.abs(value)<=4?0:value;
      sendIntent(op,id,{value:snapped}); // ACK/snapshot, no optimistic PAN.
    }
    else if(op==='SET_TRIM'){
      const db=Number(n.value);
      if(typeof ch.controls?.trimDb==='number'&&Number.isFinite(db))
        sendIntent(op,id,{valueDb:clamp(db,-24,24)});
    }
    else if(op==='SET_SEND_LEVEL'){
      const send=(ch.sends||[]).find(s=>String(s.id)===n.dataset.send);
      const value=Number(n.value);
      if(send&&Number.isFinite(Number(send.levelDb))&&Number.isFinite(value))
        sendIntent('SET_SEND_LEVEL',id,{sendId:send.id,levelDb:clamp(value,-60,12)});
    }
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
    pendingCount:pending.size,selectedChannelIds:[...selectedIds],feedback
  });
})();
