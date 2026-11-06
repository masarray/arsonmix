# ArSonMix approved application icon

The owner-approved artwork is the mixer channel-strip symbol: MUTE / SOLO / PFL, stereo meters, and silver fader. Preserve its recognisable composition and exterior alpha; no replacement design.

- `brand-source-128.png` is the repository's size-optimised, indexed/alpha **taskbar source derived from the owner-provided original**. The high-resolution original is retained in the owner-supplied branding package (not reconstructed from this small source). Do not mislabel this 128px version as a lossless original.
- `python scripts/generate_brand_icons.py --out build/brand_icons` deterministically produces 16–1024 PNG sizes, multi-resolution ICO, Apple Touch, Android 192/512, favicon 16/32 and site.webmanifest using Python's standard library only.
- Windows JUCE CMake embeds generated 256/32 resources as EXE icons; the native title bar uses the same data.
- Pages publishes root favicon/PWA icons and the same preview favicon inside the native WebView2 resource set.

Do not alter the approved `/preview/` GUI, engine, signal routing, VST3, audio callback or license. No native compile or real hardware test is implied by icon generation.
