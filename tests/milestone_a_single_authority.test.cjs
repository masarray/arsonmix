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


const protocol=require("../web/approved-native-contract.js");
const fixture=JSON.parse(read("integration/console-v6/fixtures.json")).g0;
const copy=value=>JSON.parse(JSON.stringify(value));

// These tests use the very same schema-v6 normalizeSnapshot as native
// transport; no alternate JS mixer session or browser-only LIVE model.
function installedReadModelFixture() {
 const raw=copy(fixture),input="9007199254740993",
       main="18446744073709551615";
 raw.device.epoch="18446744073709551610";
 raw.device.outputArmed=true;
 raw.productionCallbackLive=true;
 raw.graph.revision="5";raw.graph.rosterRevision="7";
 raw.graph.mainChannelId=main;
 raw.graph.capabilities.callbackOwnsChannelIds=true;
 raw.graph.capabilities.supportedOps=["SET_FADER","APPLY_WIRE_TRANSACTION"];
 raw.graph.channels=[
  {id:input,role:"INPUT",name:"Vocal",processingFormat:"MONO",
   primaryOutId:main,controls:{faderDb:-6,pan:0,mute:false,solo:false},
   inserts:[],sends:[]},
  {id:main,role:"MAIN",name:"MAIN",processingFormat:"STEREO",
   controls:{faderDb:0,pan:0,mute:false,solo:false},inserts:[],sends:[]}
 ];
 raw.graph.destinations=[{id:main,role:"MAIN"}];
 raw.graph.edges=[{edgeId:"9223372036854775808",from:input,to:main,
   tap:"POST_FADER",gainLinear:1,enabled:true,signalKind:"AUDIO",
   primaryOutput:true}];
 raw.mixerSession={authority:"CHANNEL_ID",deviceEpoch:raw.device.epoch};
 return raw;
}
function offlineReadModelFixture() {
 const raw=copy(fixture);
 raw.mixerSession={authority:"NONE",deviceEpoch:raw.device.epoch};
 raw.logicalMixer={
  schema:"arsonmix.native.logical-mixer.v1",authority:"LOGICAL_ONLY",
  workspaceToken:"offline-smix-v1",readOnly:false,revision:"12",
  physicalOutputAdmitted:false,outputArmed:false,
  graph:{revision:"12",rosterRevision:"12",
   capabilities:{callbackOwnsChannelIds:false,supportedOps:["SET_FADER"]},
   channels:[{id:"9007199254740993",role:"INPUT",name:"Vocal",
    processingFormat:"MONO",inserts:[],sends:[],
    controls:{faderDb:-6,pan:0,mute:false,solo:false}}],
   edges:[]}
 };
 return raw;
}
test("MV-3 native read-model prioritizes installed ChannelId without physical LIVE fabrication",()=>{
 const raw=installedReadModelFixture(),saved=JSON.stringify(raw);
 const normalized=protocol.normalizeSnapshot(raw);
 assert.ok(normalized?.admitted);
 const view=protocol.selectNativeReadModel(normalized,false);
 assert.equal(view.kind,"CHANNEL_ID");
 assert.strictEqual(view.view,normalized,
  "selector must reuse the exact accepted readback, not clone a new graph");
 assert.equal(view.view.graph.channels[0].id,"9007199254740993");
 assert.equal(view.view.graph.edges[0].edgeId,"9223372036854775808");
 assert.equal(view.view.productLive,true);
 assert.equal(JSON.stringify(raw),saved,"read-model cannot mutate installed graph");
 assert.equal(protocol.selectNativeReadModel({...normalized,
  graph:{...normalized.graph}},false).kind,"INVALID",
  "independent UI copy is not the normalized native graph");
 const g0=copy(fixture);
 g0.mixerSession={authority:"CHANNEL_ID",deviceEpoch:g0.device.epoch};
 const unadmitted=protocol.normalizeSnapshot(g0);
 assert.equal(unadmitted.admitted,false);
 assert.equal(protocol.selectNativeReadModel(unadmitted,false).kind,"UNADMITTED",
  "naming CHANNEL_ID cannot turn an uninstalled graph into PCM");
 g0.mixerSession.deviceEpoch="18446744073709551615";
 assert.equal(protocol.selectNativeReadModel(protocol.normalizeSnapshot(g0),false).kind,"STALE",
  "mismatched producer epoch cannot grant native device status");
});
test("MV-3 offline .smix read-model is nonphysical and cannot outrank installed/M4",()=>{
 const raw=offlineReadModelFixture(),original=JSON.stringify(raw);
 const normalized=protocol.normalizeSnapshot(raw);
 assert.ok(normalized && !normalized.admitted && !normalized.retained);
 const eligible=protocol.selectNativeReadModel(normalized,false);
 assert.equal(eligible.kind,"LOGICAL_ONLY");
 assert.equal(eligible.view.logical,true);
 assert.equal(eligible.view.admitted,false);
 assert.equal(eligible.view.productLive,false);
 assert.equal(eligible.view.retained,false);
 assert.equal(eligible.view.telemetry && Object.keys(eligible.view.telemetry).length,0);
 assert.strictEqual(eligible.view.graph,raw.logicalMixer.graph,
  "offline presenter must reuse exact native .smix readback, not construct a second session");
 assert.equal(eligible.view.identity,"logical:offline-smix-v1:12");
 assert.equal(JSON.stringify(raw),original,"selector never modifies native or offline source");
 const readonly=copy(raw);readonly.logicalMixer.readOnly=true;
 assert.equal(protocol.selectNativeReadModel(protocol.normalizeSnapshot(readonly),false).view.readOnly,true);
 for(const mutate of [
  x=>{x.logicalMixer.physicalOutputAdmitted=true;},
  x=>{x.logicalMixer.outputArmed=true;},
  x=>{x.logicalMixer.graph.capabilities.callbackOwnsChannelIds=true;},
  x=>{x.logicalMixer.graph.rosterRevision="11";},
  x=>{x.logicalMixer.revision="11";},
  x=>{x.logicalMixer.workspaceToken="";},
  x=>{x.logicalMixer.graph.edges=null;}
 ]) {
  const bad=copy(raw);mutate(bad);
  assert.notEqual(protocol.selectNativeReadModel(protocol.normalizeSnapshot(bad),false).kind,
   "LOGICAL_ONLY","invalid offline status must never be projected");
 }
 const m4=copy(raw);m4.mixerSession.authority="M4";
 assert.equal(protocol.selectNativeReadModel(protocol.normalizeSnapshot(m4),true).kind,"M4");
 assert.notEqual(protocol.selectNativeReadModel(protocol.normalizeSnapshot(m4),false).kind,"LOGICAL_ONLY");
 assert.notEqual(protocol.selectNativeReadModel(normalized,true).kind,"LOGICAL_ONLY",
  "a verified legacy physical renderer takes precedence over saved logical edit");
});
test("MV-3 retained device-loss readback wins over stale logical state, never rearms",()=>{
 const raw=installedReadModelFixture();
 raw.graph.bindingState="NEEDS_REMAP";
 raw.graph.capabilities.callbackOwnsChannelIds=false;
 raw.graph.capabilities.supportedOps=[];
 raw.productionCallbackLive=false;
 raw.device.outputArmed=false;
 raw.mixerSession.bindingState="NEEDS_REMAP";
 raw.logicalMixer=offlineReadModelFixture().logicalMixer;
 const next=protocol.normalizeSnapshot(raw);
 assert.ok(next && next.retained && !next.admitted,
  "retained graph is normalized by original schema-v6 decoder");
 const view=protocol.selectNativeReadModel(next,false);
 assert.equal(view.kind,"NEEDS_REMAP");
 assert.strictEqual(view.view,next);
 assert.equal(view.view.device.outputArmed,false);
 assert.equal(view.view.productLive,false);
 assert.equal(view.view.graph.channels.length,2);
 assert.equal(view.view.graph.edges[0].edgeId,"9223372036854775808",
  "source/edge intent survives device remap without phantom PCM");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/const readModel=protocol\.selectNativeReadModel\(next,m4Verified\(\)\)/);
 assert.match(bridge,/if\(readModel\.kind==="LOGICAL_ONLY"\|\|readModel\.kind==="RETAINED_OFFLINE"\)/);
 assert.match(bridge,/const nativeAdmitted=readModel\.kind==="CHANNEL_ID"/);
 assert.doesNotMatch(bridge,/const logicalAvailable=/,
  "legacy duplicate offline authority policy must be deleted from bridge");
 assert.doesNotMatch(bridge,/const nativeAdmitted=session\.authority===/,
  "native callback admission belongs to schema-v6 read-model, not DOM");
});


test("FUX-1 retained FX offline access requires identical native authority and does not arm output",()=>{
 const source=installedReadModelFixture();
 source.graph.bindingState="NEEDS_REMAP";
 source.graph.capabilities.callbackOwnsChannelIds=false;
 source.graph.capabilities.supportedOps=[];
 source.device.outputArmed=false;source.productionCallbackLive=false;
 source.mixerSession.bindingState="NEEDS_REMAP";
 source.logicalMixer=offlineReadModelFixture().logicalMixer;
 source.logicalMixer.graph.capabilities.supportedOps=["SET_FX_SLOT_BYPASS"];
 const retained=protocol.normalizeSnapshot(source);
 assert.ok(retained?.retained&&!retained?.admitted);
 assert.equal(protocol.selectNativeReadModel(retained).kind,"NEEDS_REMAP",
  "a disconnected graph and saved .smix alone cannot authorize offline edits");
 const approved=copy(source);
 approved.logicalMixer.retainedOffline=true;
 approved.logicalMixer.retainedDeviceEpoch=approved.device.epoch;
 approved.logicalMixer.retainedGraphRevision=approved.graph.revision;
 const ready=protocol.selectNativeReadModel(protocol.normalizeSnapshot(approved));
 assert.equal(ready.kind,"RETAINED_OFFLINE");
 assert.equal(ready.view.logical,true);
 assert.equal(ready.view.physicalRetained,true);
 assert.equal(ready.view.retained,false);
 assert.equal(ready.view.admitted,false);
 assert.equal(ready.view.productLive,false);
 assert.equal(ready.view.device.outputArmed,false);
 assert.strictEqual(ready.view.graph,ready.view.raw.logicalMixer.graph);
 for(const change of [
  x=>{x.logicalMixer.retainedDeviceEpoch="1";},
  x=>{x.logicalMixer.retainedGraphRevision="99";},
  x=>{x.logicalMixer.retainedOffline=false;},
  x=>{x.logicalMixer.outputArmed=true;},
  x=>{x.logicalMixer.physicalOutputAdmitted=true;},
  x=>{x.device.outputArmed=true;},
  x=>{x.mixerSession.authority="M4";},
  x=>{x.mixerSession.bindingState="LIVE";},
  x=>{x.logicalMixer.graph.capabilities.callbackOwnsChannelIds=true;}
 ]) {
  const bad=copy(approved);change(bad);
  const next=protocol.normalizeSnapshot(bad);
  assert.notEqual(protocol.selectNativeReadModel(next).kind,"RETAINED_OFFLINE",
   "stale authority must not silently create logical editor lease");
 }
 const bridge=read("web/approved-native-bridge.js");
 for(const marker of ["action:\"retainedFxOfflineOpen\"",
   "arMixRetainedFxOfflineAck","pendingRetainedEditor.acknowledged=true",
   "readModel.kind===\"RETAINED_OFFLINE\"",
   "controller.subscribe(); // only accepted native logical readback"])
  assert.ok(bridge.includes(marker.replaceAll('\\"','"')),marker);
 const main=read("src/Main.cpp");
 const host=main.slice(main.indexOf("void openRetainedFxOffline("),
  main.indexOf("void handleLogicalMixerIntent("));
 for(const marker of ["engine_.captureNativeMixerSession(document,states,true)",
  "logicalMixer_.restoreCheckpoint(std::move(document),std::move(states))",
  "document.resumeMain=false","operatorOutputMuted_=true"])
  assert.ok(host.includes(marker),marker);
 assert.ok(!/setMonitorEnabled\(true\)|setOutputMuted\(false\)|restoreNativeMixerSession\(/.test(host),
  "offline editor must not arm/restore PCM");
 assert.ok(read("src/LiveAudioEngine.h").includes(
  "const bool detachedRetained=retained &&"));
 assert.ok(read("src/LiveDryRoutingLab.h").includes(
  "const bool offlineSource=retained && hasRetainedNativeSession()"));
});
