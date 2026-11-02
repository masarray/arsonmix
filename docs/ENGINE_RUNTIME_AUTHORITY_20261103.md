# ArSonMix — ONE production engine, GUI-directed ownership
Date: 2026-11-03. Status: **binding engine ownership map** (code-enforced for existing M4 callback, NOT universal live certification).

## Only one runtime authority

```text
PR #278 + #285 approved GUI workflow (reference only, not audio)
                   |
PR #271 web native renderer → ConsoleLiveBridgeV6 snapshot/intent/ACK
                   |
          Native JUCE LiveAudioEngine
                   |
       NativeRenderAuthority (one selector)
         |              |                |
   UNPATCHED       ACCEPTED M4      DIAGNOSTIC
    silence      one real mixer      test-only 0FX
                       |
                LiveDryRoutingLab
                LiveChannelFxGraph
                PreparedMixerBlock
                       |
          existing CH1 original VST/Orbit lease
                AUX / MAIN / MONITOR
                       |
            actual verified device pins
```

The `NativeRenderAuthority` is an explicit, fixed-cost callback selector. **There is no second simultaneously running mixer.** The former standalone stereo plugin dispatch inside the diagnostic-only remainder was unreachable and is deleted, not hidden behind another flag. An unpatched NORMAL route stays silent and cannot silently fall back to stereo CH1.

## What is active, what is migration-only, what is test-only

| Ownership | Modules | Decision |
| --- | --- | --- |
| **PRODUCTION, temporary compatibility** | `src/LiveAudioEngine.h`, `src/LiveDryRoutingLab.h`, `engine/core/LiveChannelFxGraph.h`, `engine/core/PreparedMixerBlock.h`, native CH1 VST/Orbit lease | KEEP until truly equivalent unified render; bugfixes only, no new competing features |
| **Future canonical ChannelId engine (prepared, not yet production)** | `DynamicChannelRoster`, `DynamicPreparedMixerBlock`, `DynamicNativeSessionPublisher`, `DynamicChannelFxLeaseBank`, `UnifiedLegacyProductionAdmission`, `DynamicAcceptedSession` | KEEP as one migration target; **do not attach as a second callback** or advertise LIVE |
| **Compatibility translation** | `LegacyLiveGraphMigration`, accepted device epoch / actual port pin mapping | KEEP until cutover with exact PCM, VST/editor and Show parity; then retire in one controlled transaction |
| **Testing-only** | `PreparedNativeMixerSession.h`, dedicated offline dynamic test targets | No longer list testing-only session as shipped `ARMixConsole` source; keep its separate CTest target |
| **Diagnostics, no effect graph** | old stereo scratch path in `LiveAudioEngine`, diagnostic tone/capture, health and 0FX evidence | KEEP isolated with NO vendor plugin processing and NO production fallback |
| **Single non-RT worker** | `NativeShowSaveWorker` | KEEP Show I/O on worker, not on callback; no extra per-channel/per-plugin worker |

## Stop rules — prevents return of mixed engines

1. The approved reference GUI is the *workflow authority*. Native accepted snapshots, stable ChannelIds, real device pins and ACK remain the *data/PCM authority*. Never let JS or a browser mock own audio routing or create fake FX.
2. There is **one** `audioDeviceIOCallbackWithContext` production entry point and **one** selected render owner at any time. Do not run M4 and dynamic PCM, or duplicate native vendor VST3 instances.
3. New channels, Group, FX Return and WIRE must go into the **universal ChannelId graph**. No new business feature is allowed to extend fixed 10CH/M4 design, except fixes for actual physical audio reliability.
4. Freeze old plugin-feature experiments; preserve external #15 0FX crackle evidence and driver safety research (including draft #251). No destructive deletion or premature PR closure while hardware equivalence is unknown.
5. When universal graph passes real M4 migration/production gates, **remove** M4 callback selection, ordinal-as-identity adapters and legacy CH1 special cases in the **same integration PR**. The real diagnostic tone/capture remains a separate legitimate service, not a mixer.
6. Integration #270 alone certifies exact combined GUI/native SHA, Windows CTest/Chromium, VST/editor/Show parity and measured physical audio. Do not mark GUI as LIVE from screenshots or offline data.

## Decommission gates (NOT met yet)

- Real 10CH + 4AUX, MAIN/MONITOR, CH1 original vendor/Orbit/editor and Show state are rendered by the same accepted ChannelId graph.
- Normal continuous fader/MUTE/SOLO/SEND controls remain audible without mandatory device restart; no lost plugin tail or duplicated dry output.
- Native ACK/epoch/revision, output disarm/rearm and physical sparse port map are verified.
- Windows exact-SHA build/CTest and physical ASIO/WASAPI 0FX + 1FX FAT pass.

**Result:** no obsolete active stereo plugin engine remains available from the diagnostic fallback; current M4 compatibility is intentionally quarantined and will be retired only when the unified engine demonstrably replaces it.

## Reconciliation notice (2026-11-03)
ENGINE #266 is now the sole source branch after folding former #287. Cross-branch ownership, upstream drift, CI authority gate and the next real callback cutover are documented in [ENGINE_SINGLE_OWNER_HANDOFF_20261103.md](workstreams/ENGINE_SINGLE_OWNER_HANDOFF_20261103.md). The M1 experimental ZIP policy is historical; do not make another branch-specific Windows FAT producer without #270 integration ownership.
