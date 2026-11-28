"""Approved rounded micro key shape, composed in the existing CSS owner.

Keep the historical V28 source and original PNG state tiles byte-identical.
Only deployed control material changes; no event, native or audio state change.
"""
from pathlib import Path

def apply_rounded_micro_keys(assets: Path) -> None:
    path = assets / "mixer-utility-controls.css"
    css = path.read_text(encoding="utf-8")
    # Width/min-width occur in base, phase and remove rules. Height remains 17px.
    # The 25x15 format key retains its existing independent dimensions.
    anchors = (("width:17px;", "width:21px;", 6),
               ("border-radius:0;", "border-radius:4px;", 1))
    for before, after, count in anchors:
        if css.count(before) != count:
            raise ValueError("Rounded micro key CSS anchor drift: " + before)
        css = css.replace(before, after)
    css = css.replace("V20 true-square metal faces", "Rounded rectangular metal faces")
    # The vacant insert is not a control, but it reserves the exact same
    # horizontal lane as the instantiated 21x17 FX power key.
    rack = assets / "mixer-fx-rack.css"
    rack_css = rack.read_text(encoding="utf-8")
    old = "flex:0 0 17px;width:17px;height:17px;min-width:17px;"
    new = "flex:0 0 21px;width:21px;height:17px;min-width:21px;"
    if rack_css.count(old) != 1:
        raise ValueError("Rounded micro key FX placeholder anchor drift")
    rack_css = rack_css.replace(old, new, 1).replace(
        "Preserve its 17px hardware", "Preserve its 21px hardware", 1)
    path.write_text(css, encoding="utf-8")
    rack.write_text(rack_css, encoding="utf-8")


    # Shared recessed face; the button retains its original hit box.
    rack_key = '.app .console-body .strip button.mx-util-key:is([data-util-kind="fx-main"],[data-util-kind="fx-slot"])[aria-pressed]'
    editor_key = 'body :is(.mx-peq-global,.mx-comp-power,.mx-harm-power,.armix-builtin-fx-power)'
    face = """radial-gradient(circle closest-side,
      transparent 0 63%, #292929 64% 70%, #151515 71% 72%,
      var(--fx-led-dim) 73.5%, var(--fx-led-bright) 77.2%,
      var(--fx-led) 79.1%, var(--fx-led-mid) 85%,
      var(--fx-led-dim) 94%, #202426 96%, transparent 97%),
      radial-gradient(ellipse at 50% 75%,#4d4d4d 0%,#222 65%,#191919 100%) center/64% 64% no-repeat,
      linear-gradient(180deg,#111,#292929 28%,#686868 62%,#c2c2c2)"""

    def palette(state="off"):
        if state == "on":
            return '--fx-led:#00b6d5;--fx-led-bright:#00c7e6;--fx-led-mid:#078ba5;--fx-led-dim:#164b55;--fx-ink:#00badd;'
        if state == "rack-off":
            return '--fx-led:#c38b42;--fx-led-bright:#e7b86e;--fx-led-mid:#93602d;--fx-led-dim:#513b27;--fx-ink:#f0c483;'
        return '--fx-led:#34393b;--fx-led-bright:#42494c;--fx-led-mid:#292e30;--fx-led-dim:#24282a;--fx-ink:#9ba8ae;'

    def cap(selector, size):
        return f'''{selector}::after{{
 content:"";display:block;position:absolute;pointer-events:none;
 width:{size}px;height:{size}px;left:50%;top:50%;
 transform:translate(-50%,-50%);border-radius:50%;z-index:-1;
 background:{face};
}}
{selector}:active:not(:disabled)::after{{transform:translate(-50%,-50%) scale(.96);}}
'''
    # Keep the existing ::before hit envelope and accessible SVG glyph.
    # Only FX power loses the old sprite; phase, format and remove are untouched.
    global_key = rack_key.replace('.console-body', '.console-body.global-fx-bypassed')
    css += '\n/* Recessed FX power: original 21x17 hit box, circular 17px face. */\n'
    css += f'{rack_key}{{{palette()}background:transparent;box-shadow:none;isolation:isolate;color:var(--fx-ink);}}\n'
    css += f'{rack_key}[aria-pressed="true"]{{{palette("on")}}}\n'
    css += f'{global_key}{{{palette()}color:var(--fx-ink);background:transparent;}}\n'
    css += cap(rack_key, 17)
    css += f'{rack_key} svg.mi-svg{{width:9px;height:9px;}}\n'
    path.write_text(css, encoding="utf-8")

    # Replace only the shared editor power chrome in the composed owner.
    # Keep accepted state, rack gate, unavailable state and hit dimensions.
    context = assets / "mixer-contextual-buttons.css"
    editor = context.read_text(encoding="utf-8")
    start = editor.index(editor_key + '{')
    end = editor.index('\n', editor.index(editor_key + ':focus-visible', start))
    old = editor[start:end]
    if old.count('[data-fx-state=') != 4:
        raise ValueError("Built-in FX power chrome anchor drift")
    new = f'''{editor_key}{{
 display:inline-flex!important;align-items:center!important;justify-content:center!important;
 box-sizing:border-box!important;flex:0 0 26px!important;width:26px!important;min-width:26px!important;max-width:26px!important;
 height:24px!important;min-height:24px!important;max-height:24px!important;
 position:relative!important;isolation:isolate;opacity:1!important;padding:0!important;
 border:0!important;border-radius:50%!important;background:transparent!important;box-shadow:none!important;
 font-size:13px!important;font-family:"Segoe UI Symbol","DejaVu Sans",sans-serif!important;
 font-weight:400!important;line-height:1!important;letter-spacing:0!important;text-align:center!important;
 filter:none!important;margin:0!important;transition:none!important;text-shadow:none!important;
 {palette()}color:var(--fx-ink)!important;
}}
{editor_key}[data-fx-state="on"]{{{palette("on")}}}
{editor_key}[data-fx-state="rack-off"]{{{palette("rack-off")}cursor:not-allowed!important;}}
{editor_key}[data-fx-state="unknown"]{{--fx-ink:#647078;cursor:not-allowed!important;}}
{editor_key}:focus-visible{{outline:2px solid #9cdbe7!important;outline-offset:2px!important;}}
'''
    new += cap(editor_key, 24)
    editor = editor[:start] + new + editor[end:]
    # SonKu's old rectangle-specific exception must not paint a second rim.
    sonku_start = editor.index('body #mxNativeSonKu #skPower:not(')
    sonku_end = editor.index('}', sonku_start) + 1
    editor = editor[:sonku_start] + editor[sonku_end:]
    context.write_text(editor, encoding="utf-8")
