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
    // The fixed upper rows can exceed the old 680px floor after ribbon /
    // routing changes. Preserve the existing 125px FX reserve AND a
    // balanced lower deck by allowing only the necessary vertical scroll.
    // Reuse this measured rail and existing resize/fold observer; no new reads.
    const bothExpanded=!root.classList.contains('collapsed-send')&&!root.classList.contains('collapsed-fx');
    const minExpanded=bothExpanded?Math.max(680,Math.ceil(2*(identity+route+send+125))):0;
    const half=Math.round(Math.max(h,minExpanded)/2);
    const fxBase=Math.max(125,Math.round(half-identity-route-send));
    const boost=Math.min(48,Math.max(0,fxBase-125));
    const fader=Math.max(130,Math.round(half-pan-switches-footer+boost));
    const fx=fxBase-boost;
    const signature=fader+':'+fx+':'+minExpanded;
    if(signature===previous)return;
    previous=signature;
    root.style.setProperty('--ux-expanded-min-h',minExpanded+'px');
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
/* V60 physical-fader-priority console, same rail/strip/Master tokens.
   At very short heights preserve minimum usable controls and scroll vertically. */
.console-pane{overflow-y:auto!important;overflow-x:hidden!important}
.console-body{min-height:610px!important;
  --v13-fader-h:max(130px,calc(50% - var(--pan-h) - var(--switch-h) - var(--footer-h)))!important;
  --ux-fx-balanced-h:max(125px,calc(50% - var(--identity-h) - var(--route-h) - var(--send-h)));
}
.console-body:not(.collapsed-send):not(.collapsed-fx){min-height:max(680px,var(--ux-expanded-min-h,680px))!important}
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


# V7 Plugin Manager: explicit desktop catalog and scanner workspace. The immutable
# historical HTML remains untouched; both native and Pages consume this composition.
PLUGIN_WORKSPACE_CSS = r"""/* Professional Plugin Manager: one compact desktop workspace, not mixer keys. */
.plugin-browser{position:fixed!important;z-index:90!important;top:76px!important;right:max(14px,calc((100vw - 1000px)/2))!important;left:auto;bottom:auto!important;width:min(1000px,calc(100vw - 28px))!important;height:min(620px,calc(100dvh - 90px))!important;max-width:calc(100vw - 28px)!important;max-height:calc(100dvh - 48px)!important;min-width:min(740px,calc(100vw - 28px))!important;min-height:300px!important;display:flex;flex-direction:column;box-sizing:border-box;resize:both!important;overflow:hidden!important;background:#1a2b35!important;color:#c6d6df;border:1px solid #49616f!important;border-radius:5px!important;box-shadow:0 12px 32px #0009!important;pointer-events:auto!important;font-size:11px;font-weight:400}
.plugin-browser[hidden]{display:none!important}
.plugin-browser[style*="left:"]{right:auto!important}
.plugin-browser:not([hidden]) .plugin-head{cursor:move!important;touch-action:none!important;user-select:none!important}
.plugin-head{height:44px;flex:none;display:flex;align-items:center;padding:6px 12px;background:#233743!important;border-bottom:1px solid #405663;gap:8px}
.plugin-head-title{flex:1;min-width:0;display:flex;flex-direction:column;gap:3px;font-size:13px;font-weight:500;letter-spacing:.01em;pointer-events:none!important;color:#edf4f8}
.plugin-target{font-size:10px;color:#a3becd;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:400}
/* Close has no local face, icon, hover or pressed rule. Shared contextual owner only. */
.plugin-workspace{display:grid;grid-template-columns:246px minmax(0,1fr);flex:1;min-height:0;overflow:hidden}
.plugin-sidebar{display:flex;flex-direction:column;min-width:0;min-height:0;overflow:auto;background:#1d303b;border-right:1px solid #3b515e;padding:12px;gap:12px}
.plugin-sidebar-title{font-size:10px;color:#a7beca;letter-spacing:.055em;text-transform:uppercase;font-weight:500}
.plugin-library-actions{display:grid;grid-template-columns:1fr 1fr;gap:6px}
.plugin-browser .plugin-library-actions button,.plugin-browser .scan-add button{height:30px!important;min-height:30px!important;padding:0 8px!important;background:#263e4c!important;background-image:none!important;border:1px solid #466576!important;border-radius:3px!important;box-shadow:none!important;color:#d9e8ef!important;font-size:11px!important;font-weight:500!important;white-space:nowrap}
.plugin-browser #pluginScan{background:#24566b!important;border-color:#487c90!important;color:#edf8ff!important}
.plugin-browser .plugin-library-actions button:hover:not(:disabled),.plugin-browser .scan-add button:hover:not(:disabled){background:#345568!important;border-color:#75a9bb!important}
.plugin-browser .plugin-library-actions button:disabled{opacity:.5!important;cursor:not-allowed}
.plugin-scan-settings{display:flex;flex-direction:column;min-width:0;gap:8px;padding:0;background:transparent;border:0}
.scan-title{font-size:10px;letter-spacing:.035em;color:#9cb4c3;font-weight:500}
.scan-paths{display:flex;flex-direction:column;gap:0;max-height:184px;min-height:48px;overflow:auto;border:1px solid #3c5361;background:#162832}
.scan-path{display:flex;align-items:center;gap:5px;min-width:0;min-height:29px;padding:3px 6px;border-bottom:1px solid #2b414d;font-size:10px}
.scan-path>span{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.scan-path small{color:#94afbd;flex:none}.scan-path button{background:transparent;border:0;color:#a7bdc8}
.scan-add{display:flex;flex-wrap:wrap;gap:6px}.scan-add input{flex:1 1 100%;min-width:0;height:29px;padding:0 7px;color:#dfedf3;background:#132630;border:1px solid #3f5866;border-radius:3px}
.scan-add button{width:100%}
.scan-status{font-size:10px;color:#b1c4cf;line-height:1.5;overflow-wrap:anywhere}
.plugin-disclaimer{margin-top:auto;color:#93aab8;font-size:10px;line-height:1.45;padding-top:8px;border-top:1px solid #394f5c;overflow-wrap:anywhere}
.plugin-catalog{display:flex;flex-direction:column;min-width:0;min-height:0;overflow:hidden;background:#142630}
.plugin-catalog-tools{display:flex;flex:none;gap:8px;align-items:center;padding:9px 10px;border-bottom:1px solid #3b515e;background:#1b303b}
.plugin-search-row{display:flex;min-width:0;flex:1}
.plugin-search-row input{width:100%;min-width:0;height:30px;padding:0 9px;background:#10232d;color:#e0edf4;border:1px solid #45606e;border-radius:3px;font-size:11px}
.plugin-filters{display:flex;gap:6px;min-width:0}
.plugin-filters select{width:112px;min-width:0;height:30px;background:#203642;color:#c6d9e4;border:1px solid #405b6b;border-radius:3px;padding:0 5px;font-size:11px}
.plugin-catalog{--plugin-columns:minmax(0,1fr) 112px 25px 25px;--plugin-identity:minmax(0,1.55fr) minmax(0,1fr) minmax(0,.75fr)}
.plugin-headings,.plugin-item{display:grid;grid-template-columns:var(--plugin-columns);gap:0;align-items:center;min-width:0;width:100%;box-sizing:border-box}
.plugin-headings{height:28px;flex:none;background:#233b49;color:#bdd0db;font-size:10px;font-weight:500;border-bottom:1px solid #4a6473;padding:0 6px}
.plugin-heading-identity,.plugin-browser .plugin-choose{display:grid!important;grid-template-columns:var(--plugin-identity);gap:0;align-items:center;min-width:0;width:100%;box-sizing:border-box;text-align:left!important;justify-content:stretch!important}
.plugin-heading-identity>span,.plugin-choose>span{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;padding:0 7px;box-sizing:border-box;text-align:left}
.plugin-heading-status,.plugin-labels{display:grid;grid-template-columns:48px 64px;align-items:center;min-width:0;width:100%;box-sizing:border-box}
.plugin-heading-status>span,.plugin-labels>span{padding:0 4px;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;box-sizing:border-box;text-align:left}
.plugin-heading-icon{text-align:center;justify-self:center;width:24px}
.plugin-results{min-height:0;background:#142630}
.plugin-table-scroll{flex:1;min-height:0;overflow:auto;scrollbar-gutter:stable;background:#142630;overscroll-behavior:contain}
.plugin-headings{position:sticky;top:0;z-index:2;box-sizing:border-box}
.plugin-item{height:27px;min-height:27px;padding:0 6px;border-bottom:1px solid #29404b;background:#182b35;box-sizing:border-box}
.plugin-item:nth-child(even){background:#1a2e39}
.plugin-item:hover,.plugin-item:focus-within{background:#264553;box-shadow:inset 2px 0 #74b9ce}
.plugin-browser .plugin-choose{height:26px!important;min-height:26px!important;padding:0!important;border:0!important;border-radius:0!important;background:transparent!important;background-image:none!important;box-shadow:none!important;align-items:center;cursor:pointer;color:#d9e6ee;font-size:11px}
.plugin-item.disabled .plugin-choose{opacity:.52}
.plugin-name{font-size:11px;color:#e2edf3;font-weight:400}
.plugin-meta,.plugin-category{font-size:10px;color:#a9bfcc;font-weight:400}
.plugin-labels{font-size:10px;color:#a7bdca}
.plugin-pill{color:#b6cbd8;border:0;border-radius:0;background:none}
.plugin-pill.problem,.plugin-item.disabled .plugin-labels{color:#e2a49b}
.plugin-browser .plugin-flag{display:grid!important;place-items:center;justify-self:center;text-align:center!important;height:24px!important;width:24px!important;min-width:0!important;padding:0!important;border:0!important;border-radius:2px!important;background:transparent!important;background-image:none!important;box-shadow:none!important;color:#849ca9;font-size:13px;line-height:24px}
.plugin-browser .plugin-flag.starred{color:#d9bd79!important}
.plugin-browser .plugin-flag:hover{background:#3a5665!important;color:#edf7fc!important}
.plugin-empty{padding:24px 12px;text-align:center;color:#9cb4c2;font-size:11px}
.plugin-footer{display:flex;align-items:center;min-height:30px!important;height:auto!important;flex:none;padding:5px 12px!important;border-top:1px solid #3b515e;background:#1c303b;color:#a7bdca;font-size:10px;gap:8px}
.plugin-footer #pluginCount{flex:1;color:#afc4cf}
.plugin-browser button:not(#pluginClose):focus-visible,.plugin-browser input:focus-visible,.plugin-browser select:focus-visible{outline:2px solid #9ad9ed!important;outline-offset:-2px}
.console-body.plugin-open #viewport{margin-right:0!important}
.plugin-browser[hidden],.fast-picker[hidden]{display:none!important}
@media(max-width:760px){
 .plugin-browser{min-width:min(310px,calc(100vw - 28px))!important;min-height:230px!important;top:55px!important;height:min(620px,calc(100dvh - 70px))!important}
 .plugin-workspace{grid-template-columns:minmax(0,1fr);grid-template-rows:minmax(0,1fr) auto}
 .plugin-sidebar{grid-row:2;max-height:145px;border-right:0;border-top:1px solid #3b515e;padding:8px;gap:7px}
 .plugin-catalog{grid-row:1;--plugin-identity:minmax(0,1fr) minmax(0,.75fr);--plugin-columns:minmax(0,1fr) 84px 23px 23px}
 .plugin-heading-status,.plugin-labels{grid-template-columns:38px 46px}
 .plugin-category,.plugin-heading-category{display:none}
 .plugin-catalog-tools{flex-wrap:wrap;gap:5px;padding:6px}
 .plugin-search-row{flex-basis:100%}
 .plugin-filters{flex:1}.plugin-filters select{width:auto;flex:1}
}
"""

def apply_plugin_workspace(html: str, plugin: str) -> tuple[str, str]:
    start = html.index('    <aside id="pluginBrowser"')
    end = html.index('    </aside>', start) + len('    </aside>')
    original = html[start:end]
    # Retain exact controls, IDs, existing native event handlers and file input.
    def line(marker):
        matches = [s.strip() for s in original.splitlines() if marker in s]
        if len(matches) != 1:
            raise ValueError("Plugin workspace anchor drift: " + marker)
        return matches[0]
    head = line('id="pluginDragHandle"')
    head = once(head, 'PLUGIN MANAGER', 'Plugin Manager', 'manager title')
    head = once(head, 'class="plugin-close"', 'class="plugin-close armix-floating-close"', 'shared Close semantic owner')
    search = line('class="plugin-search-row"')
    filters = line('class="plugin-filters"')
    results = line('id="pluginResults"')
    footer = line('id="pluginCount"')
    footer_match = re.fullmatch(r'<div class="plugin-footer">(<span id="pluginCount">.*?</span>)(.*)</div>', footer)
    if not footer_match:
        raise ValueError("Plugin Manager footer drift")
    count, actions = footer_match.groups()
    actions = once(actions, 'Import list…', 'Import…', 'import label')
    actions = once(actions, 'Import plugin metadata from a JSON registry exported by a scanner', 'Import VST3 in native host; import metadata in browser reference', 'import tooltip')
    scan_start = original.index('<div class="plugin-scan-settings">')
    scan_end = original.index('<div class="plugin-disclaimer">', scan_start)
    scan = original[scan_start:scan_end].strip()
    disclaimer = line('class="plugin-disclaimer"')
    headings = ('<div class="plugin-headings"><div class="plugin-heading-identity">'
                '<span>Name</span><span>Vendor</span><span class="plugin-heading-category">Category</span></div>'
                '<div class="plugin-heading-status"><span>Format</span><span>Status</span></div>'
                '<span class="plugin-heading-icon" title="Favorites" aria-label="Favorite">★</span>'
                '<span class="plugin-heading-icon" title="Hide or show effect" aria-label="Visibility">⊘</span></div>')
    markup = ('    <aside id="pluginBrowser" class="plugin-browser" aria-label="Effects plugin manager" hidden>\n'
              + head + '\n<div class="plugin-workspace"><section class="plugin-sidebar" aria-label="Plugin management">'
              + '<div class="plugin-sidebar-title">Library actions</div><div class="plugin-library-actions">' + actions + '</div>'
              + scan + disclaimer + '</section><section class="plugin-catalog" aria-label="Plugin catalog">'
              + '<div class="plugin-catalog-tools">' + search + filters + '</div>'
              + '<div class="plugin-table-scroll">' + headings + results + '</div><div class="plugin-footer">' + count
              + '<span>Click a plugin to insert</span></div></section></div>\n    </aside>')
    html = html[:start] + markup + html[end:]
    html = once(html,
        """<span class="plugin-meta">'+escapeHtml(p.vendor)+' · '+escapeHtml(p.category)+(latency?' · '+escapeHtml(latency):'')+(usedBy.length?' · Used '+usedBy.length:'')+'</span>""",
        """<span class="plugin-meta" title="'+escapeHtml(p.vendor)+'">'+escapeHtml(p.vendor)+'</span><span class="plugin-category" title="'+escapeHtml([p.category,latency,usedBy.length?'Used '+usedBy.length:''].filter(Boolean).join(' · '))+'">'+escapeHtml(p.category)+'</span>""",
        "plugin row column layout")
    # Replace only the manager portion of its existing CSS owner. Quick Picker
    # keeps its existing implementation, CSS and interactions.
    fast = plugin.index('.fast-picker{position:absolute;')
    return html, PLUGIN_WORKSPACE_CSS + "\n" + plugin[fast:]

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
    # V6: flat Plugin Manager actions, not physical mixer keys.
    # Preserve immutable V28 source and shared 24px Close chrome.
    chrome, removed = re.subn(r"\.plugin-footer button,\s*", "", chrome)
    if removed != 14:
        raise ValueError("Plugin Manager hardware key selector inventory drift")
    chrome_path.write_text(chrome.rstrip()+"\n"+OPERATOR_CSS+"\n",encoding="utf-8")
    # V9: unified modal close material is compiled from its sole CSS owner.
    html, plugin = apply_plugin_workspace(html, plugin)
    plugin_path.write_text(plugin,encoding="utf-8")
    html = once(html, "restorePluginPreferences();init();", SPLIT_BOOTSTRAP+"\nrestorePluginPreferences();init();", "startup geometry bootstrap")
    if html.count('id="trackCount"')!=1 or 'id="pluginMinimize"' in html:
        raise ValueError("Operator UX DOM invariants rejected")
    return html
