# ArSonMix Hardware Key V1 — approved MUTE/SOLO material as the global button token

**Source:** owner-approved HTML MUTE/SOLO hardware button prototype, 2026-11-02.  
**Integration source:** design-references/audition-mixer-study/index.html (appended CSS token before the closing stylesheet).  
**Workstreams:** GUI visual authority #278 → native-adaptation GUI #271.  
**Safety:** CSS-only design token. No JS action/mixer state, PCM, routing graph, WebView bridge, native C++, or CI workflow changes.

## Mechanical visual contract

- **Sharp-edged with a very small radius:** \`--am-hw-key-radius:2px\`. Not a pill.
- **Top rim:** a **2px edge-aligned** left-transparent → white-at-center → right-transparent line; **no wide interior glossy band**.
- **Bottom rim:** same fade at **1px**, lighter than upper rim.
- **Rest:** a neutral or semantic face gradient within the same key; thin horizontal face highlights, mild bevel; an outer **2px physical bottom edge plus 3px cast shadow**, applied identically to off and on.
- **Physical press:** only \`:active\` translates down **2px** and removes outside cast shadow; the resting/latching state stays raised. MUTE/SOLO activation changes color **but does not flatten** the key permanently.
- **Semantic faces:** neutral silver, top toolbar dark graphite, MUTE red, SOLO warm yellow, PFL cyan, direct-action green. Face color is defined by \`--am-hw-key-face\`; text and border by \`--am-hw-key-ink\` and \`--am-hw-key-border\`. Do not fork bevel/shadow by color.
- **No shading drift:** edge pseudos use \`top:0\` or \`bottom:0\` against the button's own box (not an inset child positioned at 2–7px).
- **Low-cost GPU:** no CSS filter/backdrop-filter, no permanent per-frame animation. Reduced-motion rule disables transitions.

## Scope now wired in the live GUI reference

- Channel, Master **MUTE / SOLO / PFL**
- Main toolbar and More menu hardware command keys
- Channel identity Save/Cancel
- Plugin footer actions
- Route-choice dialog controls, except actual circular routing jacks

For future push-buttons, add \`class="hw-key"\`; do **not** add it to fader thumbs, rotary knobs, LED meters, source/destination WIRE circles, or compact disclosure/canvas nodes. Hardware design tokens are shared, but input widgets are not all physically the same shape.

## Native integration contract for #271

1. Use one shared hardware-key CSS module in the actual WebView stylesheet source rather than hand-editing generated \`web/styles.css\` only.
2. Keep the existing \`.btn-180\` and state/ARIA mapping as compatibility hooks, but replace the *visual material* on the native production branch after verifying same rendered result. **Do not alter ChannelId/Intent/Ack or control handlers**.
3. Rebuild the generated standalone HTML through the existing repo generator and validate CSS parity; no new mixer mock or fake audio telemetry.
4. Screenshot at 1366×768, 1680×900, 1920×1080, normal/active/latched MUTE/SOLO/PFL and narrow bank; compare face shape and edge glints to the owner-approved HTML. Do not claim exact pixel equivalence without screenshots.
5. Keep PR #278 as design authority and PR #271 as native implementation gate. Do not merge #278 wholesale into #271 or main.

**Acceptance (source):** same physical dimensions; shared material across push buttons; edge gradients centered and edge-aligned; cast shadow disappears during actual press; semantic colorways preserved; patch circles/faders untouched; no changes to JS script or motor fader/routing graph.
