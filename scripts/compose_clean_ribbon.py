"""Compile owner-approved ribbon into existing Pages/native CSS owner."""
from pathlib import Path

def apply_clean_ribbon(html: str, assets: Path) -> str:
    # Replace only the two historical bank vectors at assembly time. The
    # original button IDs, event handlers and approved source remain intact.
    for old, new in (("M14,17L9,12l5-5V17Z", "M15 5L8 12l7 7"),
                     ("M10,17V7l5,5l-5,5Z", "M9 5l7 7-7 7")):
        anchor = '<path d="' + old + '"/>'
        if html.count(anchor) != 1:
            raise ValueError("Clean Ribbon V6 bank glyph anchor drift: " + old)
        html = html.replace(anchor, '<path d="' + new + '"/>', 1)
    # Matrix gestures in the deployed copy only. Locked reference source
    # stays untouched, and native assembly still intercepts wireGraphCommit.
    matrix_rewrites = (
        ("const targets=all.filter(wireWsMatches);",
         "const targets=[...all,master].filter(wireWsMatches);"),
        ("permitted=!self&&wireWsCanJoin(src.id,dst.id),",
         "permitted=!self&&(dst.kind==='master'||wireWsCanJoin(src.id,dst.id)),"),
        ("action=info?'data-ws-inspect':'data-ws-join',",
         "action='data-ws-crosspoint',"),
        ("(info?'Inspect ':'Connect ')+wireWsLabel(src.id)",
         "(info?'Disconnect ':'Connect ')+wireWsLabel(src.id)"),
        (" const inspect=e.target.closest?.('[data-ws-inspect]');",
         """ const crosspoint=e.target.closest?.('[data-ws-crosspoint]');
 if(crosspoint){
  const parts=crosspoint.dataset.wsCrosspoint.split(':').map(Number);
  if(parts.length!==2||parts.some(x=>!Number.isInteger(x)))return;
  const [from,to]=parts,src=wireSourceTrack(from),dst=to===master.id?master:wireSourceTrack(to);
  if(!src||!dst||from===to)return;
  const info=wireWsRouteKind(from,to,wireWsRoutes());
  const before=wireWsSnapshot();
  if(info?.edge){
   const edges=wireEdges.filter(x=>x.enabled&&x.sourceId===from&&x.destId===to);
   if(edges.length!==1){wireWsInspect={src:from,dst:to};renderWireWsInspector();return;}
   if(replugWireEdge(from,to,null))wireWsRecord(before);
  }else if(!info&&(dst.kind==='master'||wireWsCanJoin(from,to))){
   const op={type:'CONNECT',sourceId:from,destId:to,...(dst.kind==='master'?{}:{mode:'send'})};
   if(wireGraphCommit([op])){wireWsRecord(before);render();}
  }else if(info){wireWsInspect={src:from,dst:to};renderWireWsInspector();}
  renderWireWorkspace();return;
 }
 const inspect=e.target.closest?.('[data-ws-inspect]');"""),
        ("document.getElementById('wireWsSearch').addEventListener('input',e=>{",
         """document.getElementById('wireWorkspace').addEventListener('contextmenu',e=>{
 const cell=e.target.closest?.('[data-ws-crosspoint]');
 if(!cell)return;
 const [src,dst]=cell.dataset.wsCrosspoint.split(':').map(Number);
 if(!wireWsRouteKind(src,dst,wireWsRoutes()))return;
 e.preventDefault();wireWsInspect={src,dst};wireWsRemoveConfirm=null;renderWireWsInspector();
});
document.getElementById('wireWsSearch').addEventListener('input',e=>{"""),
    )
    for old, new in matrix_rewrites:
        if html.count(old) != 1:
            raise ValueError("Matrix operator anchor drift: " + old[:80])
        html = html.replace(old, new, 1)
    # Navigating a category is not an unrelated mixer command. Keep the
    # pinned OUT through the cascade until a real action or click-away;
    # scope the exemption to actual category buttons, not the whole menu.
    old = "#moreTools > summary,[data-wire-open],#routeChoice"
    new = "#moreTools > summary,#moreTools [data-menu-category],[data-wire-open],#routeChoice"
    if html.count(old) != 1:
        raise ValueError("Clean Ribbon V6 WIRE navigation anchor drift")
    html = html.replace(old, new, 1)
    ribbon_css = Path(__file__).with_name("clean_ribbon_v6.css").read_text(encoding="utf-8")
    chrome = assets / "mixer-chrome-material.css"
    chrome.write_text(chrome.read_text(encoding="utf-8")+"\n"+ribbon_css+"\n",encoding="utf-8")
    tag = '<script defer src="assets/clean-ribbon-v6.js"></script>'
    if html.count("</body>") != 1 or tag in html:
        raise ValueError("Clean Ribbon V6 assembly anchor drift")
    return html.replace("</body>",tag+"\n</body>",1)
