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
test("ten on-strip hardware selectors dispatch exact native channel, await ACK then readback",()=>{
 const vm=require("node:vm");
 const sent=[],listeners=new Map(),handlers=new Map();
 const selects=new Map();
 const tracks=Array.from({length:10},(_,i)=>({kind:"audio",id:i+1,name:"CH"+(i+1)}));
 const scope={tracks,master:{id:11,kind:"master"},stopDemo(){}};
 for(const tr of tracks){
   const sel={dataset:{},value:"None",disabled:false,options:[],
     replaceChildren(...opts){this.options=opts;this.value=opts[0]?.value??"";},
     closest(selector){return selector.includes('field="input"')?this:null;}};
   selects.set(String(tr.id),sel);
 }
 const info={textContent:""},preview={disabled:false,title:""};
 const doc={readyState:"complete",documentElement:{dataset:{}},
   getElementById:id=>id==="info"?info:id==="preview"?preview:null,
   querySelector:selector=>{
     const id=/^\.strip\[data-id="(\d+)"\]$/.exec(selector)?.[1];
     return id?{querySelector:q=>q.includes('field="input"')?selects.get(id):null}:null;
   },
   createElement:tag=>({value:"",textContent:"",dataset:{},tag}),
   addEventListener:(type,listener)=>handlers.set(type,listener)
 };
 const backend={emitEvent:(name,payload)=>sent.push({name,payload}),
   addEventListener:(name,fn)=>listeners.set(name,fn)};
 const window={__JUCE__:{backend},ArSonApprovedNativeContract:protocol,
   __ARMIX_NATIVE_SCOPE__:scope,setTimeout(){},addEventListener(){}};
 vm.runInNewContext(read("web/approved-native-bridge.js"),
   {window,document:doc,performance:{now:()=>0}},{timeout:3000});
 const snap=clone(fixtures.g0);
 snap.device.epoch="7";
 snap.device.enabledInputs=[0,2];
 snap.device.inputEndpoints=[
   {id:"in:0",port:0,callback:0,label:"Mic 1",active:true},
   {id:"in:2",port:2,callback:1,label:"Mic 3",active:true}
 ];
 snap.legacyM4={active:true,routeAccepted:false,routingRevision:"8",
   channelSources:tracks.map(()=>({mode:"NONE",left:-1,right:-1}))};
 listeners.get("arMixConsoleSnapshot")(snap);
 const selected=selects.get("8");
 assert.equal(selected.dataset.nativeChannel,"7");
 assert.equal(selected.disabled,false);
 assert.deepEqual(selected.options.map(o=>o.value),["NONE","MONO:0","MONO:2"]);
 selected.value="MONO:2";
 let prevented=false,stopped=false;
 handlers.get("change")({target:selected,preventDefault(){prevented=true;},
   stopImmediatePropagation(){stopped=true;}});
 assert.equal(prevented,true);assert.equal(stopped,true);
 const source=sent.findLast(x=>x.name==="arMixAction");
 assert.equal(source.payload.action,"routingSourceSwitch");
 assert.equal(source.payload.channel,7);
 assert.equal(source.payload.mode,"MONO");
 assert.equal(source.payload.left,2);
 assert.equal(source.payload.right,-1);
 assert.equal(source.payload.deviceEpoch,7);
 assert.equal(source.payload.revision,8);
 assert.equal(selected.disabled,true,"another channel edit cannot overtake ACK");
 const stale=clone(snap);
 stale.device.epoch="9";
 listeners.get("arMixConsoleSnapshot")(stale);
 assert.equal(selected.disabled,false,"device epoch change releases stale UI transaction");
 listeners.get("arMixConsoleSnapshot")(snap);
 selected.value="MONO:2";
 handlers.get("change")({target:selected,preventDefault(){},stopImmediatePropagation(){}});
 const pending=sent.findLast(x=>x.name==="arMixAction").payload;
 const rows=tracks.map(()=>({mode:"NONE",left:-1,right:-1}));
 rows[7]={mode:"MONO",left:2,right:-1};
 listeners.get("arMixSourceSwitchAck")({
   requestId:pending.requestId,channel:7,ok:true,error:"",
   state:{epoch:7,acceptedRoute:{channels:rows}}
 });
 assert.equal(selected.disabled,true,"ACK alone must not project native source");
 const readback=clone(snap);
 readback.legacyM4.routeAccepted=true;
 readback.legacyM4.channelSources=rows;
 listeners.get("arMixConsoleSnapshot")(readback);
 assert.equal(selected.value,"MONO:2");
 assert.equal(selected.disabled,false);
 assert.equal(selects.get("1").value,"NONE",
   "unselected channel must not inherit another channel's patch");
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
 const vm=require("node:vm"),sent=[],listeners=new Map();
 let nextId=100,nextEdgeId=200,paints=0,rollbacks=0;
 const scope={
   tracks:[],master:{id:1,name:"Master"},colors:["#568ead"],
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
   __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},setTimeout(){return 0;}
 };
 vm.runInNewContext(read("web/approved-native-bridge.js"),{
   window,document,performance:{now:()=>0},console
 },{timeout:1500});
 assert.equal(sent[0].name,"arMixGuiReady");
 assert.equal(sent[1].name,"arMixConsoleSubscribe");
 assert.equal(window.__ARMIX_APPROVED_NATIVE_GUI__.acceptedGraphConnected,false);
 const accepted=acceptedFixture();
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

test("approved native runtime routes ANY of ten visible DAW channel IN selectors",()=>{
 const js=read("web/approved-native-bridge.js");
 const cpp=read("src/Main.cpp");
 const native=read("integration/ApprovedNativeRuntimeSnapshot.h");
 const bundle=read("scripts/build_approved_native_gui.py");
 assert.match(js,/physicalUiIds=\(scope\?\.tracks\|\|\[\]\)/);
 assert.match(js,/\.route-select\[data-field="input"\]/);
 assert.match(js,/for\(let i=0;i<physicalUiIds\.length;i\+\+\)/);
 assert.match(js,/action:"routingSourceSwitch"/);
 assert.match(js,/channel:index,mode,left,right/);
 assert.match(js,/deviceEpoch:epoch,revision/);
 assert.match(js,/arMixSourceSwitchAck/);
 assert.match(js,/sourcePending=null/);
 assert.match(js,/route\?\.mode===p\.mode/);
 assert.match(native,/"channelSources",juce::var\(sources\)/);
 assert.match(native,/list->size\(\)==10/);
 assert.match(native,/inputs\.getArray\(\)->contains\(left\)/);
 assert.match(js,/event\.stopImmediatePropagation\(\)/);
 assert.match(js,/document\.addEventListener\("change",changeNativeInput,true\)/);
 assert.doesNotMatch(js,/Physical input for CH1|Patch CH1|pendingSourceId/);
 assert.match(js,/openNativeAudioSetup\(\)/);
 assert.match(js,/action:"audioDeviceInventory"/);
 assert.match(js,/action:"audioDeviceApply"/);
 assert.match(js,/arMixAudioDeviceApplyAck/);
 assert.match(js,/data-native-monitor/);
 assert.match(js,/action:"monitor",value:!armed/);
 assert.match(js,/action:"monitor",value:false/);
 assert.match(js,/expectedEpoch:epoch,expectedRevision:rev/);
 assert.match(js,/NO VERIFIED AUDIO DEVICE/);
 assert.match(js,/NATIVE PCM · HARDWARE FAT PENDING/);
 assert.match(js,/Virtual playback output/);
 assert.match(cpp,/action=="routingSourceSwitch"/);
 assert.match(cpp,/engine_\.liveOutputArmFault\(\)/);
 assert.match(cpp,/applyCustomAudioDevice\(args\)/);
 assert.match(bundle,/approved-native-bridge\.js/);
 assert.doesNotMatch(bundle,/NATIVE_ADAPTER = ROOT \/ "web" \/ "mixer-io\.js"/);
 assert.doesNotMatch(js,/AudioContext|AudioWorklet|processBlock/);
});
