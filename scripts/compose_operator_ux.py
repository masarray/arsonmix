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
        "const panel=plugEl('pluginBrowser'),parent=document.querySelector('.console-pane');\n  const bounds=parent?.getBoundingClientRect?.();",
        "const panel=plugEl('pluginBrowser');\n  const bounds={left:0,top:0,width:window.innerWidth,height:window.innerHeight};",
        "drag within entire window")
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
    plugin_path.write_text(plugin,encoding="utf-8")
    if html.count('id="trackCount"')!=1 or 'id="pluginMinimize"' in html:
        raise ValueError("Operator UX DOM invariants rejected")
    return html
