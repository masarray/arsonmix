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
    <div class="ap-statusbar"><div id="playerFeedback" class="ap-feedback" role="status" aria-live="polite">Add music to begin</div><button id="playerChooseOutput" type="button" hidden>Choose output</button></div>
    <div class="ap-listhead"><strong>Setlist</strong><span id="playerCount" class="ap-sub">0 songs</span><button id="playerAdd" type="button">+ Add Music</button></div>
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
