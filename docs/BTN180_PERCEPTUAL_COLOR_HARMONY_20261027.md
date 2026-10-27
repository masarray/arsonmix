# ArSonMix BTN-180 semantic color harmony · 2026-10-27

Goal: consistent bright-on-dark digital-mixer pad illumination without oversaturation. Reference was the user's actual browser screenshot of the mixer. Unlike choosing equal RGB intensities, perceptual color balance is based on the face color's OKLab lightness (target 0.74–0.85 for distinct roles) and a dark-caption contrast threshold of >=6:1. Color psychology is contextual, not a claim of a single universally pleasurable palette.

| Role | ON face | Identity / meaning |
|---|---|---|
| Utility HPF / polarity | `#83d6e0` | cool cyan signal conditioning |
| Left rail | `#e3a6d8` | soft magenta selected presets and tabs |
| MUTE / mono | `#f38895` | bright coral red warning, not harsh pure red |
| SOLO | `#edc669` | musical amber-yellow |
| STEREO | `#77baeb` | brighter medium sky blue |
| Native Audio Device connected + no error | `#79dd9e` | bright soft green ready state |
| PRE / master tools | `#c4a9e8` | soft violet |
| Toolbar view selected | `#f0f1f3` | neutral white navigation selected |
| All OFF | `#b3b3b3` | consistent neutral grey |

The shadow/press geometry of BTN-180 remains **bit-identical** to PR #81. All accents share one palette in `web/styles-src/20-button-180.css`; the stereo and mono face references in `42-mixer-premium.css` consume those same tokens. A single `--b180-border` per semantic state adds edge depth without blur or fake glow.

Audio Device: the `btn-180 spec spec-device` cap now matches toolbar height/padding/letter spacing. The previous `.spec strong {color:#87e7ff}` tint is overridden only for `#headerDriver`. In preview the device is necessarily neutral OFF. Native bridge listeners illuminate it green only after a healthy connected device is reported, and return it to neutral for disconnected/error. Do not confuse visual simulation in test screenshots with an actual connection.

Acceptance: deterministic stylesheet/WebView2/standalone HTML parity; muted red/amber/blue face updates, green simulated CSS screenshot, no changes to keyboard/press geometry, CSS token contrast/lightness regression, Firefox UI smoke, Windows CTest. No DSP/native processing behavior has been changed.
