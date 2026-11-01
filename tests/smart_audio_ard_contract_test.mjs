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
const currentPrd = read('docs/PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md');
const currentArd = read('docs/ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md');
const executionLock = read('docs/EXECUTION_LOCK_UNIVERSAL_LIVE_CONSOLE_V6_20261101.md');

test('active entrypoints follow PRD/ARD v5 and execution lock v6, not retired v1', () => {
  for (const content of [agents, readme]) {
    assert.match(content, /PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101\.md/);
    assert.match(content, /ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101\.md/);
    assert.match(content, /EXECUTION_LOCK_UNIVERSAL_LIVE_CONSOLE_V6_20261101\.md/);
    assert.ok(content.includes(issue), 'hardware crackle blocker must remain visible');
  }
  for (const [label, content] of [
    ['PRD v5', currentPrd], ['ARD v5', currentArd], ['Execution lock v6', executionLock]
  ]) {
    assert.ok(content.includes('ChannelId') || content.includes('channel'),
      label + ' must describe real mixer channels');
  }
  assert.match(executionLock, /one accepted PCM graph/i);
  assert.match(executionLock, /CH11/);
  assert.match(executionLock, /rollback/i);
  // The old documents still exist as historical technical references.
  // They are no longer the active instruction linked by repo entrypoints.
  assert.ok(read(visualLock).length > 100);
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
    '±100', '±300', '≥30m', 'rollback',
    'affinity', 'hardware', 'GUI v0.8'
  ]) assert.ok(impl.includes(concept), 'missing blueprint concept: ' + concept);
});
