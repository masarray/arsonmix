"""One additive native Parametric EQ injection into approved /preview/ assembler.
No alternate GUI, pseudo FX row, fixed pre-VST DSP bank or demo audio engine.
An accepted EQ uses the same rack slot, ChannelId, and FX control as VST3.
"""
CATALOG="armix.builtin.channel-eq.v1"
SCRIPT='<script defer src="assets/mixer-native-peq.js?v=eq-graph1"></script>'
PICKER_HEAD="function renderFastPicker(){"
PICKER_FN="""function insertChosenNativeEq(){
 const t=tracks.concat(master).find(x=>x.id===activeFxTarget?.channelId);
 const slot=Number(activeFxTarget?.slot);
 if(!t||!Number.isInteger(slot)||slot<0||slot>=8){
   say('Parametric EQ · choose an empty FX insert');return false;
 }
 if(window.__JUCE__?.backend?.emitEvent){
   const bridge=window.__ARMIX_NATIVE_M2__;
   if(!bridge?.insertPlugin){
     say('Native FX host not ready · audio unchanged');return false;
   }
   bridge.insertPlugin('armix.builtin.channel-eq.v1',activeFxTarget);
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
function renderFastPicker(){"""
PICKER_RENDER="plugEl('fastResults').innerHTML=list.length?list.map(p=>"
PICKER_RENDER_NEW="""const builtin=!q||['parametric eq','peq','channel eq','built-in fx'].some(n=>n.includes(q))?
 '<div class="fast-fx-category">BUILT-IN FX</div><button class="fast-effect" type="button" data-fast-native-eq="1"><span class="fast-effect-title">Parametric EQ</span><span class="fast-effect-kind">Native · 8-band · ordered insert</span></button>':'';
 plugEl('fastResults').innerHTML=builtin+'<div class="fast-fx-category">VST3 PLUGINS</div>'+(list.length?list.map(p=>"""
PICKER_END=""").join(''):'<div class="fast-empty">No matching effects</div>';"""
PICKER_END_NEW=""").join(''):'<div class="fast-empty">No matching effects</div>');"""
PICKER_CLICK=" const btn=e.target.closest('[data-fast-id]');if(btn)insertCatalogPlugin(btn.dataset.fastId);"
PICKER_CLICK_NEW=""" if(e.target.closest('[data-fast-native-eq]')){insertChosenNativeEq();return;}
 const btn=e.target.closest('[data-fast-id]');if(btn)insertCatalogPlugin(btn.dataset.fastId);"""
PICKER_ENTER="if(b){e.preventDefault();insertCatalogPlugin(b.dataset.fastId);}"
PICKER_ENTER_NEW="if(b){e.preventDefault();if(b.hasAttribute('data-fast-native-eq'))insertChosenNativeEq();else insertCatalogPlugin(b.dataset.fastId);}"
OPEN=" if(a==='openPlugin'){openFastPicker(t,Number(btn.dataset.slot),btn);return;}"
OPEN_NEW=""" if(a==='openPlugin'){
  const index=Number(btn.dataset.slot);
  if(t.fx[index]==='Parametric EQ'){
   window.ArSonMixNativeEq?.open(t,index);return;
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
       SCRIPT in html or 'insertChosenNativeEq' in html:
        raise ValueError("Parametric EQ requires one pristine approved compiled GUI")
    for before,after in anchors:
        if html.count(before)!=1:
            raise ValueError('Ambiguous approved picker/editor anchor: '+before[:90])
        html=html.replace(before,after,1)
    html=html.replace('</body>',SCRIPT+'\n</body>',1)
    return html
