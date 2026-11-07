"""Single official /preview/ FX interaction composer; historical V28 stays immutable."""
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def once(source, before, after, label):
    if source.count(before)!=1:
        raise ValueError("FX composer anchor drift: "+label)
    return source.replace(before,after,1)

def apply_fx_rack_interaction(html, assets=None):
    start=html.index('function paintMainWireFragment(strip,edge,isMain){')
    end=html.index('\nfunction syncMainWireFragments()',start)
    html=html[:start]+'''function paintMainWireFragment(strip,edge,isMain){
 strip?.querySelectorAll('.wire-main-fragment').forEach(layer=>layer.remove());
}'''+html[end:]
    anchor='function moveReferenceFx(t,from,to){\n'
    html=once(html,anchor,anchor+''' if(window.__JUCE__?.backend){
  ensureFxInsertIds(t);
  return !!window.ArSonFxRackAuthority?.move?.(t.id,from,t.id,to,t.fxInsertIds[from]?.id);
 }
''',"native MOVE_INSERT bridge")
    html=once(html,
        "document.querySelector('.strip[data-id=\"'+t.id+'\"] .fx-grip[data-insert-id=\"'+id+'\"]')?.focus();",
        "document.querySelector('.strip[data-id=\"'+t.id+'\"] .fxslot-row[data-insert-id=\"'+id+'\"] .fx-pick')?.focus({preventScroll:true});",
        "preserve keyboard focus after handle-free reorder")
    html=once(html,'Ctrl+Z on grip to undo','Ctrl+Z on FX name to undo',
              "retire grip keyboard tooltip")
    anchor='function undoReferenceFx(t){\n'
    html=once(html,anchor,anchor+' if(undoFxTransfer(t))return true;\n',"transfer undo")
    before="const i=Number(btn.dataset.slot);t.fx[i]='None';t.slotBypass[i]=false;if(t.fxInsertIds)t.fxInsertIds[i]=null;fxReorderUndo=null;refreshStrip(t);"
    html=once(html,before,"const i=Number(btn.dataset.slot);removeFxReference(t,i);","remove compactly")

    # The whole occupied FX slot is the drag surface. Power and Delete keep
    # their native click semantics; there is no extra grab handle or DnD owner.
    grip_start="if(name!=='None')out+='<button type=\"button\" class=\"grip fx-grip\""
    grip_end='<button class="mx-util-key" data-util-kind="fx-remove"'
    if html.count(grip_start)!=1:
        raise ValueError("FX grip markup drift")
    a=html.index(grip_start)
    b=html.index(grip_end,a)
    html=html[:a]+"if(name!=='None')out+='"+html[b:]
    a=html.index("/* FX V53: the focused 16px grip alone owns drag;")
    b=html.index("document.querySelector('.console-body').addEventListener('pointerover'",a)
    html=html[:a]+'''/* Native and Pages share one row-wide pointer gesture (see assembled JS).
   Keyboard move/undo lives on the existing, focusable plugin-name button. */
consoleBody.addEventListener('keydown',e=>{
 const button=e.target.closest?.('.fx-pick:not(.empty)');
 if(!button)return;
 const t=findTrack(button),from=Number(button.dataset.slot);
 if(!t)return;
 if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='z'){
  e.preventDefault();undoReferenceFx(t);return;
 }
 if(e.altKey&&!e.ctrlKey&&!e.metaKey&&['ArrowUp','ArrowDown'].includes(e.key)){
  e.preventDefault();
  const used=t.fx.map((name,i)=>name==='None'?-1:i).filter(i=>i>=0);
  const current=used.indexOf(from),next=used[current+(e.key==='ArrowUp'?-1:1)];
  if(Number.isInteger(next)&&!moveReferenceFx(t,from,next))say('FX move unavailable');
 }
},true);
'''+html[b:]
    anchor='restorePluginPreferences();init();\n})();'
    html=once(html,anchor,(ROOT/'approved_fx_rack_interaction.js').read_text()+'\n'+anchor,"pointer drag composer")
    if assets is not None:
        sheet=Path(assets)/'mixer-fx-rack.css'
        css=sheet.read_text(encoding='utf-8')
        a=css.index('.fxslot-row .grip{')
        b=css.index('@media(max-height:750px)',a)
        css=css[:a]+css[b:]
        css=once(css,
          '.console-body .strip .fxslot-row .grip .mi-svg{width:13px;height:14px;pointer-events:none}\n\n.console-body .fx-section .grip{display:inline-flex;align-items:center;justify-content:center}\n.console-body .fx-section .grip .mi-svg{width:13px;height:15px}\n\n',
          '',"remove retired grip visual selectors")
        css=once(css,'.fx-grip{touch-action:none;}',
          '.fxslot-row:not(.is-empty){touch-action:none;}',"row pointer touch ownership")
        css=once(css,'.fxslot-row{cursor:default}',
          '.fxslot-row{cursor:default}\n'
          '.fxslot-row:not(.is-empty){cursor:grab;user-select:none}\n'
          '.fxslot-row:not(.is-empty):active{cursor:grabbing}\n'
          '.fxslot-row:not(.is-empty) .fx-pick{cursor:grab}',
          "row drag pointer affordance")
        sheet.write_text(css,encoding='utf-8')
    return html
