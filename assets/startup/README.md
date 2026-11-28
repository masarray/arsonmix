# Cinematic startup background

The embedded JPEG is original ArSonMix artwork, rendered from the geometry in
[`render_background.py`](render_background.py). It contains no external photo,
texture, font or logo. The artwork and its editable generator are distributed
under the repository's AGPL-3.0-only license. The official product PNG remains
a separate embedded asset.

Run `python assets/startup/render_background.py` with Pillow installed to render
the 384 × 216 JPEG. The renderer bakes the lighting, vignette and blur offline.

Windows acceptance: cold/warm first paint, opaque full HWND guard, logo clarity, aspect crop and centering at 100/125/150/200% DPI, correct actual startup stages, ready/error dismissal, baseline time-to-first-paint/time-to-ready. Source contracts are not runtime timing or screenshot evidence.
