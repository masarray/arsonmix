"""Deterministic additive Built-in EQ in the ONE approved ArSonMix /preview/.

Never mutate frozen V28 HTML, VST3 catalog, or create another renderer.
This P0 adapter composes the fixed PRE-VST3 native EQ into the existing rack.
"""
SCRIPT = '<script defer src="assets/mixer-builtin-eq.js?v=builtin-eq-native-2"></script>'
HEADER_BEFORE = '</span><span class="push"></span><button class="mx-util-key"'
HEADER_AFTER = """</span>'+(t.kind==='master'?'':'<button type="button" class="mx-eq-open" data-action="openBuiltinEq" title="Open lightweight built-in channel EQ" aria-label="Open EQ for '+escapeHtml(t.name)+'">EQ</button>')+'<span class="push"></span><button class="mx-util-key" """
HEADER_AFTER = HEADER_AFTER.rstrip()
CLICK_BEFORE = " if(a==='openPlugin'){openFastPicker(t,Number(btn.dataset.slot),btn);return;}"
CLICK_AFTER = """ if(a==='openBuiltinEq'){window.ArSonMixBuiltinEq?.open(t);return;}
 if(a==='builtinEqPower'){window.ArSonMixBuiltinEq?.power(t);return;}
 if(a==='builtinEqRemove'){window.ArSonMixBuiltinEq?.remove(t);return;}"""
RACK_START = 'function sectionFx(t){'
RACK_HOOK = """window.__ARMIX_BUILTIN_EQ_REFRESH__=function(uiId){
 const t=tracks.concat(master).find(x=>x.id===Number(uiId));
 if(t)refreshStrip(t);
};
window.__ARMIX_BUILTIN_EQ_FIND_CHANNEL__=function(nativeChannelId){
 return tracks.concat(master).find(t=>
    window.__ARMIX_BUILTIN_EQ_CHANNEL__?.resolve?.(t.id)===nativeChannelId
 )||null;
};
function sectionFx(t){"""
RACK_ROWS = " for(const {name,i} of rows){"
RACK_ROWS_HOOK = """ out+=window.ArSonMixBuiltinEq?.slotMarkup?.(t,materialIcon)||'';
 for(const {name,i} of rows){"""
PICKER_START = " plugEl('fastResults').innerHTML=list.length?list.map(p=>"
PICKER_HOOK = """ const builtin=(!q||'channel eq built-in fx'.includes(q))?
  '<div class="fast-fx-category">BUILT-IN FX</div><button class="fast-effect" type="button" data-fast-builtin-eq="1"><span class="fast-effect-title">Channel EQ</span><span class="fast-effect-kind">Native · PRE VST3 · zero extra buffer</span></button>':'';
 plugEl('fastResults').innerHTML=builtin+'<div class="fast-fx-category">VST3 PLUGINS</div>'+(list.length?list.map(p=>"""
PICKER_END = """).join(''):'<div class="fast-empty">No matching effects</div>';"""
PICKER_END_HOOK = """).join(''):'<div class="fast-empty">No matching effects</div>');"""
PICKER_CLICK = " const btn=e.target.closest('[data-fast-id]');if(btn)insertCatalogPlugin(btn.dataset.fastId);"
PICKER_CLICK_HOOK = """ const eq=e.target.closest('[data-fast-builtin-eq]');
 if(eq){
  const t=tracks.concat(master).find(x=>x.id===activeFxTarget?.channelId);
  if(t)window.ArSonMixBuiltinEq?.insert(t);
  setFastPicker(false);return;
 }
 const btn=e.target.closest('[data-fast-id]');if(btn)insertCatalogPlugin(btn.dataset.fastId);"""

def compose_builtin_eq(source: str) -> str:
    anchors=(HEADER_BEFORE,CLICK_BEFORE,RACK_START,RACK_ROWS,
             PICKER_START,PICKER_END,PICKER_CLICK)
    if source.count('</head>')!=1 or SCRIPT in source or 'openBuiltinEq' in source:
        raise ValueError('Built-in EQ expects pristine approved HTML seed')
    if any(source.count(a)!=1 for a in anchors):
        raise ValueError('Built-in EQ requires exactly one canonical rack/picker owner')
    return (source.replace('</head>',SCRIPT+'\n</head>',1)
                  .replace(HEADER_BEFORE,HEADER_AFTER,1)
                  .replace(RACK_START,RACK_HOOK,1)
                  .replace(RACK_ROWS,RACK_ROWS_HOOK,1)
                  .replace(PICKER_START,PICKER_HOOK,1)
                  .replace(PICKER_END,PICKER_END_HOOK,1)
                  .replace(PICKER_CLICK,PICKER_CLICK_HOOK,1)
                  .replace(CLICK_BEFORE,CLICK_AFTER+'\n'+CLICK_BEFORE,1))
