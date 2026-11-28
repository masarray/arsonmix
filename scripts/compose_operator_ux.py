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
.plugin-browser{position:fixed!important;z-index:90!important;top:76px!important;right:max(14px,calc((100vw - 1000px)/2))!important;left:auto;bottom:auto!important;width:min(1000px,calc(100vw - 28px))!important;height:min(620px,calc(100dvh - 90px))!important;max-width:calc(100vw - 28px)!important;max-height:calc(100dvh - 48px)!important;min-width:min(740px,calc(100vw - 28px))!important;min-height:300px!important;display:flex;flex-direction:column;box-sizing:border-box;resize:both!important;overflow:hidden!important;background:#1a2b35!important;color:#c6d6df;border:1px solid #49616f!important;border-radius:5px!important;box-shadow:0 12px 32px #0009!important;pointer-events:auto!important;font-size:11px;font-weight:400;--plugin-control-height:34px;--plugin-control-radius:calc(var(--plugin-control-height) / 2)}
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
/* V6 pill material, taller manager controls; radius follows height. */
.plugin-browser :is(.plugin-library-actions,.scan-add) button{display:inline-flex!important;align-items:center;justify-content:center;gap:5px;box-sizing:border-box!important;height:var(--plugin-control-height)!important;min-height:var(--plugin-control-height)!important;max-height:var(--plugin-control-height)!important;padding:0 10px!important;background:#78c8d514!important;background-image:none!important;border:1px solid #83c7d344!important;border-radius:var(--plugin-control-radius)!important;box-shadow:none!important;color:#b6edf6!important;font:400 11px/1 Inter,"Segoe UI",Arial,sans-serif!important;letter-spacing:0!important;text-transform:none!important;text-shadow:none!important;white-space:nowrap;cursor:pointer}
.plugin-browser :is(.plugin-library-actions,.scan-add) button:hover:not(:disabled){background:#9abccb19!important;border-color:#93b5c22b!important;color:#f1f8fb!important}
.plugin-browser :is(.plugin-library-actions,.scan-add) button:active:not(:disabled){background:#4bb6ce26!important;border-color:#62b9cb56!important;color:#9ce8f4!important}
.plugin-browser :is(.plugin-library-actions,.scan-add) button:disabled{opacity:.45!important;cursor:not-allowed}
.plugin-scan-settings{display:flex;flex-direction:column;min-width:0;gap:8px;padding:0;background:transparent;border:0}
.scan-add{display:flex;gap:6px}
.scan-add button{width:100%}
.plugin-native-paths{display:grid;gap:4px;min-width:0;max-height:170px;overflow:auto;margin:0;padding:0;list-style:none}
.plugin-native-paths li{display:flex;align-items:center;gap:6px;min-width:0;padding:5px 6px;border:1px solid #83a6b42e;border-radius:4px;background:#152731}
.plugin-native-paths li>span{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:11px}
.plugin-native-paths small{color:#9ab6c4;font-size:10px}
.plugin-browser .plugin-native-paths button{flex:none;min-width:24px!important;height:24px!important;padding:0 5px!important;border:1px solid #83a6b444!important;border-radius:4px!important;background:#233b49!important;color:#c3d6de!important;font:11px Inter,"Segoe UI",sans-serif!important;cursor:pointer}
.plugin-browser .plugin-native-paths button:disabled{opacity:.45;cursor:not-allowed}
.scan-status{font-size:10px;color:#b1c4cf;line-height:1.5;overflow-wrap:anywhere;padding:8px 9px;border-left:2px solid #567b8c;background:#172b36;border-radius:0 3px 3px 0}
.scan-status:empty{display:none!important}
.plugin-catalog{display:flex;flex-direction:column;min-width:0;min-height:0;overflow:hidden;background:#142630}
.plugin-catalog-tools{display:flex;flex:none;gap:8px;align-items:center;padding:9px 10px;border-bottom:1px solid #3b515e;background:#1b303b}
.plugin-browser .plugin-search-row{position:relative;display:flex;align-items:center;min-width:0;flex:1;height:var(--plugin-control-height);padding:0;margin:0}
.plugin-search-row::before{content:"";position:absolute;left:10px;top:calc(50% - 6px);width:9px;height:9px;border:1.5px solid #8eacbc;border-radius:50%;pointer-events:none}
.plugin-search-row::after{content:"";position:absolute;left:19px;top:calc(50% + 5px);width:5px;height:1.5px;border-radius:1px;background:#8eacbc;transform:rotate(45deg);pointer-events:none}
.plugin-browser .plugin-filters{display:flex;align-items:center;gap:6px;min-width:0;height:var(--plugin-control-height);padding:0;margin:0}
.plugin-browser :is(.plugin-search-row input,.plugin-filters select){box-sizing:border-box!important;height:var(--plugin-control-height)!important;min-height:var(--plugin-control-height)!important;max-height:var(--plugin-control-height)!important;min-width:0;margin:0!important;border:1px solid #83a6b42e!important;border-radius:var(--plugin-control-radius)!important;background-color:#0d202a!important;color:#d2e2e9!important;box-shadow:none!important;font:400 11px Inter,"Segoe UI",Arial,sans-serif!important;letter-spacing:0!important;text-transform:none!important}
.plugin-browser .plugin-search-row input{width:100%;padding:0 9px 0 31px!important;background-image:none!important}
.plugin-browser .plugin-filters select{width:112px;appearance:none!important;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath d='m3 4 3 3 3-3' stroke='%239db5c2' stroke-width='1.5' fill='none' stroke-linecap='round'/%3E%3C/svg%3E")!important;background-position:right 8px center!important;background-repeat:no-repeat!important;background-size:12px 12px!important;padding:0 24px 0 10px!important;cursor:pointer}
.plugin-browser :is(.plugin-search-row input,.plugin-filters select):hover:not(:disabled){border-color:#93b5c280!important;color:#f1f8fb!important}
.plugin-catalog{--plugin-columns:minmax(0,1fr) 112px 25px 25px;--plugin-identity:minmax(0,1.55fr) minmax(0,1fr) minmax(0,.75fr)}
.plugin-headings,.plugin-item{display:grid;grid-template-columns:var(--plugin-columns);gap:0;align-items:center;min-width:0;width:100%;box-sizing:border-box}
.plugin-headings{height:28px;flex:none;background:#233b49;color:#bdd0db;font-size:10px;font-weight:500;border-bottom:1px solid #4a6473;padding:0 6px}
.plugin-heading-identity,.plugin-browser .plugin-choose{display:grid!important;grid-template-columns:var(--plugin-identity);gap:0;align-items:center;min-width:0;width:100%;box-sizing:border-box;text-align:left!important;justify-content:stretch!important}
.plugin-heading-identity>span,.plugin-choose>span{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;padding:0 7px;box-sizing:border-box;text-align:left}
.plugin-heading-status,.plugin-labels{display:grid;grid-template-columns:48px 64px;align-items:center;min-width:0;width:100%;box-sizing:border-box}
.plugin-heading-status>span,.plugin-labels>span{padding:0 4px;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;box-sizing:border-box;text-align:left}
/* Lucide star/ban, ISC; notice in workstream doc. Shared mask per icon,
   no emoji/font baseline, no extra SVG nodes per catalog entry. */
.plugin-browser{--plugin-star:url("data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20viewBox%3D%220%200%2024%2024%22%20fill%3D%22none%22%20stroke%3D%22black%22%20stroke-width%3D%222%22%20stroke-linecap%3D%22round%22%20stroke-linejoin%3D%22round%22%3E%3Cpath%20d%3D%22M11.525%202.295a.53.53%200%200%201%20.95%200l2.31%204.679a2.123%202.123%200%200%200%201.595%201.16l5.166.756a.53.53%200%200%201%20.294.904l-3.736%203.638a2.123%202.123%200%200%200-.611%201.878l.882%205.14a.53.53%200%200%201-.771.56l-4.618-2.428a2.122%202.122%200%200%200-1.973%200L6.396%2021.01a.53.53%200%200%201-.77-.56l.881-5.139a2.122%202.122%200%200%200-.611-1.879L2.16%209.795a.53.53%200%200%201%20.294-.906l5.165-.755a2.122%202.122%200%200%200%201.597-1.16z%22%2F%3E%3C%2Fsvg%3E");--plugin-star-on:url("data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20viewBox%3D%220%200%2024%2024%22%20fill%3D%22black%22%20stroke%3D%22black%22%20stroke-width%3D%222%22%20stroke-linecap%3D%22round%22%20stroke-linejoin%3D%22round%22%3E%3Cpath%20d%3D%22M11.525%202.295a.53.53%200%200%201%20.95%200l2.31%204.679a2.123%202.123%200%200%200%201.595%201.16l5.166.756a.53.53%200%200%201%20.294.904l-3.736%203.638a2.123%202.123%200%200%200-.611%201.878l.882%205.14a.53.53%200%200%201-.771.56l-4.618-2.428a2.122%202.122%200%200%200-1.973%200L6.396%2021.01a.53.53%200%200%201-.77-.56l.881-5.139a2.122%202.122%200%200%200-.611-1.879L2.16%209.795a.53.53%200%200%201%20.294-.906l5.165-.755a2.122%202.122%200%200%200%201.597-1.16z%22%2F%3E%3C%2Fsvg%3E");--plugin-ban:url("data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20viewBox%3D%220%200%2024%2024%22%20fill%3D%22none%22%20stroke%3D%22black%22%20stroke-width%3D%222%22%20stroke-linecap%3D%22round%22%20stroke-linejoin%3D%22round%22%3E%3Ccircle%20cx%3D%2212%22%20cy%3D%2212%22%20r%3D%2210%22%2F%3E%3Cpath%20d%3D%22m4.929%204.929%2014.142%2014.142%22%2F%3E%3C%2Fsvg%3E")}
.plugin-heading-icon,.plugin-browser .plugin-flag{display:grid!important;place-items:center;justify-self:center;box-sizing:border-box;width:24px!important;height:24px!important;color:#96b0be;font-size:0!important;line-height:1!important}
.plugin-heading-icon::before,.plugin-browser .plugin-flag::before{content:"";display:block;width:16px;height:16px;background:currentColor;mask:var(--plugin-glyph) center/contain no-repeat;pointer-events:none}
.plugin-heading-icon[data-plugin-icon="star"],.plugin-browser .plugin-flag[data-plugin-action="fav"]{--plugin-glyph:var(--plugin-star)}
.plugin-heading-icon[data-plugin-icon="ban"],.plugin-browser .plugin-flag[data-plugin-action="hide"]{--plugin-glyph:var(--plugin-ban)}
.plugin-browser .plugin-flag[data-plugin-action="fav"][aria-pressed="true"]{--plugin-glyph:var(--plugin-star-on);color:#e4bf70!important}
.plugin-browser .plugin-flag[data-plugin-action="hide"][aria-pressed="true"]{color:#f0a39e!important;background:#d66c6a20!important}
.plugin-browser .plugin-flag:hover{background:#9abccb19!important;color:#f1f8fb!important}
.plugin-browser .plugin-flag[aria-pressed="true"]:hover{color:#fff!important}

.plugin-results{min-height:0;background:#142630}
.plugin-table-scroll{flex:1;min-height:0;overflow:auto;scrollbar-gutter:stable;background:#142630;overscroll-behavior:contain}
.plugin-headings{position:sticky;top:0;z-index:2;box-sizing:border-box}
.plugin-item{height:27px;min-height:27px;padding:0 6px;border-bottom:1px solid #29404b;background:#182b35;box-sizing:border-box}
.plugin-item:nth-child(even){background:#1a2e39}
.plugin-item:hover,.plugin-item:focus-within{background:#264553;box-shadow:inset 2px 0 #74b9ce}
.plugin-browser .plugin-choose{height:26px!important;min-height:26px!important;padding:0!important;border:0!important;border-radius:0!important;background:transparent!important;background-image:none!important;box-shadow:none!important;align-items:center;cursor:pointer;color:#d9e6ee;font-size:11px}
.plugin-item.disabled .plugin-choose{opacity:.52}
.plugin-name{font-size:11px;color:#e2edf3;font-weight:400;letter-spacing:0}
.plugin-meta,.plugin-category{font-size:11px;color:#a9bfcc;font-weight:400;letter-spacing:0}
.plugin-labels{font-size:10px;color:#a7bdca;letter-spacing:0}
.plugin-pill{color:#b6cbd8;border:0;border-radius:0;background:none}
.plugin-pill.problem,.plugin-item.disabled .plugin-labels{color:#e2a49b}
.plugin-browser .plugin-flag{min-width:0!important;padding:0!important;border:0!important;border-radius:4px!important;background:transparent!important;background-image:none!important;box-shadow:none!important;cursor:pointer;color:#96b0be}
.plugin-empty{padding:24px 12px;text-align:center;color:#9cb4c2;font-size:11px}
.plugin-footer{display:flex;align-items:center;min-height:30px!important;height:auto!important;flex:none;padding:5px 12px!important;border-top:1px solid #3b515e;background:#1c303b;color:#a7bdca;font-size:10px;gap:8px}
.plugin-footer #pluginCount{flex:1;color:#afc4cf;font-variant-numeric:tabular-nums;letter-spacing:0}
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
 .plugin-browser .plugin-search-row{flex-basis:100%}
 .plugin-browser .plugin-filters{flex:1}.plugin-browser .plugin-filters select{width:auto;flex:1}
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
    filters = line('class="plugin-filters"').replace('★ Favorites', 'Favorites')
    results = line('id="pluginResults"')
    footer = line('id="pluginCount"')
    footer_match = re.fullmatch(r'<div class="plugin-footer">(<span id="pluginCount">.*?</span>)(.*)</div>', footer)
    if not footer_match:
        raise ValueError("Plugin Manager footer drift")
    count, actions = footer_match.groups()
    actions = once(actions, 'Import list…', 'Import…', 'import label')
    actions = once(actions, 'Import plugin metadata from a JSON registry exported by a scanner', 'Import VST3 in native host; import metadata in browser reference', 'import tooltip')
    # Native Add folder opens a JUCE chooser. Expose only authoritative
    # scan paths, not the historical reference path editor or fake defaults.
    add_folder = re.search(r'<button id="pluginAddPath">.*?</button>', line('id="pluginAddPath"'))
    if not add_folder:
        raise ValueError("Plugin folder chooser anchor drift")
    scan = ('<div class="plugin-scan-settings"><div class="scan-add">'
            + add_folder.group().replace('+ Add folder', 'Add folder…') + '</div>'
            + '<div class="plugin-sidebar-title">Scan folders</div><ul id="pluginNativePaths" class="plugin-native-paths" aria-label="VST3 scan folders"></ul>'
            + '<div class="scan-status" id="pluginScanStatus" role="status" aria-live="polite"></div></div>')
    headings = ('<div class="plugin-headings"><div class="plugin-heading-identity">'
                '<span>Name</span><span>Vendor</span><span class="plugin-heading-category">Category</span></div>'
                '<div class="plugin-heading-status"><span>Format</span><span>Status</span></div>'
                '<span class="plugin-heading-icon" data-plugin-icon="star" title="Favorites" aria-label="Favorite"></span>'
                '<span class="plugin-heading-icon" data-plugin-icon="ban" title="Hide or show effect" aria-label="Visibility"></span></div>')
    markup = ('    <aside id="pluginBrowser" class="plugin-browser" aria-label="Effects plugin manager" hidden>\n'
              + head + '\n<div class="plugin-workspace"><section class="plugin-sidebar" aria-label="Plugin management">'
              + '<div class="plugin-sidebar-title">Library actions</div><div class="plugin-library-actions">' + actions + '</div>'
              + scan + '</section><section class="plugin-catalog" aria-label="Plugin catalog">'
              + '<div class="plugin-catalog-tools">' + search + filters + '</div>'
              + '<div class="plugin-table-scroll">' + headings + results + '</div><div class="plugin-footer">' + count
              + '</div></section></div>\n    </aside>')
    html = html[:start] + markup + html[end:]
    html = once(html,
        """<span class="plugin-meta">'+escapeHtml(p.vendor)+' · '+escapeHtml(p.category)+(latency?' · '+escapeHtml(latency):'')+(usedBy.length?' · Used '+usedBy.length:'')+'</span>""",
        """<span class="plugin-meta" title="'+escapeHtml(p.vendor)+'">'+escapeHtml(p.vendor)+'</span><span class="plugin-category" title="'+escapeHtml([p.category,latency,usedBy.length?'Used '+usedBy.length:''].filter(Boolean).join(' · '))+'">'+escapeHtml(p.category)+'</span>""",
        "plugin row column layout")
    # The native bridge intercepts Add folder and invokes chooseFolder().
    # In the reference, fail honestly on request instead of editing fake paths.
    html = once(html, " const container=plugEl('pluginPaths');",
        " const container=plugEl('pluginPaths');if(!container)return;", "retire reference path rendering")
    add_start = html.index("plugEl('pluginAddPath').addEventListener('click',()=>{")
    add_end = html.index("plugEl('pluginScan').addEventListener('click',()=>{", add_start)
    html = html[:add_start] + """plugEl('pluginAddPath').addEventListener('click',()=>{
 plugEl('pluginScanStatus').textContent='Folder selection requires the desktop app.';
});
""" + html[add_end:]
    html = once(html, 'Windows VST3 scan requires installed native host · GitHub Pages cannot access local plugin files',
        'VST3 scanning requires the desktop app.', 'concise reference scan feedback')
    # Keep existing IDs/handlers and preference state; change only presentation.
    html = once(html, """title="Toggle favorite">★</button>""",
        """aria-pressed="'+String(!!p.favorite)+'" title="Toggle favorite"></button>""", "favorite vector")
    html = once(html, """title="Hide or show effect">'+(p.hidden?'◉':'⊘')+'</button>""",
        """aria-pressed="'+String(!!p.hidden)+'" title="Hide or show effect"></button>""", "visibility vector")
    # On resize/open, clamp actual fixed bounds including prior drag or native
    # first-show centering. No polling or meter work.
    html = once(html, "function setBrowser(open){", """function fitPluginWindow(){
 const panel=plugEl('pluginBrowser');
 if(panel.hidden)return;
 const r=panel.getBoundingClientRect(),cs=getComputedStyle(panel),gap=4;
 const x=Math.max(gap,Math.min(innerWidth-r.width-gap,r.left));
 const y=Math.max(gap,Math.min(innerHeight-r.height-gap,r.top));
 if(Math.abs(x-r.left)>.5){
  const left=parseFloat(cs.left);
  panel.style.setProperty('left',((Number.isFinite(left)?left:panel.offsetLeft)+x-r.left)+'px','important');
  panel.style.setProperty('right','auto','important');
 }
 if(Math.abs(y-r.top)>.5){
  const top=parseFloat(cs.top);
  panel.style.setProperty('top',((Number.isFinite(top)?top:panel.offsetTop)+y-r.top)+'px','important');
 }
}
window.addEventListener('resize',()=>{if(!plugEl('pluginBrowser').hidden)requestAnimationFrame(fitPluginWindow)},{passive:true});
function setBrowser(open){""", "bounded manager viewport")
    html = once(html, "renderPluginCatalog();renderScanPaths();", 
        "renderPluginCatalog();renderScanPaths();fitPluginWindow();", "fit open manager")
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
