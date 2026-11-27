"""ArSonMix V29 operator UX composition for the ONE approved public/native console.

The V28 historical source is byte locked. Apply this intentional, audited
product evolution once to the deployed copy BEFORE Stage C CSS compilation.
This is not an alternate renderer or an extra late stylesheet.
"""
from __future__ import annotations
from pathlib import Path
import re

CLOSE_ICON = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" '
              'width="20" height="20" fill="currentColor" aria-hidden="true">'
              '<path d="M6.4,19L5,17.6L10.6,12L5,6.4L6.4,5L12,10.6L17.6,5L19,6.4L13.4,12L19,17.6L17.6,19L12,13.4L6.4,19Z"/>'
              '</svg>')

SPLIT_BOOTSTRAP = """
// One console layout authority. CSS percent flex tracks were resolving against
// differing ancestors during WebView2 first show, causing 48:52 and clipped
// faders. Measure the real console once at boot and on actual size/fold changes.
// No timer, PCM access, animation loop, or per-meter layout work.
(function installConsoleSplit(){
  const root=document.querySelector('.console-body');
  if(!root || root.__armixBalancedSplitInstalled)return;
  root.__armixBalancedSplitInstalled=true;
  let scheduled=false,previous='';
  function update(){
    scheduled=false;
    const rail=root.querySelector('.section-rail');
    if(!rail)return;
    const h=root.getBoundingClientRect().height;
    if(!(h>0))return;
    const height=selector=>rail.querySelector(selector)?.getBoundingClientRect().height||0;
    const pan=height('.rail-cell.pan'),
          switches=height('.rail-cell.switch'),
          footer=height('.rail-cell.footer'),
          identity=height('.rail-cell.identity'),
          route=height('.rail-cell.route'),
          send=height('.rail-cell.send');
    // V60 one authoritative layout allocator, not an overriding stylesheet.
    // Favor a taller physical fader by borrowing at most 48px of genuinely
    // spare FX space. Retain at least 125px for insert controls, and never
    // change the synchronized source/MASTER/rail mechanical alignment.
    const half=Math.round(h/2);
    const fxBase=Math.max(125,Math.round(half-identity-route-send));
    const boost=Math.min(48,Math.max(0,fxBase-125));
    const fader=Math.max(130,Math.round(half-pan-switches-footer+boost));
    const fx=fxBase-boost;
    const signature=fader+':'+fx;
    if(signature===previous)return;
    previous=signature;
    root.style.setProperty('--v13-fader-h',fader+'px','important');
    root.style.setProperty('--ux-fx-balanced-h',fx+'px','important');
  }
  function queue(){
    if(scheduled)return;
    scheduled=true;requestAnimationFrame(update);
  }
  update(); // before first paint, not after operator starts using the mixer
  if(window.ResizeObserver)new ResizeObserver(queue).observe(root);
  new MutationObserver(queue).observe(root,{attributes:true,attributeFilter:['class']});
  window.addEventListener('resize',queue,{passive:true});
})();
"""

PROJECT_MENU = """<details id="projectMenu" class="project-menu">
 <summary class="project-menu-trigger" aria-label="Main menu" title="Main menu">
  <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" aria-hidden="true"><path d="M5 6h14M5 12h14M5 18h14"/></svg>
 </summary>
 <div class="project-menu-panel" aria-label="Mixer menu">
  <div class="project-menu-label">PROJECT</div>
  <button type="button" class="project-menu-item mixer-file-tool" data-project-file="New" data-native-new-project="true" disabled title="Available in the Windows mixer"><img src="assets/mixer-new.svg" alt="" width="16" height="16">New</button>
  <button type="button" class="project-menu-item mixer-file-tool" data-project-file="Save" disabled title="Available in the Windows mixer"><img src="assets/mixer-save.svg" alt="" width="16" height="16">Save</button>
  <button type="button" class="project-menu-item mixer-file-tool" data-project-file="Open" disabled title="Available in the Windows mixer"><img src="assets/mixer-open.svg" alt="" width="16" height="16">Open</button>
  <div class="project-menu-label">MIXER</div>
  <button type="button" class="project-menu-item" data-project-action="wireNavigatorToggle"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" aria-hidden="true"><path d="M5 6h14M5 12h14M5 18h14"/><circle cx="5" cy="6" r="2"/><circle cx="12" cy="12" r="2"/><circle cx="19" cy="18" r="2"/></svg>Routes</button>
  <button type="button" class="project-menu-item" data-project-action="wireMatrixToggle"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" aria-hidden="true"><rect x="4" y="4" width="16" height="16" rx="2"/><path d="M4 12h16M12 4v16"/></svg>Routing matrix</button>
  <button type="button" class="project-menu-item" data-project-action="wireConfigToggle"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" aria-hidden="true"><path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="3"/><circle cx="15" cy="17" r="3"/></svg>WIRE setup</button>
  <button type="button" class="project-menu-item" data-project-action="audioIO" disabled title="Available in the Windows mixer"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" aria-hidden="true"><rect x="4" y="7" width="16" height="10" rx="2"/><circle cx="9" cy="12" r="2"/><path d="M15 12h2M8 4v3M16 4v3"/></svg>Audio I/O</button>
 </div>
 </details>"""

PROJECT_MENU_BOOTSTRAP = """
(function installProjectMenu(){
 const menu=document.getElementById('projectMenu');
 if(!menu)return;
 const trigger=menu.querySelector('summary');
 const close=()=>{menu.open=false;};
 menu.addEventListener('click',e=>{
  const item=e.target.closest('[data-project-action]');
  if(!item||item.disabled)return;
  const id=item.dataset.projectAction;
  const target=id==='audioIO'?document.querySelector('[data-native-io]'):document.getElementById(id);
  close();target?.click();
 });
 document.addEventListener('pointerdown',e=>{if(menu.open&&!menu.contains(e.target))close();},true);
 document.addEventListener('focusin',e=>{if(menu.open&&!menu.contains(e.target))close();},true);
 document.addEventListener('keydown',e=>{if(e.key==='Escape'&&menu.open){close();trigger.focus();e.preventDefault();}},true);
 menu.addEventListener('toggle',()=>trigger.setAttribute('aria-expanded',String(menu.open)));
})();
"""

OPERATOR_CSS = """
/* V60 physical-fader-priority console, same rail/strip/Master tokens.
   At very short heights preserve minimum usable controls and scroll vertically. */
.console-pane{overflow-y:auto!important;overflow-x:hidden!important}
.console-body{min-height:610px!important;
  --v13-fader-h:max(130px,calc(50% - var(--pan-h) - var(--switch-h) - var(--footer-h)))!important;
  --ux-fx-balanced-h:max(125px,calc(50% - var(--identity-h) - var(--route-h) - var(--send-h)));
}
.console-body:not(.collapsed-send):not(.collapsed-fx){min-height:680px!important}
.console-body.collapsed-route{--route-h:23px!important}
.console-body:not(.collapsed-fx) .strip .fx-section,
.console-body:not(.collapsed-fx) .section-rail>.rail-cell.fx{
  height:var(--ux-fx-balanced-h)!important;min-height:var(--ux-fx-balanced-h)!important;
  max-height:var(--ux-fx-balanced-h)!important;flex:0 0 var(--ux-fx-balanced-h)!important;
}
.mixer-title{display:none!important}
.window-title{gap:12px!important;overflow:hidden!important}
.window-title .file-name{min-width:0;overflow:hidden;text-overflow:ellipsis}
.window-title .right{margin-left:auto;min-width:0;max-width:48%;overflow:hidden;
  text-overflow:ellipsis;white-space:nowrap;color:#90b6c8;font-size:10px;font-weight:400}
/* Project commands are one left-anchored menu, not three permanent toolbar buttons. */
.app .command-bar .project-menu{position:relative;flex:0 0 auto;margin:0}
.project-menu-trigger{list-style:none;display:grid;place-items:center;width:30px;height:29px;
 border:1px solid #50616b;border-radius:4px;background:#263844;color:#d8e3e9;cursor:pointer}
.project-menu-trigger::-webkit-details-marker{display:none}
.project-menu-trigger:hover,.project-menu[open] .project-menu-trigger{background:#3a4c58}
.project-menu-panel{position:absolute;top:calc(100% + 6px);left:0;width:205px;
 max-height:calc(100vh - 90px);overflow-y:auto;padding:5px;background:#18262f;
 border:1px solid #536571;border-radius:6px;box-shadow:0 8px 25px #0007;
 display:flex;flex-direction:column;gap:1px}
.project-menu:not([open]) .project-menu-panel{display:none}
.project-menu-label{padding:7px 8px 3px;color:#8eacbc;font-size:9px;font-weight:500;letter-spacing:.08em}
.project-menu-label:not(:first-child){border-top:1px solid #53657155;margin-top:3px}
.project-menu-panel .project-menu-item{display:flex;align-items:center;gap:10px;
 width:100%;height:29px;padding:0 8px;border:0;border-radius:3px;
 background:transparent;color:#e0e8ed;text-align:left;font:400 12px Inter,Arial,sans-serif;cursor:pointer}
.project-menu-panel .project-menu-item img,.project-menu-panel .project-menu-item svg{
 width:16px;height:16px;flex:none;pointer-events:none}
.project-menu-panel .project-menu-item:hover:not(:disabled),.project-menu-panel .project-menu-item:focus-visible{background:#314752}
.project-menu-panel .project-menu-item:disabled{opacity:.42;cursor:not-allowed}
.app .command-bar:has(.project-menu[open]){z-index:1200!important}
.app .command-bar #mixContext{display:inline-flex!important;align-items:center;
 min-width:0;max-width:140px;overflow:hidden;white-space:nowrap;text-overflow:ellipsis;
 font-size:10px;color:#a9c1cf;letter-spacing:.025em;padding:0 4px}
.app .command-bar:has(#sofToggle[aria-pressed="true"]) #mixContext{color:#8fe1de}
@media(max-width:1200px){.app .command-bar #mixContext{max-width:88px}}
@media(max-width:760px){.app .command-bar #mixContext{display:none!important}}
.command-bar .mixer-file-tool img{display:block;flex:none;pointer-events:none}
.command-bar #densitySelect,.command-bar [for="densitySelect"],.command-bar #mode{display:none!important}
#channelIdentity .identity-head{cursor:move;touch-action:none}
#channelIdentity .identity-head button{cursor:pointer}
/* Modal Close chrome is solely owned by mixer-contextual-buttons.css. */
@media(max-width:1050px){
 .window-title .file-name{max-width:25vw}
 .window-title .right{max-width:57%;font-size:9px}
}
@media(max-width:680px){
 .window-title .file-name{display:none!important}
 .window-title .right{max-width:72%}
}
"""

def once(source: str, before: str, after: str, label: str) -> str:
    if source.count(before) != 1:
        raise ValueError("Operator UX anchor drift: " + label)
    return source.replace(before, after, 1)

def apply_operator_ux(html: str, assets: Path) -> str:
    """Evolve only the deployed HTML and two existing CSS owners, fail-closed."""
    html = once(html,
        '<header class="window-title"><span class="logo">ArSonMix</span><span class="file-name">Mixer · Reference Session</span><span class="study">OPERATOR-FIRST · SINGLE LIVE CONSOLE REFERENCE</span></header>',
        '<header class="window-title"><span class="logo">ArSonMix</span><span class="file-name">Mixer · Reference Session</span><span class="right" id="trackCount" aria-live="off">10 audio · 1 bus · master</span></header>',
        "compact title status")
    html = once(html,
        '<div class="command-bar" role="toolbar" aria-label="Live mixer controls">',
        '<div class="command-bar" role="toolbar" aria-label="Live mixer controls">'+PROJECT_MENU,
        "leftmost project menu")
    html = once(html, '</body>', '<script>'+PROJECT_MENU_BOOTSTRAP+'</script></body>',
        "project menu keyboard and pointer ownership")
    html = once(html, 'title="Less-frequent mixer operations">More ···',
        'title="Channel and advanced mixer operations">Tools ···', "contextual tools label")
    html = once(html, 'aria-pressed="false">Sends on Faders</button>',
        'aria-pressed="false">SOF</button>', "compact mixer mode key")
    old_title = re.compile(r'(?m)^    <div class="mixer-title">.*id="trackCount".*</div>\n')
    html, count = old_title.subn("", html)
    if count != 1:
        raise ValueError("Operator UX old mixer status bar drift")
    html = once(html,
        '<label class="toolbar-label" for="densitySelect">VIEW</label><select id="densitySelect" class="density-select"',
        '<select id="densitySelect" class="density-select" hidden aria-hidden="true" tabindex="-1"',
        "hide density dropdown (retain internal compatibility ID)")
    html = once(html,
        "const density=['comfort','standard','compact'].includes(mode)?mode:'comfort';",
        "const density='comfort'; // V29: one permanently comfortable operator view",
        "single comfort mode")
    html = once(html,
        "sendKnobDrag={id:e.pointerId,pot,input,x:e.clientX,y:e.clientY,start:Number(input.value)||0};",
        """if(input.disabled)return;
 input.focus({preventScroll:true});
 sendKnobDrag={id:e.pointerId,pot,input,x:e.clientX,y:e.clientY,value:Number(input.value)||0,
  start:Number(input.value)||0};""",
        "focus and hold the WIRE knob")
    html = once(html,
        """ const distance=e.clientX-g.x-(e.clientY-g.y),next=wireGainValue(g.start+distance/(e.shiftKey?14:3));
 if(Number(g.input.value)!==next){g.input.value=String(next);handleField({target:g.input,type:'input'});}""",
        """ const dx=e.clientX-g.x,dy=g.y-e.clientY;g.x=e.clientX;g.y=e.clientY;
 const delta=Math.abs(dy)>=Math.abs(dx)?dy:dx;
 const previous=wireGainValue(g.value);
 g.value=Math.max(-60,Math.min(6,g.value+delta/(e.shiftKey?48:8)));
 const next=wireGainValue(g.value);
 if(previous!==next){
  g.input.value=String(next);paintWireKnob(g.input,next);
  handleField({target:g.input,type:'input'});
 }""",
        "precision WIRE drag without shift jumps")
    html = once(html,
        "let sendKnobDrag=null;",
        """let sendKnobDrag=null;
function paintWireKnob(input,db){
 const pct=wireGainPercent(db),pot=input.closest('.wire-pot');
 pot?.style.setProperty('--wire-pct',pct+'%');
 pot?.style.setProperty('--wire-deg',(-135+pct*2.7).toFixed(1)+'deg');
 input.style.setProperty('--wire-pct',pct+'%');
 input.setAttribute('aria-valuetext',wireDbText(db));
}""",
        "immediate WIRE knob feedback")
    html = once(html,
        "if(cancel){g.input.value=String(g.start);",
        "if(cancel){g.input.value=String(g.start);paintWireKnob(g.input,g.start);",
        "restore canceled WIRE knob visual")
    html = once(html,
        " else handleField({target:g.input,type:'change'});",
        """ else {
  g.input.value=String(wireGainValue(g.value));
  handleField({target:g.input,type:'change'});
 }""",
        "flush exact WIRE pointer release value")
    html = once(html,
        "sendControlSurface.addEventListener('pointercancel',e=>finishSendKnob(e,true));",
        """sendControlSurface.addEventListener('pointercancel',e=>finishSendKnob(e,true));
sendControlSurface.addEventListener('lostpointercapture',e=>finishSendKnob(e,true));""",
        "cancel an interrupted WIRE pointer")
    html = once(html,
        " if(e.button!==0)return;\n const t=findTrack(e.target);\n if(t?.kind==='master'",
        " if(e.button!==0||e.target.closest?.('.wire-pot'))return;\n const t=findTrack(e.target);\n if(t?.kind==='master'",
        "keep WIRE focus while turning receiver knob")
    old_minimize = re.compile(r'<button class="plugin-minimize" id="pluginMinimize"[^>]*>−</button>')
    html, count = old_minimize.subn("", html)
    if count != 1:
        raise ValueError("Operator UX minimize button drift")
    old_minimize_handler = re.compile(
        r"plugEl\('pluginMinimize'\)\.addEventListener\('click',\(\)=>\{\n.*?\n\}\);\n", re.S)
    html, count = old_minimize_handler.subn("", html)
    if count != 1:
        raise ValueError("Operator UX minimize event drift")
    html = once(html,
        "const panel=plugEl('pluginBrowser'),parent=document.querySelector('.console-pane');\n const bounds=parent?.getBoundingClientRect?.();",
        "const panel=plugEl('pluginBrowser');\n  const bounds={left:0,top:0,width:window.innerWidth,height:window.innerHeight};",
        "drag within entire window")
    # V30: Channel Editor is a body-level, modeless viewport window.
    # Historic V28 HTML remains untouched; both Pages and native share this.
    identity_start = html.index('    <div id="channelIdentity" class="channel-identity"')
    identity_end = html.index('    <aside id="pluginBrowser"', identity_start)
    identity_markup = html[identity_start:identity_end]
    if html.count('id="channelIdentity"') != 1 or identity_markup.count('    </div>\n') != 1:
        raise ValueError("Channel editor DOM anchor drift")
    html = once(html, identity_markup, "", "remove editor from mixer flow")
    html = once(html,
        '</div>\n<script src="assets/wire-graph-model.js?v=wire-a1"></script>',
        '</div>\n' + identity_markup +
        '<script src="assets/wire-graph-model.js?v=wire-a1"></script>',
        "place editor under body outside scrolling app")
    html = once(html,
        "function openChannelIdentity(t,anchor){",
        """function channelIdentityViewport(){
 const visual=window.visualViewport;
 return {left:visual?.offsetLeft??0,top:visual?.offsetTop??0,
  width:visual?.width||window.innerWidth,height:visual?.height||window.innerHeight};
}
function placeChannelIdentity(dialog,box,view,x,y){
 const gap=8;
 dialog.style.left=Math.max(view.left+gap,Math.min(view.left+view.width-box.width-gap,x))+'px';
 dialog.style.top=Math.max(view.top+gap,Math.min(view.top+view.height-box.height-gap,y))+'px';
}
function positionChannelIdentity(dialog,anchor){
 // One geometry read on explicit open, not in the live meter/PCM path.
 const view=channelIdentityViewport();
 dialog.style.maxWidth=Math.max(1,view.width-16)+'px';
 dialog.style.maxHeight=Math.max(1,view.height-16)+'px';
 const box=dialog.getBoundingClientRect(),a=anchor?.getBoundingClientRect?.();
 placeChannelIdentity(dialog,box,view,a?a.left+(a.width-box.width)/2:view.left+8,
  a?a.top+18:view.top+8);
}
const channelIdentityCard=document.getElementById('channelIdentity');
const channelIdentityHandle=channelIdentityCard.querySelector('.identity-head');
let channelIdentityDrag=null;
channelIdentityHandle.addEventListener('pointerdown',e=>{
 if(e.button!==0||!e.isPrimary||e.target.closest('button'))return;
 const box=channelIdentityCard.getBoundingClientRect();
 channelIdentityDrag={id:e.pointerId,box,x:e.clientX-box.left,y:e.clientY-box.top};
 channelIdentityHandle.setPointerCapture(e.pointerId);
 e.preventDefault();
});
channelIdentityHandle.addEventListener('pointermove',e=>{
 const drag=channelIdentityDrag;
 if(!drag||drag.id!==e.pointerId)return;
 placeChannelIdentity(channelIdentityCard,drag.box,channelIdentityViewport(),
  e.clientX-drag.x,e.clientY-drag.y);
});
channelIdentityHandle.addEventListener('lostpointercapture',e=>{
 if(channelIdentityDrag?.id===e.pointerId)channelIdentityDrag=null;
});
function openChannelIdentity(t,anchor){""",
        "install measured viewport positioning")
    html = once(html,
        """ const parent=document.querySelector('.console-pane'),a=anchor?.getBoundingClientRect?.(),p=parent?.getBoundingClientRect?.();
 if(a&&p&&dialog.style?.setProperty){
  dialog.style.setProperty('left',Math.max(8,Math.min(p.width-302,a.left-p.left+a.width/2-148))+'px');
  dialog.style.setProperty('top',Math.max(8,Math.min(p.height-295,a.top-p.top+18))+'px');
 }
 document.getElementById('identityName').focus?.();""",
        """ positionChannelIdentity(dialog,anchor);
 const name=document.getElementById('identityName');
 name.focus({preventScroll:true});
 name.select();""",
        "remove mixer-relative geometry and focus scroll")
    targets = ("identityClose","pluginClose","fastClose",
               "wireConfigClose","wireWsClose","routeChoiceClose")
    for target in targets:
        pattern = re.compile(r'(<button\b[^>]*\bid="'+target+r'"[^>]*>)×(</button>)')
        html, count = pattern.subn(lambda m:m.group(1)+CLOSE_ICON+m.group(2), html)
        if count != 1:
            raise ValueError("Operator UX square close anchor drift: "+target)
    chrome_path = assets / "mixer-chrome-material.css"
    plugin_path = assets / "mixer-plugin-browser.css"
    chrome = chrome_path.read_text(encoding="utf-8")
    plugin = plugin_path.read_text(encoding="utf-8")
    # Edit the existing compiled CSS owner, not a late override stylesheet.
    chrome = once(chrome,
        " position:absolute;z-index:115;left:28px;top:100px;width:302px;max-width:calc(100% - 24px);",
        " position:fixed;z-index:115;left:8px;top:8px;width:302px;max-width:calc(100vw - 16px);"
        "\n max-height:calc(100dvh - 16px);overflow-y:auto;overscroll-behavior:contain;",
        "viewport-based bounded editor geometry")
    if "V29 adaptive 50/50 console" in chrome:
        raise ValueError("Operator UX cannot stack duplicate styles")
    # Replace the original movable-window geometry instead of layering another
    # competing positioning declaration. Inline important drag stays authoritative.
    plugin = once(plugin,
        "position:absolute!important;right:14px!important;left:auto;top:40px!important;bottom:auto!important;",
        "position:fixed!important;right:14px!important;left:auto;top:70px!important;bottom:auto!important;",
        "plugin fixed floating geometry")
    plugin = once(plugin,
        "width:min(408px,calc(100% - 32px))!important;",
        "width:min(408px,calc(100vw - 28px))!important;",
        "plugin width")
    plugin = once(plugin,
        "height:min(536px,calc(100% - 54px))!important;",
        "height:min(536px,calc(100dvh - 90px))!important;",
        "plugin height")
    plugin = once(plugin,
        "max-height:calc(100% - 54px)!important;",
        "max-height:calc(100dvh - 48px)!important;",
        "plugin max height")
    plugin = once(plugin,
        "min-width:min(310px,calc(100% - 32px))!important;",
        "min-width:min(310px,calc(100vw - 28px))!important;",
        "plugin min width")
    plugin, count = re.subn(r"\.plugin-browser\.minimized\{\s*height:48px!important;.*?\}\s*"
        r"\.plugin-browser\.minimized > :not\(\.plugin-head\)\{display:none!important\}\s*",
        "", plugin, count=1, flags=re.S)
    if count != 1:
        raise ValueError("Operator UX obsolete minimized CSS drift")
    chrome_path.write_text(chrome.rstrip()+"\n"+OPERATOR_CSS+"\n",encoding="utf-8")
    # V9: unified modal close material is compiled from its sole CSS owner.
    plugin_path.write_text(plugin,encoding="utf-8")
    html = once(html, "restorePluginPreferences();init();", SPLIT_BOOTSTRAP+"\nrestorePluginPreferences();init();", "startup geometry bootstrap")
    if html.count('id="trackCount"')!=1 or 'id="pluginMinimize"' in html:
        raise ValueError("Operator UX DOM invariants rejected")
    return html
