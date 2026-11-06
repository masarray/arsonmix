"""Approved single-GUI FX rack power semantics, injected at the ONE Pages build.

The historical V28 source HTML is immutable. Apply exactly FOUR audited
semantic substitutions to its JavaScript when assembling both public /preview/
and native JUCE WebView2. No additional stylesheet, second state store, or
private fork of the mixer. t.fxOn is rack authority, slotBypass[] is the
preserved per-insert preference. Effective = fxOn && !slotBypass[i].
"""
def apply_authoritative_fx_power(html: str) -> str:
    substitutions = [
        (
            "name==='None'?'is-empty':t.slotBypass[i]?'is-bypassed':'is-active'",
            "name==='None'?'is-empty':(!t.fxOn||t.slotBypass[i])?'is-bypassed':'is-active'",
        ),
        (
            'aria-pressed="\'+Boolean(!t.slotBypass[i])+\'">',
            'aria-pressed="\'+Boolean(t.fxOn&&!t.slotBypass[i])+\'">',
        ),
        (
            "title=\"'+(t.slotBypass[i]?'Enable':'Bypass')+' only insert",
            "title=\"'+(!t.fxOn?'Rack bypassed, saved '+(t.slotBypass[i]?'OFF':'ON'):t.slotBypass[i]?'Enable':'Bypass')+' only insert",
        ),
        (
            "aria-label=\"'+(t.slotBypass[i]?'Enable':'Bypass')+' only FX insert",
            "aria-label=\"'+(!t.fxOn?'Rack bypassed, saved '+(t.slotBypass[i]?'OFF':'ON'):t.slotBypass[i]?'Enable':'Bypass')+' only FX insert",
        ),
    ]
    for before, after in substitutions:
        if html.count(before) != 1:
            raise ValueError("FX master semantics anchor missing or ambiguous: " + before[:90])
        html=html.replace(before,after,1)
    return html
