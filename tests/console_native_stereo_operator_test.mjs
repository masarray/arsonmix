// SPDX-License-Identifier: AGPL-3.0-only
// Regression against real accepted physical pin identity + native ChannelId GUI.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const js=readFileSync('web/console-native.js','utf8');
const html=readFileSync('web/console-native.html','utf8');
const native=readFileSync('src/LiveDryRoutingLab.h','utf8');
const bridge=readFileSync('src/ConsoleLiveBridgeV6.h','utf8');
test('Create Channel selects real mono/stereo processing format before native ACK',()=>{
 assert.match(html,/id="nativeChannelFormat"/);
 assert.match(html,/<option value="MONO">MONO<\/option>/);
 assert.match(html,/<option value="STEREO">STEREO<\/option>/);
 assert.match(js,/el\('nativeChannelFormat'\)\?\.value==='STEREO'/);
 assert.match(js,/\{name,format\}/);
 assert.match(js,/focusNewChannel=true/);
 assert.match(js,/snapshot\?\.device\?\.outputArmed/);
 assert.match(bridge,/operation=="NATIVE_CREATE_CHANNEL"/);
 assert.match(bridge,/format=="STEREO"\?armix::mixer::ProcessingFormat::stereo/);
});
test('two independently verified stereo pin selectors scale linearly',()=>{
 assert.match(js,/function stereoSourceMarkup\(row,attr\)/);
 assert.match(js,/for\(const p of pins\)list\.push/);
 assert.match(js,/data-stereo-side/);
 assert.doesNotMatch(js,/for\(let i=0;i<endpoints\.length;i\+\+\)for\(let j=0/);
 assert.match(js,/sourceFormat:'STEREO'/);
 assert.match(js,/l===r\|\|!allowed\.some/);
 assert.match(bridge,/operation=="NATIVE_PATCH_CHANNEL"/);
 assert.match(native,/source\.inputR>=0/);
 assert.match(native,/setProperty\("portIdRight"/);
 assert.match(native,/enabledPorts_\[source\.inputR\]/);
 const css=readFileSync('web/console-native.css','utf8');
 assert.match(css,/\.native-stereo-pair\{display:flex/);
 assert.match(css,/min-width:0/);
});
test('accepted snapshot carries correct mono/stereo patch and no invented signal',()=>{
 assert.match(js,/patch\?\.portIdRight/);
 assert.match(js,/current===p\.port/);
 assert.match(js,/NATIVE JUCE BACKEND REQUIRED/);
 assert.doesNotMatch(js,/new AudioContext|AudioWorklet|Math\.sin\(/);
});

test('CH11 and dynamic GROUP meters consume post-fader native PCM taps',()=>{
 const js=readFileSync('web/console-native.js','utf8');
 const native=readFileSync('src/LiveDryRoutingLab.h','utf8');
 assert.match(js,/nativeScoped\?Number\(r\.measuredL\)/);
 assert.match(js,/nativeScoped\?Number\(r\.measuredR\)/);
 assert.match(native,/nativePublisher_\.render\(/);
 assert.match(native,/nativeMaxL\[ch\]/);
 assert.match(native,/nativeMeterPeaksL_\[meterIndex\]/);
 assert.match(native,/row->setProperty\("peakL"/);
 assert.match(native,/nativeMeterEpoch_\.store\(0,std::memory_order_release\)/);
 assert.doesNotMatch(js,/new AudioContext|AudioWorklet|Math\.sin\(/);
});
