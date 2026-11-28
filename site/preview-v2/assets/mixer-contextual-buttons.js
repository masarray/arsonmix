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
    <div id="playerScreen" class="ap-screen">
    <div class="ap-top"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3 10v4M6 6v12M9 3v18M12 8v8M15 5v14M18 9v6M21 11v2"/></svg><span class="ap-brand">ArSon Player</span><div class="ap-chrome"><button id="playerMinimize" type="button" class="ap-icon" aria-label="Minimize to MiniPlayer" title="Minimize to MiniPlayer" ><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3" y="5" width="18" height="14" rx="3"/><path d="M8 15h8"/></svg></button><button id="playerOptions" type="button" class="ap-icon" aria-label="Player options" title="Player options" aria-haspopup="menu"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/></svg></button><button id="playerClose" type="button" class="ap-close" aria-label="Close player" title="Close player" >×</button></div></div>
    <section class="ap-hero">
      <div class="ap-nowhead"><div class="ap-trackmark" aria-label="Music artwork"><img id="playerArtwork" alt="Album artwork" hidden><span id="playerArtworkFallback" class="ap-cover-fallback" data-art="still" role="img" aria-label="No song loaded"></span></div><div class="ap-nowtext"><div class="ap-overline"><i></i><span id="playerState">NO TRACK</span></div><div id="playerNow" class="ap-title">No music loaded</div><div id="playerNowSubtitle" class="ap-subtitle">Music player</div><div id="playerNowMeta" class="ap-meta"></div></div><div class="ap-remaining"><output id="playerRemaining">−00:00</output><span>Remaining</span></div></div>
      <canvas id="playerWaveform" class="ap-waveform" width="256" height="70" aria-label="Progressive music waveform" role="img"></canvas><div class="ap-time"><span id="playerElapsed">00:00</span><input id="playerSeek" type="range" min="0" max="1000" step="1" value="0" aria-label="Seek through current song"><span id="playerDuration">00:00</span></div>
      <div class="ap-transport"><button id="playerPrevious" type="button" class="ap-minor" aria-label="Play previous setlist song" title="Play previous setlist song" ><svg viewBox="0 0 24 24" aria-hidden="true" fill="currentColor"><rect x="4" y="5" width="3" height="14" rx="1"/><path d="M20 5 9 12l11 7z"/></svg></button><button id="playerStop" type="button" class="ap-stop" aria-label="Stop" title="Stop" ><svg viewBox="0 0 24 24" aria-hidden="true" fill="currentColor"><rect x="5" y="5" width="14" height="14" rx="2"/></svg></button><button id="playerPlay" type="button" class="ap-play" aria-label="Play" title="Play" ><svg viewBox="0 0 24 24" aria-hidden="true" fill="currentColor"><path d="M6 5Q6 4 7 4.5L18 11Q19.5 12 18 13L7 19.5Q6 20 6 19Z"/></svg></button><button id="playerNext" type="button" class="ap-minor" aria-label="Next song" title="Next song" ><svg viewBox="0 0 24 24" aria-hidden="true" fill="currentColor"><rect x="17" y="5" width="3" height="14" rx="1"/><path d="M4 5l11 7-11 7z"/></svg></button><button id="playerLoopAB" type="button" class="ap-minor ap-repeat" aria-label="Loop A–B (saved IN and OUT markers)" title="Loop A–B (saved IN and OUT markers)" aria-pressed="false"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m18 3 3 3-3 3M21 6H8a5 5 0 0 0-5 5m3 10-3-3 3-3m-3 3h13a5 5 0 0 0 5-5"/></svg></button></div>
      <div class="ap-pitchtempo" aria-label="Music pitch and playback speed"><div class="ap-pitch-section"><span id="playerPitchTarget" class="ap-pitchtarget">KEY · CURRENT</span><div class="ap-step"><button id="playerKeyMinus" type="button" class="" aria-label="Lower music key one semitone" title="Lower music key one semitone" >−</button><output id="playerKeyValue" aria-live="polite">0 st</output><button id="playerKeyPlus" type="button" class="" aria-label="Raise music key one semitone" title="Raise music key one semitone" >+</button></div></div><div class="ap-pitch-section"><span>TEMPO</span><div class="ap-step"><button id="playerTempoMinus" type="button" class="" aria-label="Slow music by five percent" title="Slow music by five percent" >−</button><output id="playerTempoValue" aria-live="polite">100%</output><button id="playerTempoPlus" type="button" class="" aria-label="Speed music by five percent" title="Speed music by five percent" >+</button></div></div><button id="playerPitchReset" type="button" class="ap-icon" aria-label="Reset key and tempo" title="Reset key and tempo" ><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M5 8a7 7 0 1 1-1 8M5 3v5h5"/></svg></button></div>
      <div class="ap-statusbar"><span id="playerRouteLight" class="ap-route-light" aria-hidden="true"></span><div id="playerFeedback" class="ap-feedback" role="status" aria-live="polite">Add music to begin</div><button id="playerAutoNext" type="button" class="ap-mode" aria-label="Playback mode" title="Playback mode" aria-haspopup="menu" aria-expanded="false" aria-pressed="false"><span id="playerAutoText">Stop after song</span><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m6 9 6 6 6-6"/></svg></button></div><div class="ap-recovery"><button id="playerChooseOutput" type="button" class="" aria-label="Choose output" title="Choose output" hidden>Choose output</button><button id="playerRelink" type="button" class="" aria-label="Locate file" title="Locate file" hidden>Locate file</button></div>
      <button id="playerNextCard" class="ap-upnext" type="button" title="Show upcoming songs"><span class="ap-nextname"><span id="playerNextLabel">UP NEXT</span><strong id="playerUpNext">End of setlist</strong></span><span id="playerNextMeta" class="ap-nextmeta"></span><span id="playerTransitionStatus" role="status" aria-live="polite"></span><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m9 6 6 6-6 6"/></svg></button>
    </section>
    <div class="ap-listhead"><div class="ap-view-switch" role="group" aria-label="Player collection"><button id="playerSetlistView" type="button" class="" aria-label="Setlist" title="Setlist" aria-pressed="true">Setlist</button><button id="playerLibraryView" type="button" class="" aria-label="Global music library" title="Library: files in active setlist" aria-pressed="false">Library</button><button id="playerQueueView" type="button" class="" aria-label="Queue" title="Queue" aria-pressed="false">Queue (<span id="playerQueueCount">0</span>)</button></div><button id="playerAddFolder" type="button" class="ap-icon" aria-label="Add all music from a folder" title="Add all music from a folder" ><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3 7V5h6l2 3h10v11H3zM3 11h18M14 15v6m-3-3h6"/></svg></button><button id="playerAdd" type="button" class="ap-icon" aria-label="Add music files" title="Add music files" ><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 5v14M5 12h14"/></svg></button></div>
    <div id="playerSetlistTools"><div class="ap-settools"><select id="playerSetlistSelect" aria-label="Active setlist"></select><span id="playerCount">0 songs</span><button id="playerSetOptions" type="button" class="ap-icon" aria-label="Setlist options" title="Setlist options" aria-haspopup="menu"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/></svg></button><button id="playerUndoRemove" type="button" class="" aria-label="Undo remove" title="Undo remove" hidden>Undo</button></div><div id="playerLibraryNote" class="ap-meta ap-library-note" hidden>All imported music · double-click to add a track not in this setlist</div><select id="playerLibraryFilter" class="ap-library-filter" aria-label="Filter music library" hidden><option value="all">All music</option><option value="favorites">Favorites</option><option value="recent">Recently played · session</option></select><div class="ap-searchwrap"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.5"/><path d="m16 16 5 5"/></svg><input id="playerSearch" type="search" placeholder="Search songs..." title="Ctrl+F · Enter prepares · Ctrl+Enter queues" aria-label="Search music in setlist" autocomplete="off"></div><div class="ap-columns"><span>#</span><span>Title</span><span>Key</span><span>Time</span><span></span></div></div>
    <div id="playerList" role="list" aria-label="Setlist songs"></div><div id="playerQueuePanel" class="ap-queue" hidden><div class="ap-queue-title">Playback order · drag to reorder</div><div id="playerQueueList" role="list" aria-label="Queued songs"></div><div id="playerQueueEmpty" class="ap-empty" hidden>Queue is empty. Add songs from their actions.</div></div>
    <div class="ap-foot"><span>Native audio engine</span><button id="playerDetailsToggle" type="button" class="" aria-label="Song details" title="Song details" aria-expanded="false">Song details <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m6 9 6 6 6-6"/></svg></button></div>
    <section id="playerDetails" class="ap-details" hidden><div class="ap-showtools ap-volume"><label for="playerVolume">Player level</label><input id="playerVolume" type="range" min="0" max="100" step="1" value="50" aria-label="Player level before channel FX"><output id="playerVolumeDb" for="playerVolume">−6.0 dB</output></div><div class="ap-showtools"><label for="playerFadeMs">Transition</label><select id="playerFadeMs" aria-label="Player next song transition"><option value="0">Cut</option><option value="180" selected>Smooth 180 ms</option><option value="500">Crossfade 0.5 s</option><option value="1500">Crossfade 1.5 s</option></select></div><div class="ap-showtools"><label id="playerAfterSongTarget" for="playerAfterSong">After selected song</label><select id="playerAfterSong" aria-label="After selected song"><option value="-1">Use global mode</option><option value="0">Stop after song</option><option value="1">Continue automatically</option></select></div><div class="ap-cues"><span id="playerCueSummary">IN 00:00 · OUT end</span><button id="playerCueIn" type="button" class="" aria-label="Set song IN" title="Set song IN" >Set IN</button><button id="playerCueOut" type="button" class="" aria-label="Set song OUT" title="Set song OUT" >Set OUT</button><button id="playerCueGoIn" type="button" class="" aria-label="Seek to song IN" title="Seek to song IN" >Go IN</button><button id="playerCueClear" type="button" class="" aria-label="Clear non-destructive song markers" title="Clear non-destructive song markers" >Clear</button></div><details id="playerKeyboardHelp" class="ap-disclosure"><summary>Keyboard shortcuts</summary><div class="ap-shortcuts"><span>Find songs</span><kbd>Ctrl+F</kbd><span>Navigate tracks</span><kbd>↑ ↓ · Home · End</kbd><span>Prepare first search result</span><kbd>Enter (search)</kbd><span>Add focused song to queue</span><kbd>Ctrl+Enter</kbd><span>Play focused track</span><kbd>Enter (row)</kbd><span>Track actions</span><kbd>Shift+F10</kbd><span>Clear search / close menu</span><kbd>Esc</kbd></div></details><details id="playerSetManage" class="ap-disclosure"><summary>Manage setlist</summary><div class="ap-settools"><button id="playerNewSetlist" type="button" class="" aria-label="Create an empty setlist" title="Create an empty setlist" >New setlist</button><button id="playerDeleteSetlist" type="button" class="" aria-label="Remove this setlist, not its music files" title="Remove this setlist, not its music files" >Remove set</button></div><input id="playerSetlistName" type="text" maxlength="64" aria-label="Rename active setlist" placeholder="Setlist name"></details></section></div>
    <div id="playerSongMenu" class="ap-song-menu" role="menu" aria-label="Player actions" hidden></div>`;
  toolbar.insertAdjacentElement('afterend',panel);
  const mini=document.createElement('div');
  mini.id='playerMini';mini.hidden=true;mini.setAttribute('aria-label','ArSon MiniPlayer');
  mini.innerHTML=`<span class="ap-mini-cover"><img id="playerMiniArtwork" alt="Album artwork" hidden><span id="playerMiniArtworkFallback" class="ap-cover-fallback" data-art="still" role="img" aria-label="No song loaded"></span></span><button id="playerMiniExpand" type="button" aria-label="Expand player"><span id="playerMiniTitle">No music loaded</span><small id="playerMiniTime">00:00 / 00:00</small></button><button id="playerMiniPlay" type="button" class="ap-mini-play" aria-label="Play" title="Play" ><svg viewBox="0 0 24 24" aria-hidden="true" fill="currentColor"><path d="M6 5Q6 4 7 4.5L18 11Q19.5 12 18 13L7 19.5Q6 20 6 19Z"/></svg></button><button id="playerMiniNext" type="button" class="ap-mini-next" aria-label="Next song" title="Next song" ><svg viewBox="0 0 24 24" aria-hidden="true" fill="currentColor"><rect x="17" y="5" width="3" height="14" rx="1"/><path d="M4 5l11 7-11 7z"/></svg></button>`;
  playerButton.insertAdjacentElement('afterend',mini);
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
    ['#arsonPlayer','.ap-top'],
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
  if(document.documentElement?.dataset?.armixPlayerWindow==="true")return;
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
    // The centred Player is already a single-anchor floating window,
    // even if it has never been dragged. Keep it inside a resized viewport.
    document.querySelectorAll('.armix-floating-moved,#arsonPlayer[data-armix-centered-once="true"]').forEach(panel=>{
      if(isVisible(panel)){
        const rect=panel.getBoundingClientRect();
        place(panel,rect.left,rect.top);
      }
    });
  },{passive:true});
})();

/* Built-in FX wheel gestures are owned by each editor through mixer-builtin-fx-gesture.js. */

/* Shared initial placement, distinct lifetime policies. First open is
   centred; subsequent opens retain the user's moved location.
   Transient utilities dismiss on outside pointer/focus. The Player is a
   persistent modeless workspace: only its explicit controls can hide it. */
(()=>{
 'use strict';
 if(document.documentElement?.dataset?.armixPlayerWindow==="true")return;
 const list=[
  ['pluginBrowser','pluginClose','pluginsButton'],
  ['arsonPlayer','playerClose','playerToggle',false],
  ['wireWorkspace','wireWsClose','wireNavigatorToggle'],
  ['wireConfigWindow','wireConfigClose','wireConfigToggle']
 ];
 const panels=()=>list.map(([id,close,trigger,dismissOnOutside=true])=>({
  panel:document.getElementById(id),close:document.getElementById(close),
  trigger:document.getElementById(trigger),dismissOnOutside
 })).filter(x=>x.panel&&x.close);
 const visible=panel=>!panel.hidden&&panel.getClientRects().length>0;
 const margin=12;
 function centerFirst(panel){
  if(panel.dataset.armixCenteredOnce==='true'||!visible(panel))return;
  const rect=panel.getBoundingClientRect();
  if(rect.width<1||rect.height<1)return;
  const fixed=getComputedStyle(panel).position==='fixed';
  const parent=fixed?null:(panel.offsetParent||document.documentElement);
  const area=parent?.getBoundingClientRect?.()||{
   left:0,top:0,width:document.documentElement.clientWidth,
   height:document.documentElement.clientHeight};
  const x=Math.max(margin,Math.round((area.width-rect.width)/2));
  const y=Math.max(margin,Math.round((area.height-rect.height)/2));
  panel.style.setProperty('left',x+'px','important');
  panel.style.setProperty('top',y+'px','important');
  panel.style.setProperty('right','auto','important');
  if(fixed){
   // The Player originally fills a top/bottom-anchored drawer. Freeze the
   // measured height when converting it to a centred movable utility card.
   if(panel.id==='arsonPlayer')
    panel.style.setProperty('height',Math.min(rect.height,area.height-2*margin)+'px','important');
   panel.style.setProperty('bottom','auto','important');
  }
  panel.dataset.armixCenteredOnce='true';
 }
 function observe(panel){
  const observer=new MutationObserver(()=>centerFirst(panel));
  observer.observe(panel,{attributes:true,attributeFilter:['hidden']});
  centerFirst(panel);
 }
 for(const {panel} of panels())observe(panel);
 function dismissOutside(target){
  if(!target?.closest)return;
  if(target.closest('#routeChoice,#fastPicker,.mx-fxp-form'))return;
  for(const {panel,close,trigger,dismissOnOutside} of panels()){
   if(!dismissOnOutside||!visible(panel)||panel.contains(target)||trigger?.contains(target))continue;
   if(panel.id==='wireWorkspace'&&
      target.closest('#wireMatrixToggle,#wireNavigatorToggle,[data-wire-open]'))continue;
   if(panel.id==='wireConfigWindow'&&
      target.closest('#wireConfigToggle,[data-wire-open="config"]'))continue;
   close.click();
  }
 }
 document.addEventListener('pointerdown',e=>dismissOutside(e.target),true);
 document.addEventListener('focusin',e=>dismissOutside(e.target),true);
})();


/* Operator-first Add Audio and immediate replace-name, shared by public
   /preview/ and the native-composed WebView. Keep immutable HTML/CSS/PNG
   ownership unchanged: MOVE the one existing bound button, never clone it. */
(()=>{
  'use strict';
  const add=document.getElementById('addTrack');
  const bankNext=document.getElementById('bankNext');
  if(add && bankNext && add.closest('#moreTools')){
    const originalMenu=add.parentElement, originalNext=add.nextSibling;
    const toolbar=bankNext.parentElement;
    add.type='button';
    add.setAttribute('aria-label','Add audio channel');
    add.title='Add audio channel';
    const place=()=>{
      // Desktop: always visible beside BANK. Phone: retain the original
      // More-panel action so a 360px toolbar can scroll without hiding it.
      // The *same bound node* moves; channel creation never gains an owner.
      if(window.innerWidth<=900){
        if(add.parentElement!==originalMenu)
          originalMenu.insertBefore(add,originalNext);
      }else if(add.parentElement!==toolbar){
        bankNext.insertAdjacentElement('afterend',add);
      }
    };
    window.addEventListener('resize',place,{passive:true});
    place();
  }
  // The native composer already preselects on open. The original public
  // preview uses the same identity editor but only focused its text field.
  // Delegated events run AFTER the existing editor-open handlers and do not
  // change mixer state, channel IDs, routes, or the audio/FX graph.
  const body=document.querySelector('.app .console-body');
  const selectName=target=>{
    if(!target?.closest?.('.strip-identity-top,.footer-label .name'))return;
    const panel=document.getElementById('channelIdentity');
    const input=document.getElementById('identityName');
    if(!panel || panel.hidden || !input)return;
    if(input.selectionStart===0 && input.selectionEnd===input.value.length)return;
    input.focus({preventScroll:true});
    input.select();
  };
  body?.addEventListener('dblclick',e=>selectName(e.target));
  body?.addEventListener('keydown',e=>{
    if(e.key==='F2' || e.key==='Enter')selectName(e.target);
  });
})();


/* FX RACK workstation recovery (ONE approved /preview/).
   Double-click an FX rack lane/header to bring a visible built-in FX editor
   back to screen centre. Applies to the existing GEQ, PEQ, Compressor,
   Harmonice, Vocal Throw and SonKu modeless editors. No FX state, audio
   graph, WebView2 instance or renderer is duplicated.
   A DOM editor cannot move beyond the enclosing Windows WebView2 HWND.
   Real out-of-mixer popout requires a JUCE-owned separate HWND. */
(()=>{
 'use strict';
 // One bounded layer range for the nine approved inline editor surfaces.
 // Native leases and audio state remain exclusively in their existing owners.
 const owners=Object.freeze({
  '#mxNativeGeq31':'ArSonMixGraphicEq31','#mxNativePeq':'ArSonMixNativeEq',
  '#mxNativeComp':'ArSonMixNativeCompressor','#mxNativeHarmony':'ArSonMixNativeHarmony',
  '#mxNativeDucker':'ArSonMixNativeDucker',
  '#mxNativeVocalThrow':'ArSonMixNativeVocalThrow','#mxNativeSonKu':'ArSonMixNativeSonKu',
  '#mxNativeAntiNoiz':'ArSonMixNativeAntiNoiz','#mxNativeDeEzzerHost':'ArSonMixNativeDeEzzer'
 });
 const selectors=Object.keys(owners),rootSelector=selectors.join(',');
 // Reserve 9000..9008 for FX; native context/hotkey panels start at
 // 10018 and modal/confirmation surfaces remain above this band.
 const layerBase=9000;
 let order=[];
 const popupSelector='.mx-fxp-form,.mx-fxp-menu,#mxHPresetForm,#mxHPresetMenu,.mx-ducker-picker';
 const modalSelector='[aria-modal="true"],dialog[open],#fastPicker,#routeChoice,#channelIdentity,#nativeFxSelector,#nativePluginManager,#nativeMySounds,#nativeOrbit,#armixContext,#armixOrbit,#armixFxParamBrowse,#armixHotkeyCard,#armixHotkeyRegister';
 const isVisible=el=>el&&!el.hidden&&!el.closest('[hidden]')&&
   el.getClientRects().length>0&&getComputedStyle(el).visibility!=='hidden';
 const activeModal=()=>Array.from(document.querySelectorAll(modalSelector)).find(isVisible);
 const popupIn=root=>Array.from(root?.querySelectorAll(popupSelector)||[]).find(isVisible);
 function paintOrder(){
  order=order.filter(panel=>{
   if(isVisible(panel))return true;
   delete panel.dataset.fxActive;return false;
  });
  for(let i=0;i<order.length;i++){
   const panel=order[i],layer=String(layerBase+i);
   if(panel.style.zIndex!==layer)panel.style.zIndex=layer;
   if(panel.dataset.fxActive!==(i===order.length-1?'true':'false'))
    panel.dataset.fxActive=i===order.length-1?'true':'false';
  }
 }
 function raise(panel){
  if(!panel?.matches?.(rootSelector)||!isVisible(panel)||activeModal())return false;
  // Populate only untracked visible roots; then move the selected existing
  // node to the top. No growing counter, DOM reparenting or geometry writes.
  for(const {el} of visibleEditors())if(!order.includes(el))order.push(el);
  order=order.filter(el=>el!==panel);order.push(panel);paintOrder();
  return true;
 }
 function activeEditor(){
  paintOrder();
  return order.at(-1)||visibleEditors().at(-1)?.el||null;
 }
 function focusInsert(trackId,slot,rootId,instance){
  const panel=document.getElementById(rootId);
  // Preview only: exact local insert identity, not a stale occupied slot.
  return !!instance&&isVisible(panel)&&panel.dataset.fxTrackId===String(trackId)&&
    panel.dataset.fxSlot===String(slot)&&panel.dataset.fxPreviewInstance===String(instance)&&raise(panel);
 }
 function openedPreview(panel,instance){
  if(!panel)return false;
  panel.dataset.fxPreviewInstance=String(instance||'');
  return raise(panel);
 }
 function closeActive(){
  const panel=activeEditor();
  if(!panel)return false;
  const key=owners['#'+panel.id];
  const host=window[key];
  if(typeof host?.close!=='function')return false;
  host.close(); // original close lifecycle, never remove/alter an audio insert
  if(panel.hidden){delete panel.dataset.fxActive;paintOrder();}
  return panel.hidden;
 }
 // Capture all editor interactions BEFORE panel-specific drag/knob handlers.
 // Neither pointerdown nor focusin is cancelled or redirected.
 document.addEventListener('pointerdown',event=>{
  if(event.isTrusted&&event.button===0&&event.isPrimary!==false)
   raise(event.target?.closest?.(rootSelector));
 },true);
 document.addEventListener('focusin',event=>{
  if(event.isTrusted)raise(event.target?.closest?.(rootSelector));
 },true);
 window.addEventListener('keydown',event=>{
  if(event.key!=='Escape'||event.repeat||event.defaultPrevented||
     event.altKey||event.ctrlKey||event.metaKey||event.shiftKey||activeModal())return;
  const panel=activeEditor();
  // Let the editor's own preset form/menu consume Escape before the editor.
  // Native menus and dialogs retain their established close behavior.
  if(document.querySelector('.fx-drag-ghost'))return; // rack drag cancellation has precedence
  if(window.__ARMIX_NATIVE_INLINE_FX_CLIP__?.cancelDrag?.()){
   event.preventDefault();event.stopImmediatePropagation();return;
  }
  if(!panel)return;
  const popup=popupIn(panel);
  if(popup){
   // Use the existing cancel lifecycle. Menus have no pending edit; hiding
   // just their menu never affects the accepted DSP or the editor identity.
   const cancel=popup.querySelector('.mx-fxp-cancel,#mxHPresetCancel,.mx-ducker-cancel');
   if(cancel)cancel.click();
   else if(popup.matches('.mx-fxp-menu,#mxHPresetMenu'))popup.hidden=true;
   event.preventDefault();event.stopImmediatePropagation();return;
  }
  // The capture-phase window manager must yield to an actively edited
  // BPM field's existing target-level Escape handler. It restores the
  // accepted value and blurs without issuing an audio setting change.
  const local=event.target?.closest?.('.vt-clock-screen[data-editing="true"] #vtBpm');
  if(local&&panel.contains(local))return;
  if(closeActive()){event.preventDefault();event.stopImmediatePropagation();}
 },true);
 const center=el=>{
  if(!isVisible(el))return false;
  // The native mixer has ONE overlay geometry owner. The old Pages
  // recenter writes fixed/transform and would fight its cropped pan offset.
  // Keep the unchanged public Pages recenter when there is no native host.
  if(window.__ARMIX_NATIVE_INLINE_FX_CLIP__?.center)
   return window.__ARMIX_NATIVE_INLINE_FX_CLIP__.center(el);
  const rect=el.getBoundingClientRect();
  const w=document.documentElement.clientWidth||innerWidth;
  const h=document.documentElement.clientHeight||innerHeight;
  const x=Math.max(0,Math.round((w-rect.width)/2));
  const y=Math.max(0,Math.round((h-rect.height)/2));
  el.style.setProperty('position','fixed','important');
  el.style.setProperty('right','auto','important');
  el.style.setProperty('bottom','auto','important');
  // GEQ S/M/L keeps the SAME SVG/PNG hardware panel at 50/75/100%.
  // Never clear its transform when the operator rescues an FX from rack:
  // that silently returned S/M to L and invalidated the pointer geometry.
  const geqScale=el.id==='mxNativeGeq31'?Number(el.dataset.fxScale):null;
  if([0.5,0.75,1].includes(geqScale)){
   el.style.setProperty('transform-origin','top left','important');
   el.style.setProperty('transform','scale('+geqScale+')','important');
  }else el.style.setProperty('transform','none','important');
  el.style.setProperty('left',x+'px','important');
  el.style.setProperty('top',y+'px','important');
  el.dataset.fxRecovered='center';
  return true;
 };
 const visibleEditors=()=>selectors.map((s,i)=>({
  el:document.querySelector(s),index:i
 })).filter(x=>isVisible(x.el)).sort((a,b)=>{
  const z=el=>Number.parseInt(getComputedStyle(el).zIndex,10)||0;
  return z(a.el)-z(b.el)||a.index-b.index;
 });
 document.addEventListener('dblclick',event=>{
  const target=event.target;
  if(!target?.closest?.(
    '.fxslot-row,.fx-section > .subhead,.section-rail .rail-cell.fx'
  ))return;
  const windows=visibleEditors();
  const row=target.closest('.fxslot-row');
  const trackId=row?.closest('.strip')?.dataset.id;
  const slot=row?.querySelector('[data-slot]')?.dataset.slot;
  // The double-clicked INSERT must recover *its own* editor, not the
  // last DOM child when several modeless built-in editors are open.
  const matched=trackId!==undefined&&slot!==undefined?
   windows.filter(item=>item.el.dataset.fxTrackId===String(trackId)&&
     item.el.dataset.fxSlot===String(slot)):[];
  // Notify the one installed native bridge of the exact insert. Its own
  // ChannelId/slot/instanceId lease verification decides whether to reopen a
  // VST3 HWND. The public /preview/ has no native listener or DSP side effect.
  if(row&&trackId!==undefined&&slot!==undefined){
   document.dispatchEvent(new CustomEvent('arMixFxRackDoubleClick',{
    detail:{trackId:String(trackId),slot:Number(slot)}
   }));
   // Native rack dispatch verifies ChannelId/slot/instance and alone
   // positions its editor. Do not run a second, legacy center afterward.
   if(window.__ARMIX_NATIVE_INLINE_FX_CLIP__)return;
  }
  // A row must NEVER recenter an unrelated open built-in editor. The header
  // shortcut may still rescue the top visible modeless editor.
  const focus=(row?matched:windows).at(-1);
  if(focus){raise(focus.el);center(focus.el);}
 });
 window.ArSonMixFxWorkspace=Object.freeze({
  raise,focusInsert,openedPreview,activeEditor,closeActive,
  centerVisible:()=>{const panel=activeEditor();return panel?center(panel):false;},
  recenter:center,
  centerInsert:(trackId,slot)=>{
   const matches=visibleEditors().filter(item=>
    item.el.dataset.fxTrackId===String(trackId)&&
    item.el.dataset.fxSlot===String(slot));
   return matches.length?center(matches.at(-1).el):false;
  }
 });
})();
