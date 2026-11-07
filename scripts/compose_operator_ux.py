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
    // Panels remain equal halves when upper racks are expanded. If all upper
    // modules fold, the existing flex fader may use the deliberately freed bay.
    const half=Math.round(h/2);
    const fader=Math.max(130,Math.round(half-pan-switches-footer));
    const fx=Math.max(125,Math.round(half-identity-route-send));
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

OPERATOR_CSS = """
/* V29 adaptive 50/50 console, authored for the same rail/strip/Master tokens.
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
.command-bar .mixer-file-tool{display:inline-flex;align-items:center;gap:5px}
.command-bar .mixer-file-tool img{display:block;flex:none;pointer-events:none}
.command-bar #densitySelect,.command-bar [for="densitySelect"],.command-bar #mode{display:none!important}
/* Every console floating editor uses one square Material close key. */
:is(#pluginClose,#identityClose,#fastClose,#wireConfigClose,#wireWsClose,#routeChoiceClose){
  box-sizing:border-box!important;flex:0 0 29px!important;width:29px!important;
  height:29px!important;min-width:29px!important;min-height:29px!important;
  max-width:29px!important;max-height:29px!important;padding:0!important;
  display:inline-flex!important;align-items:center!important;justify-content:center!important;
  background:#253944!important;border:1px solid #57717f!important;border-radius:3px!important;
  color:#d7e6eb!important;line-height:1!important;cursor:pointer!important;
}
:is(#pluginClose,#identityClose,#fastClose,#wireConfigClose,#wireWsClose,#routeChoiceClose):hover{
 background:#355161!important;color:#fff!important}
:is(#pluginClose,#identityClose,#fastClose,#wireConfigClose,#wireWsClose,#routeChoiceClose):active{
 background:#962f38!important;border-color:#e6505d!important;color:#fff!important}
:is(#pluginClose,#identityClose,#fastClose,#wireConfigClose,#wireWsClose,#routeChoiceClose) svg{
 width:20px;height:20px;display:block;pointer-events:none}
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
        """function positionChannelIdentity(dialog,anchor){
 // One geometry read on explicit open, not in the live meter/PCM path.
 const visual=window.visualViewport;
 const vx=visual?.offsetLeft??0,vy=visual?.offsetTop??0;
 const vw=visual?.width||window.innerWidth,vh=visual?.height||window.innerHeight;
 const gap=8;
 dialog.style.maxWidth=Math.max(1,vw-2*gap)+'px';
 dialog.style.maxHeight=Math.max(1,vh-2*gap)+'px';
 const box=dialog.getBoundingClientRect(),a=anchor?.getBoundingClientRect?.();
 const preferredX=a?a.left+a.width/2-box.width/2:vx+gap;
 const preferredY=a?a.top+18:vy+gap;
 const x=Math.max(vx+gap,Math.min(vx+vw-box.width-gap,preferredX));
 const y=Math.max(vy+gap,Math.min(vy+vh-box.height-gap,preferredY));
 dialog.style.left=x+'px';dialog.style.top=y+'px';
}
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
 document.getElementById('identityName').focus({preventScroll:true});""",
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
    # Same plugin-browser CSS owner: pressed state outranks the legacy
    # .plugin-close transparent background after Stage C compilation.
    plugin += ("\n#pluginBrowser #pluginClose:active{"
               "background:#962f38!important;background-color:#962f38!important;"
               "border-color:#e6505d!important;color:#fff!important}\n")
    plugin_path.write_text(plugin,encoding="utf-8")
    html = once(html, "restorePluginPreferences();init();", SPLIT_BOOTSTRAP+"\nrestorePluginPreferences();init();", "startup geometry bootstrap")
    if html.count('id="trackCount"')!=1 or 'id="pluginMinimize"' in html:
        raise ValueError("Operator UX DOM invariants rejected")
    return html
