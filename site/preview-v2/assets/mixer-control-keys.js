/* ArSonMix V6 reference mixer tactile keys. One renderer; one CSS owner.
   Presentation only: [data-action] and existing mixer state remain authoritative.
   No polling, no canvas, no hover animations and no DSP or WIRE changes. */
(()=>{
  'use strict';
  const SELECTOR='.console-body .mx-hw-row>button.mx-hw-key';
  const COLORS=Object.freeze({
    mute:'255,78,90',solo:'255,207,68',pfl:'101,210,236'
  });
  const records=new WeakMap();
  const escapeXml=value=>String(value).replace(/[&<>"']/g,ch=>({
    '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'
  })[ch]);
  let count=0;
  const resize=typeof ResizeObserver==='function'
    ?new ResizeObserver(entries=>{for(const entry of entries)render(entry.target);})
    :null;
  const round=value=>Number(value.toFixed(2));
  function face(legend,w,h,id,color){
    // Viewbox matches physical pixels; NEVER stretch glyphs horizontally.
    const rx=Math.min(6,Math.max(3,h*.16));
    const size=Math.max(9.4,Math.min(11.5,h*.49,(w-8)/(.66*legend.length)));
    const x=round(w/2),y=round(h*.625),safe=escapeXml(legend);
    const width=round(w),height=round(h),radius=round(rx);
    const shineL=round(Math.max(4,w*.07)),shineR=round(w-shineL);
    return '<svg class="mx-hw-face" viewBox="0 0 '+width+' '+height+'" xmlns="http://www.w3.org/2000/svg" aria-hidden="true" focusable="false">'+
      '<defs>'+
      '<linearGradient id="mx-cap-'+id+'" x1="0" y1="0" x2="0" y2="1">'+
      '<stop offset="0" stop-color="#728590"/>'+
      '<stop offset=".24" stop-color="#5e727f"/>'+
      '<stop offset=".72" stop-color="#4b5d69"/>'+
      '<stop offset="1" stop-color="#374a56"/></linearGradient>'+
      '<linearGradient id="mx-lamp-'+id+'" x1="0" y1="0" x2="0" y2="1">'+
      '<stop offset="0" stop-color="rgb('+color+')" stop-opacity=".84"/>'+
      '<stop offset=".46" stop-color="rgb('+color+')" stop-opacity="1"/>'+
      '<stop offset="1" stop-color="rgb('+color+')" stop-opacity=".82"/></linearGradient>'+
      '<linearGradient id="mx-shine-'+id+'" gradientUnits="userSpaceOnUse" '+
      'x1="'+shineL+'" y1="0" x2="'+shineR+'" y2="0">'+
      '<stop offset="0" stop-color="white" stop-opacity="0"/>'+
      '<stop offset=".18" stop-color="white" stop-opacity=".28"/>'+
      '<stop offset=".5" stop-color="white" stop-opacity=".96"/>'+
      '<stop offset=".82" stop-color="white" stop-opacity=".28"/>'+
      '<stop offset="1" stop-color="white" stop-opacity="0"/></linearGradient>'+
      '<mask id="mx-knockout-'+id+'" maskUnits="userSpaceOnUse" '+
      'maskContentUnits="userSpaceOnUse" x="0" y="0" width="'+width+'" height="'+height+'">'+
      '<rect width="'+width+'" height="'+height+'" fill="black"/>'+
      '<rect x="1" y="1" width="'+round(w-2)+'" height="'+round(h-2)+'" rx="'+radius+'" fill="white"/>'+
      '<text x="'+x+'" y="'+y+'" text-anchor="middle" '+
      'font-family="ArSonMix Inter,Arial,Helvetica,sans-serif" font-size="'+round(size)+'" '+
      'font-weight="700" letter-spacing=".2" fill="black">'+safe+'</text></mask></defs>'+
      '<rect x=".5" y=".5" width="'+round(w-1)+'" height="'+round(h-1)+'" rx="'+round(rx+1)+'" fill="#0b1920"/>'+
      // White/ice ambience SHINES through the single letter knockout while OFF.
      // ON lamp paints only behind that same physical cut-out, not the full cap.
      '<rect x="1.5" y="1.5" width="'+round(w-3)+'" height="'+round(h-3)+'" rx="'+radius+'" fill="#edf6fa"/>'+
      '<rect class="mx-hw-lamp" x="1.5" y="1.5" width="'+round(w-3)+'" height="'+round(h-3)+'" rx="'+radius+'" fill="url(#mx-lamp-'+id+')"/>'+
      '<rect x="1" y="1" width="'+round(w-2)+'" height="'+round(h-2)+'" rx="'+radius+'" '+
      'fill="url(#mx-cap-'+id+')" mask="url(#mx-knockout-'+id+')"/>'+
      '<path d="M '+shineL+' 2.8 H '+shineR+'" fill="none" '+
      'stroke="url(#mx-shine-'+id+')" stroke-width="1.15"/>'+
      '</svg>';
  }
  function render(button){
    if(!button.isConnected)return;
    const state=records.get(button);
    if(!state)return;
    const rect=button.getBoundingClientRect();
    if(rect.width<20||rect.height<15)return;
    const w=round(rect.width-4),h=round(rect.height-4);
    const signature=w+'x'+h+'/'+state.legend;
    if(signature===state.signature&&button.querySelector(':scope > svg.mx-hw-face'))return;
    state.signature=signature;
    button.innerHTML='<span class="mx-hw-screen-reader">'+escapeXml(state.legend)+'</span>'+
      face(state.legend,w,h,state.id,COLORS[state.semantic]);
  }
  function upgrade(button){
    if(!(button instanceof HTMLButtonElement))return;
    if(!button.matches(SELECTOR))return;
    if(records.has(button)){if(button.dataset.hwKind!=='mute')render(button);return;}
    const semantic=button.dataset.hwKind;
    if(!Object.hasOwn(COLORS,semantic))return;
    const legend=({mute:'MUTE',solo:'SOLO',pfl:'PFL'})[semantic];
    if(!button.hasAttribute('aria-label'))button.setAttribute('aria-label',legend);
    // MUTE uses lightweight DOM + CSS; no SVG, image asset, or resize redraw.
    if(semantic==='mute'){
      records.set(button,{semantic,legend});
      button.innerHTML='<span class="mx-hw-screen-reader">'+legend+'</span>'+
        '<span class="mx-mute-face" aria-hidden="true">'+
          '<span class="mx-mute-legend">'+legend+'</span>'+
          '<span class="mx-mute-lamp"></span></span>';
      return;
    }
    records.set(button,{id:++count,semantic,legend,signature:null});
    resize?.observe(button);
    render(button);
  }
  function scan(node){
    if(!node||node.nodeType!==1)return;
    if(node.matches?.(SELECTOR))upgrade(node);
    node.querySelectorAll?.(SELECTOR).forEach(upgrade);
  }
  const queue=new Set();
  let queued=false;
  function update(){
    queued=false;
    for(const node of queue)scan(node);
    queue.clear();
  }
  function install(){
    scan(document.body);
    new MutationObserver(entries=>{
      for(const entry of entries){
        for(const node of entry.removedNodes){
          if(node.nodeType!==1)continue;
          if(node.matches?.(SELECTOR))resize?.unobserve(node);
          node.querySelectorAll?.(SELECTOR).forEach(x=>resize?.unobserve(x));
        }
        for(const node of entry.addedNodes){
          if(node.nodeType!==1)continue;
          if(!node.matches?.(SELECTOR)&&!node.querySelector?.(SELECTOR))continue;
          queue.add(node);
        }
      }
      if(queue.size&&!queued){queued=true;queueMicrotask(update);}
    }).observe(document.body,{childList:true,subtree:true});
  }
  window.ArSonMixHardwareKeys=Object.freeze({install});
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',install,{once:true});
  else install();
})();
