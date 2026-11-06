# Cold WIRE and native VST3 recovery

Continues #418 at `16fe8c42`. The supplied Windows screenshots show two
different FX failures: stale routing publication and a blanket plugin-latency
rejection. The reported first-WIRE toast was not captured in those images;
cold admission is covered explicitly below rather than inferred from a
previously working OUT-dropdown transaction.

## Audited faults

1. The picker chose M4 for original CH1–10 while ChannelId already owned PCM,
   unless an unrelated WIRE edit had set `nativeBusStructural`. PCM authority
   now selects native insertion for all admitted INPUT/GROUP/FX_RETURN/MAIN
   channels. An occupied external CH1 vendor chain keeps its ownership guard.
2. The legacy compatibility insertion cloned `sourceTemplate_` with its old
   APPLY revision. It now stamps the validated current epoch/revision, and
   APPLY rejects stale revisions before expensive vendor preparation.
3. Native plugin admission compared a routing epoch to a device-start count.
   Reopening a device can make those counters differ. Admission now compares
   the actual routing epoch, consistently with accepted native readback.
4. Clicking an admitted native insert still queried M4's FX array. Editor
   acquisition now uses exact ChannelId, native PCM revision, slot and opaque
   instance token. The existing JUCE editor window retains the real owner.
5. Catalog refresh called the M4 FX painter even while ChannelId owned PCM.
   Its stale/empty FX array could erase an installed native rack in the GUI,
   and equal-revision telemetry would not rebuild it. The M4 painter now
   refuses to paint over native authority; a catalog-refresh regression
   verifies the accepted name/token survives.
6. A first drag could reach the bridge while SOURCE admission or another
   native intent was pending. One bounded latest CONNECT/ROUTE_ONLY gesture
   is retained, requests independent readback and dispatches only after native
   projection is ready. Epoch changes, changed mapped endpoint identities,
   failure and timeout discard it. No automatic OUT rewrite, output arm or
   optimistic wire is introduced.

## Fixed-latency VST3 support

The native graph now computes latency along the same prepared DAG. Each
fan-in aligns incoming edges and its own hardware source before processing.
PRE-fader edges in this kernel are post-insert, so they include that source's
FX latency. MAIN and MONITOR terminal timing is aligned as well. A bypassed
insert/rack retains its declared latency through an aligned dry delay, so
scalar bypass does not change compensation or rebuild the graph.

All delay storage is prepared on CONTROL, bounded by the graph scratch
budget and 131072 samples per path. Callback processing uses preallocated
stereo rings, with no heap/lock/device operation. The host reports actual
native plugin delay in snapshots/audio status. Excessive latency rejects
the candidate and retains the old graph.

A plugin changing its latency after preparation is conservatively rendered
as aligned dry with `latencyChanged` readback until reloaded. Automatic live
latency reconfiguration is not claimed. Legacy external CH1 paths retain
their existing restrictions; this is not a license to remove their guards.

## Regression evidence

- `approved_native_cold_start_smoke.py`: compiled native DOM, real mouse drag
  from G0 before admission, no OUT priming, native picker insert on an original
  channel with structural=false and unrelated M4 revision, then editor token.
  Three desktop sizes. Its backend is emulated for UI isolation.
- `dynamic_latency_compensation_test.cpp`: actual sample impulses through
  source latency, serial/parallel PRE/POST sends, receiver hardware fan-in,
  MAIN/MONITOR, varying callback sizes, zero render allocations and rejection
  rollback on budget overflow.
- `live_channel_fx_graph_test.cpp`: real JUCE AudioProcessor sample delay,
  wet/individual/rack bypass timing, zero render allocations, latency-change
  fallback and existing vendor-instance migration/ownership regressions.
- Existing decoder/channel-to-channel/source-only PCM and approved GUI bundle
  provenance checks remain required.

Physical user-rig ASIO/WASAPI/VST3 listening and soak remain PENDING. Software
tests do not prove that a third-party vendor plugin or a public rig is safe.
