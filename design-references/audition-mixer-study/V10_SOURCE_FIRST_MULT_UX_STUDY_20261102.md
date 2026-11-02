# ArSonMix — Source-first 1→N Channel Mult UX (decision record)

Date: 2026-11-02 | Isolated PR #278 | Source: CH1 → destinations CH3–CH6 | Status: **UX DESIGN DECISION, NOT NATIVE IMPLEMENTATION**

## Problem and scope

One physical/live audio source is required to feed four **independently processed** full mixer channels, not four source channels summed into one group, not four linked faders, and not sidechain-only. For the operator: CH1 → CH3, CH4, CH5, CH6 (each has its own insert rack, pan, M/S/PFL, fader, meter, and OUT). Input is captured once. Do not silently produce a fifth audible dry path to Master.

## Official competing app workflows (verified 2026-11-02)

| Product | Actual source → multi-destination operation | UX advantage | Limitation for this exact task |
| --- | --- | --- | --- |
| **Image-Line FL Studio** | Select source mixer insert, click send/routing switch below each destination insert; set each send amount. Also select several target tracks and use **Track routing → Route this track to selected** or **…selected only**. The latter disables current direct Master send. | Source-first, visual destination selection directly on console, bulk destination selection, no required intermediate bus | Compact arrow/switches can be cryptic to beginners; must show direction and avoid confusing sidechain and audible send |
| **PreSonus Studio One → Fender Studio Pro** | Channel **Add Send** targets existing Bus/FX Channel; drop effect onto Send rack to create an FX Channel instantly. Each Send has level, pan and pre/post-fader; **send source remains post-inserts**. Multiple Bus sends are explicitly described as **multing**. | Excellent fast FX creation, per-send state | Bus/FX destination role and per-send creation are more work than a single bulk route to four ordinary independently processed channels |
| **Adobe Audition** | Open Sends panel, enable a send, choose destination Bus, set pre/post-fader, level/pan. Up to **16 sends/channel** independent of primary OUT. | Explicit structured signal path, visible per-send controls | For 4 receiver channels needs destination Bus tracks and several repeated slot configurations |

Primary sources:
- FL Studio Mixer Menu, direct multi-target: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_mixermenu.htm
- FL Studio Mixer, clickable destination switches: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm
- FL Studio I/O routing, existing Master path behavior: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm
- Fender Studio Pro Effects Signal Routing and Sends: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm
- Adobe Audition routing and 16 sends: https://helpx.adobe.com/audition/desktop/mixing-multitrack-sessions/multitrack-routing-eq-controls.html

**Interpretation, not usability study**: the best conceptual fit is FL Studio's **source-first direct mult**, while Adobe's clear state/levels and Fender Studio Pro's efficient FX creation should be borrowed. Avoid claiming an independently measured usability ranking.

## Approved proposal: ArSonMix MULT / SPLIT source-first, in the same Live Console

1. **Select source CH1** (no pop-up needed). Its IN and existing dry OUT remain visible.
2. Click a compact **MULT ⇢** command in its ROUTE section. This arms a clearly indicated routing-selection mode; do not hide the FX rack, navigate to a new screen, or replace the main mixer with a DAW patch editor.
3. **Click CH3, CH4, CH5, CH6**, directly on their nameplates/target indicators; **Shift-click** a consecutive range to select CH3–CH6 together. Existing channel group selection and F2/double-click identity have no behavioral changes outside this mode.
4. The same nonmodal small anchored menu summarizes **CH1 → [CH3] [CH4] [CH5] [CH6] (4)**. Choose **Add parallel** (preserves CH1 direct OUT) or **Only selected** (explicitly removes CH1 direct MAIN, retaining source capture); no implicit duplicate audible MAIN contribution.
5. Optional advanced settings show **PRE INSERT** vs **POST INSERT** source tap independently of **PRE/POST FADER** and any destination send amount; do not label a pre-fader send as 'pre-FX'. Default source tap **PRE INSERT** for clean independently processed receiver lanes, when native graph supports it.
6. Click **Apply**. Each receiver's IN is explicitly labeled **CH1 · PRE INSERT [MULT]** (or the chosen tap), source shows **⇢4**, with a one-click **View receivers** action. All receivers remain full independent channel strips with their own FX/fader/OUT.
7. Editing a destination should not require reopening CH1: target's IN dropdown can select an existing internal source as an **advanced alternative**, but not the *primary* four-times-repeated operator workflow.

## Native/audio invariants — blocker before LIVE state

- Selection mode creates a **pending graph intent**, never GUI-local pretend live output. Native ACK returns revision/deviceEpoch and per-receiver accepted source binding; only after then mark each edge LIVE; failures keep the old audible graph intact.
- Multi-channel fanout reads a single immutable source buffer/tap; never four hardware capture callback instances. Publish prepared acyclic graph off audio thread, no realtime allocations/locks/scanner calls.
- Refuse source→itself, transitive cycles, deleted/incompatible target, stale output identity, accidental second direct Master feed, format/latency mismatch; no hidden hardware input replacement. If CH3–CH6 already have real inputs, disclose replacements in the review and require explicit operator acceptance.
- Audio output path must be explicit per destination. Monitor/ASIO safety persists; a best-effort WASAPI monitor is secondary and never clock-master for ASIO.
- Independent plugin PDC and deterministic latency policy: PRE INSERT split must not require CH1 insert latency. A source channel with 'Only selected' remains an audible source *through receivers*, not automatically muted.
- This is **not** GROUP/VCA, SENDS ON FADERS, or Sidechain. Bus/FX sends continue to coexist for normal group/return mixes.

## UX acceptance criteria to implement next (not yet shipped in V9)

- Scenario A: CH1 → CH3–CH6, one source selection, one MULT action, four destination clicks (or CH3 + Shift CH6), one Apply. No duplicate per-target dropdown ceremony.
- Scenario B: `Only selected` means CH1 is **not** fed directly to Main, but CH3–CH6 each can route to Main exactly once. No pop or level doubling during graph transition.
- Scenario C: Source badge ⇢4; each destination displays linked source identity/tap, independent fader/FX/meter, and clear nonlive/pending/failed states.
- Scenario D: Starting CH1→CH3–CH6 then attempting CH4→CH1 is rejected with no audible graph mutation; removal CH6 gives ⇢3.
- Scenario E: All 4 outputs independently isolated/bypassed tested with timestamped PCM, levels and CPU/latency baseline on real Windows native engine (V9 reference is never proof of this).
- Scenario F: FX rack/fader geometry from V9 must not change when MULT mode opens/closes; 1366/1680/1920px screenshots reviewed against LV1 hardware-console visual identity.

## Implementation ownership

- **PR #278 GUI prototype:** source-first MULT selection interaction, semantic signals, no fake live indicator; retire dropdown-only as primary path.
- **Native engine:** ChannelId & TapPoint fanout, immutable GraphIntent/ACK, topology validation, PCM/latency, deterministic callback execution.
- **Integration/FAT:** independently audible 4 destinations from 1 physical source, routing/monitor isolation and fault-case test.

Do not merge this GUI-only prototype as native functionality. The initial V9 receiver-side IN dropdown is useful as an advanced alternative **but is NOT the approved primary workflow**.
