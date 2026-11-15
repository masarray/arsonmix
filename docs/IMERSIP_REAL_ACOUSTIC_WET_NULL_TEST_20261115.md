# ImerSip: true acoustic wet and live PCM-evidence gate (15 Nov 2026)

User's real headphone verdict: effect remained virtually unchanged even as the
ImerSip implementation grew. The previous ImerSip calculated actual SonKu
P8.4 directional HRIR and P8.2 room but multiplied its already downmixed
wet SIDE by a second gain ~0.05 at default. The existing tests only proved
a near-zero numerical difference, not useful acoustics. Neither equal-looking
VU meters nor a successful SPSC ACK proves an effect is audible.

## Architectural repair
One existing native SonKuSpatialCore, same P8.2 six-wall image reflections
and same 3-band 8-line FDN. Its process method optionally copies already
computed EARLY and LATE stereo returns into a fixed 8192-sample array owned
by ImerSip; ordinary SonKu uses nullptr. No second DSP, no second reverb,
no additional 140-tap FIR, no callback allocation/mutex/FFT.

The ImerSip mixer now has independently slewed actual early/late returns,
P8.4 directional SIDE, and source direct L/R. The acoustic return is
highpassed at 220Hz (early) and 160Hz (late) so low-end pressure is provided
by existing coherent LF shelf, not phase-sensitive delayed bass. Only 32%
of early MID and 12% of late MID is blended; direct vocal is still
predominant. Source stays 1.6m; room remains large; no rotating sound,
new mute, global AGC or fake browser audio.

Add the real callback PCM delta energy ratio (and processed frame count) to
JUCE editor; distinguish NO PCM CALLBACK, SILENT INPUT, and nonzero
LIVE PCM DELTA -xx dB. This tests actual callback, not just slider/ACK.
New tests exercise 44.1/48k real PCM mono, independent and anti-correlated
stereo and require full room DIFFERENCE to original input above -24dB
RMS (source independent, tonal boosts OFF). This does not prove perceived
3D sound quality or externalization. Need loudness-matched owner audition
against verified SonKu P8.4 baseline and true peak/CPU FAT before release.

Do not merge into main or claim sound success while owner reports it is bad.

## Default preset audibility fix
Prior default Immersion 58% / Venue Space 45% left an already-weak room below the dedicated -32dB musical delta gate even though MAX settings passed. The new default is 82% / 65%, with unchanged 1.6m source and original direct stereo MID. The default null test uses the actual C++ Settings initializer, *not* duplicated numbers, so old near-bypass defaults cannot return unnoticed. Existing saved .smix is intentionally not silently overwritten; controls still need actual PCM delta readback. No claim of superior acoustic quality until owner FAT.
