# Physical console button artwork

Original assets:
- `web/assets/buttons/SO-M-SLV.png`: inactive/neutral silver
- `web/assets/buttons/SO-M-RED.png`: MUTE active and MASTER MUTE active
- `web/assets/buttons/SO-M-GRE.png`: SOLO active, HPF/Ø enabled and A/B routing enabled

The image contains the bevel and edge shadow; CSS provides live label text, keyboard focus, pointer hover, pressed position and reduced-motion support. All assets are 54×37 RGBA. They are embedded in `web/styles.css` as byte-identical data URLs so the one-file HTML trial and the JUCE/WebView2 binary need no network or external asset dependencies.

The supplied `OFF-ON.png` / `OFF-OFF.png` images have the text OFF baked into the image and **are not used for controls whose ON state needs to be legible**, to avoid misleading users. They can be treated as style references.

Default dark-silver = up; stateful red/green = depressed by 1px; pointer press depresses 3px. Do not simulate depth with chunky cards or CSS-only candy gradients.
