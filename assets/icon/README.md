# ArSonMix approved application icon

Owner-approved channel-strip symbol: MUTE / SOLO / PFL, stereo meters and silver fader. Preserve its recognisable design and transparent exterior.

The committed asset package contains arsonmix-app.ico (7 embedded PNG sizes from 16 to 256 px), favicon.ico, PNG 16/24/32/48/64/128/180/192/256/512/1024, apple-touch-icon.png, Android 192/512 PNGs and site.webmanifest. The same web files are also checked in to site/ for standalone landing-page use. The web build deterministically reproduces them in public output.

Source fidelity: brand-source-128.png is a compact indexed/alpha derivative of the user's selected original, NOT the high-resolution original. Derivatives above 128 px are upscaled. The previously supplied standalone high-resolution brand ZIP preserves the original. Do not label this compact repo source as lossless.

Run: python scripts/sync_brand_assets.py
Check: python scripts/sync_brand_assets.py --check

Windows JUCE CMake embeds generated PNG icons into EXE/taskbar and applies the same image to native windows. arsonmix-app.ico is ready for packaging, shortcuts and installers. Changes do not touch DSP, VST3 or approved GUI design.
