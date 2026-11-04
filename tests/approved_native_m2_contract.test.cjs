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
  assert.match(page,new RegExp("__ARMIX_NATIVE_M2__\\?\\."+key));
  assert.match(adapter,new RegExp(key+"\\("));
 }
 for(const name of ["arMixConsoleSubscribe","arMixConsoleSnapshot","arMixConsoleIntent","arMixConsoleAck"]) {
  assert.match(native,new RegExp(name));
  assert.match(adapter,new RegExp(name));
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
