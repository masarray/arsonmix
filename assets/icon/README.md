# ArSonMix approved application icon

The exact owner-supplied 1254 × 1254 RGBA image is archived unchanged as [brand-master-1254.png](brand-master-1254.png). SHA-256: `04d3be499e404d72518871f7d5062e6a2e57d2a5a12b83190d05d51c940a22da`.

This is the approved MUTE / SOLO / PFL channel-strip symbol with stereo meters, silver fader and transparent alpha. Do not redesign it, create alternative product marks or flatten its exterior transparency.

- `brand-512.png` is a high-quality Lanczos downsample **directly from the original 1254px master**. It supplies the website hero and main README; site/brand-512.png is the same file.
- `brand-source-128.png` is a master-derived favicon/taskbar source, not a lossless original.
- `python scripts/generate_brand_icons.py --out build/brand_icons` creates ICO (16/24/32/48/64/128/256), favicon PNG, Apple and Android assets, and scaled PNGs from the 128px source.
- `python scripts/sync_brand_assets.py --check` verifies all committed icon derivatives, site favicons and the 512px brand visual.
- JUCE CMake embeds Windows EXE/taskbar icons; Pages exposes site/favicon.ico, Apple/Android assets, and the high-resolution brand image. The logo does not imply installer availability.

Changing branding files must not modify DSP, VST3, audio routing or the one approved /preview/ mixer GUI.
