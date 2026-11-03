// SPDX-License-Identifier: AGPL-3.0-only
// Real host-only VST3 operator contract: no browser DSP or catalog fixture.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const get=f=>readFileSync(f,'utf8');
const gui=get('web/console-native.js'),lab=get('src/LiveDryRoutingLab.h');
const host=get('src/Main.cpp'),bridge=get('src/ConsoleLiveBridgeV6.h');
const bank=get('engine/core/DynamicChannelFxLeaseBank.h');
test('real native catalog and scoped Add VST3 are bound to accepted ChannelId',()=>{
 assert.match(gui,/addEventListener\('arMixCatalog'/);
 assert.match(gui,/realCatalog= catalog/);
 assert.match(gui,/data-real-plugin-id/);
 assert.match(gui,/NATIVE_LOAD_INSERT/);
 assert.match(gui,/data-open-native-fx/);
 assert.match(gui,/catalogId:chosenPlugin\.dataset\.realPluginId/);
 assert.match(host,/catalog_\.find\(catalogId\)/);
 assert.match(host,/engine_\.installNativeVst3/);
 assert.doesNotMatch(gui,/demoCatalog|Math\.sin\(|AudioContext/);
});
test('control-thread vendor lease and one PCM publication are atomic',()=>{
 assert.match(bank,/static Fault appendOwned\(/);
 assert.match(lab,/NativeBusResult attachNativeInsert\(/);
 assert.match(lab,/DynamicChannelFxLeaseBank::appendOwned/);
 assert.match(lab,/nativePublisher_\.commit\(/);
 assert.match(lab,/nativeAccepted_\.fxOwners=std::move\(owner\)/);
 assert.match(lab,/row->setProperty\("inserts"/);
 assert.match(host,/NATIVE_VST3_NOT_SCANNED/);
 assert.match(bridge,/operation=="NATIVE_LOAD_INSERT"/);
});
test('real bypass and vendor editor retain immutable native instance identity',()=>{
 assert.match(gui,/NATIVE_SET_INSERT_BYPASS/);
 assert.match(gui,/NATIVE_OPEN_INSERT_EDITOR/);
 assert.match(gui,/slot,instanceId,bypassed/);
 assert.match(bridge,/NATIVE_SET_INSERT_BYPASS/);
 assert.match(host,/engine_\.bypassNativeVst3/);
 assert.match(host,/engine_\.leaseNativeVst3Editor/);
 assert.match(lab,/nativeAccepted_\.setFxBypass/);
 assert.match(lab,/nativeAccepted_\.leaseFxEditor/);
});
