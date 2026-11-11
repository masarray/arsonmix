// SPDX-License-Identifier: AGPL-3.0-only
// M2 deterministic native transport: no fake ChannelIds, ACK-only success or PCM.
const test=require("node:test");
const assert=require("node:assert/strict");
const {readFileSync}=require("node:fs");
const protocol=require("../web/approved-native-contract.js");
const read=p=>readFileSync(p,"utf8");
const fixtures=JSON.parse(read("integration/console-v6/fixtures.json"));
const clone=x=>JSON.parse(JSON.stringify(x));
function acceptedFixture(){
 const s=clone(fixtures.g0);
 s.device.epoch="18446744073709551610";
 s.productionCallbackLive=true;
 s.graph.revision="5";s.graph.rosterRevision="7";
 s.graph.mainChannelId="18446744073709551615";
 s.graph.capabilities.callbackOwnsChannelIds=true;
 s.graph.capabilities.supportedOps=["SET_FADER","SET_MUTE","APPLY_WIRE_TRANSACTION"];
 const input="9007199254740993",main=s.graph.mainChannelId;
 s.graph.channels=[
  {id:input,role:"INPUT",name:"Vocal",processingFormat:"MONO",primaryOutId:main,
   controls:{faderDb:-6,pan:0,mute:false,solo:false},inserts:[],sends:[]},
  {id:main,role:"MAIN",name:"MAIN",processingFormat:"STEREO",
   controls:{faderDb:0,pan:0,mute:false,solo:false},inserts:[],sends:[]}
 ];
 s.graph.destinations=[{id:main}];
 s.graph.edges=[{edgeId:"9223372036854775808",from:input,to:main,
   tap:"POST",gainLinear:1,enabled:true,signalKind:"AUDIO"}];
 return s;
}
test("M5 rejected FX intents update observable native status without emitting or repainting PCM",()=>{
 const s=acceptedFixture();
 const ch=s.graph.channels[0];
 ch.inserts=[{id:"fx-stable-instance-1",catalogId:"armix.builtin.channel-eq.v1"}];
 s.graph.capabilities.supportedOps.push("REMOVE_INSERT","MOVE_INSERT");
 const emitted=[],notifications=[];
 const control=protocol.createController({
  emit:(name,payload)=>emitted.push({name,payload}),
  onStatus:value=>notifications.push(value)
 });
 assert.equal(control.onSnapshot(s),true);
 const before=control.state().current;
 assert.ok(before?.admitted);
 const check=(result,expected)=>{
  assert.deepEqual(result,{ok:false,error:expected});
  assert.equal(control.state().lastError,expected);
  assert.equal(notifications.at(-1).lastError,expected);
  assert.equal(control.state().pending,null);
  assert.equal(control.state().current,before);
  assert.equal(emitted.length,0,"rejected edit must never reach native dispatcher");
 };
 check(control.intent("REMOVE_INSERT",ch.id,{slot:0,instanceId:"wrong-instance"}),
       "STALE_FX_INSTANCE");
 check(control.intent("MOVE_INSERT",ch.id,{
  slot:0,instanceId:"fx-stable-instance-1",toChannelId:s.graph.mainChannelId,toSlot:9
 }),"INVALID_FX_DESTINATION");
 check(control.intent("MOVE_INSERT",ch.id,{
  slot:0,instanceId:"fx-stable-instance-1",toChannelId:"0",toSlot:0
 }),"INVALID_FX_DESTINATION");
 check(control.intent("SET_FADER",ch.id,[]),"INVALID_PAYLOAD");
 check(control.intent("UNSUPPORTED_OP",ch.id,{}),"UNSUPPORTED_OR_UNADMITTED");
 assert.ok(notifications.length>=6,"all refusals are published to the existing UI");
});
test("G0 current JUCE device snapshot is valid, never ChannelId PCM LIVE",()=>{
 const s=protocol.normalizeSnapshot(fixtures.g0);
 assert.ok(s);assert.equal(s.admitted,false);
 assert.deepEqual(s.graph.channels,[]);
 assert.deepEqual(s.graph.capabilities.supportedOps,[]);
 assert.equal(protocol.decimal("18446744073709551615"),"18446744073709551615");
 for(const invalid of ["01","-1","1.1",1,9007199254740993,"18446744073709551616",null," 1"]) {
  assert.equal(protocol.decimal(invalid),null);
 }
});
test("native G0 preserves verified first-sound M4 route without claiming ChannelId PCM",()=>{
  const g0=clone(fixtures.g0);
  g0.legacyM4={active:true,routeAccepted:false,routingRevision:"12"};
  const initial=protocol.normalizeSnapshot(g0);
  assert.ok(initial);
  assert.equal(initial.admitted,false);
  assert.deepEqual(initial.raw.legacyM4,g0.legacyM4);
  g0.legacyM4.routeAccepted=true;
  const patched=protocol.normalizeSnapshot(g0);
  assert.ok(patched);
  assert.equal(patched.raw.legacyM4.routeAccepted,true);
  assert.equal(patched.admitted,false,
    "a physical M4 patch is NOT fabricated ChannelId or product admission");
  // The existing JUCE 12Hz status publisher must also deliver late native
  // snapshots: WebView's first subscribe occurs before device initialise().
  const nativeMain=read("src/Main.cpp");
  assert.match(nativeMain,/if \(consoleSubscribed_\)[\s\S]*?"arMixConsoleSnapshot", approvedNativeSnapshot\(state\)/);
  const nativeBridge=read("web/approved-native-bridge.js");
  assert.match(nativeBridge,/const legacy=\(\)=>hardware\?\.raw\?\.legacyM4/);
  assert.match(read("src/ApprovedConsoleNativeTransportV6.h"),
    /"outputArmed", primaryOutputArmed/);
});
test("First Sound evidence maps sparse physical pins to actual callback ordinals and diagnoses silence",()=>{
  const g0=clone(fixtures.g0);
  g0.legacyM4={active:true,routeAccepted:true,routingRevision:"9"};
  g0.device.enabledInputs=[0,2];
  g0.device.inputEndpoints.push({
    id:"in:2",port:2,callback:1,label:"Mic 3",active:true
  });
  g0.device.outputArmed=true;
  g0.telemetry={
    captureObserved:true,rawCapturePresent:true,rawCapturePeak:0.38,
    captureCallbackPeaks:[0,0.38],mainPcmObserved:true,
    mainPcmPresent:true,mainPcmPeak:0.17,monitorPcmPresent:false
  };
  const snapshot=protocol.normalizeSnapshot(g0);
  assert.ok(snapshot);assert.equal(snapshot.admitted,false);
  const first=protocol.diagnoseFirstSound(snapshot,0);
  assert.equal(first.selectedPin,0);
  assert.equal(first.rawPeak,0);
  assert.match(first.nextAction,/Selected input is silent/);
  const second=protocol.diagnoseFirstSound(snapshot,2);
  assert.equal(second.selectedPin,2);
  assert.equal(second.rawPeak,0.38);
  assert.equal(second.mainPeak,0.17);
  assert.match(second.nextAction,/physically/);
  assert.match(second.readout,/IN 3/);
  const all=protocol.diagnoseFirstSound(snapshot,"");
  assert.equal(all.rawPeak,0.38);
  // A mistaken output claim never becomes a loudspeaker certification.
  g0.device.outputArmed=false;
  const muted=protocol.diagnoseFirstSound(protocol.normalizeSnapshot(g0),2);
  assert.match(muted.nextAction,/ARM output/);
  g0.device.outputArmed=true;
  g0.legacyM4.routeAccepted=false;
  const unpatched=protocol.diagnoseFirstSound(protocol.normalizeSnapshot(g0),2);
  assert.match(unpatched.nextAction,/PATCH CH1/);
  g0.legacyM4.routeAccepted=true;
  g0.telemetry.mainPcmPeak=0;g0.telemetry.mainPcmPresent=false;
  assert.match(protocol.diagnoseFirstSound(protocol.normalizeSnapshot(g0),2).nextAction,/MAIN PCM silent/);
  g0.telemetry.captureObserved=false;
  const unknown=protocol.diagnoseFirstSound(protocol.normalizeSnapshot(g0),2);
  assert.equal(unknown.rawPeak,null);
  assert.match(unknown.nextAction,/No current-epoch capture callback/);
});
test("native signal evidence is produced from measured engine fields, never JS simulated PCM",()=>{
  const header=read("integration/FirstSoundSignalEvidence.h");
  const transport=read("src/ApprovedConsoleNativeTransportV6.h");
  const bridge=read("web/approved-native-bridge.js");
  assert.match(header,/captureObservationBlocks/);
  assert.match(header,/callbackInputChannels/);
  assert.match(header,/captureCallbackPeaks/);
  assert.match(header,/directRouteAuthorized/);
  assert.match(transport,/projectFirstSoundSignalEvidence/);
  assert.doesNotMatch(transport,/setProperty \("rawCapturePeak", 0\.0\)/);
  assert.match(bridge,/protocol\.diagnoseFirstSound/);
});
test("P0 DAW approved Windows GUI directly binds every IN to current JUCE ports, never Pages demo",()=>{
  const bridge=read("web/approved-native-bridge.js");
  const producer=read("integration/ApprovedNativeRuntimeSnapshot.h");
  const builder=read("scripts/build_approved_native_gui.py");
  const nativeMain=read("src/Main.cpp");
  assert.match(builder,/type==='input'&&window\.__JUCE__\?\.backend/);
  assert.match(builder,/__ARMIX_NATIVE_M2__\?\.inputChoices/);
  assert.match(bridge,/inputChoices\(t\)/);
  assert.match(bridge,/hardware\.device\.enabledInputs\.includes\(p\.port\)/);
  assert.match(bridge,/field==="input"/);
  // Reordering /preview/ strips must NOT change the real hardware lane.
  assert.match(bridge,/const channel=m4Index\(t\)/);
  assert.match(bridge,/const m4TrackIds=Array\.isArray\(scope\?\.tracks\)/);
  assert.match(bridge,/const savedId = uiByNativeId\.get\(ch\.id\) \?\? m4Id/);
  assert.match(bridge,/action:"routingSourceSwitch"/);
  assert.match(bridge,/row\?\.left===p\.choice\.left/);
  assert.match(nativeMain,/autoMainEpoch_!=engine_\.dryRoutingLabEpoch\(\)/);
  assert.match(bridge,/inputSelect\.hidden=true;routeBtn\.hidden=true;armBtn\.hidden=true/);
  assert.match(bridge,/openNativeAudioIO\(\)/);
  assert.match(bridge,/\.armix-audio-shade\[hidden\]\{display:none\}/);
  assert.match(producer,/firstSound->setProperty\("channels",juce::var\(verifiedChannels\)\)/);
  assert.match(producer,/publishAuthoritativeMixerSession/);
  assert.match(nativeMain,/publishAuthoritativeMixerSession\(accepted\)/);
  assert.match(bridge,/session\.authority==="CHANNEL_ID"/);
  assert.match(bridge,/m4ControlIsOwner\(\)/);
  assert.match(producer,/list->size\(\)==10/);
  assert.match(nativeMain,/engine_\.switchDryRoutingLabSource\(channel,/);
  assert.match(nativeMain,/engine_\.liveOutputArmFault\(\)/);
  // Public Pages still has its simulation, but NONE of those fake inputs
  // can be submitted by the native bridge. Only the native injected source.
  assert.doesNotMatch(bridge,/channelOptions\(t,'input'\)/);
});
test("DAW G0 native M4 controls, source clear, OUT & monitoring are real",()=>{
 const producer=read("integration/ApprovedNativeRuntimeSnapshot.h");
 const bridge=read("web/approved-native-bridge.js");
 const native=read("src/Main.cpp");
 const source=read("src/NativeVerifiedSourcePatch.h");
 const builder=read("scripts/build_approved_native_gui.py");
 assert.match(producer,/firstSound->setProperty\("channelControls",controls\)/);
 assert.match(producer,/firstSound->setProperty\("channelPeaksL",levelsL\)/);
 assert.match(bridge,/function paintM4Fallback\(next\)/);
 assert.match(bridge,/function sendM4Control\(parameter,t,value\)/);
 assert.match(bridge,/action:"routingM4Control"/);
 assert.match(bridge,/action:"routingChannelPatch"/);
 assert.match(bridge,/const channel=m4Index\(t\)/);
 assert.match(bridge,/data-native-monitor/);
 assert.match(native,/void serviceAutomaticMain\(\)/);
 assert.match(native,/if\(action=="routingM4Control"\)/);
 assert.match(native,/engine_\.setDryRoutingLabParameter\(channel,/);
 assert.match(native,/if\(action=="routingChannelPatch"\)/);
 assert.match(native,/engine_\.repatchDryRoutingLabChannel\(channel,/);
 assert.match(source,/if\(mode!="NONE"&&mode!="MONO"&&mode!="STEREO"\)/);
 assert.match(source,/row->setProperty\("sourceKind",clearing\?"NONE":"PHYSICAL"\)/);
 assert.match(builder,/type==='output'/);
 assert.doesNotMatch(bridge,/AudioContext|AudioWorklet/);
});
test("P0 physical SOURCE switching rejects fractional and stale native session tokens before PCM",()=>{
 const main=read("src/Main.cpp");
 const tokens=read("src/NativeSessionToken.h");
 const start=main.indexOf('if(action=="routingSourceSwitch")');
 const end=main.indexOf('if (action == "routingDryApply")',start);
 assert.ok(start>=0&&end>start);
 const source=main.slice(start,end);
 assert.match(source,/nativeSessionTokenMatches\(/);
 assert.match(source,/nativeSessionTokenValue\(ep\)/);
 assert.match(source,/nativeSessionTokenValue\(rv\)/);
 assert.match(source,/channel>=0&&channel<LiveDryRoutingLab::count&&tokens/);
 assert.doesNotMatch(source,/!ep\.isVoid\(\)&&!rv\.isVoid\(\)/);
 assert.doesNotMatch(source,/static_cast<juce::int64>\(ep\)/);
 assert.match(tokens,/std::isfinite\(n\)/);
 assert.match(tokens,/std::floor\(n\)==n/);
});
test("Native FX GUI routes through ChannelId PCM with no operator MONITOR ritual",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const main=read("src/Main.cpp");
 const engine=read("src/LiveAudioEngine.h");
 const pub=read("engine/core/DynamicNativeSessionPublisher.h");
 const lab=read("src/LiveDryRoutingLab.h");
 const contract=read("web/approved-native-contract.js");
 const translate=read("integration/ApprovedNativeControlIntent.h");
 const snapshot=read("integration/ApprovedNativeRuntimeSnapshot.h");
 const activeProjection=read("integration/ApprovedNativeV8Projection.h");
 assert.match(activeProjection,/insert->setProperty\("catalogId",catalog\)/);
 assert.match(activeProjection,/ch->setProperty\("fxRackBypassed",rackBypassed\)/);
 assert.match(bridge,/controller\.intent\("LOAD_INSERT",nativeId,/);
 assert.match(bridge,/if\(accepted\?\.admitted && nativeCh\)/);
 assert.match(lab,/nativeAccepted_\.fxOwners->externalChannel\(\)==target/);
 assert.match(bridge,/pendingFxInsert=\{authority:"CHANNEL_ID"/);
 assert.match(bridge,/inserted\?\.catalogId===p\.id/);
 assert.match(translate,/if\(op=="LOAD_INSERT"\)/);
 assert.match(snapshot,/available\.add\(juce::String\("LOAD_INSERT"\)\)/);
 assert.match(main,/engine_\.installNativeVst3\(/);
 assert.match(contract,/if \(operation === "LOAD_INSERT"\)/);
 assert.match(contract,/ch\.inserts\[payload\.slot\]\?\.catalogId===payload\.catalogId/);
 assert.match(lab,/epoch\(\),false,std::move\(fx\),false,true\)/);
 assert.match(pub,/verifiedInternalFxOnly/);
 const install=engine.slice(engine.indexOf("installNativeVst3("),
                            engine.indexOf("bool bypassNativeVst3(",engine.indexOf("installNativeVst3(")));
 assert.doesNotMatch(install,/!allOutputsDisarmed\(\)/);
 assert.match(install,/secondaryOutput_\.armed\(\)/);
 assert.match(install,/getLatencySamples\(\)>armix::dsp::StereoLatencyDelay::maximumSamples/);
});
test("Native VST3 insert ACK + readback distinguishes vendor catalog key from live instance token",()=>{
  const base=acceptedFixture();
  base.graph.capabilities.supportedOps.push("LOAD_INSERT");
  const sent=[],accepted=[];
  const controller=protocol.createController({emit:(name,payload)=>sent.push({name,payload}),
    onAccepted:s=>accepted.push(s)});
  assert.equal(controller.onSnapshot(base),true);
  const source=base.graph.channels[0].id;
  const cmd=controller.intent("LOAD_INSERT",source,{catalogId:"vendor.eq.catalog",slot:0});
  assert.equal(cmd.ok,true);
  const after=clone(base);
  after.graph.revision="6";after.graph.rosterRevision="8";
  after.graph.channels[0].inserts=[{id:"vst3-instance-unique-7",
    catalogId:"vendor.eq.catalog",name:"Vendor EQ",bypassed:false}];
  const next=protocol.normalizeSnapshot(after);
  const pending={operation:"LOAD_INSERT",targetChannelId:source,
    payload:{catalogId:"vendor.eq.catalog",slot:0},
    baseGraphRevision:"5",baseRosterRevision:"7"};
  assert.ok(next?.admitted);
  assert.equal(protocol.readbackProvesIntent(next,pending),true,
    "live instance token differs from catalog ID but effect is verified");
  for(const spoof of [
    {id:"vst3-instance-unique-7",name:"Vendor EQ",bypassed:false},
    {id:"vst3-instance-unique-7",catalogId:"different.plugin",name:"Vendor EQ",bypassed:false},
    {id:"",catalogId:"vendor.eq.catalog",name:"Vendor EQ",bypassed:false}
  ]){
    const bad=clone(after);bad.graph.channels[0].inserts=[spoof];
    assert.equal(protocol.readbackProvesIntent(protocol.normalizeSnapshot(bad),pending),false);
  }
  assert.equal(controller.onAck({requestId:cmd.requestId,status:"ACCEPTED",
    deviceEpoch:base.device.epoch,graphRevision:"6",rosterRevision:"8",errorCode:"NONE"}),true);
  assert.equal(controller.state().lastError,"PENDING_ACCEPTED_SNAPSHOT");
  assert.equal(controller.onSnapshot(after),true);
  assert.equal(controller.state().lastError,"ACCEPTED_READBACK");
  assert.equal(accepted.at(-1).graph.channels[0].inserts[0].id,"vst3-instance-unique-7");
  assert.ok(sent.some(evt=>evt.name==="arMixConsoleSubscribe"));
});
test("WIRE ROUTE ONLY to BUS preserves explicit MAIN parallel send after native ACK and readback",()=>{
  const before=acceptedFixture();
  const source=before.graph.channels[0], main=before.graph.mainChannelId;
  const oldBus="9007199254740994",newBus="9007199254740995";
  const group=id=>({id,role:"GROUP",name:id===oldBus?"BUS A":"BUS B",
    processingFormat:"STEREO",primaryOutId:main,
    controls:{faderDb:0,pan:0,mute:false,solo:false},inserts:[],sends:[]});
  before.graph.channels.splice(1,0,group(oldBus),group(newBus));
  before.graph.destinations=[{id:oldBus},{id:newBus},{id:main}];
  source.primaryOutId=oldBus;
  source.source={sourceFormat:"MONO",portId:"in:0"};
  const mkEdge=(id,from,to,primary)=>({edgeId:id,from,to,
    tap:"POST_FADER",gainLinear:1,enabled:true,signalKind:"AUDIO",
    primaryOutput:primary});
  before.graph.edges=[
    mkEdge("9223372036854775808",source.id,oldBus,true),
    mkEdge("9223372036854775809",source.id,main,false),
    mkEdge("9223372036854775810",oldBus,main,true),
    mkEdge("9223372036854775811",newBus,main,true)
  ];
  const events=[],controller=protocol.createController({
    emit:(name,payload)=>events.push({name,payload})});
  assert.equal(controller.onSnapshot(before),true);
  assert.equal(controller.state().current.admitted,true);
  const change={op:"ROUTE_ONLY",from:source.id,to:newBus,
    tap:"POST_FADER",gainLinear:1};
  const submitted=controller.intent("APPLY_WIRE_TRANSACTION",source.id,
    {changes:[change]});
  assert.equal(submitted.ok,true);
  const after=clone(before);
  after.graph.revision="6";after.graph.rosterRevision="8";
  after.graph.channels[0].primaryOutId=newBus;
  after.graph.edges[0].to=newBus; // exact native EdgeId promoted/retargeted
  assert.equal(controller.onAck({requestId:submitted.requestId,status:"ACCEPTED",
    deviceEpoch:before.device.epoch,graphRevision:"6",rosterRevision:"8",
    errorCode:"NONE"}),true);
  assert.equal(controller.state().lastError,"PENDING_ACCEPTED_SNAPSHOT");
  assert.equal(controller.onSnapshot(after),true);
  assert.equal(controller.state().lastError,"ACCEPTED_READBACK",
    "verified ROUTE_ONLY must not reject intentionally preserved parallel MAIN");
  assert.equal(controller.state().current.graph.channels[0].primaryOutId,newBus);
  assert.equal(controller.state().current.graph.edges.find(e=>
    e.edgeId==="9223372036854775809").primaryOutput,false);
  assert.ok(events.some(e=>e.name==="arMixConsoleSubscribe"));
  const invalid=clone(after);
  invalid.graph.edges[1].primaryOutput=true;
  const pending={operation:"APPLY_WIRE_TRANSACTION",targetChannelId:source.id,
    baseGraphRevision:"5",baseRosterRevision:"7",
    originalPrimaryOutId:oldBus,payload:{changes:[change]}};
  assert.equal(protocol.readbackProvesIntent(protocol.normalizeSnapshot(invalid),pending),false,
    "a second MAIN primary must still fail closed");
  invalid.graph.edges[1].primaryOutput=false;
  invalid.graph.edges[0].to=oldBus;
  assert.equal(protocol.readbackProvesIntent(protocol.normalizeSnapshot(invalid),pending),false,
    "old primary may not survive as an independent cable");
});
test("WIRE SOURCE ONLY readback retains physical input but removes primary MAIN",()=>{
  const before=acceptedFixture();
  const source=before.graph.channels[0];
  before.graph.edges[0].primaryOutput=true;
  source.source={sourceFormat:"MONO",portId:"in:0"};
  const after=clone(before);
  after.graph.revision="6";after.graph.rosterRevision="8";
  delete after.graph.channels[0].primaryOutId;
  after.graph.edges=[];
  const controller=protocol.createController({emit:()=>{}});
  assert.equal(controller.onSnapshot(before),true);
  const cmd=controller.intent("APPLY_WIRE_TRANSACTION",source.id,{changes:[
    {op:"REMOVE",from:source.id,edgeId:before.graph.edges[0].edgeId}
  ]});
  assert.equal(cmd.ok,true);
  assert.equal(controller.onAck({requestId:cmd.requestId,status:"ACCEPTED",
    deviceEpoch:before.device.epoch,graphRevision:"6",rosterRevision:"8",
    errorCode:"NONE"}),true);
  assert.equal(controller.onSnapshot(after),true);
  assert.equal(controller.state().lastError,"ACCEPTED_READBACK");
  assert.equal(controller.state().current.graph.channels[0].source.portId,"in:0");
  assert.equal(controller.state().current.graph.channels[0].primaryOutId,undefined);
});
test("M/ST after BUS/WIRE uses native ChannelId intent and accepted PCM processing width",()=>{
  const before=acceptedFixture();
  before.graph.capabilities.supportedOps.push("SET_PROCESSING_FORMAT");
  before.graph.channels[0].source={sourceFormat:"MONO",portId:"in:0"};
  const source=before.graph.channels[0].id,events=[];
  const controller=protocol.createController({emit:(name,payload)=>
    events.push({name,payload})});
  assert.equal(controller.onSnapshot(before),true);
  const change=controller.intent("SET_PROCESSING_FORMAT",source,{format:"STEREO"});
  assert.equal(change.ok,true);
  assert.equal(events.at(-1).payload.operation,"SET_PROCESSING_FORMAT");
  assert.equal(events.at(-1).payload.targetChannelId,source);
  const after=clone(before);
  after.graph.revision="6";after.graph.rosterRevision="8";
  after.graph.channels[0].processingFormat="STEREO";
  assert.equal(controller.onAck({requestId:change.requestId,status:"ACCEPTED",
    deviceEpoch:before.device.epoch,graphRevision:"6",rosterRevision:"8",
    errorCode:"NONE"}),true);
  assert.equal(controller.state().lastError,"PENDING_ACCEPTED_SNAPSHOT");
  assert.equal(controller.onSnapshot(after),true);
  assert.equal(controller.state().lastError,"ACCEPTED_READBACK");
  assert.equal(controller.state().current.graph.channels[0].source.portId,"in:0");
  const pending={operation:"SET_PROCESSING_FORMAT",targetChannelId:source,
    baseGraphRevision:"5",baseRosterRevision:"7",payload:{format:"STEREO"}};
  const stale=clone(after);
  stale.graph.channels[0].processingFormat="MONO";
  assert.equal(protocol.readbackProvesIntent(protocol.normalizeSnapshot(stale),pending),false);
  stale.graph.channels[0].processingFormat="STEREO";
  stale.graph.rosterRevision="7";
  assert.equal(protocol.readbackProvesIntent(protocol.normalizeSnapshot(stale),pending),false,
    "no M/ST success from a stale native roster revision");
  const bridge=read("web/approved-native-bridge.js");
  const fn=bridge.slice(bridge.indexOf("function requestProcessingFormat(t)"),
    bridge.indexOf("function previewNativePan(t,el,event)"));
  assert.match(fn,/session\(\)\?\.authority|mixerSession\(\)\?\.authority/);
  assert.match(fn,/controller.intent\("SET_PROCESSING_FORMAT",channelId,/);
  assert.ok(fn.indexOf('authority==="CHANNEL_ID"')<
    fn.indexOf('action:"routingChannelPatch"'));
});
test("P0 first Add FX discovers real local VST3 lazily without an audio-thread scan",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const native=read("src/Main.cpp");
 const catalog=read("src/PluginCatalog.h");
 assert.match(bridge,/function discoverFxOnFirstPickerOpen\(\)/);
 assert.match(bridge,/nativeFxDiscoveryRequested=true;/);
 assert.match(bridge,/backend\.emitEvent\("arMixAction",\{action:"scanVst3"\}\)/);
 assert.match(bridge,/discoverFxOnFirstPickerOpen\(\);[\s\S]*?scope\?\.openFastPicker/);
 assert.match(native,/action == "scanVst3"[\s\S]*?catalog_\.scanDefaults\(\)/);
 assert.match(catalog,/struct Scanner final : juce::Thread/);
 // The approved picker publishes real compiled-in FX independently of
 // external scanner state; the verified VST3 library follows those entries.
 assert.match(bridge,/const presentFxCatalog=\(\)=>scope\?\.setNativeCatalog\?\.\(/);
 assert.ok(bridge.includes('[...nativeBuiltins.filter(fx=>fx.showInPicker!==false),...nativeCatalog.values()]'));
 // Neither startup nor audio callback can trigger a surprise system scan.
 assert.doesNotMatch(bridge.slice(0,bridge.indexOf("function discoverFxOnFirstPickerOpen")),
   /action:"scanVst3"/);
});

test("P0 MAIN remains enabled through verified STEREO -> NONE -> STEREO edits",()=>{
 const engine=read("src/LiveAudioEngine.h");
 const host=read("src/Main.cpp");
 const bridge=read("web/approved-native-bridge.js");
 const guard=read("src/NativeTopologyEditSafety.h");
 // First ARM remains strict, including inactive hardware and known VB-Cable loops.
 assert.match(engine,/juce::String liveOutputArmFault\(\)\s*\{\s*return liveOutputArmFault\(true\);\s*\}/,
   "manual MONITOR always uses strict source verification");
 assert.match(engine,/juce::String liveOutputArmFault\(bool requirePhysicalSource\)/,
   "previously-authorized silent MAIN may resume without a physical IN");
 assert.match(engine,/if \(!physicalSource && requirePhysicalSource\)/);
 assert.match(engine,/knownVirtualCableFeedbackPair\(currentSetup\.inputDeviceName,/);
 assert.match(host,/fault=engine_\.liveOutputArmFault\(\)/);
 // Structural SOURCE changes gate physical output only at publication.
 // The user's MONITOR decision persists on the same safe device/route.
 assert.match(engine,/if\(wasMonitoring\)\s*topologyPublicationMute_\.store\(true,std::memory_order_release\)/);
 assert.match(engine,/if\(wasMonitoring && !safeToResume\)setOutputMuted\(true\)/);
 assert.match(engine,/topologyPublicationMute_\.store\(false,std::memory_order_release\)/);
 assert.match(engine,/monitor_\.load\(std::memory_order_relaxed\) &&\s*!topologyPublicationMute_\.load\(std::memory_order_acquire\)/);
 assert.match(engine,/wasMonitoring,sameEpoch,liveOutputArmFault\(false\)\.isEmpty\(\),/);
 assert.match(guard,/return previouslyArmed && sameDeviceEpoch && outputArmSafe && !streamArmed/);
 assert.match(bridge,/action:"routingSourceSwitch"/);
 // The browser must not be the authority for re-arming a previously verified
 // route; the engine owns state across silent source changes.
 assert.doesNotMatch(bridge,/routingSourceSwitch[^;]+monitor\(false\)/);
});

test("P0 post-BUS native IN routes NONE MONO STEREO via one PCM owner",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const host=read("src/Main.cpp");
 const engine=read("src/LiveAudioEngine.h");
 const lab=read("src/LiveDryRoutingLab.h");
 assert.match(bridge,/function dispatchNativeSource\(\)/);
 assert.match(bridge,/action:"routingNativeSourcePatch"/);
 assert.match(bridge,/arMixNativeSourceAck/);
 assert.match(bridge,/pendingNativeSources.set\(id,choice\)/);
 assert.match(host,/if\(action=="routingNativeSourcePatch"\)/);
 assert.match(host,/engine_\.patchNativeInputSource\(ep,rev,roster,/);
 assert.match(lab,/const bool disconnect=!stereo&&physicalLeft==-1&&physicalRight==-1/);
 assert.match(lab,/nativeStructureLive_=true; \/\/ M4 source roster no longer authoritative/);
 assert.match(engine,/NATIVE_SOURCE_PUBLICATION_EXCEPTION/);
});
test("native right-click track deletion and vendor-specific Orbit are graph-authoritative",()=>{
 const menu=read("web/approved-native-context-menu.js");
 // Parse the actual deferred WebView2 menu script before any Windows package.
 assert.doesNotThrow(()=>new (require("node:vm").Script)(menu));
 const bridge=read("web/approved-native-bridge.js");
 const host=read("src/Main.cpp");
 const lab=read("src/LiveDryRoutingLab.h");
 const roster=read("engine/core/DynamicChannelRoster.h");
 const fxBank=read("engine/core/DynamicChannelFxLeaseBank.h");
 const installer=read("scripts/build_approved_native_gui.py");
 assert.match(menu,/document\.addEventListener\("contextmenu"/);
 assert.match(menu,/e\.preventDefault\(\)/);
 assert.match(menu,/\.strip-identity-top/);
 assert.match(menu,/\.fxslot-row:not\(\.is-empty\)/);
 assert.match(menu,/Confirm Delete Track/);
 assert.match(menu,/requestDeleteTrack\(track\)/);
 assert.match(menu,/hooks\.orbitTarget\(track,slot\)/);
 assert.match(bridge,/action:"routingNativeTrackDelete"/);
 assert.match(bridge,/arMixNativeTrackDeleteAck/);
 assert.match(bridge,/function finishNativeTrackDelete\(next\)/);
 assert.match(bridge,/pending\.acked/);
 assert.match(bridge,/kind==="inspect"\?"routingNativeOrbitInspect"/);
 assert.match(bridge,/kind==="inspect"\?"routingM4OrbitInspect"/);
 assert.match(host,/engine_\.leaseRoutedFxForEditor\(/);
 assert.match(host,/engine_\.removeNativeInputChannel\(ep,rev,roster,/);
 assert.match(host,/engine_\.leaseNativeVst3Editor\(/);
 assert.match(host,/nativeOrbitLease_/);
 assert.match(host,/arMixNativeOrbitState/);
 assert.match(lab,/removeNativeInputChannel/);
 assert.match(lab,/request\.sources/);
 assert.match(roster,/removeInputAndDisconnect/);
 assert.match(fxBank,/reuseWithoutChannel/);
 assert.match(installer,/approved-native-context-menu\.js/);
});

test("all native strip menus bind true AUX/BUS deletion and persistent Rename, Orbit anchors and moves",()=>{
 const menu=read("web/approved-native-context-menu.js");
 const bridge=read("web/approved-native-bridge.js");
 const host=read("src/Main.cpp");
 const lab=read("src/LiveDryRoutingLab.h");
 const projection=read("integration/ApprovedNativeRuntimeSnapshot.h");
 const cmake=read("CMakeLists.txt");
 assert.doesNotThrow(()=>new (require("node:vm").Script)(menu));
 assert.match(menu,/\["audio","fx","bus","master"\]/);
 assert.match(menu,/data-menu="rename"/);
 assert.match(menu,/data-menu="delete"/);
 assert.match(menu,/hooks\.renameStrip\?\.\(track,anchor\)/);
 assert.match(menu,/hooks\.requestDeleteStrip\?\.\(track\)/);
 assert.match(menu,/const originX=menu\.offsetLeft,originY=menu\.offsetTop\+menu\.offsetHeight/);
 assert.match(menu,/placeOrbitNear\(originX,originY\)/);
 assert.match(menu,/setPointerCapture\(e\.pointerId\)/);
 assert.match(menu,/pointermove/);
 assert.match(menu,/user-select:none/);
 assert.match(menu,/clearTitles\(document\.documentElement\)/);
 assert.match(menu,/attributeFilter:\["title"\]/);
 assert.match(bridge,/action:"routingNativeBusDelete"/);
 assert.match(bridge,/requestDeleteStrip:requestNativeStripDelete/);
 assert.match(bridge,/renameStrip:openNativeIdentity/);
 assert.match(bridge,/canDeleteStrip:canDeleteNativeStrip/);
 assert.match(bridge,/pending\.acked/);
 assert.match(host,/if\(action=="routingNativeBusDelete"\)/);
 assert.match(host,/engine_\.editNativeBus\(ep,rev,roster,role,/);
 assert.match(host,/arMixNativeTrackDeleteAck/);
 assert.match(lab,/reuseWithoutChannel/);
 assert.match(lab,/nativeStructureLive_=true;/);
 assert.doesNotMatch(lab,/FIXED_M4_AUX_NOT_REMOVABLE/);
 assert.match(projection,/available\.add\(juce::String\("REMOVE_BUS"\)\)/);
 assert.match(cmake,/web\/approved-native-context-menu\.js/);
});

test("P0 actual Add BUS reaches the live ChannelId graph",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const host=read("src/Main.cpp");
 const engine=read("src/LiveAudioEngine.h");
 const lab=read("src/LiveDryRoutingLab.h");
 const projection=read("integration/ApprovedNativeRuntimeSnapshot.h");
 assert.match(bridge,/function requestNativeBusCreate\(track=false\)/);
 assert.match(bridge,/action:track\?"routingNativeTrackCreate":"routingNativeBusCreate"/);
 assert.match(bridge,/target.id==="addBus"/);
 assert.match(bridge,/arMixNativeBusAck/);
 assert.match(bridge,/addBus.hidden=!nativeAdmitted/);
 assert.match(host,/if\(action=="routingNativeBusCreate" \|\| action=="routingNativeTrackCreate"\)/);
 assert.match(host,/engine_\.editNativeBus\(ep,rev,roster,/);
 assert.match(engine,/topologyPublicationMute_\.store\(/);
 assert.match(lab,/if\(beforePublish\)beforePublish\(\)/);
 assert.match(projection,/available\.add\(juce::String\("CREATE_GROUP"\)\)/);
});
test("P0 installed ChannelId WIRE capability reaches the GUI",()=>{
 const projection=read("integration/ApprovedNativeRuntimeSnapshot.h");
 const native=read("src/LiveDryRoutingLab.h");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(native,/NATIVE_APPLY_WIRE_TRANSACTION/);
 assert.match(projection,/if\(has\("NATIVE_APPLY_WIRE_TRANSACTION"\)\)/);
 assert.match(projection,/available\.add\(juce::String\("APPLY_WIRE_TRANSACTION"\)\)/);
 assert.match(bridge,/const kind=isPrimary\?"primary"/);
 assert.match(bridge,/if \(isPrimary && edge\.gainLinear!==1\)/);
 assert.doesNotMatch(bridge,/if \(isMain && edge\.gainLinear!==1\)/);
});
test("P0 WIRE-to-MAIN and channel OUT use one acknowledged native transaction",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const builder=read("scripts/build_approved_native_gui.py");
 const native=read("src/Main.cpp");
 assert.match(bridge,/function requestPrimaryOut\(t,target\)/);
 assert.match(bridge,/session\?\.authority==="CHANNEL_ID"/);
 assert.match(bridge,/controller\.intent\("APPLY_WIRE_TRANSACTION",channelId,/);
 assert.match(bridge,/const to=target==="MAIN"\?main:destination\?\.id\|\|null/);
 assert.match(bridge,/change=\{op:"ROUTE_ONLY",from:channelId,to,/);
 assert.match(bridge,/change=\{op:"REMOVE",from:channelId,edgeId:edge\.edgeId\}/);
 // Only the accepted PRIMARY edge may be detached. A parallel SEND to
 // MAIN must not be misclassified as a physical OUT.
 assert.match(bridge,/e\.primaryOutput===true/);
 const primaryOutPath=bridge.slice(bridge.indexOf("function requestPrimaryOut"),bridge.indexOf("const pendingNativeSources"));
 assert.doesNotMatch(primaryOutPath,/e\.primaryOutput!==false/);
 const roster=read("engine/core/DynamicChannelRoster.h");
 const translator=read("integration/ApprovedNativeControlIntent.h");
 assert.match(roster,/target->primaryOutput=true/);
 assert.match(roster,/s\.primaryOutput &&/);
 assert.doesNotMatch(translator,/WIRE_MAIN_PRIMARY_REQUIRED/);
 assert.match(bridge,/const channel=m4Index\(t\)/);
 // A native-owned MAIN WIRE never triggers the legacy routingChannelPatch.
 const primary=bridge.slice(bridge.indexOf("function requestPrimaryOut(t,target)"),
                            bridge.indexOf("let pendingFxInsert"));
 assert.ok(primary.indexOf('session?.authority==="CHANNEL_ID"')>=0);
 assert.ok(primary.indexOf('session?.authority==="CHANNEL_ID"')<
           primary.indexOf("const channel=m4Index(t)"),
           "ChannelId branch must precede the legacy M4 fallback inside requestPrimaryOut");
 assert.match(bridge,/const isMainConnect=/);
 assert.match(bridge,/const isMainRemove=/);
 assert.match(bridge,/current\.graph\.edges\.some/);
 assert.match(bridge,/if\(requestPrimaryOut\(t,target\)\)/);
 assert.match(native,/action=="routingChannelPatch"/);
 assert.match(native,/engine_\.repatchDryRoutingLabChannel\(channel,/);
 assert.match(bridge,/scope\?\.closeNativePicker\?\.\(\)/);
 assert.match(builder,/closeNativePicker\(\)\{setFastPicker\(false\);\}/);
 assert.match(builder,/Native EQ\/Compressor\/Harmony ready/);
 assert.match(builder,/external VST3 require verified scan/);
 assert.doesNotMatch(bridge,/AudioContext|AudioWorklet/);
});
test("P0 typed mono-stereo, PAN detent, fader geometry & FX failure diagnostics",()=>{
 const page=read("site/preview-v2/index.html");
 const bridge=read("web/approved-native-bridge.js");
 const main=read("src/Main.cpp");
 const snapshot=read("integration/ApprovedNativeRuntimeSnapshot.h");
 const nativeBundle=read("scripts/build_approved_native_gui.py");
 assert.match(nativeBundle,/const FADER_TAPER=Object\.freeze/);
 assert.match(nativeBundle,/function faderDbFromTravelPercent\(percent,isSend\)/);
 assert.match(nativeBundle,/faderDbFromTravelPercent\(100\*\(1-frac\),max===6\)/);
 assert.doesNotMatch(page,/FADER_TAPER/, "public Pages reference remains immutable");
 assert.match(bridge,/function previewNativePan\(t,el,event\)/);
 assert.match(bridge,/const width=event\.type==="change"\?12:latched\?12:7/);
 assert.match(bridge,/readout\.textContent=value===0\?"C"/);
 assert.match(bridge,/function requestProcessingFormat\(t\)/);
 assert.match(bridge,/operation:"SET_PROCESSING_FORMAT"/);
 assert.match(bridge,/if\(action==="format"&&t\?\.kind==="audio"\)/);
 assert.match(main,/operation=="SET_PROCESSING_FORMAT"/);
 assert.match(main,/operation,target\)/);
 assert.match(snapshot,/verified->setProperty\("channelFormat",format\)/);
 assert.match(bridge,/backend\.addEventListener\("arMixNotice"/);
 assert.doesNotMatch(bridge,/AudioContext|AudioWorklet/);
});
test("P0 MAIN WIRE unplug uses UI-to-native EdgeId and no fake graph",()=>{
 const adapter=read("web/approved-native-bridge.js");
 assert.match(adapter,/const edgeId=wireByUiId\.get\(op\.edgeId\)/);
 assert.match(adapter,/menu\.setAttribute\("aria-busy","true"\)/);
 assert.match(adapter,/el\.removeAttribute\("aria-busy"\)/);
 assert.doesNotMatch(adapter,/const edgeId=uiByWireId\.get\(op\.edgeId\)/);
 assert.match(adapter,/current\.graph\.edges\.some/);
 assert.match(adapter,/WIRE BUS\/FX return is not admitted/);
 assert.match(adapter,/requestPrimaryOut\(t,target\)/);
});
test("P0 real slot VST3 power and native editor use verified instance tokens",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const main=read("src/Main.cpp");
 assert.match(bridge,/function verifiedFxFor\(t,slot\)/);
 assert.match(bridge,/function requestNativeFxPower\(t,slot,bypass\)/);
 assert.match(bridge,/if\(action==="slotPower"\)/);
 assert.match(bridge,/if\(action==="fxOn"\)/);
 assert.doesNotMatch(bridge,/rackSavedBypass/);
 assert.match(bridge,/requestM4RackPower\(t\)/);
 assert.match(bridge,/requestAdmittedFxPower\(t,action,btn\)/);
 assert.match(bridge,/FX rack unchanged/);
 assert.match(bridge,/pendingM4RackPowers/);
 assert.match(bridge,/action:"routingFxEditor"/);
 assert.match(bridge,/action:"routingFxBypass"/);
 assert.match(bridge,/backend\.addEventListener\("arMixRoutingFxBypassAck"/);
 assert.match(main,/ack->setProperty\("requestId",args\.getProperty\("requestId",juce::var\(\)\)\)/);
 assert.match(main,/action == "routingFxEditor"/);
 assert.match(main,/action == "routingFxBypass"/);
});
test("P0 VST3 preparation runs with old live PCM; monitor closes only at publication",()=>{
 const lab=read("src/LiveDryRoutingLab.h");
 const engine=read("src/LiveAudioEngine.h");
 assert.match(lab,/#include <functional>/);
 assert.match(lab,/if\(beforePublish\)beforePublish\(\)/);
 assert.match(lab,/const auto epochAtPublish=deviceEpoch_\.load/);
 assert.match(lab,/next,formats,sampleRate,deviceFrames,true,nullptr,\s*beforePublish/);
 assert.match(engine,/bool enteredPublication=false/);
 assert.match(engine,/enteredPublication=true;[\s\S]*?if\(wasMonitoring\)\s*topologyPublicationMute_\.store\(true,std::memory_order_release\)/);
 assert.match(engine,/if\(!enteredPublication\)return error/);
 assert.match(engine,/return finishGuardedDryTopology\(/);
 assert.match(engine,/return applyGuardedDryTopology\(epoch,frames,\[&\]\(const auto& beforePublish\)/);
 // FX and SOURCE recompiles must inherit the SAME real active fader, pan,
 // mute and solo from native ChannelId or M4 before replacing a graph.
 assert.match(lab,/juce::String inheritLiveControlsForRecompile\(/);
 assert.match(lab,/juce::String insertCatalogFx[\s\S]*?const auto inherited=inheritLiveControlsForRecompile\(\s*command,expectedEpoch\)/);
 assert.match(lab,/juce::String publishRevisedTopology[\s\S]*?const auto inherited=inheritLiveControlsForRecompile\(\s*staged,expectedEpoch\)/);
 assert.match(lab,/NativeTopologyEditSafety::preserveAcceptedMix\(/);
 assert.match(lab,/case Publisher::Result::applied:[\s\S]*?nativeControlDirty_=false/);
});
test("P0 SOURCE, OUT, M-ST and FX share late-gated graph publication",()=>{
 const lab=read("src/LiveDryRoutingLab.h");
 const engine=read("src/LiveAudioEngine.h");
 // Format/input edits and native VST3 insert must all enter the same gate.
 assert.match(lab,/juce::String repatchPhysicalSource[\s\S]*?int deviceFrames,\s*const std::function<void\(\)>& beforePublish/);
 assert.match(lab,/juce::String repatchChannelStructure[\s\S]*?int deviceFrames,\s*const std::function<void\(\)>& beforePublish/);
 assert.match(lab,/juce::String publishRevisedTopology[\s\S]*?int deviceFrames,\s*const std::function<void\(\)>& beforePublish/);
 assert.match(lab,/acceptedFxSelections_,formats,sampleRate,deviceFrames,true,\s*nullptr,beforePublish/);
 assert.match(engine,/if\(!structural\)\s*return dryRoutingLab_\.repatchPhysicalSource/);
 assert.match(engine,/return applyGuardedDryTopology\(epoch,frames,\[&\]\(const auto& beforePublish\)/);
 assert.match(engine,/frames,beforePublish\);/);
});
test("P0 FX command preflight never mutes monitoring for deterministic rejection",()=>{
 const lab=read("src/LiveDryRoutingLab.h");
 const engine=read("src/LiveAudioEngine.h");
 assert.match(lab,/juce::String fxInsertPreflight\(int lane,int slot/);
 assert.match(lab,/const auto preflight=fxInsertPreflight\(/);
 assert.match(engine,/dryRoutingLab_\.fxInsertPreflight\(/);
 assert.match(engine,/pluginFormat->fileMightContainThisPluginType\(chosen\.path\)/);
 assert.match(engine,/FX INSERT: VST3 bundle unavailable or invalid/);
});
test("P0 JUCE native errors use the actual notice.text event contract",()=>{
 const host=read("src/Main.cpp");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(host,/o->setProperty \("text", message\)/);
 assert.match(host,/o->setProperty \("error", error\)/);
 assert.match(bridge,/backend\.addEventListener\("arMixNotice",notice=>/);
 assert.match(bridge,/const message=notice\?\.text/);
 assert.match(bridge,/nativeToast\(message\)/);
 assert.doesNotMatch(bridge,/notice\.message/);
});
test("P0 native WIRE MAIN disconnect requires exact accepted EdgeId or verified M4 route",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const bundle=read("scripts/build_approved_native_gui.py");
 assert.match(bridge,/const edgeId=wireByUiId\.get\(op\.edgeId\)/);
 assert.match(bridge,/const graphMainRemove=op\.type==="REMOVE"/);
 assert.match(bridge,/const m4MainRemove=op\.type==="REMOVE"/);
 assert.match(bridge,/current\?\.admitted!==true/);
 assert.match(bridge,/legacy\(\)\?\.channels\?\.\[lane\]\?\.main===true/);
 assert.match(bridge,/e\.id===op\.edgeId&&e\.sourceId===op\.sourceId/);
 assert.match(bridge,/e\.destId===scope\?\.master\?\.id/);
 assert.match(bundle,/get wireEdges\(\)\{return wireEdges;\}/);
});
test("P0 native-only WIRE cables reflect actual M4 MAIN readback, not demo ghosts",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const bundle=read("scripts/build_approved_native_gui.py");
 assert.match(bridge,/if\(session\.authority==="M4" && m4Verified\(\)\)/);
 assert.match(bridge,/scope\?\.syncVerifiedM4Wires\?\.\(session\.channels\.map\(ch=>ch\.source\),m4TrackIds\)/);
 assert.match(bundle,/syncVerifiedM4Wires\(rows,ids\)/);
 assert.match(bundle,/typeof r\.main!=='boolean'/);
 assert.match(bundle,/if\(rows\[i\]\.main\)/);
 assert.match(bundle,/nextWireId\+\+/);
 assert.match(bundle,/verifiedM4WireDirty/);
 assert.match(bundle,/refreshWireRoutingNodes\(\)/);
 assert.match(bundle,/!wireGesture&&!faderGesture/);
});
test("P0 same-VB-Cable feedback is blocked before native MAIN arm",()=>{
 const engine=read("src/LiveAudioEngine.h");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(engine,/knownVirtualCableFeedbackPair/);
 assert.match(engine,/in\.contains\("cable output"\) && out\.contains\("cable input"\)/);
 assert.match(engine,/currentSetup\.inputDeviceName/);
 assert.match(engine,/currentSetup\.outputDeviceName/);
 assert.match(engine,/MONITOR OFF: VB-Cable capture\/playback self-loop/);
 assert.match(bridge,/const virtualCableFeedbackPair=/);
 const native=read("src/Main.cpp");
 assert.match(native,/if\(engine_\.liveOutputArmFault\(autoMainRequiresInput_\)\.isNotEmpty\(\)\)return;/);
 assert.match(native,/autoMainEpoch_!=engine_\.dryRoutingLabEpoch\(\)/);
});
test("single /preview/ GUI: channel IN, actual OUT and correlated device safety ACK",()=>{
 const pages=read("scripts/build_pages.py");
 const builder=read("scripts/build_approved_native_gui.py");
 const bridge=read("web/approved-native-bridge.js");
 const native=read("src/Main.cpp");
 assert.match(pages,/buttons_dest=dst\/'preview'/);
 assert.match(builder,/public = destination \/ "preview"/);
 assert.match(bridge,/source:"\/preview\/ approved compiled Pages GUI"/);
 assert.match(bridge,/field==="input"/);
 assert.match(bridge,/nativeUiSourcePending=null;[\s\S]*?INPUT timeout/);
 assert.match(bridge,/ack\?\.requestId!==devicePanel.dataset.pendingRequest/);
 assert.match(native,/requestAutomaticMain\(\);\s*send\(true,engine_\.isMonitorEnabled\(\)\?"ACCEPTED_LIVE":"ACCEPTED_MUTED"/);
 assert.match(bridge,/Virtual output · No speakers/);
 assert.match(bridge,/Advanced driver recovery/);
 assert.match(native,/ack->setProperty\("requestId",[\s\S]*?args\.getProperty\("requestId",juce::var\(\)\)/);
 assert.doesNotMatch(bridge,/AudioContext|AudioWorklet/);
});
test("strict producer rejects JS Number identities, invalid hardware pins and fake LIVE",()=>{
 const cases=[
  s=>{s.device.epoch=69;},
  s=>{s.graph.revision=5;},
  s=>{s.device.enabledInputs=[0,0];},
  s=>{s.device.inputEndpoints[0].port=88;},
  s=>{s.graph.channels[0].id=Number.MAX_SAFE_INTEGER+5;},
  s=>{s.graph.edges[0].edgeId="18446744073709551616";},
  s=>{s.graph.edges[0].to="0";},
  s=>{s.graph.channels[1].id=s.graph.channels[0].id;},
 ];
 for(const corrupt of cases){
  const s=acceptedFixture();corrupt(s);
  assert.equal(protocol.normalizeSnapshot(s),null);
 }
 const s=acceptedFixture();
 assert.equal(protocol.normalizeSnapshot(s).admitted,true);
 assert.equal(s.graph.channels[0].id,"9007199254740993");
});
test("real ChannelId PCM graph is usable before product/vendor FAT, without fake LIVE",()=>{
 const staged=acceptedFixture();
 staged.productionCallbackLive=false;
 const parsed=protocol.normalizeSnapshot(staged);
 assert.ok(parsed);
 assert.equal(parsed.admitted,true,"native callback admission is independent of product FAT");
 assert.equal(parsed.productLive,false,"staged graph NEVER claims full product LIVE");
 const shipped=acceptedFixture();
 assert.equal(protocol.normalizeSnapshot(shipped).productLive,true);
 const lying=acceptedFixture();
 lying.graph.capabilities.callbackOwnsChannelIds=false;
 assert.equal(protocol.normalizeSnapshot(lying),null,
   "product LIVE cannot be claimed without an installed ChannelId PCM callback");
 const fake=acceptedFixture();
 fake.graph.channels[0].controls=undefined;
 assert.equal(protocol.normalizeSnapshot(fake),null,
   "missing native fader/mute/solo readback cannot be papered over in JS");
});

test("ACCEPTED ACK without matching physical control readback is rejected, not painted",()=>{
 const sent=[],visible=[];
 const c=protocol.createController({emit:(name,payload)=>sent.push({name,payload}),
   onAccepted:x=>visible.push(x.graph.channels[0].controls.faderDb)});
 const staged=acceptedFixture();staged.productionCallbackLive=false;
 assert.equal(c.onSnapshot(staged),true);
 const i=c.intent("SET_FADER","9007199254740993",{valueDb:-23});
 assert.equal(i.ok,true);
 assert.equal(c.onAck({requestId:i.requestId,status:"ACCEPTED",
   deviceEpoch:staged.device.epoch,graphRevision:"6",rosterRevision:"7",
   errorCode:"NONE"}),true);
 const lied=clone(staged);
 lied.graph.revision="6"; // claimed graph revision, but fader still -6 dB
 assert.equal(c.onSnapshot(lied),false);
 assert.equal(c.state().current.graph.revision,"5");
 assert.deepEqual(visible,[-6]);
 assert.equal(c.state().pending,null);
 assert.equal(c.state().lastError,"ACK_READBACK_EFFECT_MISMATCH");
 assert.equal(sent.at(-1).name,"arMixConsoleSubscribe");
});

test("staged PCM PAN/MUTE checks real native controls and rejects wrong readback",()=>{
 const s=acceptedFixture();s.productionCallbackLive=false;
 const c=protocol.createController({emit:()=>{}});
 c.onSnapshot(s);
 const pan=c.intent("SET_PAN","9007199254740993",{value:0.5});
 assert.equal(pan.ok,false,"never advertise an uninstalled operation");
 s.graph.capabilities.supportedOps.push("SET_PAN");
 const d=protocol.createController({emit:()=>{}});
 d.onSnapshot(s);
 const p=d.intent("SET_PAN","9007199254740993",{value:0.5});
 assert.equal(p.ok,true);
 assert.equal(d.onAck({requestId:p.requestId,status:"ACCEPTED",
   deviceEpoch:s.device.epoch,graphRevision:"5",rosterRevision:"7",errorCode:"NONE"}),true);
 const updated=clone(s);updated.graph.channels[0].controls.pan=0.5;
 assert.equal(d.onSnapshot(updated),true);
 assert.equal(d.state().current.graph.channels[0].controls.pan,0.5);
 const m=d.intent("SET_MUTE","9007199254740993",{value:true});
 assert.equal(m.ok,true);
 assert.equal(d.onAck({requestId:m.requestId,status:"ACCEPTED",
   deviceEpoch:s.device.epoch,graphRevision:"5",rosterRevision:"7",errorCode:"NONE"}),true);
 const mute=clone(updated);mute.graph.channels[0].controls.mute=true;
 assert.equal(d.onSnapshot(mute),true);
 assert.equal(d.state().current.graph.channels[0].controls.mute,true);
});

test("WIRE REMOVE requires actual native EdgeId absent from later accepted graph",()=>{
 const s=acceptedFixture();s.productionCallbackLive=false;
 const source="9007199254740993",edge="9223372036854775808";
 const sent=[];
 const c=protocol.createController({emit:(n,p)=>sent.push({n,p})});
 c.onSnapshot(s);
 const removed=c.intent("APPLY_WIRE_TRANSACTION",source,
   {changes:[{op:"REMOVE",from:source,edgeId:edge}]});
 assert.equal(removed.ok,true);
 assert.equal(c.onAck({requestId:removed.requestId,status:"ACCEPTED",
   deviceEpoch:s.device.epoch,graphRevision:"6",rosterRevision:"7",errorCode:"NONE"}),true);
 const before=clone(s);before.graph.revision="6";
 assert.equal(c.onSnapshot(before),false,"ACK with unchanged cable MUST NOT light a false connection");
 assert.equal(c.state().current.graph.edges.length,1);
 assert.equal(c.state().lastError,"ACK_READBACK_EFFECT_MISMATCH");
 const verified=protocol.createController({emit:()=>{}});
 verified.onSnapshot(s);
 const req=verified.intent("APPLY_WIRE_TRANSACTION",source,
   {changes:[{op:"REMOVE",from:source,edgeId:edge}]});
 verified.onAck({requestId:req.requestId,status:"ACCEPTED",
   deviceEpoch:s.device.epoch,graphRevision:"6",rosterRevision:"7",errorCode:"NONE"});
 before.graph.edges=[];
 assert.equal(verified.onSnapshot(before),true);
 assert.deepEqual(verified.state().current.graph.edges,[]);
});

test("native meter and PAN scale are driven by PCM readback, not Pages demo",()=>{
 const source=read("web/approved-native-bridge.js");
 assert.match(source,/paintNativeMeters\(next\)/);
 assert.match(source,/displayMeterDbfs\(fill,/);
 assert.match(source,/t\.pan=ch\.controls\.pan\*100/);
 assert.ok(source.includes("next.productLive"));
 assert.ok(source.includes("op:op.type,from:sourceId"));
 assert.ok(!source.includes("changes.push({type:op.type,sourceId"));
 assert.doesNotMatch(source,/Math\.sin\(/);
});

test("SET_FADER: one exact string request, ACK then LATER same-revision native readback",()=>{
 const sent=[],visible=[];
 const c=protocol.createController({emit:(name,payload)=>sent.push({name,payload}),onAccepted:s=>visible.push(s.identity)});
 const s=acceptedFixture();
 assert.equal(c.onSnapshot(s),true);
 const result=c.intent("SET_FADER","9007199254740993",{valueDb:-12});
 assert.equal(result.ok,true);
 const message=sent.at(-1).payload;
 assert.match(message.requestId,/^m2-[0-9a-z]+-[0-9a-z]+-1$/);
 assert.equal(message.targetChannelId,"9007199254740993");
 assert.equal(message.deviceEpoch,s.device.epoch);
 assert.equal(message.baseGraphRevision,"5");
 assert.equal(message.baseRosterRevision,"7");
 const later=clone(s);later.graph.revision="6";later.graph.channels[0].controls.faderDb=-12;
 assert.equal(c.onSnapshot(later),true);
 assert.equal(c.state().current.graph.revision,"5","no unACKed repaint");
 assert.equal(visible.length,1);
 assert.equal(c.onAck({requestId:"wrong",status:"ACCEPTED",deviceEpoch:s.device.epoch,
   graphRevision:"6",rosterRevision:"7",errorCode:"NONE"}),false);
 assert.equal(c.onAck({requestId:result.requestId,status:"ACCEPTED",deviceEpoch:s.device.epoch,
   graphRevision:"6",rosterRevision:"7",errorCode:"NONE"}),true);
 assert.equal(c.state().current.graph.revision,"5","ACK alone MUST NOT repaint");
 assert.equal(sent.at(-1).name,"arMixConsoleSubscribe","later readback requested");
 assert.equal(c.onSnapshot(later),true);
 assert.equal(c.state().current.graph.revision,"6");
 assert.equal(c.state().pending,null);
 assert.equal(visible.length,2);
});
test("STALE/REJECTED/timeout leave accepted GUI untouched and request fresh native readback",()=>{
 const sent=[],c=protocol.createController({emit:(name,payload)=>sent.push({name,payload})});
 const s=acceptedFixture();c.onSnapshot(s);
 assert.equal(c.intent("SET_PAN","9007199254740993",{value:0.5}).ok,false);
 assert.equal(c.intent("SET_FADER","1",{valueDb:0}).ok,false);
 const one=c.intent("SET_FADER","9007199254740993",{valueDb:0});
 assert.equal(c.intent("SET_FADER","9007199254740993",{valueDb:1}).ok,false);
 assert.equal(c.onAck({requestId:one.requestId,status:"REJECTED",
   deviceEpoch:s.device.epoch,graphRevision:"5",rosterRevision:"7",errorCode:"STALE_GRAPH"}),false);
 assert.equal(c.state().current.graph.revision,"5");
 assert.equal(c.state().pending,null);
 assert.equal(c.state().lastError,"STALE_GRAPH");
 const two=c.intent("SET_FADER","9007199254740993",{valueDb:1});
 assert.equal(c.expire(two.requestId),true);
 assert.equal(c.expire(two.requestId),false);
 assert.equal(c.state().pending,null);
 assert.equal(c.state().current.graph.revision,"5");
 const three=c.intent("SET_FADER","9007199254740993",{valueDb:2});
 const changed=clone(s);changed.device.epoch="18446744073709551611";
 changed.graph.revision="0";changed.graph.rosterRevision="0";
 changed.productionCallbackLive=false;
 changed.graph.channels=[];changed.graph.edges=[];changed.graph.destinations=[];
 delete changed.graph.mainChannelId;
 changed.graph.capabilities.callbackOwnsChannelIds=false;
 changed.graph.capabilities.supportedOps=[];
 assert.equal(c.onSnapshot(changed),true);
 assert.equal(c.state().current.admitted,false);
 assert.equal(c.state().pending,null);
 assert.equal(c.state().lastError,"STALE_DEVICE");
 assert.equal(c.onAck({requestId:three.requestId,status:"ACCEPTED",
   deviceEpoch:s.device.epoch,graphRevision:"6",rosterRevision:"7",errorCode:"NONE"}),false);
 assert.ok(sent.filter(x=>x.name==="arMixConsoleSubscribe").length>=2);
});
test("M2 code edits approved Pages *events* only, not a second native GUI/audio engine",()=>{
 const page=read("site/preview-v2/index.html");
 const native=read("src/Main.cpp");
 const header=read("src/ApprovedConsoleNativeTransportV6.h");
 const adapter=read("web/approved-native-bridge.js");
 const bundle=read("scripts/build_approved_native_gui.py");
 for(const key of ["consumeField","consumeAction","consumeWire","consumeUndo","consumeDoubleClick"]) {
  assert.doesNotMatch(page,/__ARMIX_NATIVE_M2__/,"public Pages checkpoint stays untouched");
  assert.match(read("scripts/build_approved_native_gui.py"),new RegExp("__ARMIX_NATIVE_M2__\\?\\."+key));
  assert.match(adapter,new RegExp(key+"\\("));
 }
 const protocolSource=read("web/approved-native-contract.js");
 for(const name of ["arMixConsoleSubscribe","arMixConsoleSnapshot","arMixConsoleIntent","arMixConsoleAck"]) {
  assert.match(native,new RegExp(name));
  assert.ok(adapter.includes(name)||protocolSource.includes(name),
    "same transport operation must be sent or consumed by JS: "+name);
 }
 assert.match(header,/getActiveInputChannels/);
 assert.match(header,/getActiveOutputChannels/);
 assert.match(header,/G0_CURRENT_JUCE_DEVICE_ONLY__CHANNELID_NOT_ADMITTED/);
 assert.match(header,/UNSUPPORTED_OP/);
 assert.match(header,/productionCallbackLive", false/);
 assert.match(header,/callbackOwnsChannelIds", false/);
 assert.match(bundle,/approved-native-contract\.js/);
 assert.doesNotMatch(native,/BinaryData::consolenative_html|BinaryData::index_html/);
 assert.doesNotMatch(adapter,/AudioContext|AudioWorklet|processBlock|setChannelGainDb/);
});

test("M3 native-only Pages scope performs real bridge projection, fader intent and ACK readback",()=>{
 const vm=require("node:vm"),sent=[],listeners=new Map(),timers=[],microtasks=[];
 let nextId=100,nextEdgeId=200,paints=0,rollbacks=0;
 const scope={
   tracks:[],master:{id:1,name:"Master"},colors:["#568ead"],
   get wireEdges(){return this.edges||[];},
   syncNativeWireGains(rows){for(const r of rows){const e=this.edges?.find(e=>e.id===r.id);if(e)e.levelDb=r.levelDb;}},
   wireGraph:{validateCandidateGraph:()=>({ok:true})},
   make:(name,kind,color)=>({id:nextId++,name,kind,color,selected:false}),
   render(){paints++;},
   findTrack(){return this.tracks[0];},
   displayMeterDbfs(){},
   allocateWireId(){return nextEdgeId++;},
   installGraph(channels,main,edges){
     this.tracks=channels;this.master=main;this.edges=edges;this.render();
   },
   stopDemo(){}
 };
 const native={
   emitEvent:(name,payload)=>sent.push({name,payload}),
   addEventListener:(name,fn)=>listeners.set(name,fn)
 };
 const info={textContent:""},previewButton={disabled:false,setAttribute(){}};
 const document={
   readyState:"complete",documentElement:{dataset:{}},
   getElementById:id=>id==="info"?info:id==="preview"?previewButton:null,
   querySelector:()=>null,addEventListener(){}
 };
 const window={
   __JUCE__:{backend:native},ArSonApprovedNativeContract:protocol,
   __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},queueMicrotask(fn){microtasks.push(fn);},setTimeout(fn,ms){timers.push({fn,ms});return timers.length;}
 };
 vm.runInNewContext(read("web/approved-native-bridge.js"),{
   window,document,performance:{now:()=>0},console
 },{timeout:1500});
 assert.equal(sent[0].name,"arMixGuiReady");
 assert.equal(sent[1].name,"arMixConsoleSubscribe");
 assert.equal(window.__ARMIX_APPROVED_NATIVE_GUI__.acceptedGraphConnected,false);
 const accepted=acceptedFixture();
 // The native callback, not this browser fixture, selects ChannelId PCM.
 accepted.mixerSession={
   authority:"CHANNEL_ID",deviceEpoch:accepted.device.epoch,
   routeRevision:accepted.graph.revision,
   rosterRevision:accepted.graph.rosterRevision,
   channels:accepted.graph.channels,main:{db:0,mute:false}
 };
 listeners.get("arMixConsoleSnapshot")(accepted);
 assert.equal(window.__ARMIX_APPROVED_NATIVE_GUI__.acceptedGraphConnected,true);
 assert.equal(scope.tracks.length,1);
 assert.equal(scope.tracks[0].level,-6);
 assert.equal(scope.edges[0].sourceId,scope.tracks[0].id);
 assert.equal(previewButton.disabled,true);
 const gesture={dataset:{field:"level"},value:"-12",min:"-60",max:"10",closest:()=>null};
 assert.equal(window.__ARMIX_NATIVE_M2__.consumeField({target:gesture,type:"change"}),true);
 const command=sent.findLast(x=>x.name==="arMixConsoleIntent");
 assert.ok(command);
 assert.equal(command.payload.operation,"SET_FADER");
 assert.equal(command.payload.targetChannelId,"9007199254740993");
 assert.equal(command.payload.payload.valueDb,-12);
 assert.equal(scope.tracks[0].level,-6,"GUI cannot optimistically commit before readback");
 listeners.get("arMixConsoleAck")({
   requestId:command.payload.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:accepted.device.epoch,graphRevision:"5",rosterRevision:"7"
 });
 assert.equal(scope.tracks[0].level,-6,"ACK alone cannot repaint");
 const updated=clone(accepted);updated.graph.channels[0].controls.faderDb=-12;
 listeners.get("arMixConsoleSnapshot")(updated);
 assert.equal(scope.tracks[0].level,-12);
 const before=paints;
 gesture.value="-24";
 window.__ARMIX_NATIVE_M2__.consumeField({target:gesture,type:"change"});
 const rejected=sent.findLast(x=>x.name==="arMixConsoleIntent").payload;
 listeners.get("arMixConsoleAck")({
   requestId:rejected.requestId,status:"REJECTED",errorCode:"UNSUPPORTED_OP",
   deviceEpoch:accepted.device.epoch,graphRevision:"5",rosterRevision:"7"
 });
 rollbacks=paints-before;
 assert.ok(rollbacks>0,"REJECTED restores the original GUI model");
 assert.equal(scope.tracks[0].level,-12);
 assert.equal(window.__ARMIX_NATIVE_M2__.state().pending,null);
 // Selected hardware WIRE gesture must bind to the *real* decimal-string
 // EdgeId and publish only after native graph ACK + a later PCM readback.
 const sourceUi=scope.tracks[0].id,edgeUi=scope.edges[0].id;
 assert.equal(window.__ARMIX_NATIVE_M2__.consumeWire([
   {type:"REMOVE",sourceId:sourceUi,edgeId:edgeUi}
 ]),true);
 const wire=sent.findLast(x=>x.name==="arMixConsoleIntent").payload;
 assert.equal(wire.operation,"APPLY_WIRE_TRANSACTION");
 assert.equal(wire.targetChannelId,"9007199254740993");
 assert.equal(wire.payload.changes[0].edgeId,"9223372036854775808");
 assert.equal(scope.edges.length,1,"pending WIRE cannot change drawn routing");
 listeners.get("arMixConsoleAck")({
   requestId:wire.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:accepted.device.epoch,graphRevision:"6",rosterRevision:"7"
 });
 assert.equal(scope.edges.length,1,"ACK is not the graph publication");
 const unwired=clone(updated);unwired.graph.revision="6";unwired.graph.edges=[];
 listeners.get("arMixConsoleSnapshot")(unwired);
 assert.equal(scope.edges.length,0,"accepted snapshot replaces reference wire");
 // A receiver is an ordinary INPUT channel with no physical source. Its
 // native ChannelId, never its visual ordinal, must receive the WIRE intent.
 const receiver=clone(unwired.graph.channels[0]);
 receiver.id="9007199254740995";receiver.name="CH04";
 delete receiver.primaryOutId;
 const ready=clone(unwired);ready.graph.revision="7";
 delete ready.graph.channels[0].primaryOutId;
 ready.graph.channels.splice(1,0,receiver);
 ready.graph.destinations.push({id:receiver.id,role:"INPUT"});
 listeners.get("arMixConsoleSnapshot")(ready);
 const receiverUi=scope.tracks.find(t=>t.name==="CH04").id;
 assert.equal(window.__ARMIX_NATIVE_M2__.consumeWire([
   {type:"CONNECT",sourceId:sourceUi,destId:receiverUi,mode:"send"}
 ]),true);
 let send=sent.findLast(x=>x.name==="arMixConsoleIntent").payload;
 assert.equal(send.payload.changes[0].to,receiver.id);
 assert.equal(scope.edges.length,0,"INPUT WIRE stays pending until native readback");
 const originalSend=send;
 const flushMicro=()=>{while(microtasks.length)microtasks.shift()();};
 listeners.get("arMixConsoleAck")({requestId:send.requestId,status:"REJECTED",
   errorCode:"STALE_NATIVE_CONTROL_REVISION",deviceEpoch:ready.device.epoch,
   graphRevision:"7",rosterRevision:"8"});
 listeners.get("arMixConsoleSnapshot")(ready);flushMicro();
 assert.equal(sent.findLast(x=>x.name==="arMixConsoleIntent").payload.requestId,originalSend.requestId,
   "unchanged snapshot cannot authorize replay");
 const fresh=clone(ready);fresh.graph.rosterRevision="8";
 listeners.get("arMixConsoleSnapshot")(fresh);flushMicro();
 send=sent.findLast(x=>x.name==="arMixConsoleIntent").payload;
 assert.notEqual(send.requestId,originalSend.requestId,"new ledger ID for one verified rebase");
 assert.equal(send.baseRosterRevision,"8");
 assert.deepEqual(JSON.parse(JSON.stringify(send.payload)),JSON.parse(JSON.stringify(originalSend.payload)));

 listeners.get("arMixConsoleAck")({
   requestId:send.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:ready.device.epoch,graphRevision:"8",rosterRevision:"8"
 });
 assert.equal(scope.edges.length,0,"INPUT WIRE ACK alone never paints a cable");
 const routed=clone(ready);routed.graph.revision="8";routed.graph.rosterRevision="8";
 routed.graph.edges=[{edgeId:"9223372036854775810",from:send.targetChannelId,
   to:receiver.id,tap:"POST_FADER",gainLinear:1,enabled:true,
   primaryOutput:false,signalKind:"AUDIO"}];
 listeners.get("arMixConsoleSnapshot")(routed);
 assert.equal(scope.edges.length,1);
 assert.equal(scope.edges[0].destId,receiverUi);
 assert.equal(window.__ARMIX_NATIVE_M2__.state().pending,null);
 // Continuous send gain coalesces the latest gesture while the native ACK
 // is outstanding. Gain-only accepted readback must preserve strip DOM.
 routed.graph.capabilities.supportedOps.push("SET_WIRE_GAIN");
 listeners.get("arMixConsoleSnapshot")(routed);
 const gainUi=scope.edges[0].id,paintBeforeGain=paints;
 const pump=()=>{const ready=timers.splice(0);for(const t of ready)if(t.ms===32)t.fn();};
 window.__ARMIX_NATIVE_M2__.consumeWire([{type:"GAIN",sourceId:sourceUi,edgeId:gainUi,levelDb:-6}]);
 pump();
 const firstGain=sent.findLast(x=>x.name==="arMixConsoleIntent").payload;
 assert.equal(firstGain.operation,"SET_WIRE_GAIN");
 assert.equal(firstGain.payload.edgeId,"9223372036854775810");
 assert.equal(scope.edges[0].levelDb,0,"no optimistic send gain acceptance");
 window.__ARMIX_NATIVE_M2__.consumeWire([{type:"GAIN",sourceId:sourceUi,edgeId:gainUi,levelDb:-9}]);
 window.__ARMIX_NATIVE_M2__.consumeWire([{type:"GAIN",sourceId:sourceUi,edgeId:gainUi,levelDb:-12}]);
 pump();
 assert.equal(sent.findLast(x=>x.name==="arMixConsoleIntent").payload.requestId,firstGain.requestId);
 const ackGain=request=>listeners.get("arMixConsoleAck")({requestId:request.requestId,status:"ACCEPTED",
   errorCode:"NONE",deviceEpoch:routed.device.epoch,graphRevision:"8",rosterRevision:"8"});
 ackGain(firstGain);
 assert.equal(scope.edges[0].levelDb,0,"ACK alone is not PCM gain readback");
 const gained=clone(routed);gained.graph.edges[0].gainLinear=firstGain.payload.gainLinear;
 listeners.get("arMixConsoleSnapshot")(gained);
 assert.equal(scope.edges[0].levelDb,-6);
 assert.equal(paints,paintBeforeGain,"scalar WIRE gain cannot rebuild mounted strips");
 pump();
 const latestGain=sent.findLast(x=>x.name==="arMixConsoleIntent").payload;
 assert.notEqual(latestGain.requestId,firstGain.requestId);
 assert.ok(Math.abs(latestGain.payload.gainLinear-Math.pow(10,-12/20))<1e-9);
 ackGain(latestGain);gained.graph.edges[0].gainLinear=latestGain.payload.gainLinear;
 listeners.get("arMixConsoleSnapshot")(gained);
 assert.equal(scope.edges[0].levelDb,-12);
 assert.equal(paints,paintBeforeGain);
 // OUT uses the same actual INPUT destination as WIRE, not a name-only
 // GROUP lookup. An exclusive route promotes the existing native edge.
 const hooks=window.__ARMIX_NATIVE_M2__,sourceTrack=scope.tracks[0];
 assert.ok(hooks.outputChoices(sourceTrack).includes("CH04"));
 const output={dataset:{field:"output"},value:"CH04",closest:()=>null};
 hooks.consumeField({target:output,type:"change"});
 const outRequest=sent.findLast(x=>x.name==="arMixConsoleIntent").payload;
 assert.equal(outRequest.operation,"APPLY_WIRE_TRANSACTION");
 assert.equal(outRequest.payload.changes[0].op,"ROUTE_ONLY");
 assert.equal(outRequest.payload.changes[0].to,receiver.id);
 listeners.get("arMixConsoleAck")({requestId:outRequest.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:gained.device.epoch,graphRevision:"9",rosterRevision:"9"});
 const primary=clone(gained);primary.graph.revision="9";primary.graph.rosterRevision="9";
 primary.graph.channels[0].primaryOutId=receiver.id;
 primary.graph.edges[0].primaryOutput=true;primary.graph.edges[0].gainLinear=1;
 listeners.get("arMixConsoleSnapshot")(primary);
 assert.equal(scope.tracks[0].output,"CH04");
 const duplicate=clone(primary),other=clone(receiver);
 other.id="9007199254740997";duplicate.graph.channels.splice(2,0,other);
 const reserved=clone(receiver);reserved.id="9007199254740999";reserved.name="NONE";
 duplicate.graph.channels.splice(3,0,reserved);
 duplicate.graph.revision="10";duplicate.graph.rosterRevision="10";
 listeners.get("arMixConsoleSnapshot")(duplicate);
 const choices=hooks.outputChoices(scope.tracks[0]);
 assert.ok(choices.includes("CH04 ["+receiver.id+"]"));
 assert.ok(choices.includes("CH04 ["+other.id+"]"));
 assert.ok(!choices.includes("CH04"),"duplicate names cannot address the wrong native channel");
 assert.ok(!choices.includes("NONE"),"a channel name cannot alias SOURCE ONLY");
 assert.ok(choices.includes("NONE ["+reserved.id+"]"));
 output.value="CH04";hooks.consumeField({target:output,type:"change"});
 assert.equal(sent.findLast(x=>x.name==="arMixConsoleIntent").payload.requestId,outRequest.requestId);
 output.value="CH04 ["+other.id+"]";hooks.consumeField({target:output,type:"change"});
 assert.equal(sent.findLast(x=>x.name==="arMixConsoleIntent").payload.payload.changes[0].to,other.id);
});

test("P0 native FX rack and per-slot readback update mounted strip without replacing fader DOM",()=>{
 const vm=require("node:vm"),listeners=new Map(),sent=[];
 let paints=0,serial=100,wire=300,strip=null;
 const scope={
  tracks:[],master:{id:1,name:"Master"},colors:["#456789"],
  wireGraph:{validateCandidateGraph:()=>({ok:true})},
  make:(name,kind,color)=>({id:serial++,name,kind,color,selected:false}),
  render(){paints++;},
  installGraph(tracks,master,edges){
   this.tracks=tracks;this.master=master;this.edges=edges;this.render();
  },
  allocateWireId:()=>wire++,displayMeterDbfs(){},stopDemo(){}
 };
 const native={emitEvent:(name,payload)=>sent.push({name,payload}),
   addEventListener:(name,fn)=>listeners.set(name,fn)};
 const info={textContent:""},preview={disabled:false},toast={hidden:true};
 const doc={
  readyState:"complete",documentElement:{dataset:{}},
  getElementById:id=>id==="info"?info:id==="preview"?preview:
   id==="armixNativeToast"?toast:null,
  querySelector:selector=>selector.startsWith('.strip[data-id=')?strip:null,
  addEventListener(){}
 };
 const win={__JUCE__:{backend:native},ArSonApprovedNativeContract:protocol,
  __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},
  setTimeout(){return 0},clearTimeout(){},queueMicrotask(fn){fn();}};
 vm.runInNewContext(read("web/approved-native-bridge.js"),
  {window:win,document:doc,performance:{now:()=>0},console},{timeout:1500});
 const base=acceptedFixture();
 base.graph.channels[0].fxRackBypassed=false;
 base.graph.channels[0].inserts=[
  {id:"vst-live-1",catalogId:"eq-a",name:"EQ",bypassed:false},
  {id:"vst-live-2",catalogId:"comp-b",name:"Compressor",bypassed:true}
 ];
 base.mixerSession={authority:"CHANNEL_ID",deviceEpoch:base.device.epoch,
  routeRevision:base.graph.revision,rosterRevision:base.graph.rosterRevision,
  channels:base.graph.channels,main:{db:0,mute:false}};
 listeners.get("arMixConsoleSnapshot")(base);
 assert.equal(win.__ARMIX_APPROVED_NATIVE_GUI__.acceptedGraphConnected,true);
 assert.equal(paints,1);
 const track=scope.tracks[0];
 const button=()=>({
  attributes:new Map(),
  setAttribute(key,val){this.attributes.set(key,String(val));},
  getAttribute(key){return this.attributes.get(key)||null;}
 });
 const style=()=>({values:new Set(),toggle(name,enabled){
  if(enabled)this.values.add(name);else this.values.delete(name);
 },contains(name){return this.values.has(name);}});
 const header=button(),rows=base.graph.channels[0].inserts.map((entry,i)=>{
  const power=button();
  return {dataset:{slot:String(i),insertId:entry.id},classList:style(),
   querySelector:selector=>selector==='[data-action="slotPower"]'?power:null,
   power};
 });
 const rack={dataset:{},classList:style(),querySelector(selector){
   return selector==='[data-action="fxOn"]'?header:null;
  },querySelectorAll:selector=>selector==='.fxslot-row:not(.is-empty)'?rows:[]};
 const stableStrip={querySelector:selector=>
  selector===".fx-section"?rack:null};
 strip=stableStrip;
 const snapshot=clone(base);
 snapshot.graph.channels[0].fxRackBypassed=true;
 listeners.get("arMixConsoleSnapshot")(snapshot);
 assert.equal(paints,1,"rack bypass must NOT replace the entire GUI channel DOM");
 assert.equal(strip,stableStrip);
 assert.equal(track.fxOn,false);
 assert.deepEqual(Array.from(track.slotBypass.slice(0,2)),[false,true],
  "header bypass must never overwrite independent insert bypass preferences");
 assert.equal(rack.dataset.fxChainEnabled,"false");
 assert.equal(header.getAttribute("aria-pressed"),"false");
 assert.deepEqual(rows.map(r=>r.power.getAttribute("aria-pressed")),["false","false"]);
 assert.deepEqual(rows.map(r=>r.dataset.fxSlotEnabled),["true","false"]);
 assert.deepEqual(rows.map(r=>r.dataset.fxEffective),["false","false"]);
 assert.ok(rows.every(r=>r.classList.contains("is-bypassed")));
 const restored=clone(snapshot);
 restored.graph.channels[0].fxRackBypassed=false;
 listeners.get("arMixConsoleSnapshot")(restored);
 assert.equal(paints,1,"re-enabling the FX rack preserves mounted controls");
 assert.equal(track.fxOn,true);
 assert.deepEqual(rows.map(r=>r.power.getAttribute("aria-pressed")),["true","false"]);
 assert.deepEqual(rows.map(r=>r.dataset.fxEffective),["true","false"]);
 const individual=clone(restored);
 individual.graph.channels[0].inserts[0].bypassed=true;
 listeners.get("arMixConsoleSnapshot")(individual);
 assert.equal(paints,1,"slot power is a realtime scalar, not a topology edit");
 assert.deepEqual(Array.from(track.slotBypass.slice(0,2)),[true,true]);
 assert.deepEqual(rows.map(r=>r.power.getAttribute("aria-pressed")),["false","false"]);
 const replaced=clone(individual);
 replaced.graph.channels[0].inserts[0].id="new-vst-instance";
 listeners.get("arMixConsoleSnapshot")(replaced);
 assert.equal(paints,2,"an actual VST instance identity change MUST re-project");
});

test("P0 automatic cold-start Show replaces only pristine SOURCE NONE ChannelId",()=>{
 const main=read('src/Main.cpp');
 const engine=read('src/LiveAudioEngine.h');
 const route=read('src/LiveDryRoutingLab.h');
 assert.match(main,/nativeAlreadyActive && !retained &&/);
 assert.match(main,/\(startupOnSavedDevice \|\| recoveringAfterDeviceApply \|\| firstBinding\) &&/);
 assert.match(main,/recoveringAfterDeviceApply=bindingMode!=MixerBindingMode::exact &&\s*mixerSessionRecoveryPending_/);
 assert.match(main,/!usedPrevious && !retained/); // only normal auto-resume, not recovery admission
 assert.match(main,/engine_\.hasPristineStartupMixer\(\)/);
 assert.match(main,/restoreNativeMixerSession\(std::move\(doc\),states,\s*replacePristineStartup/);
 assert.match(engine,/dryRoutingLab_\.canReplacePristineStartupMixer\(\)/);
 assert.match(route,/bool canReplacePristineStartupMixer\(\) const noexcept/);
 assert.match(route,/!nativeControlDirty_ && nativeAccepted_\.fxOwners/);
});

test("P0 startup banner never demands legacy Patch CH1 / Arm wizard",()=>{
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/SELECT CHANNEL IN · NATIVE MONITOR AFTER ACK/);
 assert.match(bridge,/next.admitted\?"CHOOSE AN INPUT"/);
 assert.doesNotMatch(bridge,/FAT PENDING/);
 assert.doesNotMatch(bridge,/hardware\.device\.enabledInputs\.length>0/);
 assert.match(bridge,/NO VERIFIED INPUT ROUTE · OPEN AUDIO I\/O/);
 assert.doesNotMatch(bridge,/NO AUDIBLE OUTPUT · PATCH INPUT \+ ARM/);
 assert.match(bridge,/const preparedFirstSound=session\?\.authority==="NONE"/);
 assert.match(bridge,/const deviceReady=\(\)=>/);
});
test("P0 native built-in FX appear before vendor scan and remain after catalog refresh",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const builder=read("scripts/build_approved_native_gui.py");
 for(const id of ["armix.builtin.channel-eq.v1",
                  "armix.builtin.channel-compressor.v1",
                  "armix.builtin.arson-harmony.v1"])
   assert.ok(bridge.includes('id:"'+id+'"'));
 assert.ok(bridge.includes('[...nativeBuiltins.filter(fx=>fx.showInPicker!==false),...nativeCatalog.values()]'));
 assert.ok(bridge.includes('presentFxCatalog(); // compiled native FX'));
 assert.ok(builder.includes("x.format==='Native'?'Native':'VST3'"));
 assert.ok(!bridge.includes('scope?.setNativeCatalog?.([])'));
});
test("P0 built-in EQ/Compressor/Harmony load independent of slow external VST3 catalog scan",()=>{
 const cpp=read("src/Main.cpp");
 const gate=cpp.slice(cpp.indexOf('const auto* builtIn=armix::builtin_fx::findNativeBuiltInFx(catalogId)'),
   cpp.indexOf('const auto result=builtIn?'));
 assert.ok(gate.includes('const bool externalCatalog=builtIn==nullptr'));
 assert.ok(gate.includes('externalCatalog&&(!plugin||catalog_.busy())'));
 assert.ok(!gate.includes('||catalog_.busy()||nativeShowBusy_'),
  'third-party metadata discovery must not disable compiled-in FX');
 assert.ok(gate.includes('nativeShowBusy_')&&gate.includes('studioBackupBusy_'),
  'native structural session/backup safety interlocks remain active');
});
test("P0 output-only Audio I/O is a real native configuration, never blocked by missing capture",()=>{
 const cpp=read("src/Main.cpp"),bridge=read("web/approved-native-bridge.js");
 const apply=read("src/NativeDeviceApplyCoordinator.h");
 assert.match(cpp,/const bool outputOnly=separate&&capture\.isEmpty\(\)/);
 assert.match(cpp,/candidate\.useDefaultInputChannels=!outputOnly/);
 assert.match(cpp,/if\(outputOnly\)candidate\.inputChannels\.clear\(\)/);
 assert.match(cpp,/NativeDeviceApplyCoordinator::parsePinMasks/);
 assert.match(apply,/\(outputOnly&&!in\.isZero\(\)\)/);
 assert.match(apply,/out\.countNumberOfSetBits\(\)<2/);
 assert.doesNotMatch(cpp,/opened->getActiveInputChannels\(\)\.countNumberOfSetBits\(\)<1/);
 assert.match(bridge,/No hardware input \(Player \/ FX\)/);
 assert.match(bridge,/t\?\.separate\?\["",\.\.\.\(t\.inputs\|\|\[\]\)\]/);
 assert.doesNotMatch(bridge,/ack\.active\.enabledInputs\.length>0/);
 assert.match(bridge,/FX\/WIRE available/);
});
test("P0 FIRST SOUND G0 offers verified input and leaves automatic MAIN to native",()=>{
 const vm=require("node:vm"),sent=[],listeners=new Map(),renderedChoices=[];
 let win;
 const tracks=Array.from({length:10},(_,i)=>({
  id:i+1,kind:"audio",name:"CH "+(i+1),input:"None",
  output:"Master",format:"mono",fx:[],slotBypass:[],fxInsertIds:[]
 }));
 const scope={
  tracks,master:{id:50,kind:"master",output:"Master"},
  findTrack:el=>el?.track||null,
  render(){renderedChoices.push([...win.__ARMIX_NATIVE_M2__.inputChoices(tracks[0])]);},
  stopDemo(){}
 };
 const backend={
  emitEvent:(name,payload)=>sent.push({name,payload}),
  addEventListener:(name,fn)=>listeners.set(name,fn)
 };
 const info={textContent:""},preview={disabled:false};
 const document={
  readyState:"complete",documentElement:{dataset:{}},activeElement:null,
  querySelector:()=>null,
  getElementById:id=>id==="info"?info:id==="preview"?preview:null,
  addEventListener(){}
 };
 win={
  __JUCE__:{backend},ArSonApprovedNativeContract:protocol,
  __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},
  setTimeout(){return 0},clearTimeout(){},queueMicrotask(fn){fn();}
 };
 vm.runInNewContext(read("web/approved-native-bridge.js"),{
  window:win,document,performance:{now:()=>0},console
 },{timeout:1500});
 const boot=clone(fixtures.g0);
 boot.device.backend="Windows Audio";
 boot.device.captureDevice="Real hardware microphone";
 boot.device.playbackDevice="Speakers";
 boot.legacyM4={active:true,routeAccepted:false,routingRevision:"12"};
 boot.mixerSession={authority:"NONE",deviceEpoch:boot.device.epoch,
  routeRevision:"0",rosterRevision:"0",channels:[],main:{outputArmed:false}};
 listeners.get("arMixConsoleSnapshot")(boot);
 assert.equal(win.__ARMIX_APPROVED_NATIVE_GUI__.acceptedGraphConnected,false,
  "unpatched G0 must not masquerade as ChannelId PCM");
 assert.ok(renderedChoices.length>0,
  "first real JUCE pin readback must refresh the previously empty selector");
 assert.deepEqual(renderedChoices.at(-1),["None","Mono · IN 1 [IN 1]"],
  "the only offered input is the native enabled physical pin");
 const choose=(label)=>win.__ARMIX_NATIVE_M2__.consumeField({
  type:"change",target:{dataset:{field:"input"},value:label,track:tracks[4]}
 });
 assert.equal(choose("Mono · IN 1 [IN 1]"),true);
 const source=sent.filter(x=>x.name==="arMixAction"&&
  x.payload.action==="routingSourceSwitch");
 assert.equal(source.length,1,
  "G0 needs exactly one direct JUCE source command, no Start/Patch/Arm wizard");
 assert.deepEqual([
  source[0].payload.channel,source[0].payload.mode,
  source[0].payload.left,source[0].payload.right,
  source[0].payload.deviceEpoch,source[0].payload.revision
 ],[4,"MONO",0,-1,69,12]);
 assert.equal(sent.filter(x=>x.name==="arMixAction"&&
  x.payload.action==="monitor").length,0,
  "MAIN cannot auto-arm before accepted native source topology");
 // Native control-thread producer ACK, not a fake browser state.
 const acceptedRows=Array.from({length:10},(_,i)=>({
  mode:i===4?"MONO":"NONE",left:i===4?0:-1,
  right:-1,main:true
 }));
 listeners.get("arMixSourceSwitchAck")({
  requestId:source[0].payload.requestId,ok:true,
  state:{ready:true,epoch:69,revision:13,hasAcceptedRoute:true,
   nativeChannelIdPcmActive:false,
   channelControls:Array.from({length:10},()=>({db:0,pan:0,mute:false,solo:false})),
   acceptedRoute:{channels:acceptedRows}}
 });
 const monitors=sent.filter(x=>x.name==="arMixAction"&&
  x.payload.action==="monitor");
 assert.equal(monitors.length,0,
  "native owns automatic MAIN; the bridge never emits a second arm request");
 assert.equal(tracks[4].input,"None",
  "GUI must wait for a subsequent independent native readback");
 const none=clone(boot);
 none.device.enabledInputs=[];none.device.inputEndpoints=[];
 listeners.get("arMixConsoleSnapshot")(none);
 assert.deepEqual([...win.__ARMIX_NATIVE_M2__.inputChoices(tracks[0])],["None"]);
 const commandsBefore=sent.filter(x=>x.name==="arMixAction"&&
  x.payload.action==="routingSourceSwitch").length;
 assert.equal(choose("Mono · IN 1 [IN 1]"),true);
 assert.equal(sent.filter(x=>x.name==="arMixAction"&&
  x.payload.action==="routingSourceSwitch").length,commandsBefore,
  "never invent physical capture when the driver has no enabled input pins");
});
test("P0 native hardware IN remains selectable after ChannelId ownership, failed ACK drains the other channel",()=>{
 const vm=require("node:vm"),listeners=new Map(),sent=[],microtasks=[];
 let ordinal=100,wireId=400;
 const scope={
  tracks:[],master:{id:1,name:"Master"},colors:["#456789"],
  wireGraph:{validateCandidateGraph:()=>({ok:true})},
  make:(name,kind,color)=>({id:ordinal++,name,kind,color,selected:false}),
  render(){},
  installGraph(tracks,main,edges){this.tracks=tracks;this.master=main;this.edges=edges;},
  allocateWireId:()=>wireId++,displayMeterDbfs(){},stopDemo(){},
  findTrack(el){return el.track;}
 };
 const backend={emitEvent:(name,payload)=>sent.push({name,payload}),
  addEventListener:(name,fn)=>listeners.set(name,fn)};
 const toast={textContent:"",hidden:true},info={textContent:""};
 const doc={
  readyState:"complete",documentElement:{dataset:{}},
  getElementById:id=>id==="info"?info:id==="armixNativeToast"?toast:
    id==="preview"?{disabled:true}:null,
  querySelector:()=>null,addEventListener(){}
 };
 const win={__JUCE__:{backend},ArSonApprovedNativeContract:protocol,
  __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},
  setTimeout(){return 0},clearTimeout(){},
  queueMicrotask(fn){microtasks.push(fn);}};
 vm.runInNewContext(read("web/approved-native-bridge.js"),
  {window:win,document:doc,performance:{now:()=>0},console},{timeout:1500});
 const base=acceptedFixture();
 const first=base.graph.channels[0],main=base.graph.mainChannelId;
 const second=clone(first);second.id="9007199254740994";
 second.name="Harmony";second.primaryOutId=main;
 base.graph.channels.splice(1,0,second);
 base.graph.edges.push({...base.graph.edges[0],
  edgeId:"9223372036854775809",from:second.id});
 base.device.enabledInputs=[0,1];
 base.device.inputEndpoints=[
  {id:"in:0",port:0,callback:0,label:"Mic A",active:true},
  {id:"in:1",port:1,callback:1,label:"Mic B",active:true}
 ];
 // Deliberately stale M4 renderer bit. The accepted ChannelId callback is
 // the authority for new physical source changes after BUS/WIRE cutover.
 base.legacyM4={active:false,routeAccepted:false,routingRevision:"9"};
 base.mixerSession={authority:"CHANNEL_ID",deviceEpoch:base.device.epoch,
  routeRevision:base.graph.revision,rosterRevision:base.graph.rosterRevision,
  channels:base.graph.channels,main:{db:0,mute:false}};
 listeners.get("arMixConsoleSnapshot")(base);
 assert.equal(win.__ARMIX_APPROVED_NATIVE_GUI__.acceptedGraphConnected,true);
 assert.equal(scope.tracks.length,2);
 const choose=(track,label)=>win.__ARMIX_NATIVE_M2__.consumeField({
  target:{track,dataset:{field:"input"},value:label},type:"change"});
 assert.equal(choose(scope.tracks[0],"Mono · Mic A [IN 1]"),true);
 assert.equal(choose(scope.tracks[1],"Mono · Mic B [IN 2]"),true);
 const sourceCommands=()=>sent.filter(x=>x.name==="arMixAction"&&
   x.payload.action==="routingNativeSourcePatch");
 assert.equal(sourceCommands().length,1,"first source command sent to real JUCE gateway");
 assert.equal(sourceCommands()[0].payload.channelId,first.id);
 assert.equal(sourceCommands()[0].payload.left,0);
 const id=sourceCommands()[0].payload.requestId;
 listeners.get("arMixNativeSourceAck")({requestId:id,ok:false,error:"STALE_NATIVE_SOURCE"});
 // Rejected source did not mutate graph; later authoritative readback is
 // byte-identical. The queued SECOND channel must not be stranded.
 listeners.get("arMixConsoleSnapshot")(base);
 assert.ok(microtasks.length>0,"unchanged native graph must still drain queued source");
 while(microtasks.length)microtasks.shift()();
 assert.equal(sourceCommands().length,2);
 assert.equal(sourceCommands()[1].payload.channelId,second.id);
 assert.equal(sourceCommands()[1].payload.left,1);
 assert.equal(sourceCommands()[1].payload.graphRevision,base.graph.revision);
 assert.notEqual(sourceCommands()[1].payload.requestId,id);
});
test("M3 Windows bundle owns pointerup fader gesture and fences native FX keyboard reorder",()=>{
 const builder=read("scripts/build_approved_native_gui.py");
 const bridge=read("web/approved-native-bridge.js");
 assert.ok(builder.includes("const g=faderGesture;faderGesture=null;"));
 assert.ok(builder.includes("consumeField?.({target:g.input,type:'change'})"));
 assert.ok(builder.includes("function fxNativeTransactionRequired"));
 assert.ok(builder.includes("__ARMIX_NATIVE_SCOPE__"));
 assert.ok(bridge.includes("scope.installGraph(nextTracks,nextMaster,edges)"));
 assert.ok(bridge.includes("scope?.findTrack?.(el)"));
 assert.ok(!bridge.includes('window.addEventListener("pointerup"'));
});

test("native Windows operator can explicitly patch first physical input, arm and mute",()=>{
 const js=read("web/approved-native-bridge.js");
 const main=read("src/Main.cpp");
 const engine=read("src/LiveAudioEngine.h");
 assert.match(js,/AUDIO \/ SAFETY/);
 assert.match(js,/function openNativeAudioIO\(\)/);
 assert.match(js,/action:"audioDeviceInventory"/);
 assert.match(js,/action:"audioDeviceApply"/);
 assert.match(js,/d\.inputEndpoints\.filter\(p=>/);
 assert.match(js,/d\.enabledInputs\.includes\(p\.port\)/);
 assert.match(js,/action:"routingSourceSwitch",requestId:pendingSourceId/);
 assert.match(js,/channel:0,mode:"MONO",left:pin,right:-1/);
 assert.match(js,/action:"monitor",value:true/);
 assert.match(js,/action:"monitor",value:false/);
 assert.match(js,/expectedEpoch:epoch,expectedRevision:revision/);
 assert.match(js,/arMixSourceSwitchAck/);
 assert.match(js,/arMixAudioArmAck/);
 assert.match(js,/if\(ack\.ok===true\)/);
 assert.match(js,/NATIVE PATCH REJECTED:/);
 assert.match(js,/NO VERIFIED AUDIO DEVICE/);
 assert.doesNotMatch(js,/FAT PENDING/);
 assert.match(js,/CHECK AUDIO CONNECTION/);
 assert.match(main,/action=="routingSourceSwitch"/);
 assert.match(main,/engine_\.liveOutputArmFault\(\)/);
 assert.match(main,/safe->emitApprovedNativeSnapshot\(\)/);
 assert.match(engine,/juce::String switchDryRoutingLabSource\(/);
 assert.match(engine,/setOutputMuted\(true\)/);
});

// Held-pointer native fader regression runs within existing Windows / Chromium test gates.
require("./live_fader_held_drag.test.cjs");

test("live WIRE gain requires exact same-revision sample-target readback and unchanged edge identity",()=>{
 const base=acceptedFixture();delete base.graph.channels[0].primaryOutId;
 base.graph.edges[0].primaryOutput=false;base.graph.edges[0].tap="POST_FADER";
 base.graph.capabilities.supportedOps.push("SET_WIRE_GAIN");
 const source=base.graph.channels[0].id,edge=base.graph.edges[0].edgeId;
 const create=()=>{const sent=[];const c=protocol.createController({emit:(name,payload)=>sent.push({name,payload})});
   c.onSnapshot(clone(base));return {c,sent};};
 const accept=(c,r)=>c.onAck({requestId:r.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:base.device.epoch,graphRevision:base.graph.revision,rosterRevision:base.graph.rosterRevision});
 for(const corrupt of [s=>{s.graph.edges[0].gainLinear=.5;},s=>{s.graph.edges[0].tap="PRE_FADER";},
   s=>{s.graph.edges[0].primaryOutput=true;}]){
   const {c}=create();const r=c.intent("SET_WIRE_GAIN",source,{edgeId:edge,gainLinear:.25});
   assert.ok(r.ok);accept(c,r);
   const bad=clone(base);bad.graph.edges[0].gainLinear=.25;corrupt(bad);
   assert.equal(c.onSnapshot(bad),false);
   assert.equal(c.state().lastError,"ACK_READBACK_EFFECT_MISMATCH");
   assert.equal(c.state().current.graph.edges[0].gainLinear,1);
 }
 const {c}=create();const r=c.intent("SET_WIRE_GAIN",source,{edgeId:edge,gainLinear:.25});
 accept(c,r);assert.equal(c.state().current.graph.edges[0].gainLinear,1);
 const good=clone(base);good.graph.edges[0].gainLinear=.25;
 assert.ok(c.onSnapshot(good));assert.equal(c.state().pending,null);
 assert.equal(c.state().current.graph.revision,base.graph.revision);
 assert.equal(c.state().current.graph.edges[0].gainLinear,.25);
 assert.equal(c.intent("SET_WIRE_GAIN",source,{edgeId:"99999",gainLinear:.5}).ok,false);
});

test("native FX remove/reorder/transfer requires exact token, ACK and ordered readback",()=>{
 const setup=()=>{
  const base=acceptedFixture(),source=base.graph.channels.find(c=>c.role==='INPUT');
  source.inserts=[{id:'fx-a',catalogId:'a',name:'A',bypassed:false},{id:'fx-b',catalogId:'b',name:'B',bypassed:true}];
  const dest=clone(source);dest.id='9007199254740997';dest.inserts=[];
  base.graph.channels.splice(1,0,dest);base.graph.capabilities.supportedOps.push('MOVE_INSERT','REMOVE_INSERT');
  const emitted=[],controller=protocol.createController({emit:(n,p)=>emitted.push({n,p})});
  assert.ok(controller.onSnapshot(base));return {base,source,dest,controller,emitted};
 };
 const apply=(ctx,op,payload,change)=>{
  const {base,source,controller:c,emitted}=ctx;
  assert.ok(c.intent(op,source.id,payload).ok);const request=emitted.findLast(e=>e.n==='arMixConsoleIntent').p;
  c.onAck({requestId:request.requestId,status:'ACCEPTED',errorCode:'NONE',deviceEpoch:base.device.epoch,
    graphRevision:'6',rosterRevision:'8'});
  assert.equal(c.state().current.graph.channels.find(x=>x.id===source.id).inserts.length,2,'ACK alone mutated FX');
  const next=clone(base);next.graph.revision='6';next.graph.rosterRevision='8';change(next);
  assert.ok(c.onSnapshot(next));assert.equal(c.state().pending,null);
 };
 let t=setup();apply(t,'MOVE_INSERT',{slot:0,instanceId:'fx-a',toChannelId:t.source.id,toSlot:1},
   s=>s.graph.channels[0].inserts.reverse());
 assert.deepEqual(t.controller.state().current.graph.channels[0].inserts.map(x=>x.id),['fx-b','fx-a']);
 t=setup();apply(t,'MOVE_INSERT',{slot:0,instanceId:'fx-a',toChannelId:t.dest.id,toSlot:0},s=>{
   const moved=s.graph.channels[0].inserts.splice(0,1)[0];s.graph.channels[1].inserts.push(moved);
 });
 assert.equal(t.controller.state().current.graph.channels[1].inserts[0].id,'fx-a');
 t=setup();apply(t,'REMOVE_INSERT',{slot:0,instanceId:'fx-a'},s=>s.graph.channels[0].inserts.splice(0,1));
 assert.equal(t.controller.state().current.graph.channels[0].inserts[0].id,'fx-b');
 t=setup();assert.equal(t.controller.intent('REMOVE_INSERT',t.source.id,{slot:0,instanceId:'stale'}).ok,false);
 t=setup();const c=t.controller;
 assert.ok(c.intent('MOVE_INSERT',t.source.id,{slot:0,instanceId:'fx-a',toChannelId:t.dest.id,toSlot:0}).ok);
 const request=t.emitted.findLast(e=>e.n==='arMixConsoleIntent').p;
 c.onAck({requestId:request.requestId,status:'ACCEPTED',errorCode:'NONE',deviceEpoch:t.base.device.epoch,graphRevision:'6',rosterRevision:'8'});
 const wrong=clone(t.base);wrong.graph.revision='6';wrong.graph.rosterRevision='8';
 wrong.graph.channels[1].inserts.push(clone(wrong.graph.channels[0].inserts[0]));
 assert.equal(c.onSnapshot(wrong),false,'name/token copy cannot masquerade as MOVE');
 assert.equal(c.state().lastError,'ACK_READBACK_EFFECT_MISMATCH');
});

test("native vendor editor retention follows current PCM ownership, not inactive M4 slots",()=>{
 const main=read('src/Main.cpp'),lab=read('src/LiveDryRoutingLab.h');
 assert.match(main,/engine_\.retainsRoutedFxEditor\(routedEditorEpoch_,routedEditorOwner_\)/);
 assert.match(main,/routedEditorEpoch_=retainedEpoch/);
 assert.match(main,/retainedEpoch=ep/);
 assert.match(lab,/nativeAccepted_\.fxOwners->retainsEditor\(owner\)/);
 const editor=main.slice(main.indexOf('if(owner && owner->processor)'),main.indexOf('if(owner && owner->processor)')+700);
 assert.ok(editor.indexOf('closePluginEditor();')<editor.indexOf('createEditorIfNeeded()'),'old window must be destroyed before asking for its editor pointer');
 const change=main.slice(main.indexOf('void changeListenerCallback'),main.indexOf('void timerCallback()',main.indexOf('void changeListenerCallback')));
 assert.doesNotMatch(change,/if\(routedEditorOwner_\)closePluginEditor/);
});
