/* V6 contextual button focus stabilizer: pointer only, no UI-state copies.
   Prevent mouse focus auto-scroll between button A/B; browser click and native
   app handlers still run normally. Keyboard Tab/Enter/Space are unchanged.
   Never intercept MUTE/SOLO/PFL PNG buttons or physical WIRE sockets. */
(()=>{
  'use strict';
  const owner=[
    '.app .command-bar button.tool','.app .command-bar summary.tool',
    '.app .more-tools-panel button.tool','.app .identity-actions button.console-control',
    '.app .mixer-title button.wire-master','.app .mixer-title button.wire-v34-tool',
    '.app .console-body .section-rail button.rail-cell[data-section]',
    '.app .console-body .strip button.mx-util-key',
    '.app .plugin-footer button','.app .plugin-browser .plugin-head button',
    '.app .plugin-browser .plugin-actions button','.app .plugin-browser .plugin-paths button',
    '.app .route-choice-head button','.app .route-choice-option',
    '.app .wire-workspace .wire-ws-head button',
    '.app .wire-workspace .wire-ws-tabs button',
    '.app .wire-workspace .wire-ws-foot button',
    '.app .wire-workspace .wire-ws-matrix button'
  ].join(',');
  document.addEventListener('mousedown',event=>{
    if(event.button!==0 || !event.cancelable || event.defaultPrevented)return;
    const control=event.target.closest('button,summary');
    if(!control||control.disabled||!control.matches(owner))return;
    // Prevent only the browser's mouse-induced focus-scroll. Do not stop
    // propagation, prevent click, blur keyboard focus, or alter scrollTop.
    event.preventDefault();
  },true);
})();


/* ArSon Player stays an additive module on the SAME approved /preview/.
   Do not mutate the historically checkpointed console HTML or create a new
   route authority. This loader inserts a compact drawer and starts the one
   native-control-only Player script after the existing mixer is initialized. */
(()=>{
  'use strict';
  const toolbar=document.querySelector('.app .command-bar');
  const plugins=document.getElementById('pluginsButton');
  if(!toolbar||!plugins||document.getElementById('playerToggle'))return;
  const playerButton=document.createElement('button');
  playerButton.type='button';playerButton.className='tool';
  playerButton.id='playerToggle';
  playerButton.textContent='♫ Player';
  playerButton.title='Music player and live setlist';
  playerButton.setAttribute('aria-expanded','false');
  playerButton.setAttribute('aria-controls','arsonPlayer');
  plugins.insertAdjacentElement('afterend',playerButton);
  const panel=document.createElement('aside');
  panel.id='arsonPlayer';panel.hidden=true;
  panel.setAttribute('aria-label','ArSon music player and live setlist');
  panel.innerHTML=`
    <div class="ap-top"><span class="ap-brand">ArSon Player</span><span class="ap-sub">Music · Setlist</span><button id="playerClose" type="button" class="ap-close" aria-label="Close player">×</button></div>
    <div class="ap-current"><div class="ap-overline">PLAYER A · CURRENT</div><div id="playerNow" class="ap-title">No music loaded</div><div class="ap-time"><span id="playerElapsed">00:00</span><progress id="playerProgress" max="1" value="0" aria-label="Song progress"></progress><span id="playerDuration">00:00</span></div></div>
    <div class="ap-transport"><button id="playerPlay" type="button">▶ Play</button><button id="playerStop" type="button">■ Stop</button><button id="playerNext" type="button">Next ›</button><button id="playerAutoNext" type="button" aria-pressed="false">Auto Next · Off</button></div>
    <div class="ap-pitchtempo" aria-label="Music pitch and playback speed">
      <div class="ap-pitchctrl"><span>KEY</span><button id="playerKeyMinus" type="button" aria-label="Lower music key one semitone">−</button><output id="playerKeyValue" aria-live="polite">0 st</output><button id="playerKeyPlus" type="button" aria-label="Raise music key one semitone">+</button></div>
      <div class="ap-pitchctrl"><span>TEMPO</span><button id="playerTempoMinus" type="button" aria-label="Slow music by five percent">−</button><output id="playerTempoValue" aria-live="polite">100%</output><button id="playerTempoPlus" type="button" aria-label="Speed music by five percent">+</button></div>
      <button class="ap-reset" id="playerPitchReset" type="button" title="Reset key and tempo for current song">Reset</button>
    </div>
    <div class="ap-statusbar"><div id="playerFeedback" class="ap-feedback" role="status" aria-live="polite">Add music to begin</div><button id="playerChooseOutput" type="button" hidden>Choose output</button><button id="playerRelink" type="button" hidden>Locate file</button></div>
    <div class="ap-listhead"><strong>Setlist</strong><span id="playerCount" class="ap-sub">0 songs</span><button id="playerAddFolder" class="ap-folder" type="button" aria-label="Add all music from a folder" title="Add music folder"><svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round" stroke-linecap="round" aria-hidden="true"><path d="M3 7a2 2 0 0 1 2-2h5l2 2h7a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z"/></svg></button><button id="playerAdd" type="button">+ Add Music</button></div>
    <input id="playerSearch" type="search" placeholder="Search songs" aria-label="Search music in setlist" autocomplete="off">
    <div id="playerList" role="listbox" aria-label="Setlist songs"></div>
    <div class="ap-foot">Click to prepare · Double-click to play · Native audio through your mixer</div>`;
  toolbar.insertAdjacentElement('afterend',panel);
  const base=document.currentScript?.src||document.baseURI;
  // Player CSS is folded into the EXISTING approved runtime stylesheet.
  // No second CSS layer or mobile toolbar/viewport side effects.
  const script=document.createElement('script');
  script.src=new URL('arson-player.js',base).href;
  document.body.appendChild(script);
})();

/* ArSonMiX owner wordmark; original source HTML/assembler anchor is immutable. */
(()=>{
  'use strict';
  const logo=document.querySelector('.app > .window-title .logo');
  if(!logo||logo.textContent!=='ArSonMix'||typeof logo.replaceChildren!=='function')return;
  logo.replaceChildren(...[
    ['armix-brand-ar','Ar'],['armix-brand-son','Son'],['armix-brand-mix','MiX']
  ].map(([cls,value])=>{
    const span=document.createElement('span');
    span.className=cls;span.textContent=value;
    return span;
  }));
  logo.setAttribute('aria-label','ArSonMiX');
})();


/* ArSonMiX shared floating-window movement.
   One pointer controller for the modeless tools that did not have a drag
   owner. The existing EQ/Compressor/Harmonice/Vocal Throw, identity editor
   and Plugin Manager keep their proven gesture handlers.
   This controller changes presentation geometry only: never mixer state,
   native control intent, audio routing, or plugin instance lifecycle. */
(()=>{
  'use strict';
  const windows=[
    ['#fastPicker','.fast-head'],
    ['#wireConfigWindow','.wire-config-head'],
    ['#wireWorkspace','.wire-ws-head'],
    ['#routeChoice','.route-choice-head'],
    ['.armix-audio-dialog','.armix-audio-top'],
    ['#arIOOverlay .arIOWindow',':scope > header'],
    ['#arSendEditor',':scope > header'],
    ['.arSourceChooser','.arSourceChooserTop'],
    ['#arRoutingLabPanel','.arPatchTop'],
    ['#nativeFxSelector .native-dialog','.native-dialog-header'],
    ['#nativePluginManager .native-dialog','.native-dialog-header'],
    ['#nativeMySounds .native-dialog','.native-dialog-header'],
    ['#nativeOrbit .native-dialog','.native-dialog-header']
  ];
  const gap=8, blocked='button,input,textarea,select,a,summary,[contenteditable="true"],[role="button"]';
  let moving=null,layer=1000;
  const clamp=(value,min,max)=>Math.max(min,Math.min(Math.max(min,max),value));
  const viewport=()=>({
    width:document.documentElement.clientWidth||window.innerWidth,
    height:document.documentElement.clientHeight||window.innerHeight
  });
  const place=(panel,left,top)=>{
    const size=viewport(),width=panel.offsetWidth,height=panel.offsetHeight;
    panel.style.setProperty('left',clamp(left,gap,Math.max(gap,size.width-width-gap))+'px','important');
    panel.style.setProperty('top',clamp(top,gap,Math.max(gap,size.height-height-gap))+'px','important');
  };
  const isVisible=panel=>!panel.hidden && !panel.closest('[hidden]') &&
    panel.getClientRects().length>0;
  // Matrix/Routes can change width without a viewport resize. Track only
  // panels the operator actually moved, not every mixer DOM mutation.
  const observed=new WeakSet();
  const sizeObserver=typeof ResizeObserver==='function'
    ?new ResizeObserver(entries=>{
      for(const entry of entries){
        const panel=entry.target;
        if(panel.classList.contains('armix-floating-moved')&&isVisible(panel)){
          const rect=panel.getBoundingClientRect();
          place(panel,rect.left,rect.top);
        }
      }
    }):null;
  document.addEventListener('pointerdown',event=>{
    if(event.button!==0||event.isPrimary===false||event.target.closest?.(blocked))return;
    const item=windows.find(([selector,handle])=>{
      const panel=event.target.closest?.(selector);
      return panel&&isVisible(panel)&&panel.querySelector(handle)?.contains(event.target);
    });
    if(!item)return;
    const panel=event.target.closest(item[0]),handle=panel.querySelector(item[1]);
    const rect=panel.getBoundingClientRect();
    if(!rect.width||!rect.height)return;
    panel.classList.add('armix-floating-moved');
    if(sizeObserver&&!observed.has(panel)){
      observed.add(panel);sizeObserver.observe(panel);
    }
    panel.style.setProperty('position','fixed','important');
    panel.style.setProperty('right','auto','important');
    panel.style.setProperty('bottom','auto','important');
    panel.style.setProperty('transform','none','important');
    panel.style.setProperty('margin','0','important');
    panel.style.setProperty('z-index',String(++layer),'important');
    place(panel,rect.left,rect.top);
    moving={panel,handle,id:event.pointerId,dx:event.clientX-rect.left,dy:event.clientY-rect.top};
    try{handle.setPointerCapture?.(event.pointerId);}catch{}
    event.preventDefault(); // no text selection; do not cancel controls
  });
  window.addEventListener('pointermove',event=>{
    const drag=moving;
    if(!drag||drag.id!==event.pointerId)return;
    place(drag.panel,event.clientX-drag.dx,event.clientY-drag.dy);
  },{passive:true});
  const finish=event=>{
    const drag=moving;
    if(!drag||(event&&event.pointerId!==drag.id))return;
    moving=null;
    try{
      if(drag.handle.hasPointerCapture?.(drag.id))
        drag.handle.releasePointerCapture?.(drag.id);
    }catch{}
  };
  window.addEventListener('pointerup',finish);
  window.addEventListener('pointercancel',finish);
  window.addEventListener('blur',()=>finish());
  window.addEventListener('resize',()=>{
    document.querySelectorAll('.armix-floating-moved').forEach(panel=>{
      if(isVisible(panel)){
        const rect=panel.getBoundingClientRect();
        place(panel,rect.left,rect.top);
      }
    });
  },{passive:true});
})();

/* DAW-standard wheel adjust for native built-in compressor/Harmonice dials.
   EQ and Vocal Throw already own their wheel controls; console faders and
   SEND levels have separate handlers. Do not intercept preset-list scrolling.
   This never writes native PCM directly: original input listeners own edits,
   ACK and readback. */
(()=>{
  'use strict';
  const residues=new WeakMap();
  document.addEventListener('wheel',event=>{
    if(event.ctrlKey||!Number.isFinite(event.deltaY)||event.deltaY===0)return;
    const input=event.target.closest?.('.mx-comp-slider,.mx-harm-slider input[type="range"]');
    if(!input||input.disabled||input.closest?.('[hidden]'))return;
    const min=Number(input.min),max=Number(input.max),value=Number(input.value);
    const step=Number(input.step)||1;
    if(![min,max,value,step].every(Number.isFinite)||step<=0||max<=min)return;
    event.preventDefault();
    const pixels=event.deltaY*(event.deltaMode===1?16:event.deltaMode===2?240:1);
    const residue=(residues.get(input)||0)+pixels;
    const ticks=Math.trunc(residue/100);
    residues.set(input,residue-ticks*100);
    if(!ticks)return;
    const amount=ticks*(event.shiftKey?1:1);
    const quantized=Math.round((value-min-amount*step)/step)*step+min;
    const next=Math.max(min,Math.min(max,Number(quantized.toFixed(5))));
    if(next===value)return;
    input.value=String(next);
    input.dispatchEvent(new Event('input',{bubbles:true}));
  },{passive:false});
})();
