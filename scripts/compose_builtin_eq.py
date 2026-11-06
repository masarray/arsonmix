"""Deterministic additive Built-in Channel EQ in the SINGLE approved mixer.

The frozen V28 internal HTML source remains unmodified. Called from BOTH
the Pages and JUCE resource assembler and checked by the publication contract.
No independent EQ HTML page; a compact FX-rack affordance only.
"""
SCRIPT = '<script defer src="assets/mixer-builtin-eq.js?v=builtin-eq-native-1"></script>'
HEADER_BEFORE = '</span><span class="push"></span><button class="mx-util-key"'
HEADER_AFTER = """</span>'+(t.kind==='master'?'':'<button type="button" class="mx-eq-open" data-action="openBuiltinEq" title="Open lightweight built-in channel EQ" aria-label="Open EQ for '+escapeHtml(t.name)+'">EQ</button>')+'<span class="push"></span><button class="mx-util-key" """
HEADER_AFTER = HEADER_AFTER.rstrip()
CLICK_BEFORE = " if(a==='openPlugin'){openFastPicker(t,Number(btn.dataset.slot),btn);return;}"
CLICK_AFTER = " if(a==='openBuiltinEq'){window.ArSonMixBuiltinEq?.open(t);return;}"

def compose_builtin_eq(source: str) -> str:
    if source.count('</head>')!=1 or SCRIPT in source or 'openBuiltinEq' in source:
        raise ValueError('Builtin EQ source must be pristine approved seed')
    if source.count(HEADER_BEFORE)!=1 or source.count(CLICK_BEFORE)!=1:
        raise ValueError('Builtin EQ expected exactly one canonical FX owner hook')
    return (source.replace('</head>',SCRIPT+'\n</head>',1)
                  .replace(HEADER_BEFORE,HEADER_AFTER,1)
                  .replace(CLICK_BEFORE,CLICK_AFTER+'\n'+CLICK_BEFORE,1))
