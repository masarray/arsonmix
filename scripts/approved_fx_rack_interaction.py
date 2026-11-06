"""One shared assembled-GUI interaction owner; historical V28 seed stays intact."""
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def apply_fx_rack_interaction(html):
    start=html.index('function paintMainWireFragment(strip,edge,isMain){')
    end=html.index('\nfunction syncMainWireFragments()',start)
    html=html[:start]+'''function paintMainWireFragment(strip,edge,isMain){
 strip?.querySelectorAll('.wire-main-fragment').forEach(layer=>layer.remove());
}'''+html[end:]
    anchor='function moveReferenceFx(t,from,to){\n'
    assert html.count(anchor)==1
    html=html.replace(anchor,anchor+''' if(window.__JUCE__?.backend){
  ensureFxInsertIds(t);
  return !!window.ArSonFxRackAuthority?.move?.(t.id,from,t.id,to,t.fxInsertIds[from]?.id);
 }
''',1)
    anchor='function undoReferenceFx(t){\n'
    assert html.count(anchor)==1
    html=html.replace(anchor,anchor+' if(undoFxTransfer(t))return true;\n',1)
    before="const i=Number(btn.dataset.slot);t.fx[i]='None';t.slotBypass[i]=false;if(t.fxInsertIds)t.fxInsertIds[i]=null;fxReorderUndo=null;refreshStrip(t);"
    assert html.count(before)==1
    html=html.replace(before,"const i=Number(btn.dataset.slot);removeFxReference(t,i);",1)
    anchor='restorePluginPreferences();init();\n})();'
    assert html.count(anchor)==1
    return html.replace(anchor,(ROOT/'approved_fx_rack_interaction.js').read_text()+'\n'+anchor,1)
