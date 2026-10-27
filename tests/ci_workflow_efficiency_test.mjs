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
 assert.match(windows,/if: \$\{\{ github\.event_name != 'pull_request' \}\}/);
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
 assert.match(windows,/if: \$\{\{ github\.event_name != 'pull_request' \}\}/);
});
