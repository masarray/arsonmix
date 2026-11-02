/* ArSonMix hardware backlit keys — approved owner V5 geometry/occluded light.
   UI-only decoration: existing data-*, click handlers and native ACK remain
   the sole authority. No audio state is inferred or dispatched here. */
(function(){
  'use strict';
  const RULES=[
    ['#mixer .channel-actions [data-mute],#masterMute','mute','MUTE'],
    ['#mixer .channel-actions [data-solo]','solo','SOLO'],
    ['#mixer .utility-row [data-pre]','phase','Ø'],
    ['#mixer .utility-row [data-hpf]','processing','HPF'],
    ['#mixer [data-channel-format-toggle]','neutral',null],
    ['#masterMode','neutral',null],
    ['#mixer [data-fxpower],#masterLeft [data-master-power]','processing','FX'],
    ['#app .arV6Console [data-op="SET_MUTE"]','mute','M'],
    ['#app .arV6Console [data-op="SET_SOLO"]','solo','S'],
    ['#app .arV6Console [data-op="SET_CUE"]','cue','C'],
    ['#app .arV6Console [data-op="SET_INSERT_BYPASS"]','processing','FX'],
    ['#app .arV6Console [data-op="SET_SEND_TAP"]','cue',null],
    ['#app [data-sof-toggle]','cue','SEND'],
    ['#app [data-chainbypass]','processing','FX']
  ];
  const PALETTE={
    mute:'255,64,85',solo:'255,208,44',cue:'90,200,231',
    processing:'87,211,189',phase:'243,164,81',
    link:'163,141,235',neutral:'210,225,234'
  };
  const TARGET=RULES.map(rule=>rule[0]).join(',');
  const instances=new Map();
  let serial=0;
  let observer=null;
  const escapeXml=(value)=>String(value).replace(/&/g,'&amp;').replace(/</g,'&lt;')
    .replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&apos;');
  function source(button){
    for(const [selector,semantic,label] of RULES) {
      if(button.matches(selector))return {semantic,label};
    }
    return null;
  }
  function rawLabel(button){
    // Native accepted values can change PRE ↔ POST, MONO ↔ STEREO, etc.
    return (button.textContent||'').trim().replace(/\s+/g,' ');
  }
  function classifyOn(button,legend){
    const pressed=button.getAttribute('aria-pressed');
    if(pressed!==null)return pressed==='true';
    if(button.dataset.op==='SET_SEND_TAP')return legend==='PRE';
    return button.classList.contains('active')||button.classList.contains('selected')||
      button.classList.contains('power-on')||button.classList.contains('on');
  }
  function svgMarkup(legend,w,h,id){
    // V5: a SINGLE SVG mask text cut-out; never overlay duplicate text paints.
    // ViewBox follows actual key size, preventing distorted glyphs in 28px strips.
    const safe=escapeXml(legend);
    const rx=Math.min(6,Math.max(2.5,h*.135));
    const fontSize=Math.max(8,Math.min(15,h*.46,(w-7)/(Math.max(1,legend.length)*.70)));
    const x=w/2,y=h*.615,capW=Math.max(1,w-3),capH=Math.max(1,h-3);
    const left=Math.max(3,w*.07),right=w-left,shineY=2.65;
    const r=n=>Number(n.toFixed(2));
    return '<svg class="mx-hw-face" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 '+r(w)+' '+r(h)+'" preserveAspectRatio="none" aria-hidden="true" focusable="false">'+
      '<defs>'+
      '<linearGradient id="mx-hw-cap-'+id+'" x1="0" y1="0" x2="0" y2="1">'+
      '<stop offset="0" stop-color="#667680"/><stop offset=".20" stop-color="#46555f"/>'+
      '<stop offset=".66" stop-color="#35444d"/><stop offset="1" stop-color="#26323b"/></linearGradient>'+
      '<linearGradient id="mx-hw-led-'+id+'" x1="0" y1="0" x2="0" y2="1">'+
      '<stop offset="0" stop-color="rgb(var(--mx-hw-led))" stop-opacity=".88"/>'+
      '<stop offset=".46" stop-color="rgb(var(--mx-hw-led))" stop-opacity="1"/>'+
      '<stop offset="1" stop-color="rgb(var(--mx-hw-led))" stop-opacity=".74"/></linearGradient>'+
      '<linearGradient id="mx-hw-shine-'+id+'" gradientUnits="userSpaceOnUse" x1="'+r(left)+'" y1="0" x2="'+r(right)+'" y2="0">'+
      '<stop offset="0" stop-color="white" stop-opacity="0"/>'+
      '<stop offset=".2" stop-color="white" stop-opacity=".26"/>'+
      '<stop offset=".5" stop-color="white" stop-opacity=".96"/>'+
      '<stop offset=".8" stop-color="white" stop-opacity=".26"/>'+
      '<stop offset="1" stop-color="white" stop-opacity="0"/></linearGradient>'+
      '<mask id="mx-hw-cut-'+id+'" maskUnits="userSpaceOnUse" maskContentUnits="userSpaceOnUse" x="0" y="0" width="'+r(w)+'" height="'+r(h)+'">'+
      '<rect width="'+r(w)+'" height="'+r(h)+'" fill="black"/>'+
      '<rect x="1.5" y="1.5" width="'+r(capW)+'" height="'+r(capH)+'" rx="'+r(rx)+'" fill="white"/>'+
      '<text x="'+r(x)+'" y="'+r(y)+'" text-anchor="middle" font-family="Arial,Helvetica,sans-serif" font-size="'+r(fontSize)+'" font-weight="700" letter-spacing=".35" fill="black">'+safe+'</text>'+
      '</mask></defs>'+
      '<rect x=".5" y=".5" width="'+r(w-1)+'" height="'+r(h-1)+'" rx="'+r(rx+1)+'" fill="#0c1820"/>'+
      '<rect x="1.5" y="1.5" width="'+r(capW)+'" height="'+r(capH)+'" rx="'+r(rx)+'" fill="#0e151c"/>'+
      '<rect class="mx-hw-lamp" x="2.5" y="2.5" width="'+r(Math.max(1,w-5))+'" height="'+r(Math.max(1,h-5))+'" rx="'+r(Math.max(1,rx-1))+'" fill="url(#mx-hw-led-'+id+')"/>'+
      '<rect x="1.5" y="1.5" width="'+r(capW)+'" height="'+r(capH)+'" rx="'+r(rx)+'" fill="url(#mx-hw-cap-'+id+')" mask="url(#mx-hw-cut-'+id+')"/>'+
      '<path d="M '+r(left)+' '+shineY+' H '+r(right)+'" fill="none" stroke="url(#mx-hw-shine-'+id+')" stroke-width="1.25"/>'+
      '</svg>';
  }
  function render(button,force){
    if(!button.isConnected)return;
    const record=instances.get(button);
    if(!record)return;
    const rect=button.getBoundingClientRect();
    const w=Math.max(16,rect.width-4),h=Math.max(16,rect.height-4);
    const legend=button.dataset.mxHwLegend||record.legend;
    const size=w.toFixed(1)+'x'+h.toFixed(1)+'/'+legend;
    if(!force&&record.size===size&&button.querySelector(':scope > svg.mx-hw-face'))return;
    record.size=size;
    button.innerHTML='<span class="mx-hw-screen-reader">'+escapeXml(legend)+'</span>'+svgMarkup(legend,w,h,record.id);
  }
  function upgrade(button){
    if(!(button instanceof HTMLButtonElement))return;
    const rule=source(button);
    if(!rule)return;
    const existing=instances.get(button);
    if(existing&&button.querySelector(':scope > svg.mx-hw-face'))return;
    const original=rawLabel(button);
    let legend=rule.label||original||'ON';
    // A source text replacement (e.g. native PRE/POST) is authoritative.
    if(existing&&original&&original!==existing.legend&&original!=='')legend=original;
    if(!/^[\wØ/+\- ]{1,10}$/.test(legend))return;
    if(!existing){
      // Drop the broken BTN-180 visual skin, retaining functionality/data hooks.
      button.classList.remove('btn-180','candy');
      button.classList.add('mx-hw-key','mx-hw-'+rule.semantic);
      if(!button.hasAttribute('aria-label'))button.setAttribute('aria-label',original||legend);
      button.style.setProperty('--mx-hw-led',PALETTE[rule.semantic]);
      instances.set(button,{id:++serial,legend,size:''});
      if(resizer)resizer.observe(button);
    }else{
      existing.legend=legend;
    }
    if(button.dataset.op==='SET_SEND_TAP')button.dataset.mxHwOn=String(classifyOn(button,legend));
    button.dataset.mxHwLegend=legend;
    render(button,true);
  }
  function scan(root){
    if(!root||root.nodeType!==1)return;
    if(root.matches('button.mx-hw-key')&&!root.querySelector('svg.mx-hw-face')){
      // An existing app handler replaced the button's textContent.
      const prev=instances.get(root),changed=rawLabel(root);
      if(prev&&changed&&changed!==prev.legend){
        root.dataset.mxHwLegend=changed;
        prev.legend=changed;
      }
      upgrade(root);
    }else if(root.matches(TARGET))upgrade(root);
    for(const button of root.querySelectorAll(TARGET))upgrade(button);
  }
  const resizer=typeof ResizeObserver==='function'?new ResizeObserver(entries=>{
    for(const entry of entries)render(entry.target,false);
  }):null;
  const pending=new Set();
  let scheduled=false;
  function flush(){
    scheduled=false;
    for(const node of pending)scan(node);
    pending.clear();
  }
  function schedule(node){
    if(!node||node.nodeType!==1)return;
    pending.add(node);
    if(!scheduled){scheduled=true;queueMicrotask(flush)}
  }
  function install(){
    if(observer)return;
    scan(document.body);
    observer=new MutationObserver(records=>{
      for(const record of records){
        if(record.target instanceof HTMLButtonElement&&record.target.classList.contains('mx-hw-key') &&
           !record.target.querySelector('svg.mx-hw-face'))schedule(record.target);
        for(const node of record.addedNodes)if(node.nodeType===1&&
           (node.matches?.(TARGET)||node.querySelector?.(TARGET)))schedule(node);
      }
    });
    // Watch DOM replacement only, not high-frequency meter/style attributes.
    observer.observe(document.body,{childList:true,subtree:true});
  }
  window.ArSonMixHardwareKeys=Object.freeze({install,upgrade,scan,PALETTE});
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',install,{once:true});
  else install();
})();