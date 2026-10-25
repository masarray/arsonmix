// SPDX-License-Identifier: AGPL-3.0-only
// Architecture-document contract. This enforces handoff/document presence, not runtime audio safety.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const read = path => readFileSync(path, 'utf8');
const agents = read('AGENTS.md');
const readme = read('README.md');
const old = read('docs/ARD_AR_MIX_LIVE_FX_HOST_v0.3.md');
const ard = read('docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md');
const impl = read('docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md');
const issue = 'https://github.com/masarray/arsonmix/issues/15';
const visualLock = 'docs/GUI_BASELINE_LOCK_2026-10-25.md';

test('all entrypoints direct engineers to canonical ARD and implementation blueprint', () => {
  for (const s of [agents, readme]) {
    assert.match(s, /ARD_SMART_AUDIO_ENGINE_V1_2026-10-25\.md/);
    assert.match(s, /SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT\.md/);
    assert.ok(s.includes(issue));
    assert.ok(s.includes(visualLock));
  }
  assert.match(old, /HISTORICAL 0\.3 DESIGN/);
  assert.match(old, /Current architecture \(overrides outdated v0\.3 proposals\)/);
});
test('ARD covers device-clock, realtime and hardware acceptance without false shipping claims', () => {
  for (const concept of [
    'render-device clock', 'ClockDomainBridge', 'AUTO_STABLE',
    'P2.5.2', 'P2.6', 'GUI v0.8', 'JUCE 8.0.15',
    'IAudioClient3', 'libsamplerate', 'capture discontinuity',
    'NOT YET IMPLEMENTED', 'FAT', 'VST3'
  ]) assert.ok(ard.includes(concept), 'missing ARD concept: ' + concept);
  assert.ok(ard.includes(issue));
});
test('implementation order, fake clocks, lifecycle and physical gates remain documented', () => {
  for (const concept of [
    'P0-00', 'P0-01', 'P0-02', 'P0-03', 'P0-04',
    'src/LiveAudioEngine.h', 'smart_clock_bridge_test.cpp',
    'autoStable', 'deviceGeneration', 'captureFifoUnderrunFrames',
    'renderUnderrunFrames', 'ClockDomainBridge',
    '±100', '±300', '30 min', 'rollback',
    'affinity', 'hardware', 'GUI v0.8'
  ]) assert.ok(impl.includes(concept), 'missing blueprint concept: ' + concept);
});
