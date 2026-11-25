# Harmonice H32 — lead-first factory presets and centered bypass

Base: 717f8dc5b5c8a26def95f87fcf87cd2aeee21b61, global integration PR #899. Owner screenshot has window center ~791,445 and amber glyph center ~792,462; the overlay starts below a 32px header. Keep the header interactive/undimmed and compensate the measured header in the Harmonice shade only, retaining shared accepted slot/rack/global/unavailable authority.

## Factory voicing candidates

|Preset|Mode|Voices|Color|Space|Wet|Gain dB|
|---|---|---:|---:|---:|---:|---:|
|Double Light|Doubler|unchanged|74|48|36|0|
|Double Wide|Doubler|unchanged|80|82|52|0|
|Natural Duo|Harmony|2|75|68|52|0|
|Soft Trio|Harmony|3|75|77|65|0.6|
|Wide Quartet|Harmony|4|78|85|60|-0.5|
|Lush Five|Harmony|5|76|88|66|-1|
|Tight Choir|Harmony|4|68|58|56|-0.5|
|Close Blend|Harmony|2|68|44|34|0|
|Warm Ballad|Harmony|3|66|68|48|0|
|Pop Chorus|Harmony|3|82|85|58|0|
|Gospel Ensemble|Harmony|5|73|88|69|-1|

Soft Trio exactly follows the owner's screenshot values (3 voices, 75/77/65%, +0.6 dB). Other values are engineering audition candidates, not acoustically certified. Wet is additive backing amplitude; H31 has compensation above .45. Lower wet and differentiated width for lead clarity; avoid generic maximum-width/maximum-wet settings. No change to DSP, default insertion, state schema, key, guide source, correction, or saved user/session settings. Existing factory IDs retained. Doubler remains a single unison take; stored harmony count untouched.

References: [TC-Helicon VoiceLive 3 manual](https://manualzz.com/doc/8333295/tc-helicon-voicelive-3-reference-manual), [Waves Harmony](https://www.waves.com/plugins/waves-harmony), [iZotope vocal chain](https://www.izotope.com/en/learn/crafting-a-basic-vocal-chain), [MDN CSS centering](https://developer.mozilla.org/en-US/docs/Learn_web_development/Howto/Solve_CSS_problems/Center_an_item). References inform principles only, not numeric parameter equivalence.

## Qualification

Existing assembled Chromium tests now assert exact factory settings sent through native transport, matching ACK and readback, preserved musical context, and label geometry across the existing multi-viewport bypass matrix. Physical singer A/B still required: match output level, male/female registers, vibrato, consonants, dense backing, mono fold, key correctness, peak/headroom and ASIO XRUN. Do not claim listening pass based on synthetic test or screenshots. No public release until RC exact-head checks and field acceptance.
