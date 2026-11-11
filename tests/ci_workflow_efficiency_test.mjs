// SPDX-License-Identifier: AGPL-3.0-only
// CI cost policy: file-change -> relevant check. No new workflow or duplicate audio engine.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Native Actions path filters (not a legacy PR owner classifier) select lanes.

const read = path => readFileSync(path, 'utf8').replace(/\r\n/g, '\n');
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
const nativePrSelected = filename => selected(windowsPR, filename);
const uiPRSelected = filename => selected(uiPR, filename);

test('visual GUI changes never consume Windows native runner on a PR or on main', () => {
  for (const filename of [
    'site/preview-v2/index.html', 'site/preview-v2/assets/mixer-fader-assembly.css',
    'web/mixer-layout.js', 'web/mixer-io.js',
    'AR-MIX-Live-FX-Host-Standalone-v0.3.html'
  ]) {
    assert.ok(uiPRSelected(filename), filename + ' must run frontend PR checks');
    assert.ok(selected(uiPush, filename), filename + ' must qualify Pages');
    assert.equal(nativePrSelected(filename), false, filename + ' wrongly triggers native PR');
    assert.equal(selected(windowsPush, filename), false, filename + ' wrongly triggers native main');
  }
});

test('native PCM and device changes always use Windows/CTest, never Chromium by accident', () => {
  for (const filename of [
    'src/LiveAudioEngine.h', 'engine/core/DynamicPreparedMixerBlock.h',
    'tests/channel_routing_contract_test.cpp'
  ]) {
    assert.ok(nativePrSelected(filename), filename + ' must qualify native PR');
    assert.ok(selected(windowsPush, filename), filename + ' must qualify native main');
    assert.equal(uiPRSelected(filename), false, filename + ' wrongly triggers browser PR');
    assert.equal(selected(uiPush, filename), false, filename + ' wrongly triggers browser main');
  }
  assert.match(native, /ctest --test-dir build -C Release --output-on-failure/);
});

test('Full native/release qualification retains all CTests; isolated Harmonice FAT compiles only its verified targets', () => {
  assert.match(native, /-DBUILD_TESTING=ON/);
  const fullBuild = native.indexOf('run: cmake --build build --config Release --parallel 2');
  const fullCTest = native.indexOf('run: ctest --test-dir build -C Release --output-on-failure');
  assert.ok(fullBuild >= 0 && fullCTest > fullBuild,
    'Full CTest still requires the default all-target CMake build');
  assert.match(native, /focused_harmony_fat:/);
  assert.match(native, /FOCUSED_HARMONY_FAT: \$\{\{ github\.event_name == 'workflow_dispatch' && inputs\.package_fat == true && inputs\.focused_harmony_fat == true \}\}/);
  assert.match(native, /if: \$\{\{ env\.FOCUSED_HARMONY_FAT != 'true' \}\}/);
  assert.match(native, /if: \$\{\{ env\.FOCUSED_HARMONY_FAT == 'true' \}\}/);
  assert.match(native, /cmake --build build --config Release --parallel 2 --target \$targets/);
  assert.match(native, /'ARMixConsole'/);
  assert.match(native, /'ARMixHarmonyDspTests'/);
  assert.match(native, /'ARMixHarmonyChannelPcmTests'/);
  assert.match(native, /'ARMixNativeMixerSessionTests'/);
  assert.match(native, /'ARMixCrackleWindowTests'/);
  assert.match(native, /FOCUSED_FAT_MISSING_REQUIRED_TESTS/);
  assert.match(native, /FULL_SUITE_NOT_CLAIMED/);
  // An old PR can retain outdated workflow YAML; focused execution must
  // use a trusted main workflow and still build the exact PR input SHA.
  assert.match(read('scripts/chat_ci_dispatch.mjs'),
    /workflowRef=target\.lane==='ui'\?target\.branch:'main'/);
  assert.match(native, /HARMONY_FAT_SOURCE_SHA_MISMATCH/);
});

test('focused FAT preflights exact candidate CMake before 10-minute JUCE build', () => {
  const begin = native.indexOf('- name: Build actual JUCE app and focused Harmonice native tests');
  const end = native.indexOf('- name: Build executable and native RT tests', begin);
  assert.ok(begin >= 0 && end > begin, 'focused build section missing');
  const section = native.slice(begin, end);
  const source = read('CMakeLists.txt');
  // Previous failed source commit lacked this newer production audit target.
  // The main CI workflow must not blindly assume that it exists on a
  // long-lived candidate source branch.
  const originalTargets = section.slice(section.indexOf('$targets = @('),
    section.indexOf(')', section.indexOf('$targets = @(')) + 1);
  assert.doesNotMatch(originalTargets, /ARMixProductionRenderAuditTests/,
    'production audit reintroduced as unconditional legacy target');
  assert.match(section, /if \('M3ActualProductionRenderPathAndFailSilentCounters' -in \$available\)/);
  assert.match(section, /\$targets \+= 'ARMixProductionRenderAuditTests'/);
  assert.match(section, /Get-Content -LiteralPath 'CMakeLists\.txt' -Raw/);
  assert.match(section, /FOCUSED_FAT_UNDECLARED_CMAKE_TARGET/);
  assert.match(section, /FOCUSED_FAT_MISSING_REQUIRED_TESTS/);
  assert.ok(section.indexOf('FOCUSED_FAT_UNDECLARED_CMAKE_TARGET') <
    section.indexOf('cmake --build build'),
    'missing target should fail *before* the costly build');
  assert.ok(section.indexOf('FOCUSED_FAT_MISSING_REQUIRED_TESTS') <
    section.indexOf('cmake --build build'),
    'missing required CTest must fail during preflight');
  const requiredTargets = [...originalTargets.matchAll(/'([A-Za-z0-9_]+)'/g)]
    .map(x => x[1]);
  assert.ok(requiredTargets.length >= 11);
  for (const target of requiredTargets) {
    assert.match(source, new RegExp(
      '^\\s*(?:add_executable|juce_add_console_app|juce_add_gui_app)\\(\\s*' +
      target + '\\b', 'm'), 'main CMake missing mandatory '+target);
  }
  const testSection = native.slice(
    native.indexOf('- name: Test focused Harmonice PCM and ASIO boundary'),
    native.indexOf('- name: Test realtime control primitives'));
  const requiredBlock = testSection.slice(testSection.indexOf('$requiredTests = @('),
    testSection.indexOf(')', testSection.indexOf('$requiredTests = @(')) + 1);
  assert.doesNotMatch(requiredBlock, /M3ActualProductionRenderPathAndFailSilentCounters/);
  assert.match(testSection, /if \('M3ActualProductionRenderPathAndFailSilentCounters' -in \$available\)/);
  assert.match(testSection, /\$requiredTests \+= 'M3ActualProductionRenderPathAndFailSilentCounters'/);
  assert.match(testSection, /FULL_SUITE_NOT_CLAIMED/);
});

test('approved Pages to JUCE build seam is a native gate, never an unrelated CSS-only job', () => {
  for (const filename of [
    'web/approved-native-bridge.js',
    'web/approved-native-contract.js',
    'tests/approved_native_m2_contract.test.cjs',
    'scripts/build_approved_native_gui.py',
    'tests/approved_native_gui_bundle_test.py'
  ]) {
    assert.ok(nativePrSelected(filename), filename+' must run native build on PR');
    assert.ok(selected(windowsPush,filename), filename+' must rebuild native main bundle');
  }
  assert.match(native,/Check approved Pages GUI provenance before costly JUCE checkout/);
});

test('native WebView bridge and integration genuinely require both owners', () => {
  for (const filename of [
    'web/bridge.js', 'web/monitor.js', 'integration/console-v6/contract.json'
  ]) {
    for (const paths of [windowsPush, uiPush]) {
      assert.ok(selected(paths, filename), filename + ' needs native+GUI checks');
    }
    assert.ok(uiPRSelected(filename),filename+' needs PR Chromium tests');
    assert.ok(nativePrSelected(filename),filename+' needs PR native tests');
  }
});

test('docs and isolated CI tests do not fan out into unrelated builds', () => {
  const paths = [windowsPush, uiPush];
  for (const filename of [
    'README.md', 'docs/workstreams/START_HERE_WIRE_FIRST_20261104.md'
  ]) {
    assert.ok(paths.every(p => !selected(p, filename)), 'Doc triggered CI: ' + filename);
    assert.equal(uiPRSelected(filename),false,'Doc only needs lightweight PR admission: '+filename);
    assert.equal(nativePrSelected(filename),false,'Doc must not compile native: '+filename);
  }
  for (const filename of [
    'tests/pages_wire_first_canonical_main_smoke.py',
    'tests/wire_graph_canonical_model.test.cjs',
    'tests/wire_main_native_handoff_contract.test.cjs'
  ]) {
    assert.equal(nativePrSelected(filename), false);
    assert.equal(selected(windowsPush, filename), false);
    assert.equal(uiPRSelected(filename), true);
    assert.equal(selected(uiPush, filename), true);
  }
});

test('ordinary GUI PR and main use current targeted checks; full history is explicit', () => {
  assert.match(ui, /group: ui-smoke-/);
  assert.match(ui, /cancel-in-progress: \$\{\{ github\.event_name != 'workflow_dispatch' \}\}/);
  assert.match(ui, /python tests\/pages_wire_first_canonical_main_smoke.py/);
  assert.match(ui, /python tests\/pages_v55_context_wire_fader_smoke.py/);
  assert.match(ui, /node tests\/wire_main_native_handoff_contract.test.cjs/);
  assert.match(ui, /python tests\/pages_v51_portrait_led_peak_hold_smoke.py/);
  assert.match(ui,/historical_css:\n\s+description:.*\n\s+type: boolean\n\s+required: false\n\s+default: false/);
  const start=ui.indexOf('      - name: Mount pinned V24 historical CSS fixture');
  const end=ui.indexOf('      - name: Verify V28 exact approved /preview/ source');
  assert.ok(start>0&&end>start,'archived fixtures and current product checks must remain distinct');
  const archived=ui.slice(start,end).split(/(?=      - name: )/).filter(s=>s.trim());
  assert.equal(archived.length,28,'all archived tests and mount/restore steps remain available');
  for(const step of archived)
    assert.match(step,/if: \$\{\{ github\.event_name == 'workflow_dispatch' && inputs\.historical_css == true \}\}/);
  // Real product functional/geometry/provenance checks must never inherit
  // the historical opt-in or disappear with the old fixture mounts.
  for(const file of ['pages_wire_incoming_v43_smoke.py','preview_v15_drag_wire_smoke.py',
    'pages_m1_meter_gain_calibration_smoke.py','pages_m3_operator_visual_interaction_smoke.py',
    'pages_wire_v44_finish_smoke.py','pages_wire_v45_socket_layout_smoke.py',
    'pages_wire_v46_precision_smoke.py']) {
    const step=ui.split(/(?=      - name: )/).find(s=>s.includes('python tests/'+file));
    assert.ok(step,file+' stays required in current UI qualification');
    assert.ok(!step.includes('inputs.historical_css'),file+' was incorrectly made historical');
  }
  assert.match(ui, /python tests\/pages_css_architecture_v27_smoke.py/);
  assert.match(ui, /retention-days: 3/);
});

test('Windows realtime CTest is explicit for PR milestones; packaging requires deliberate FAT opt-in',()=>{
  assert.match(native,/branches: \[main\]/);
  assert.match(native,/^  workflow_dispatch:\n    inputs:\n      package_fat:/m);
  assert.match(native,/default: false/);
  assert.match(native,/^  quick:\n    name: Fast native contracts/m);
  assert.match(native,/^  windows-build:[\s\S]*?needs: quick\n/m);
  assert.match(native,/github\.event_name != 'pull_request'/);
  const fatOnly = [...native.matchAll(/^        if: .*inputs\.package_fat == true.*$/gm)].length;
  assert.equal(fatOnly,8,'All packaging, installer, sealing and cache steps must require deliberate FAT opt-in');
  assert.match(native,/Prove two authentic historical Inno releases upgrade and roll back/);
  assert.match(native,/scripts\/test_two_genuine_inno_releases_windows\.ps1/);
  assert.match(native,/FAT_EXACT_PR_HEAD_CHECKOUT/);
  assert.match(native,/ctest --test-dir build -C Release --output-on-failure/);
  assert.ok(native.indexOf('scripts/verify_native_windows_artifact.ps1 -OutputDir dist') <
            native.indexOf('uses: actions/upload-artifact@v4'));
});

test('Pages is single guarded downstream publisher, never triggered by PR completion', () => {
  assert.match(pages, /workflows: \["UI smoke \(Chromium\)"\]/);
  assert.match(pages, /github\.event\.workflow_run\.event == 'push'/);
  assert.match(pages, /git ls-remote origin refs\/heads\/main/);
  assert.match(pages, /fetch-depth: 2/);
  // The old first-parent only diff gate was unsafe: a docs/CI commit after
  // the GUI commit could suppress the final qualified /preview/ deployment.
  assert.doesNotMatch(pages, /git diff --quiet HEAD\^ HEAD --/);
  assert.match(pages, /always publish the assembled \/preview\//);
  assert.match(pages, /python tests\/single_preview_publication_contract_test.py dist-pages/);
  assert.match(ui, /python tests\/single_preview_publication_contract_test.py dist-pages/);
  assert.match(pages, /needs\.build\.outputs\.fresh == 'true'/);
  assert.doesNotMatch(ui, /actions\/deploy-pages@/);
  assert.doesNotMatch(native, /actions\/deploy-pages@/);
  assert.match(factory, /schedule:/);
  assert.doesNotMatch(factory, /^\s{2}push:/m);
});

test('All PR commits run inexpensive filtered contracts; expensive CI is dispatched',()=>{
  assert.match(ui,/^  pull_request:\n    types: \[opened, reopened, synchronize, ready_for_review, converted_to_draft\]\n    paths:/m);
  assert.match(native,/^  pull_request:\n    types: \[opened, reopened, synchronize, ready_for_review, converted_to_draft\]\n    paths:/m);
  assert.match(ui,/^  quick:\n    name: Fast UI contracts/m);
  assert.match(native,/^  quick:\n    name: Fast native contracts/m);
  assert.match(ui,/^  frontend:[\s\S]*?needs: quick[\s\S]*?github\.event_name != 'pull_request'/m);
  assert.match(native,/^  windows-build:[\s\S]*?needs: quick[\s\S]*?github\.event_name != 'pull_request'/m);
  assert.doesNotMatch(ui,/^  coordination:|^  qualification:|name: PR coordination admission|name: Product Qualification/m);
  assert.doesNotMatch(native,/^  workflow_call:|check_workstream_coordination\.mjs --pr/m);
  assert.doesNotMatch(ui,/uses: \.\/\.github\/workflows\/p1-windows\.yml/);
  assert.equal(uiPRSelected('src/LiveAudioEngine.h'),false);
  assert.equal(nativePrSelected('site/preview-v2/index.html'),false);
  assert.ok(uiPRSelected('web/approved-native-bridge.js'));
  assert.ok(nativePrSelected('web/approved-native-bridge.js'));
  assert.ok(nativePrSelected('integration/ApprovedNativeV8Projection.h'));
  assert.ok(nativePrSelected('tests/channel_routing_contract_test.cpp'));
  assert.equal(nativePrSelected('README.md'),false);
  assert.equal(uiPRSelected('README.md'),false);
});

test('Manual FAT and visual qualification preserve immutable FAT; native may be superseded',()=>{
  assert.match(ui,/github\.event_name == 'workflow_dispatch' && github\.run_id/);
  assert.match(ui,/cancel-in-progress: \$\{\{ github\.event_name != 'workflow_dispatch' \}\}/);
  assert.match(native,/github\.event_name == 'workflow_dispatch' && inputs\.package_fat && github\.run_id/);
  assert.match(native,/cancel-in-progress: .*?!inputs\.package_fat/);
  assert.match(native,/github\.event_name == 'workflow_dispatch' && inputs\.package_fat == true/);
});

test('WaveShell preflight fails closed but accepts historical pre-patch source',()=>{
  const begin=native.indexOf('- name: WaveShell JUCE 8 class-index regression (source-aware)');
  const end=native.indexOf('- name: Compressor exact-source native CPU A/B',begin);
  assert.ok(begin>0 && end>begin,'source-aware WaveShell quick gate missing');
  const section=native.slice(begin,end);
  assert.ok(section.includes('if [[ -f scripts/patch_juce_vst3_waveshell.py ]]'));
  assert.ok(section.includes('if [[ ! -f tests/test_waveshell_juce_patch.py ]]'));
  assert.ok(section.includes('WAVESHELL_SOURCE_TEST_MISSING'));
  assert.ok(section.includes('WAVESHELL_SOURCE_PATCH_MISSING'));
  assert.ok(section.includes('WAVESHELL_CHECK_NOT_IN_EXACT_SOURCE'));
  assert.ok(section.includes('python -m unittest discover -s tests -p test_waveshell_juce_patch.py -v'));
});


test('CI-only main promotion avoids native rebuild but any runtime change fails closed to MSVC',()=>{
  assert.match(native,/native_required: \$\{\{ steps\.native_scope\.outputs\.native_required \}\}/);
  assert.match(native,/if \[\[ "\$GITHUB_EVENT_NAME" == 'push'/);
  assert.match(native,/required=true/);
  assert.match(native,/git fetch --no-tags --depth=2 origin "\$BEFORE_SHA"/);
  assert.match(native,/git diff --name-only "\$BEFORE_SHA" HEAD/);
  assert.match(native,/\.github\/workflows\/p1-windows\.yml\|scripts\/chat_ci_dispatch\.mjs/);
  assert.match(native,/\*\) required=true; break/);
  assert.match(native,/needs\.quick\.outputs\.native_required == 'true'/);
  assert.match(native,/github\.event_name != 'push' \|\| needs\.quick\.outputs\.native_required/);
  assert.match(native,/ctest --test-dir build -C Release --output-on-failure/);
});


test('CI-only main commits never schedule unqualified full Chromium or Pages workflow_run',()=>{
  const noVisualChange=[
    '.github/workflows/ui-smoke.yml',
    '.github/workflows/pages-publish.yml',
    '.github/workflows/chat-ci-milestone.yml',
    'scripts/chat_ci_dispatch.mjs',
    'tests/chat_ci_dispatch_contract.test.mjs',
    'tests/ci_workflow_efficiency_test.mjs'
  ];
  for(const path of noVisualChange){
    assert.equal(selected(uiPush,path),false,
      'CI-only file would launch expensive main Chromium and falsely signal Pages: '+path);
    assert.equal(uiPRSelected(path),true,
      'CI-only file must still qualify through cheap PR UI contracts: '+path);
  }
  for(const path of [
    'web/app.js',
    'web/approved-native-bridge.js',
    'site/preview-v2/assets/mixer-native-compressor.js',
    'site/preview-v2/assets/mixer-contextual-buttons.css',
    'tests/pages_wire_first_canonical_main_smoke.py',
    'scripts/build_pages.py'
  ]){
    assert.ok(selected(uiPush,path),'Real GUI/runtime source must retain full main visual gate: '+path);
  }
  assert.match(pages,/github\.event\.workflow_run\.conclusion == 'success'/);
  assert.match(pages,/github\.event\.workflow_run\.event == 'push'/);
});
