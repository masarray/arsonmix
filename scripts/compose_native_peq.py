"""One additive native Parametric EQ injection into approved /preview/ assembler.
No alternate GUI, pseudo FX row, fixed pre-VST DSP bank or demo audio engine.
An accepted EQ uses the same rack slot, ChannelId, and FX control as VST3.
"""
CATALOG="armix.builtin.channel-eq.v1"
SCRIPT='<script defer src="assets/mixer-native-peq.js?v=eq-graph1"></script>'
COMP_SCRIPT='<script defer src="assets/mixer-native-compressor.js?v=comp-1"></script>'
HARMONY_SCRIPT='<script defer src="assets/mixer-native-harmony.js?v=harmony1"></script>'
THROW_SCRIPT='<script defer src="assets/mixer-native-vocal-throw.js?v=throw-1"></script>'
PRESET_SCRIPT='<script defer src="assets/mixer-builtin-fx-preset-bar.js?v=preset-shared-m4"></script>'

PICKER_HEAD="function renderFastPicker(){"
PICKER_FN="""function insertChosenNativeEq(){
 const t=tracks.concat(master).find(x=>x.id===activeFxTarget?.channelId);
 const slot=Number(activeFxTarget?.slot);
 if(!t||!Number.isInteger(slot)||slot<0||slot>=8){
   say('Parametric EQ · choose an empty FX insert');return false;
 }
 if(window.__JUCE__?.backend?.emitEvent){
   const host=window.ArSonMixNativeEq;
   if(!host?.insert){
     say('Native FX host not ready · audio unchanged');return false;
   }
   host.insert(activeFxTarget);
   // The existing native ACK + subsequent accepted snapshot will replace the
   // empty slot. Do not optimistically draw an ACTIVE effect or open editor.
   return true;
 }
 if(t.fx[slot]!=='None'){
   say('FX slot already occupied · choose an empty insert');return false;
 }
 t.fx[slot]='Parametric EQ';t.slotBypass[slot]=false;
 t.fxInsertIds[slot]={id:'visual-eq-'+t.id+'-'+slot,name:'Parametric EQ'};
 refreshStrip(t);setFastPicker(false);
 say('Parametric EQ · browser visual preview ONLY · no audio DSP');
 return true;
}
function insertChosenNativeCompressor(){
 const t=tracks.concat(master).find(x=>x.id===activeFxTarget?.channelId);
 const slot=Number(activeFxTarget?.slot);
 if(!t||!Number.isInteger(slot)||slot<0||slot>=8){
  say('Compressor · choose an empty FX insert');return false;
 }
 if(window.__JUCE__?.backend?.emitEvent){
  const host=window.ArSonMixNativeCompressor;
  if(!host?.insert){say('Native Compressor host unavailable');return false;}
  host.insert(activeFxTarget);return true;
 }
 if(t.fx[slot]!=='None'){
  say('FX slot already occupied · choose an empty insert');return false;
 }
 t.fx[slot]='Compressor';t.slotBypass[slot]=false;
 t.fxInsertIds[slot]={id:'visual-comp-'+t.id+'-'+slot,name:'Compressor'};
 refreshStrip(t);setFastPicker(false);
 say('Compressor · browser preview ONLY · no audio DSP');return true;
}
function insertChosenNativeHarmony(){
 const t=tracks.concat(master).find(x=>x.id===activeFxTarget?.channelId);
 const slot=Number(activeFxTarget?.slot);
 if(!t||!Number.isInteger(slot)||slot<0||slot>=8){
   say('Harmonice · choose an empty FX insert');return false;
 }
 if(window.__JUCE__?.backend?.emitEvent){
   if(!window.ArSonMixNativeHarmony?.insert){
     say('Native Harmony host not ready · audio unchanged');return false;
   }
   window.ArSonMixNativeHarmony.insert(activeFxTarget);
   return true;
 }
 if(t.fx[slot]!=='None'){
   say('FX slot already occupied');return false;
 }
 t.fx[slot]='Harmonice';t.slotBypass[slot]=false;
 t.fxInsertIds[slot]={id:'visual-harmony-'+t.id+'-'+slot,name:'Harmonice'};
 refreshStrip(t);setFastPicker(false);
 say('Harmonice · browser preview ONLY · no DSP');
 return true;
}
function insertChosenNativeThrow(){
 const t=tracks.concat(master).find(x=>x.id===activeFxTarget?.channelId);
 const slot=Number(activeFxTarget?.slot);
 if(!t||!Number.isInteger(slot)||slot<0||slot>=8){
  say('Vocal Throw · choose an empty FX insert');return false;
 }
 if(window.__JUCE__?.backend?.emitEvent){
  const host=window.ArSonMixNativeVocalThrow;
  if(!host?.insert){say('Native Vocal Throw host unavailable');return false;}
  host.insert(activeFxTarget);return true;
 }
 if(t.fx[slot]!=='None'){
  say('FX slot already occupied · choose an empty insert');return false;
 }
 t.fx[slot]='ArSon Vocal Throw';t.slotBypass[slot]=false;
 t.fxInsertIds[slot]={id:'visual-throw-'+t.id+'-'+slot,name:'ArSon Vocal Throw'};
 refreshStrip(t);setFastPicker(false);
 say('Vocal Throw · browser preview ONLY · no audio DSP');return true;
}
function renderFastPicker(){"""
PICKER_RENDER="plugEl('fastResults').innerHTML=list.length?list.map(p=>"
PICKER_RENDER_NEW="""const eqBuiltin=!q||['parametric eq','peq','channel eq','built-in fx'].some(n=>n.includes(q))?
 '<button class="fast-effect" type="button" data-fast-native-eq="1"><span class="fast-effect-title">Parametric EQ</span><span class="fast-effect-kind">Native · 8-band · ordered insert</span></button>':'';
 const compBuiltin=!q||['compressor','comp','dynamics','built-in fx'].some(n=>n.includes(q))?
 '<button class="fast-effect" type="button" data-fast-native-comp="1"><span class="fast-effect-title">Compressor</span><span class="fast-effect-kind">Native · 0-sample added latency · dynamics</span></button>':'';
 const harmonyBuiltin=!q||['harmonice','arson harmony','harmony','vocal harmony','built-in fx'].some(n=>n.includes(q))?
 '<button class="fast-effect" type="button" data-fast-native-harmony="1"><span class="fast-effect-title">Harmonice</span><span class="fast-effect-kind">Native · 3–5 voices · live pitch</span></button>':'';
 const throwBuiltin=!q||['vocal throw','throw','delay','echo','phrase','concert','built-in fx'].some(n=>n.includes(q))?
 '<button class="fast-effect" type="button" data-fast-native-throw="1"><span class="fast-effect-title">ArSon Vocal Throw</span><span class="fast-effect-kind">Native · phrase-aware concert echo</span></button>':'';
 const builtin=(eqBuiltin||compBuiltin||harmonyBuiltin||throwBuiltin?'<div class="fast-fx-category">BUILT-IN FX</div>':'')+eqBuiltin+compBuiltin+harmonyBuiltin+throwBuiltin;
 plugEl('fastResults').innerHTML=builtin+'<div class="fast-fx-category">VST3 PLUGINS</div>'+(list.length?list.map(p=>"""
PICKER_END=""").join(''):'<div class="fast-empty">No matching effects</div>';"""
PICKER_END_NEW=""").join(''):'<div class="fast-empty">No matching effects</div>');"""
PICKER_CLICK=" const btn=e.target.closest('[data-fast-id]');if(btn)insertCatalogPlugin(btn.dataset.fastId);"
PICKER_CLICK_NEW=""" if(e.target.closest('[data-fast-native-harmony]')){insertChosenNativeHarmony();return;}
 if(e.target.closest('[data-fast-native-eq]')){insertChosenNativeEq();return;}
 if(e.target.closest('[data-fast-native-comp]')){insertChosenNativeCompressor();return;}
 if(e.target.closest('[data-fast-native-throw]')){insertChosenNativeThrow();return;}
 const btn=e.target.closest('[data-fast-id]');if(btn)insertCatalogPlugin(btn.dataset.fastId);"""
PICKER_ENTER="if(b){e.preventDefault();insertCatalogPlugin(b.dataset.fastId);}"
PICKER_ENTER_NEW="if(b){e.preventDefault();if(b.hasAttribute('data-fast-native-harmony'))insertChosenNativeHarmony();else if(b.hasAttribute('data-fast-native-throw'))insertChosenNativeThrow();else if(b.hasAttribute('data-fast-native-eq'))insertChosenNativeEq();else if(b.hasAttribute('data-fast-native-comp'))insertChosenNativeCompressor();else insertCatalogPlugin(b.dataset.fastId);}"
OPEN=" if(a==='openPlugin'){openFastPicker(t,Number(btn.dataset.slot),btn);return;}"
OPEN_NEW=""" if(a==='openPlugin'){
  const index=Number(btn.dataset.slot);
  if(t.fx[index]==='Parametric EQ'){
   window.ArSonMixNativeEq?.open(t,index);return;
  }
  if(t.fx[index]==='Compressor'){
   window.ArSonMixNativeCompressor?.open(t,index);return;
  }
  if(t.fx[index]==='Harmonice'||t.fx[index]==='ArSon Harmony'){
   window.ArSonMixNativeHarmony?.open(t,index);return;
  }
  if(t.fx[index]==='ArSon Vocal Throw'){
   window.ArSonMixNativeVocalThrow?.open(t,index);return;
  }
  openFastPicker(t,index,btn);return;
 }"""
def apply_native_peq(html: str)->str:
    anchors=((PICKER_HEAD,PICKER_FN),
             (PICKER_RENDER,PICKER_RENDER_NEW),
             (PICKER_END,PICKER_END_NEW),
             (PICKER_CLICK,PICKER_CLICK_NEW),
             (PICKER_ENTER,PICKER_ENTER_NEW),
             (OPEN,OPEN_NEW))
    if html.count('</head>')!=1 or html.count('</body>')!=1 or \
       SCRIPT in html or COMP_SCRIPT in html or HARMONY_SCRIPT in html or THROW_SCRIPT in html or PRESET_SCRIPT in html or 'insertChosenNativeEq' in html:
        raise ValueError("Parametric EQ requires one pristine approved compiled GUI")
    for before,after in anchors:
        if html.count(before)!=1:
            raise ValueError('Ambiguous approved picker/editor anchor: '+before[:90])
        html=html.replace(before,after,1)
    html=html.replace('</body>',PRESET_SCRIPT+'\n'+SCRIPT+'\n'+COMP_SCRIPT+'\n'+HARMONY_SCRIPT+'\n'+THROW_SCRIPT+'\n</body>',1)
    return html
