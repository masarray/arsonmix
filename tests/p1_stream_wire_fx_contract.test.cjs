const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path');
const read = p => fs.readFileSync(path.join(__dirname,'..',p),'utf8');
test('P1 stream copies native post-FX MAIN through selected independent WASAPI consumer',()=>{
 const ui=read('web/approved-native-bridge.js');
 const host=read('src/Main.cpp'), engine=read('src/LiveAudioEngine.h');
 const device=read('engine/device/LiveSecondaryOutputDevice.h');
 assert.match(ui,/data-stream-panel/);
 for(const s of ['connect','live','stop'])assert.ok(ui.includes('data-stream-action="'+s+'"'));
 assert.match(ui,/arMixSecondaryOutputAck/);
 assert.match(host,/expectedDeviceEpoch/);
 assert.match(host,/ack->setProperty\("requestId",requestId/);
 assert.match(engine,/STREAM OFF: same VB-Cable input\/output feedback loop/);
 assert.match(device,/render-ONLY WASAPI clock consumer/);
});
test('P1 native FX master gate preserves slots and awaits actual readback',()=>{
 const ui=read('web/approved-native-bridge.js');
 const fx=read('engine/core/LiveChannelFxGraph.h');
 assert.match(ui,/arMixNativeFxPowerAck/);
 assert.match(ui,/pendingNativeFxPower\?\.acked/);
 assert.match(ui,/routingNativeFxRackBypass/);
 assert.match(fx,/rackBypassed \|\| bypassRequested/);
});
test('P1 WIRE gateway validates EdgeId and checks physical/stream output safety',()=>{
 const ui=read('web/approved-native-bridge.js');
 const translator=read('integration/ApprovedNativeControlIntent.h');
 assert.match(ui,/WIRE requires MONITOR OFF and STREAM stopped/);
 assert.match(translator,/WIRE_STALE_EDGE/);
 assert.match(translator,/NATIVE_APPLY_WIRE_TRANSACTION/);
 assert.match(translator,/engine\.applyNativeWireTransaction/);
});
