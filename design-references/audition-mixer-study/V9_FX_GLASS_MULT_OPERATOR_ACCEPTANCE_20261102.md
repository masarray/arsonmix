# ArSonMix V9 — Elastic FX Rack, Console-Glass Meters & One-to-Four Channel Split

Date: 2026-11-02 | Owner-selected GUI reference PR #278 | Normative native boundary: PRD v5.2 §9 and ARD v5.2 §K.

## Operator-visible changes — isolated prototype, not audio

### 1. Section collapse must pay back FX rack, NOT resize live fader

- The fader remains at a deterministic physical height `--fader-fixed-h:clamp(198px,31vh,300px)` whenever FX is expanded; its thumb/pointer travel, dB gain ruler and native-equivalent meter framing are not remapped by Route/Send toggles.
- The FX rack is now the only elastic vertical section: `flex:1 1 var(--fx-h)`, with a scrollable occupied-insert list and one Add FX row. Route/Send/identity/Pan/M-S-PFL/footer remain fixed height. Identical constraints are applied to the left section rail and Master.
- Collapse Route (~76px → 23px) -> FX receives reclaimed pixels. Expand Send (~23px → 103px) -> FX gives up those pixels, with no fader motion. If FX is itself intentionally collapsed, the fader can use the remaining space as a special case.
- The <680px-height media rule lowers the physical fader height and upper row dimensions to preserve visibility; never claim a physically usable mixer at arbitrarily tiny windows without layout review.

### 2. Meter as a recessed glass LCD instrument, not generic HTML VU

- Preserve independent stereo L/R LED lanes, mono single lane, independent dBFS ruler, clipping LED and segmented fixed green/yellow/orange/red bands. Do not confuse meter dBFS with fader gain dB.
- New multi-layer glass reflections at low alpha, milled recessed frame, colored luminous LED fill, high-contrast dBFS scale and dedicated illuminated clip lens. All are static CSS; each meter refresh still updates only the `--lit` property. No backdrop blur, canvas repaint, layout animation or WebAudio meters.
- Fader retains original silver/rose PNG assets and its machined dark rail, with matching satin-metal plates across Route/FX/Send/Pan and improved FX row readability. Physical source remains simulated in this reference.

### 3. CH1 → CH2/CH3/CH4/CH5 one-to-many split / mult (visual reference)

The consumer's request is **one source into four independent processing channels**. This is not `GROUP` (4 → 1), not `LINK FADERS` (control-only), and not `SEND → AUX/FX RETURN` (different role). The source channel is not replicated four times at the hardware-capture layer.

Reference GUI supports independent selection on each target's **IN**:
`CH01 PRE-FX SPLIT` or `CH01 POST-FX SPLIT`. These are GUI-only channel-input labels based on the immutable numeric source identity (rather than mutable channel name); 4 target selections show a `⇢4` fanout indicator on CH1. Follow-up sources can be chained but self/cyclic split bindings are rejected. The source's `OUT` may use `SOURCE ONLY (No Main)` to avoid an automatic extra dry contribution to MAIN. All operations clearly report **demo/no PCM**.

**Production contract — not implemented by V9**:

```text
Physical/virtual IN (verified PortRef, deviceEpoch)
  -> Input Channel CH1 (stable ChannelId, immutable PCM block)
     -> TAP PRE_INSERT (before CH1 FX) OR TAP POST_INSERT (after CH1 FX, before fader)
        -> ChannelSourceBinding(CH2, CH1 ChannelId, TapPoint)
        -> ChannelSourceBinding(CH3, CH1 ChannelId, TapPoint)
        -> ChannelSourceBinding(CH4, CH1 ChannelId, TapPoint)
        -> ChannelSourceBinding(CH5, CH1 ChannelId, TapPoint)
  -> optional CH1 direct OUT (explicit SOURCE ONLY disables direct feed)
Every receiver has independent FX inserts, fader, mute, pan, meter, primary OUT.
```

- Graph validation: reject alias/self/circular dependencies, duplicate audible Main edges, stale `deviceEpoch`/`baseGraphRevision`, incompatible width, phantom stereo pins, accidental +6 dB; device changes fail closed.
- Native structural update: validate and prepare off callback, publish immutable graph and issue `Ack(requestId,acceptedRevision)`, show LIVE only after PCM and output status verify. Multiple receivers can share a read-only source buffer, not multiply hardware capture callbacks. No allocations, locks, I/O or VST plugin loading inside realtime callback.
- Source-split from PRE/POST inserts must state exact latency compensation and headroom policy. Reconcile with serial GROUP, FX Return, independent MAIN/MONITOR and Show/snapshot. One-to-many fanout does not imply source fader changes receivers; choice of tap is explicit.
- Native GUI team owns `IN` picker + pending/ACK display; engine owns source fanout PCM + graph validation + PDC; integration/FAT owns audible four-receiver test with independent processor and output and a 30min 0FX acoustic baseline.

## Quality gates

Reference `smoke.test.cjs`: **162/162** DOM/behavior checks passed at V9 base, plus Source Only scenario to be confirmed at subsequent exact SHA; no native audio tested. Existing Chromium CI for previous V8 must not be reused as proof of V9. V9 must be independently rendered at 1366×768, 1680×900, 1920×1080. For every viewport test:

1. Fader height before/after Route collapse changes by ≤1px; FX height increases at least 40px.
2. Fader height before/after Send expand changes by ≤1px; FX height decreases instead.
3. Verify rail/channel/Master section edge alignment; no overlap, no truncated route or long FX strings; FX list scrolls when full; the last bank never creates a giant single channel.
4. Verify meter glass in real Chromium with source image comparison, at idle and demo level; never claim 'prettier than reference' without a human screenshot review.
5. Repeat density Comfort/Standard/Compact and plugin manager overlay tests. Headless Chromium gate is necessary, not sufficient for user visual signoff.

## Official UX references

- Waves eMotion LV1 classic User Guide: https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html
- Waves LV1 Sends on Faders: https://www.waves.com/emotion-lv1-tutorial-4-9-mixer-window-aux-mode
- Fender Studio Pro effects and multing: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm

**Status: isolated GUI reference. No real Native ChannelId consumer/PCM, audio callback change or physical VST3 processing in this PR.**
