// SPDX-License-Identifier: AGPL-3.0-only
// CI cost policy: file-change -> relevant check. No new workflow or duplicate audio engine.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const read = path => readFileSync(path, 'utf8');
const native = read('.github/workflows/p1-windows.yml');
const ui = read('.github/workflows/ui-smoke.yml');
const pages = read('.github/workflows/pages-publish.yml');
const factory = read('.github/workflows/factoryfx-release-watch.yml');

// Restrict extraction to GitHub Actions path blocks, not arbitrary matching strings.
function pathsFor(yaml, event, nextEvent) {
  const start = yaml.indexOf('\n  ' + event + ':\n');
  const end = yaml.indexOf('\n  ' + nextEvent + ':\n', start + 1);
  assert.ok(start >= 0 && end > start, 'Missing trigger block: ' + event);
  const section = yaml.slice(start, end);
  const paths = [...section.matchAll(/^      - '([^']+)'$/gm)].map(match => match[1]);
  assert.ok(paths.length > 0, 'Missing path filters for ' + event);
  return paths;
}

function globMatches(pattern, filename) {
  let regex = '^';
  for (let i = 0; i < pattern.length; i++) {
    const char = pattern[i];
    if (char === '*' && pattern[i + 1] === '*') {
      regex += '.*';
      i++;
    } else if (char === '*') regex += '[^/]*';
    else if ('\\.+?^$()[]{}|'.includes(char)) regex += '\\' + char;
    else regex += char;
  }
  return new RegExp(regex + '$').test(filename);
}

const windowsPush = pathsFor(native, 'push', 'pull_request');
const windowsPR = pathsFor(native, 'pull_request', 'workflow_dispatch');
const uiPush = pathsFor(ui, 'push', 'pull_request');
const uiPR = pathsFor(ui, 'pull_request', 'workflow_dispatch');
const selected = (paths, file) => paths.some(p => globMatches(p, file));

test('visual GUI changes never consume Windows native runner on a PR or on main', () => {
  for (const filename of [
    'site/preview-v2/index.html', 'site/preview-v2/assets/mixer-fader-assembly.css',
    'web/mixer-layout.js', 'web/mixer-io.js',
    'AR-MIX-Live-FX-Host-Standalone-v0.3.html'
  ]) {
    assert.ok(selected(uiPR, filename), filename + ' must run frontend PR checks');
    assert.ok(selected(uiPush, filename), filename + ' must qualify Pages');
    assert.equal(selected(windowsPR, filename), false, filename + ' wrongly triggers native PR');
    assert.equal(selected(windowsPush, filename), false, filename + ' wrongly triggers native main');
  }
});

test('native PCM and device changes always use Windows/CTest, never Chromium by accident', () => {
  for (const filename of [
    'src/LiveAudioEngine.h', 'engine/core/DynamicPreparedMixerBlock.h',
    'tests/channel_routing_contract_test.cpp'
  ]) {
    assert.ok(selected(windowsPR, filename), filename + ' must qualify native PR');
    assert.ok(selected(windowsPush, filename), filename + ' must qualify native main');
    assert.equal(selected(uiPR, filename), false, filename + ' wrongly triggers browser PR');
    assert.equal(selected(uiPush, filename), false, filename + ' wrongly triggers browser main');
  }
  assert.match(native, /ctest --test-dir build -C Release --output-on-failure/);
  for (const target of [
    'ARMixPreparedMixerBlockTests', 'ARMixNativeMixerSessionTests',
    'ARMixLiveRoutingEpochTests', 'ARMixRTTests', 'ARMixLiveChannelFxTests'
  ]) assert.ok(native.includes(target), 'Missing vital audio test ' + target);
});

test('native WebView bridge and integration genuinely require both owners', () => {
  for (const filename of [
    'web/bridge.js', 'web/monitor.js', 'integration/console-v6/contract.json'
  ]) {
    for (const paths of [windowsPush, windowsPR, uiPush, uiPR]) {
      assert.ok(selected(paths, filename), filename + ' needs native+GUI checks');
    }
  }
});

test('docs and isolated CI tests do not fan out into unrelated builds', () => {
  const paths = [windowsPR, windowsPush, uiPR, uiPush];
  for (const filename of [
    'README.md', 'docs/workstreams/START_HERE_WIRE_FIRST_20261104.md'
  ]) assert.ok(paths.every(p => !selected(p, filename)), 'Doc triggered CI: ' + filename);
  for (const filename of [
    'tests/pages_wire_first_canonical_main_smoke.py',
    'tests/wire_graph_canonical_model.test.cjs',
    'tests/wire_main_native_handoff_contract.test.cjs',
    'tests/ci_workflow_efficiency_test.mjs'
  ]) {
    assert.equal(selected(windowsPR, filename), false);
    assert.equal(selected(windowsPush, filename), false);
    assert.equal(selected(uiPR, filename), true);
    assert.equal(selected(uiPush, filename), true);
  }
});

test('ordinary GUI PR and main use current targeted checks; full history is explicit', () => {
  assert.match(ui, /concurrency:\s+group: ui-smoke-/);
  assert.match(ui, /cancel-in-progress: true/);
  assert.match(ui, /python tests\/pages_wire_first_canonical_main_smoke.py/);
  assert.match(ui, /python tests\/pages_v55_context_wire_fader_smoke.py/);
  assert.match(ui, /node tests\/wire_main_native_handoff_contract.test.cjs/);
  assert.match(ui, /python tests\/pages_v51_portrait_led_peak_hold_smoke.py/);
  const fullOnly = [...ui.matchAll(/^\s{8}if: .*workflow_dispatch.*integration\/product-/gm)].length;
  assert.ok(fullOnly >= 35, 'Historical/extended GUI steps must be opt-in, found ' + fullOnly);
  assert.match(ui, /python tests\/pages_css_architecture_v27_smoke.py/);
  assert.match(ui, /retention-days: 3/);
});

test('Windows runs native realtime CTest; packaging is for main, manual and release branches only', () => {
  assert.match(native, /branches: \[main\]/);
  assert.match(native, /workflow_dispatch:/);
  assert.match(native, /cancel-in-progress: true/);
  const packageGates = [...native.matchAll(/^\s{8}if: .*integration\/product-.*milestone\/m4-/gm)].length;
  assert.equal(packageGates, 3, 'All 3 packaging/staging/ZIP steps must be gated');
  assert.match(native, /scripts\/verify_native_windows_artifact.ps1/);
  assert.ok(native.indexOf('scripts/verify_native_windows_artifact.ps1 -OutputDir dist') <
            native.indexOf('uses: actions/upload-artifact@v4'));
});

test('Pages is single guarded downstream publisher, never triggered by PR completion', () => {
  assert.match(pages, /workflows: \["UI smoke \(Chromium\)"\]/);
  assert.match(pages, /github\.event\.workflow_run\.event == 'push'/);
  assert.match(pages, /git ls-remote origin refs\/heads\/main/);
  assert.match(pages, /fetch-depth: 2/);
  assert.match(pages, /git diff --quiet HEAD\^ HEAD --/);
  assert.match(pages, /site\/ web\/ AR-MIX-Live-FX-Host-Standalone-v0\.3\.html/);
  assert.match(pages, /scripts\/build_pages\.py scripts\/build_ui_css\.py/);
  assert.match(pages, /Skipping Pages: no published GUI\/landing\/assets changed/);
  assert.match(pages, /needs\.build\.outputs\.fresh == 'true'/);
  assert.doesNotMatch(ui, /actions\/deploy-pages@/);
  assert.doesNotMatch(native, /actions\/deploy-pages@/);
  assert.match(factory, /schedule:/);
  assert.doesNotMatch(factory, /^\s{2}push:/m);
});
