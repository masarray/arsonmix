// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=file=>readFileSync(file,'utf8');
const windows=read('.github/workflows/p1-windows.yml');
const ui=read('.github/workflows/ui-smoke.yml');
const pages=read('.github/workflows/pages-publish.yml');
test('only main and explicitly selected branch can produce a Windows FAT artifact',()=>{
 assert.match(windows,/branches: \[main\]/);
 assert.doesNotMatch(windows,/branches: \[ main, feature\//);
 assert.match(windows,/pull_request:\s+paths:/);
 assert.match(windows,/'src\/\*\*'/);
 assert.match(windows,/'engine\/\*\*'/);
 assert.match(windows,/'web\/\*\*'/);
 assert.match(windows,/workflow_dispatch:/);
 assert.match(windows,/cancel-in-progress: true/);
 assert.match(windows,/if: \$\{\{ github\.event_name != 'pull_request' \|\| github\.head_ref == 'milestone\/m5-universal-channelid-vst3-lease-migration-20261101' \}\}/);
 assert.match(windows,/ArSonMix-ENGINE-10CH4AUX-EXPERIMENTAL-Hardware-FAT-Windows-x64/);
 assert.ok(!windows.includes("github.head_ref == 'milestone/m4-unified-native-multichannel-fx-20261101'"));
 assert.ok(windows.indexOf('scripts/verify_native_windows_artifact.ps1') <
           windows.indexOf('uses: actions/upload-artifact@v4'));
});
test('UI PRs have one cancellable Chromium gate; heavy Firefox is main/manual only',()=>{
 assert.match(ui,/concurrency:\s+group: ui-smoke-/);
 assert.match(ui,/cancel-in-progress: true/);
 assert.match(ui,/pull_request:\s+paths:/);
 assert.match(ui,/'web\/\*\*'/);
 const tail=ui.split('name: Install Firefox for compositor functional regression')[1];
 assert.ok(tail && tail.includes("if: ${{ github.event_name != 'pull_request' }}"));
 assert.match(ui,/retention-days: 3/);
});
test('exactly one guarded downstream Pages publisher; never a second automated publisher',()=>{
 assert.match(pages,/workflows: \["UI smoke \(Chromium\)"\]/);
 assert.match(pages,/github\.event\.workflow_run\.event == 'push'/);
 assert.match(pages,/git ls-remote origin refs\/heads\/main/);
 assert.match(pages,/if: \$\{\{ needs\.build\.outputs\.fresh == 'true' \}\}/);
 assert.doesNotMatch(ui,/actions\/deploy-pages@/);
});

test('retain current native audio and dearVR CTests during CI optimization',()=>{
 const targets=windows.split('name: Build executable and native RT tests')[1]?.split('\n')[1]||'';
 for(const name of ['ARMixFxBypassEnvelopeTests','ARMixDearVrPolarPolicyTests',
   'ARMixOrbitDistanceTests','ARMixOrbitMotionTests','ARMixRTTests']){
   assert.ok(targets.includes(name),'Missing mandatory native regression '+name);
 }
 assert.match(windows,/ctest --test-dir build -C Release --output-on-failure/);
 assert.match(windows,/'scripts\/build_ui_css\.py'/);
 assert.match(windows,/if: \$\{\{ github\.event_name != 'pull_request' \|\| github\.head_ref == 'milestone\/m5-universal-channelid-vst3-lease-migration-20261101' \}\}/);
 assert.match(windows,/ArSonMix-ENGINE-10CH4AUX-EXPERIMENTAL-Hardware-FAT-Windows-x64/);
});

test('native FAT compiles the exact ENGINE PR HEAD and rejects transient merge refs',()=>{
 const shaRef="ref: ${{ github.event_name == 'pull_request' && github.event.pull_request.head.sha || github.sha }}";
 assert.ok(windows.includes(shaRef));
 assert.match(windows,/ENGINE_EXACT_SOURCE_SHA_VERIFIED/);
 assert.match(windows,/git rev-parse HEAD/);
 assert.ok(windows.indexOf('ENGINE_EXACT_SOURCE_SHA_VERIFIED')<
           windows.indexOf('name: Build executable and native RT tests'));
});

test('exact two-parent M5 integration FAT has its own guarded artifact, not a broad PR upload',()=>{
 const branch='integration/m5-native-engine-current-gui-physical-fat-20261103';
 const block=windows.split('name: Upload exact-joined M5 GUI + ENGINE physical FAT candidate')[1];
 assert.ok(block && block.includes("github.event_name == 'pull_request'"));
 assert.ok(block.includes("github.head_ref == '"+branch+"'"));
 assert.ok(block.includes("ArSonMix-INTEGRATED-M5-UNQUALIFIED-Hardware-FAT-Windows-x64"));
 assert.ok(block.includes('if-no-files-found: error'));
 assert.ok(windows.indexOf('Independently verify release ZIP and embedded GUI/engine before upload') <
           windows.indexOf('name: Upload exact-joined M5 GUI + ENGINE physical FAT candidate'));
});
