# Contributing to ArSonMix

**Status: CURRENT navigation (2026-11-11).** Development is not gated by a separate architecture committee or manual Actions approval. This page points to maintained code, exact tests and the existing smart CI mechanism. Do not use historical PRs or old workstream handoffs as the current branch.

## Start with the actual product

The **only approved GUI** is the deterministically assembled `/preview/` used by the JUCE WebView2 desktop app. The browser preview is presentation/reference, **not** a PCM producer. The native audio owner is one JUCE callback with one installed ChannelId/EdgeId graph; legacy M4 compatibility cannot silently override it.

Read [AGENTS.md](AGENTS.md), [current architecture ownership](docs/architecture/README.md), [feature-extension playbook](docs/architecture/FEATURE_EXTENSION_PLAYBOOK.md) and [smart CI policy](docs/SMART_CI_RUNNER_WORKFLOW.md). Then inspect *live* main, open PR HEADs and the actual touched code. [Maintainability issue #527](https://github.com/masarray/arsonmix/issues/527) records incomplete architecture work.

## Where to change behavior

| Intent | Primary existing owner | Nearest verification |
| --- | --- | --- |
| Logical tracks, WIRE, .smix edits | `src/NativeLogicalMixerControl.h`, `src/NativeLogicalMixerWorkspace.h`, `src/NativeMixerSessionStore.h` | `tests/native_mixer_session_store_test.cpp`, `tests/approved_native_m2_contract.test.cjs` |
| Physical output/ASIO setup | `src/NativeDeviceApplyCoordinator.h`, `src/LiveAudioEngine.h`, `engine/device/` | `tests/native_device_apply_coordinator_test.cpp`, device/ASIO CTests |
| Routing / actual PCM callback | `engine/core/DynamicNativeSessionPublisher.h`, `src/LiveDryRoutingLab.h`, `src/LiveAudioEngine.h` | `tests/live_channel_fx_graph_test.cpp`, routing golden-PCM tests |
| Built-in or VST3 effect | `src/NativeBuiltInFx.h`, `src/NativeFxControlRequest.h`, `engine/core/DynamicChannelFxLeaseBank.h`, `src/NativeMixerSessionFxRestore.h` | `tests/native_fx_registry_extension_test.cpp`, FX lease/editor CTests |
| Native UI intents and readback | `web/approved-native-contract.js` (pure ACK, validation and presentation), `web/approved-native-bridge.js` (single browser/DOM adapter) | `tests/approved_native_m2_contract.test.cjs`, assembled Chromium UI |
| User-facing UI assets | `scripts/build_pages.py`, `scripts/build_approved_native_gui.py`, approved `site/preview-buttons/` inputs | native bundle provenance and Chromium visual checks |

A new FX should add a catalog descriptor, processor/editor specialization, state/PCM tests and accepted readback **without** creating a new callback, device manager, local JS graph or unrelated device handling branch. A new hardware output must require verified physical identity and preserve the logical .smix session across device loss.

## Cross-thread compatibility — check live HEAD, not a dated PR description

The architecture lane is not an integration gate. The latest merged FX-host safeguards are [#565](https://github.com/masarray/arsonmix/pull/565) (unique prepared processor instance token) and [#567](https://github.com/masarray/arsonmix/pull/567) (shared native control envelope and deterministic effect mailbox). [#564](https://github.com/masarray/arsonmix/pull/564) and [#568](https://github.com/masarray/arsonmix/pull/568) own GUI ACK/readback and presentation rules. A current follow-on [#569](https://github.com/masarray/arsonmix/pull/569) tightens malformed input types; treat it as **unmerged until the live PR says otherwise**.

Harmonice's active [#529](https://github.com/masarray/arsonmix/pull/529) builds on this shared `NativeFxControlRequest` for editor and user-preset actions; its vocal DSP and preset UI remain independently owned by that PR. The physical multi-output [#563](https://github.com/masarray/arsonmix/pull/563) edits device/graph/Main/bridge and may predate newer host changes. Do not overwrite either branch or cherry-pick its incomplete work. Before merging either, compare changed files to **current** `main`, resolve genuine semantic overlap, and qualify its own final combined HEAD. A green branch workflow alone is not proof of merged audio or field ASIO FAT.

Native FX command envelopes use **exact decimal-string ChannelId/epoch/revision**, a **string requestId**, an **integer insert slot**, and an optional **string workspaceToken** for logical editing. The `hpres-` user-preset request family is not a `harm-` realtime DSP mailbox sequence. `deviceEpoch="0"` represents logical-only work and must never authorize hardware output. Add/modify an FX by extending the shared host contract and its native registry test, not by copying type coercion into another plugin-specific route.

## Realtime safety and failure semantics

Use CONTROL/message thread for graph compilation, VST3 construction/destruction/state, device transactions, JSON/disk and editor lifetime. Audio callback must not allocate, block, lock, access disk or log. Publish prepared immutable data and retire leases safely after realtime readers finish. ASIO primary must never wait for WASAPI secondary work.

A UI intent is not complete on ACK alone: verify an independent accepted same-revision snapshot. `LOGICAL_ONLY`, `RETAINED`, `INSTALLED` and **audible hardware** are different facts. A rejected/stale command must preserve the previous accepted mixer and output safety. SOURCE ONLY must preserve channel PCM/meter while cutting MAIN output, without losing user FX state.

## Targeted validation and delivery

For JS protocol edits, use `node --test tests/approved_native_m2_contract.test.cjs` from the repo root and the relevant contract suites. For C++ work, choose the nearest CMake/CTest targets and build on the repository's supported JUCE/MSVC workflow. Do not skip full qualification for a changed native behavior. Use path-scoped **fast** checks while iterating; after green quick checks, the connected GitHub owner agent dispatches **one** exact-SHA `ui` for visual/gesture changes or **one** exact-SHA `native` for C++/ASIO/FX changes. When a hardware-test ZIP is required, use `fat` **instead of duplicate native**, and verify its SHA/artifact provenance. See the [runbook](docs/SMART_CI_RUNNER_WORKFLOW.md).

**Never call CI PASS physical FAT.** Actual ASIO routing, hotplug, plugin-editor continuity, audible PCM, noise and latency must be witnessed on equipment. Keep that evidence separate from unit tests and do not promote a product release on an unqualified ZIP.

When finishing a scoped PR, record changed responsibilities, main and PR SHAs, test results and failures, exact dispatched lane (or why none), rollback/previous accepted-state proof, physical FAT status and remaining work. Favor one cohesive change over many cosmetic cleanups.
