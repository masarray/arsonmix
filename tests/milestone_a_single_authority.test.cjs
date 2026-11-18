// SPDX-License-Identifier: AGPL-3.0-only
// Milestone A real producer wiring contract, not a hardware FAT certificate.
const test=require("node:test"),assert=require("node:assert/strict");
const fs=require("node:fs");
const read=p=>fs.readFileSync(p,"utf8");

test("only installed callback determines GUI channel control authority",()=>{
 const producer=read("integration/ApprovedNativeRuntimeSnapshot.h");
 const host=read("src/Main.cpp"),adapter=read("web/approved-native-bridge.js");
 assert.match(host,/publishAuthoritativeMixerSession\(accepted\)/);
 assert.match(producer,/const auto authority=hasChannelId\?"CHANNEL_ID":validM4\?"M4":"NONE"/);
 assert.match(producer,/row->setProperty\("id","m4:"/);
 const source=read("web/approved-native-contract.js");
 assert.match(adapter,/const readModel=protocol\.selectNativeReadModel\(next,m4Verified\(\)\)/);
 assert.match(adapter,/if\(readModel\.kind==="M4"\)/);
 assert.match(adapter,/const nativeAdmitted=readModel\.kind==="CHANNEL_ID"/);
 assert.match(source,/session\.authority==="M4"&&verifiedM4===true/);
 assert.match(source,/session\.authority==="CHANNEL_ID"&&next\.admitted===true/);
 assert.match(adapter,/m4ControlIsOwner\(\)&&m4Verified\(\)&&m4Index\(t\)>=0/);
 assert.doesNotMatch(adapter,/rackSavedBypass/);
 assert.match(adapter,/const ctl=session\.channels\.map\(row=>row\.controls\)/);
 assert.match(adapter,/session\.channels\.map\(ch=>ch\.source\)/);
 assert.match(producer,/row->setProperty\("fxRackBypassed",rackFlags\[i\]\)/);
 assert.doesNotMatch(adapter,/AudioContext|AudioWorklet/);
});
test("M4 scalar commands require correlated ACK then later publisher snapshot",()=>{
 const host=read("src/Main.cpp"),adapter=read("web/approved-native-bridge.js");
 for(const name of ["deviceEpoch","routingRevision","channel","parameter"])
   assert.match(host,new RegExp('ack->setProperty\\("'+name+'"'));
 assert.match(adapter,/pendingM4Controls\.set\(key/);
 assert.match(adapter,/if\(!item\)return/);
 assert.match(adapter,/p\.acked=true/);
 assert.match(adapter,/reconcileM4ControlReadback\(next\)/);
 assert.match(adapter,/session\?\.channels\?\.\[p\.channel\]/);
});
test("FX rack parent is a producer gate with independent slot preferences",()=>{
 const host=read("src/Main.cpp"),adapter=read("web/approved-native-bridge.js");
 const fx=read("engine/core/LiveChannelFxGraph.h");
 const lease=read("engine/core/DynamicChannelFxLeaseBank.h");
 assert.match(host,/action=="routingFxRackBypass"/);
 assert.match(host,/setDryRoutingLabFxRackBypass/);
 assert.match(host,/routingNativeFxRackBypass/);
 assert.match(adapter,/requestM4RackPower\(t\)/);
 assert.match(adapter,/requestAdmittedFxPower\(t,action,btn\)/);
 assert.match(adapter,/reconcileM4RackReadback\(next\)/);
 assert.match(fx,/rackBypassed_\[strip\]/);
 assert.match(lease,/setRackBypass\(/);
 assert.doesNotMatch(adapter,/rackSavedBypass/);
});
test("native ChannelId source preserves post-fader physical MAIN and VST3",()=>{
 const producer=read("integration/ApprovedNativeRuntimeSnapshot.h");
 const host=read("src/Main.cpp");
 const engine=read("src/LiveAudioEngine.h");
 assert.match(producer,/physicalMasterDb/);
 assert.match(host,/channel==-1 \|\|/);
 assert.match(engine,/setDryRoutingLabParameter/);
 assert.match(engine,/insertDryRoutingLabFx/);
 assert.match(engine,/applyGuardedDryTopology/);
});
