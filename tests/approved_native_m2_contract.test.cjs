// SPDX-License-Identifier: AGPL-3.0-only
// M2 deterministic native transport: no fake ChannelIds, ACK-only success or PCM.
const test=require("node:test");
const assert=require("node:assert/strict");
const {readFileSync}=require("node:fs");
const protocol=require("../web/approved-native-contract.js");
const deviceVM=require("../web/approved-device-view-model.js");
const stripVM=require("../web/approved-channel-strip-view-model.js");
const fxRackVM=require("../web/approved-fx-rack-view-model.js");
const wireVM=require("../web/approved-wire-view-model.js");
const read=p=>readFileSync(p,"utf8");
const fixtures=JSON.parse(read("integration/console-v6/fixtures.json"));
const clone=x=>JSON.parse(JSON.stringify(x));


// A real admitted v6 snapshot needs MAIN identity and an installed graph.
// The repository's g2 fixture is intentionally a partial graph source.
function gmv20AcceptedGraph() {
 const fixture=clone(fixtures.g2);
 fixture.graph.mainChannelId="200";
 // The illustrative g2 inserts predate accepted catalog identities.
 for(const channel of fixture.graph.channels)
   for(const fx of channel.inserts||[])fx.catalogId="fixture.catalog."+fx.id;
 fixture.graph.edges=[["7","100"],["2","100"],["100","200"],
   ["101","200"]].map(([from,to],i)=>({
     edgeId:String(i+1),from,to,signalKind:"AUDIO",
     gainLinear:1,enabled:true,primaryOutput:true
   }));
 return fixture;
}

test("P0 Music Player is an explicit ChannelId INPUT source with exact deck PCM ACK/readback",()=>{
  const player={label:"Music Player",mode:"PLAYER",left:-1,right:-1,deckId:"1"};
  const options=[{label:"None",mode:"NONE",left:-1,right:-1},player];
  assert.deepEqual(stripVM.inputChoices({kind:"audio",input:"None"},
    options,false,false,"Internal BUS"),["None","Music Player"]);
  assert.equal(stripVM.sourceChoiceMatches(player,options),true);
  assert.equal(stripVM.sourceChoiceMatches({...player,deckId:"2"},options),false);
  const chosen=stripVM.sourceQueueSelection({choice:player,choices:options,
    epoch:"7",acceptedLabel:"None",hardwareConfiguration:"dev-A"});
  assert.equal(chosen.kind,"QUEUE");
  const hardware={device:{epoch:"7"},hardwareConfiguration:"dev-A",
    raw:{mixerSession:{authority:"CHANNEL_ID",deviceEpoch:"7"}}};
  const native={admitted:true,retained:false,device:{epoch:"7"},
    hardwareConfiguration:"dev-A",graph:{revision:"10",rosterRevision:"20",
      channels:[{id:"8",role:"INPUT",source:null}]}};
  const dispatched=stripVM.sourceDispatchPlan({authority:"CHANNEL_ID",
    entry:chosen.entry,hardware,native,channelId:"8",choices:options,
    deviceReady:true});
  assert.equal(dispatched.kind,"READY");
  assert.equal(dispatched.action,"routingNativeSourcePatch");
  assert.deepEqual({mode:dispatched.payload.mode,deckId:dispatched.payload.deckId},
    {mode:"PLAYER",deckId:"1"});
  assert.equal(stripVM.sourceDispatchPlan({authority:"M4",entry:chosen.entry,
    hardware,native,channel:7,choices:options,deviceReady:true}).kind,
    "UNAVAILABLE","legacy M4 must never receive an invented physical pin");
  const now={...native,graph:{...native.graph,revision:"11",
    channels:[{id:"8",role:"INPUT",
      source:{sourceFormat:"PLAYER",playerDeckId:"1"}}]},
    raw:{mixerSession:{authority:"CHANNEL_ID",deviceEpoch:"7"}}};
  const pending={id:"8",epoch:"7",requestId:"source-native-1",
    choice:chosen.entry.choice,beforeRevision:"10",acked:true,
    ackRevision:"11",hardwareConfiguration:"dev-A"};
  assert.equal(stripVM.sourceQueueReadback({authority:"CHANNEL_ID",
    pending,snapshot:now}),"CONFIRMED");
  assert.equal(stripVM.sourceQueueReadback({authority:"CHANNEL_ID",
    pending,snapshot:{...now,graph:{...now.graph,
      channels:[{id:"8",role:"INPUT",source:{
        sourceFormat:"STEREO",portId:"in:1",portIdRight:"in:2"}}]}}}),
    "PENDING","physical PCM must not satisfy a virtual music intent");
  const label=stripVM.readbackBindings({native:now,
    channel:now.graph.channels[0],channels:now.graph.channels,
    targets:[],inputLabel:()=>""});
  assert.equal(label.input,"Music Player");
});

test("G-MV25 schema-v6 owner switch cancels no-ACK/ACKed ChannelId without stale PCM repaint",()=>{
 const base=gmv20AcceptedGraph();
 base.mixerSession={authority:"CHANNEL_ID",deviceEpoch:base.device.epoch,
   routeRevision:base.graph.revision,rosterRevision:base.graph.rosterRevision};
 base.device.outputArmed=false;
 const m4=clone(base);
 m4.productionCallbackLive=false;
 m4.graph.capabilities.callbackOwnsChannelIds=false;
 m4.graph.capabilities.supportedOps=[];
 m4.mixerSession={authority:"M4",deviceEpoch:base.device.epoch,
   routeRevision:base.graph.revision,rosterRevision:base.graph.rosterRevision};
 assert.ok(protocol.normalizeSnapshot(base)?.admitted);
 assert.equal(protocol.normalizeSnapshot(m4)?.admitted,false);
 assert.equal(protocol.nativeHardwareConfiguration(base.device),
   protocol.nativeHardwareConfiguration(m4.device));
 for(const acknowledged of [false,true]){
   const emitted=[],accepted=[],controller=protocol.createController({
     emit:(name,payload)=>emitted.push({name,payload}),
     onAccepted:(snapshot,confirmed)=>accepted.push({snapshot,confirmed})
   });
   assert.equal(controller.onSnapshot(clone(base)),true);
   const pending=controller.intent("SET_FADER","7",{valueDb:-5});
   assert.equal(pending.ok,true);
   if(acknowledged) {
     assert.equal(controller.onAck({
       requestId:pending.requestId,status:"ACCEPTED",errorCode:"NONE",
       deviceEpoch:base.device.epoch,graphRevision:base.graph.revision,
       rosterRevision:base.graph.rosterRevision}),true);
     assert.equal(controller.state().pending?.requestId,pending.requestId);
   }
   assert.equal(controller.onSnapshot(clone(m4)),true,
     "same-epoch M4 authority must not be hidden behind old ChannelId ACK");
   assert.equal(controller.state().pending,null);
   assert.equal(controller.state().lastError,"STALE_NATIVE_AUTHORITY");
   assert.equal(controller.state().current.admitted,false);
   assert.equal(controller.state().current.raw.mixerSession.authority,"M4");
   assert.equal(accepted.length,2,"M4 readback is projected in the same callback");
   assert.equal(accepted[1].confirmed,undefined,
     "an old ACK cannot confirm PCM on the replacement producer");
   assert.equal(controller.onAck({
     requestId:pending.requestId,status:"ACCEPTED",errorCode:"NONE",
     deviceEpoch:base.device.epoch,graphRevision:base.graph.revision,
     rosterRevision:base.graph.rosterRevision}),false,
     "late ChannelId ACK cannot reactivate retired owner");
   assert.equal(controller.state().current.admitted,false);
   assert.equal(controller.intent("SET_FADER","7",{valueDb:-4}).ok,false,
     "unadmitted M4 has no ChannelId write authority");
   const recovered=clone(base);
   recovered.graph.revision="6";
   recovered.graph.rosterRevision="8";
   recovered.mixerSession.routeRevision="6";
   recovered.mixerSession.rosterRevision="8";
   assert.equal(controller.onSnapshot(recovered),true);
   assert.equal(controller.state().current.admitted,true);
   assert.equal(controller.state().current.device.outputArmed,false,
     "recovery never invents physical MONITOR ON");
   assert.equal(accepted.length,3);
   assert.equal(accepted[2].confirmed,undefined,
     "recovered producer readback is not confirmation of old fader intent");
   const send=controller.intent("SET_FADER","7",{valueDb:-4});
   assert.equal(send.ok,true,"fresh accepted revision can accept new UI gesture");
   assert.equal(emitted.filter(e=>e.name==="arMixConsoleIntent").length,2,
     "no automatic replay of old local controls or monitor commands");
   assert.equal(emitted.some(e=>e.payload?.action==="monitor"),false);
 }
 const altered=clone(m4);
 altered.graph.revision="4";
 const seen=[],c=protocol.createController({
   emit(){},onAccepted:n=>seen.push(n)
 });
 assert.equal(c.onSnapshot(clone(base)),true);
 assert.equal(c.onSnapshot(altered),false,
   "older graph revisions remain fail-closed even across authority changes");
 assert.equal(c.state().lastError,"OLD_NATIVE_REVISION");
 assert.equal(seen.length,1);
});

test("G-MV20 schema-v6 pending intent is fenced on same-epoch physical pin or format change",()=>{
 const base=gmv20AcceptedGraph(),before=JSON.stringify(base);
 const hardware=protocol.nativeHardwareConfiguration(base.device);
 assert.equal(hardware,JSON.stringify([48000,128,[0,1],[0,1,2,3]]));
 const normalized=protocol.normalizeSnapshot(base);
 assert.equal(normalized.hardwareConfiguration,hardware);
 const staleAck=(requestId)=>({
   requestId,status:"ACCEPTED",errorCode:"NONE",deviceEpoch:"69",
   graphRevision:"5",rosterRevision:"5"
 });
 for(const kind of ["input-mask","output-mask","rate","buffer"]){
   const sent=[],accepted=[];
   const controller=protocol.createController({
     emit:(name,payload)=>sent.push({name,payload}),
     onAccepted:(next,confirmed)=>accepted.push({next,confirmed})
   });
   assert.equal(controller.onSnapshot(base),true);
   assert.equal(accepted.length,1);
   const issued=controller.intent("SET_FADER","7",{valueDb:-6});
   assert.equal(issued.ok,true);
   const changed=clone(base);
   if(kind==="input-mask"){
     changed.device.enabledInputs=[0];
     changed.device.inputEndpoints=changed.device.inputEndpoints.filter(p=>p.port===0);
   }else if(kind==="output-mask"){
     changed.device.enabledOutputs=[0,1];
     changed.device.outputEndpoints=changed.device.outputEndpoints.filter(p=>p.port<2);
   }else if(kind==="rate")changed.device.sampleRate=44100;
   else changed.device.bufferFrames=256;
   assert.equal(controller.onSnapshot(changed),true,kind);
   assert.equal(controller.state().pending,null,
     "physical remap must cancel old native pending intent: "+kind);
   assert.equal(controller.state().lastError,"STALE_DEVICE");
   assert.equal(accepted.length,2,
     "new physical config must repaint from native even before an old ACK: "+kind);
   assert.notEqual(accepted[1].next.hardwareConfiguration,hardware);
   assert.equal(accepted[1].confirmed,undefined,"no ACK-only or false PCM success");
   assert.equal(controller.onAck(staleAck(issued.requestId)),false,
     "old ACK must not settle the replaced hardware configuration");
   assert.equal(controller.state().pending,null);
   assert.equal(controller.state().current.hardwareConfiguration,
     accepted[1].next.hardwareConfiguration);
   assert.equal(sent.filter(x=>x.name==="arMixConsoleIntent").length,1,
     "new hardware readback cannot silently redispatch an old edit");
 }
 assert.equal(JSON.stringify(base),before,"native fixture cannot be mutated");
});

test("G-MV20 pin order and MONITOR are not a new hardware owner",()=>{
 const base=gmv20AcceptedGraph(),seen=[];
 const c=protocol.createController({emit(){},
   onAccepted:next=>seen.push(next)});
 assert.equal(c.onSnapshot(base),true);
 const issued=c.intent("SET_FADER","7",{valueDb:-6});
 assert.equal(issued.ok,true);
 const rearranged=clone(base);
 rearranged.device.enabledInputs.reverse();
 rearranged.device.enabledOutputs.reverse();
 rearranged.device.outputArmed=!base.device.outputArmed;
 assert.equal(c.onSnapshot(rearranged),true);
 assert.equal(c.state().pending?.requestId,issued.requestId,
   "reordering physical pin sets must preserve a valid pending channel edit");
 assert.equal(seen.length,1,"unacknowledged operation does not repaint");
 assert.equal(protocol.nativeHardwareConfiguration(rearranged.device),
   protocol.nativeHardwareConfiguration(base.device));
});

// GUI-4: SOURCE ONLY and physical PRIMARY OUT must not consume a parallel SEND.


test("MV-7 native SOURCE requires ACKed graph revision AND independently installed input pins",()=>{
 const id="9007199254740993",epoch="18446744073709551610";
 const beforeRevision="9223372036854775807",afterRevision="9223372036854775808";
 const p={requestId:"source-native-99",id,epoch,beforeRevision,
  choice:{label:"Mic A",mode:"STEREO",left:2,right:3},acked:false};
 const snapshot={admitted:true,retained:false,device:{epoch},
  graph:{revision:beforeRevision,channels:[{id,role:"INPUT",
    source:{portId:"in:2",portIdRight:"in:3",sourceFormat:"STEREO"}}]}};
 const before=JSON.stringify(snapshot);
 const settle=(request,readback=snapshot)=>stripVM.sourceReadbackSettlement(request,readback);
 assert.equal(settle(null),"NONE");
 assert.equal(settle(p),"PENDING_ACK");
 assert.equal(settle({...p,acked:true,ackRevision:afterRevision}),"PENDING",
  "even matching pins at old revision are not accepted readback");
 const installed=clone(snapshot);installed.graph.revision=afterRevision;
 assert.equal(settle({...p,acked:true,ackRevision:afterRevision},installed),
  "CONFIRMED");
 assert.equal(JSON.stringify(snapshot),before,"VM cannot mutate accepted PCM");
 const acked={...p,acked:true,ackRevision:afterRevision};
 const other=clone(installed);other.graph.channels[0].source.portIdRight="in:4";
 assert.equal(settle(acked,other),"PENDING",
  "stereo right pin must independently match native source readback");
 const wrongFormat=clone(installed);
 wrongFormat.graph.channels[0].source.sourceFormat="MONO";
 assert.equal(settle(acked,wrongFormat),"PENDING");
 const noRoute=clone(installed);delete noRoute.graph.channels[0].source;
 assert.equal(settle(acked,noRoute),"PENDING");
 const reconnect=clone(installed);reconnect.device.epoch="18446744073709551611";
 assert.equal(settle(acked,reconnect),"STALE");
 assert.equal(settle(acked,{...installed,retained:true,admitted:false}),"STALE");
 assert.equal(settle(acked,{...installed,raw:{mixerSession:{
   authority:"M4",deviceEpoch:epoch}}}),"STALE");
 assert.equal(settle(acked,{...installed,raw:{mixerSession:{
   authority:"CHANNEL_ID",deviceEpoch:"18446744073709551611"}}}),"STALE");
 assert.equal(settle({...acked,ackRevision:beforeRevision},installed),"PENDING",
   "ACK publisher revision cannot equal original accepted source graph");
 const missing=clone(installed);missing.graph.channels=[];
 assert.equal(settle(acked,missing),"STALE");
 const mono={...acked,choice:{label:"Mono",mode:"MONO",left:2,right:-1}};
 const monoread=clone(installed);delete monoread.graph.channels[0].source.portIdRight;
 monoread.graph.channels[0].source.sourceFormat="MONO";
 assert.equal(settle(mono,monoread),"CONFIRMED");
 const none={...acked,choice:{label:"None",mode:"NONE",left:-1,right:-1}};
 assert.equal(settle(none,noRoute),"CONFIRMED");
 assert.equal(settle(none,installed),"PENDING",
  "SOURCE NONE must not remove a physical input from UI before installed PCM");
 const model=read("web/approved-channel-strip-view-model.js");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/stripVM\.sourceQueueReadback\(/);
 assert.match(bridge,/stripVM\.sourceQueueRequestLease\(\{/);
 assert.match(stripVM.sourceQueueAckTransition.toString(),/ack\.graphRevision/,
    "exact native ACK graph revision is retained in the immutable VM lease");
 assert.match(bridge,/settleSourceReadback\("CHANNEL_ID",next\)/);
 assert.doesNotMatch(model,/backend\.emitEvent|document\.|window\.setTimeout/,
  "Channel Strip readback remains pure; bridge holds one transient source queue");
});

test("G-MV23 ChannelId SOURCE queue skips an already installed choice after timeout or ACK race",()=>{
 const epoch="18446744073709551610",id="9007199254740993";
 const config=protocol.nativeHardwareConfiguration({sampleRate:48000,bufferFrames:256,
   enabledInputs:[0,1,2,3],enabledOutputs:[0,1]});
 const choices=[{label:"None",mode:"NONE",left:-1,right:-1},
   {label:"Mic 1",mode:"MONO",left:0,right:-1},
   {label:"Mic 2",mode:"MONO",left:1,right:-1},
   {label:"Stereo 3+4",mode:"STEREO",left:2,right:3}];
 const choice=choices[1];
 const queued=stripVM.sourceQueueSelection({choice,choices,epoch,
   acceptedLabel:"None",activeSameChannel:true,
   hardwareConfiguration:config}).entry;
 const hardware={device:{epoch},hardwareConfiguration:config,
   raw:{mixerSession:{authority:"CHANNEL_ID",deviceEpoch:epoch}}};
 const installed={admitted:true,retained:false,device:{epoch},
   hardwareConfiguration:config,
   graph:{revision:"70",rosterRevision:"80",channels:[{
     id,role:"INPUT",source:{sourceFormat:"MONO",portId:"in:0"}}]}};
 const q=new Map([[id,queued]]);
 const check=(snapshot=installed,h=hardware,entry=queued,active=null)=>
   stripVM.sourceQueueHead({authority:"CHANNEL_ID",queue:new Map([[id,entry]]),
     active,hardware:h,native:snapshot,choices,deviceReady:true});
 assert.deepEqual(check(),{kind:"SKIP_INSTALLED",key:id},
   "native source already installed after first in-flight patch: do not send twice");
 assert.equal(q.size,1,"pure VM must not mutate the bridge-owned pending queue");
 assert.deepEqual(check(installed,hardware,queued,{requestId:"source-native-7"}),
   {kind:"IDLE"},"never skip while a source request is still in flight");
 const firstRejected={...installed,graph:{...installed.graph,revision:"71",
   channels:[{id,role:"INPUT",source:null}]}};
 assert.equal(check(firstRejected).kind,"SEND",
   "after a rejected first request, latest choice must remain dispatchable");
 assert.equal(check(firstRejected).payload.graphRevision,"71",
   "failed first request is retried only using fresh accepted graph revision");
 const changedChoice={...queued,choice:choices[2]};
 assert.equal(check(installed,hardware,changedChoice).kind,"SEND",
   "different latest choice must not be dropped just because first ACK succeeded");
 const otherPhysical={...hardware,hardwareConfiguration:config+"-different"};
 assert.equal(check(installed,otherPhysical).kind,"DROP",
   "a no-op must NEVER mask same-epoch hardware identity mismatch");
 assert.deepEqual(check({...installed,admitted:false}),{kind:"WAIT"});
 assert.deepEqual(check(installed,{...hardware,raw:{
   mixerSession:{authority:"M4",deviceEpoch:epoch}}}),{kind:"WAIT"});
 assert.equal(check({...installed,graph:{...installed.graph,channels:[{
   id,role:"INPUT",source:{sourceFormat:"MONO",portId:"in:0",
     bindingState:"NEEDS_REMAP"}}]}}).kind,"SEND",
   "a retained physical input mapping is never verified installed PCM");
 assert.equal(check({...installed,graph:{...installed.graph,channels:[{
   id,role:"GROUP",source:{sourceFormat:"MONO",portId:"in:0"}}]}}).kind,
   "DROP","invalid graph ownership cannot be skipped");
 const none=stripVM.sourceQueueSelection({choice:choices[0],choices,epoch,
   acceptedLabel:"Mic 1",hardwareConfiguration:config}).entry;
 assert.deepEqual(check(firstRejected,hardware,none),
   {kind:"SKIP_INSTALLED",key:id},
   "SOURCE NONE is already installed only when the real ChannelId source is absent");
 assert.equal(check(installed,hardware,none).kind,"SEND",
   "SOURCE NONE must remain a real patch when an input is still bound");
 const stereo=stripVM.sourceQueueSelection({choice:choices[3],choices,epoch,
   acceptedLabel:"None",hardwareConfiguration:config}).entry;
 assert.equal(check({...installed,graph:{...installed.graph,channels:[{
   id,role:"INPUT",source:{sourceFormat:"STEREO",
     portId:"in:2",portIdRight:"in:3"}}]}},hardware,stereo).kind,"SKIP_INSTALLED");
 assert.equal(check({...installed,graph:{...installed.graph,channels:[{
   id,role:"INPUT",source:{sourceFormat:"STEREO",
     portId:"in:2",portIdRight:"in:1"}}]}},hardware,stereo).kind,"SEND",
   "right-channel pin mismatch must not be treated as already installed");
 assert.strictEqual(stripVM.sourceQueueAlreadyInstalled({authority:"CHANNEL_ID",
   key:id,entry:queued,hardware,native:installed}),true);
 assert.deepEqual([...q],[[id,queued]],"SOURCE VM cannot mutate queued drafts");
});

test("G-MV23 M4 accepted SOURCE no-op avoids duplicate patch but preserves first sound",()=>{
 const epoch="71",config=protocol.nativeHardwareConfiguration({
   sampleRate:48000,bufferFrames:128,enabledInputs:[0,1],
   enabledOutputs:[0,1]});
 const choices=[{label:"None",mode:"NONE",left:-1,right:-1},
   {label:"Mic 1",mode:"MONO",left:0,right:-1},
   {label:"Mic 2",mode:"MONO",left:1,right:-1}];
 const q=choice=>new Map([[4,{epoch,hardwareConfiguration:config,choice}]]);
 const session={authority:"M4",deviceEpoch:epoch,routeRevision:"29",
   channels:Array.from({length:10},(_,i)=>({role:"INPUT",
     source:i===4?{mode:"MONO",left:0,right:-1}:
       {mode:"NONE",left:-1,right:-1}}))};
 const legacyM4={active:true,routeAccepted:true,routingRevision:"29"};
 const hardware={admitted:false,retained:false,device:{epoch},
   hardwareConfiguration:config,raw:{mixerSession:session,legacyM4}};
 const native={admitted:false,retained:false,device:{epoch},
   hardwareConfiguration:config};
 const check=(hardwareValue=hardware,choice=choices[1])=>
   stripVM.sourceQueueHead({authority:"M4",queue:q(choice),
     hardware:hardwareValue,native,choices,deviceReady:true});
 assert.deepEqual(check(),{kind:"SKIP_INSTALLED",key:4},
   "M4 matching accepted producer readback after timeout skips redundant PCM switch");
 assert.equal(check(hardware,choices[2]).kind,"SEND",
   "a new physical selection must use the latest M4 routing revision");
 assert.equal(check(hardware,choices[0]).kind,"SEND",
   "SOURCE NONE requires real patch when old M4 route is still installed");
 const cleared={...hardware,raw:{...hardware.raw,mixerSession:{
   ...session,channels:session.channels.map((ch,i)=>i===4?
     {...ch,source:{mode:"NONE",left:-1,right:-1}}:ch)}}};
 assert.deepEqual(check(cleared,choices[0]),{kind:"SKIP_INSTALLED",key:4});
 const preparing={...hardware,raw:{legacyM4:{...legacyM4,routeAccepted:false},
   mixerSession:{...session,authority:"NONE"}}};
 assert.equal(check(preparing).kind,"SEND",
   "UNADMITTED to M4 first sound must never be mistaken for verified PCM");
 const inconsistent={...hardware,raw:{legacyM4:{...legacyM4,routingRevision:"30"},
   mixerSession:session}};
 assert.equal(check(inconsistent).kind,"SEND",
   "inconsistent route revisions cannot prove installed M4 source");
 assert.equal(check({...hardware,admitted:true}).kind,"SEND");
 assert.equal(check({...hardware,retained:true}).kind,"SEND");
 assert.equal(check({...hardware,hardwareConfiguration:config+"-old"}).kind,"DROP");
 const brokenSource={...hardware,raw:{legacyM4,mixerSession:{
   ...session,channels:session.channels.map((ch,i)=>i===4?
     {...ch,source:{mode:"STEREO",left:0,right:1}}:ch)}}};
 assert.equal(check(brokenSource).kind,"SEND");
 const bridge=read("web/approved-native-bridge.js");
 const body=bridge.slice(bridge.indexOf("  function dispatchSourceQueue(authority){"),
   bridge.indexOf("  function dispatchNativeSource()"));
 assert.match(body,/if\(plan\.kind==="SKIP_INSTALLED"\)continue;/);
 assert.ok(body.indexOf('if(plan.kind==="SKIP_INSTALLED")continue;')<
   body.indexOf('backend.emitEvent("arMixAction"'),
   "verified no-op must exit BEFORE the sole native SOURCE dispatcher");
 assert.equal((body.match(/backend\.emitEvent\("arMixAction"/g)||[]).length,1,
   "single dispatcher and no second PCM owner");
 assert.doesNotMatch(stripVM.sourceQueueAlreadyInstalled.toString(),
   /backend\.|document\.|window\.|setTimeout|AudioContext|localStorage/);
});

test("G-MV22 physical SOURCE leases reject same-epoch hardware remaps for ChannelId and M4",()=>{
 const epoch="91",id="9007199254740993";
 const d={sampleRate:48000,bufferFrames:256,enabledInputs:[0,1],enabledOutputs:[0,1]};
 const token=protocol.nativeHardwareConfiguration(d);
 const choices=[{label:"None",mode:"NONE",left:-1,right:-1},
   {label:"Mono IN1",mode:"MONO",left:0,right:-1}];
 const selection=stripVM.sourceQueueSelection({choice:choices[1],choices,
   epoch,acceptedLabel:"None",hardwareConfiguration:token});
 assert.deepEqual(selection,{kind:"QUEUE",entry:{
   epoch,hardwareConfiguration:token,choice:choices[1]}});
 assert.equal(stripVM.sourceQueueSelection({choice:choices[1],choices,
   epoch,acceptedLabel:"None",hardwareConfiguration:""}).kind,"UNAVAILABLE");
 const hardware={device:{epoch},hardwareConfiguration:token,
   raw:{mixerSession:{authority:"CHANNEL_ID"},legacyM4:{routingRevision:"34"}}};
 const native={admitted:true,retained:false,device:{epoch},
   hardwareConfiguration:token,raw:{mixerSession:{
     authority:"CHANNEL_ID",deviceEpoch:epoch}},
   graph:{revision:"61",rosterRevision:"17",
     channels:[{id,role:"INPUT",source:null}]}};
 const queue=new Map([[id,selection.entry]]);
 const pick=(authority,h=hardware,n=native,q=queue)=>
   stripVM.sourceQueueHead({authority,queue:q,hardware:h,
     native:n,choices,deviceReady:true});
 const plan=pick("CHANNEL_ID");
 assert.equal(plan.kind,"SEND");
 const pending=stripVM.sourceQueueRequestLease({authority:"CHANNEL_ID",
   requestId:"source-native-99",plan,hardware,native});
 assert.deepEqual(pending,{requestId:"source-native-99",id,epoch,
   choice:choices[1],hardwareConfiguration:token,
   beforeRevision:"61",acked:false});
 assert.ok(Object.isFrozen(pending)&&Object.isFrozen(pending.choice));
 const acked=stripVM.sourceQueueAckTransition({authority:"CHANNEL_ID",
   pending,ack:{requestId:pending.requestId,channelId:id,
     graphRevision:"62",ok:true}}).pending;
 const installed={...native,graph:{...native.graph,revision:"62",
   channels:[{id,role:"INPUT",source:{sourceFormat:"MONO",portId:"in:0"}}]}};
 assert.equal(stripVM.sourceQueueReadback({authority:"CHANNEL_ID",
   pending:acked,snapshot:installed}),"CONFIRMED");
 assert.equal(protocol.nativeHardwareConfiguration({...d,
   enabledInputs:[1,0],enabledOutputs:[1,0]}),token,
   "physical port order is a set and must not restart valid source leases");
 for(const variant of [
   {...d,enabledInputs:[0]},{...d,enabledOutputs:[0,1,2,3]},
   {...d,sampleRate:44100},{...d,bufferFrames:512}
 ]){
   const other=protocol.nativeHardwareConfiguration(variant);
   assert.notEqual(other,token);
   const changed={...hardware,hardwareConfiguration:other};
   assert.equal(pick("CHANNEL_ID",changed).kind,"DROP",
     "old source cannot dispatch to replaced hardware within same epoch");
   assert.equal(stripVM.sourceQueueRequestLease({authority:"CHANNEL_ID",
     requestId:"source-native-99",plan,hardware:changed,native}),null);
   assert.equal(stripVM.sourceQueueReadback({authority:"CHANNEL_ID",
     pending:acked,snapshot:{...installed,hardwareConfiguration:other}}),"STALE");
   assert.equal(stripVM.sourceQueueTimeoutDecision({authority:"CHANNEL_ID",
     pending:acked,requestId:acked.requestId,
     snapshot:{...installed,hardwareConfiguration:other}}),"REFRESH");
 }
 assert.equal(pick("CHANNEL_ID",hardware,
   {...native,hardwareConfiguration:"other"}).kind,"DROP");
 assert.equal(pick("CHANNEL_ID",hardware,native,
   new Map([[id,{epoch,choice:choices[1]}]])).kind,"DROP",
   "unscoped queue entries cannot enter real fingerprinted hardware");
 const m4Hardware={...hardware,raw:{
   mixerSession:{authority:"NONE"},legacyM4:{routingRevision:"34"}}};
 const m4Native={...native,admitted:false,raw:{mixerSession:{
   authority:"NONE",deviceEpoch:epoch}}};
 const m4Plan=pick("M4",m4Hardware,m4Native,
   new Map([[4,selection.entry]]));
 assert.equal(m4Plan.kind,"SEND","first-sound M4 retains same hardware lease");
 const m4Lease=stripVM.sourceQueueRequestLease({authority:"M4",requestId:101,
   plan:m4Plan,hardware:m4Hardware,native:m4Native});
 assert.deepEqual(m4Lease,{requestId:101,channel:4,epoch:91,revision:34,
   choice:choices[1],hardwareConfiguration:token,acked:false});
 const ack=stripVM.sourceQueueAckTransition({authority:"M4",pending:m4Lease,
   ack:{requestId:101,ok:true,state:{ready:true,epoch:91,revision:35,
     hasAcceptedRoute:true,acceptedRoute:{channels:Array.from({length:10},
       (_,i)=>i===4?{mode:"MONO",left:0,right:-1}:null)}}}}).pending;
 const readback={admitted:false,retained:false,device:{epoch},
   hardwareConfiguration:token,raw:{
     legacyM4:{active:true,routeAccepted:true,routingRevision:"35"},
     mixerSession:{authority:"M4",deviceEpoch:epoch,routeRevision:"35",
       channels:Array.from({length:10},(_,i)=>({role:"INPUT",
         source:i===4?{mode:"MONO",left:0,right:-1}:
           {mode:"NONE",left:-1,right:-1}}))}}};
 assert.equal(stripVM.sourceQueueReadback({authority:"M4",
   pending:ack,snapshot:readback}),"CONFIRMED");
 assert.equal(stripVM.sourceQueueReadback({authority:"M4",
   pending:ack,snapshot:{...readback,hardwareConfiguration:"other"}}),"STALE");
 assert.equal(stripVM.sourceQueueRequestLease({authority:"M4",requestId:"101",
   plan:m4Plan,hardware:m4Hardware,native:m4Native}),null);
 assert.equal(queue.size,1,"VM selection cannot mutate bridge-owned queues");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/hardwareConfiguration:hardware\?\.hardwareConfiguration\?\?""/);
 assert.equal((bridge.match(/stripVM\.sourceQueueRequestLease\(\{/g)||[]).length,1);
 assert.doesNotMatch(bridge,/activeNativeSource=\{requestId,id:plan\.key/);
 assert.doesNotMatch(stripVM.sourceQueueRequestLease.toString(),
   /backend|document\.|window\.|setTimeout|emitEvent|AudioContext/);
});

test("MV-6 SOURCE input queues bind exact native device epoch and verified pins",()=>{
 const choices=[{label:"None",mode:"NONE",left:-1,right:-1},
  {label:"Mono · IN 1",mode:"MONO",left:0,right:-1},
  {label:"Mono · IN 2",mode:"MONO",left:1,right:-1},
  {label:"Stereo · IN 1 + 2",mode:"STEREO",left:0,right:1}];
 const change=stripVM.sourceQueueSelection({choice:choices[1],choices,
  epoch:"19",acceptedLabel:"None"});
 assert.deepEqual(change,{kind:"QUEUE",entry:{epoch:"19",choice:choices[1]}});
 assert.notEqual(change.entry.choice,choices[1],
  "queue stores an independent transient operator draft");
 const undo=stripVM.sourceQueueSelection({choice:choices[0],choices,
  epoch:"19",acceptedLabel:"None"});
 assert.equal(undo.kind,"CANCEL","return to installed PCM cancels stale queued edit");
 const compensate=stripVM.sourceQueueSelection({choice:choices[0],choices,
  epoch:"19",acceptedLabel:"None",activeSameChannel:true});
 assert.equal(compensate.kind,"QUEUE",
  "an already in-flight edit must be followed by explicit revert to old input");
 for(const invalid of [
  {choice:choices[1],choices,epoch:"0",acceptedLabel:"None"},
  {choice:choices[1],choices,epoch:19,acceptedLabel:"None"},
  {choice:choices[1],choices,epoch:"18446744073709551616",acceptedLabel:"None"},
  {choice:choices[1],choices:[...choices,choices[1]],epoch:"19"},
  {choice:{...choices[1],left:2},choices,epoch:"19"},
  {choice:{...choices[3],right:3},choices,epoch:"19"},
  {choice:{...choices[0],left:0},choices,epoch:"19"}
 ])assert.equal(stripVM.sourceQueueSelection(invalid).kind,"UNAVAILABLE");
 assert.equal(change.entry.epoch,"19");
 const hugeEpoch="18446744073709551610";
 const huge=stripVM.sourceQueueSelection({choice:choices[1],choices,
  epoch:hugeEpoch,acceptedLabel:"None"});
 assert.equal(huge.kind,"QUEUE",
  "full uint64 ChannelId-era device epoch must stay exact decimal text");
 const hugeHardware={device:{epoch:hugeEpoch},raw:{
  mixerSession:{authority:"CHANNEL_ID"}}};
 const hugeNative={admitted:true,retained:false,device:{epoch:hugeEpoch},
  graph:{revision:"9223372036854775808",rosterRevision:"9223372036854775809",
   channels:[{id:"9007199254740993",role:"INPUT"}]}};
 const hugePlan=stripVM.sourceDispatchPlan({authority:"CHANNEL_ID",
  entry:huge.entry,hardware:hugeHardware,native:hugeNative,
  channelId:"9007199254740993",choices,deviceReady:true});
 assert.equal(hugePlan.kind,"READY");
 assert.equal(hugePlan.payload.deviceEpoch,hugeEpoch);
 assert.equal(hugePlan.payload.graphRevision,"9223372036854775808");
 assert.equal(stripVM.sourceDispatchPlan({authority:"M4",entry:huge.entry,
  hardware:{...hugeHardware,raw:{...hugeHardware.raw,
   legacyM4:{routingRevision:"9"}}},channel:0,choices,
  deviceReady:true}).kind,"UNAVAILABLE",
  "old M4 cannot coerce a 64-bit native epoch to a rounded JS Number");
 const hardware={device:{epoch:"19"},raw:{mixerSession:{authority:"CHANNEL_ID"},
  legacyM4:{routingRevision:"22"}}};
 const id="9007199254740993";
 const native={admitted:true,retained:false,device:{epoch:"19"},
  graph:{revision:"30",rosterRevision:"31",
   channels:[{id,role:"INPUT"}]}};
 const plan=stripVM.sourceDispatchPlan({authority:"CHANNEL_ID",
  entry:change.entry,hardware,native,channelId:id,choices,deviceReady:true});
 assert.deepEqual(plan,{kind:"READY",action:"routingNativeSourcePatch",
  payload:{channelId:id,deviceEpoch:"19",graphRevision:"30",
   rosterRevision:"31",mode:"MONO",left:0,right:-1}});
 for(const invalid of [
  {hardware:{...hardware,device:{epoch:"20"}}},
  {native:{...native,device:{epoch:"20"}}},
  {native:{...native,admitted:false}},
  {native:{...native,retained:true}},
  {native:{...native,graph:{...native.graph,
    channels:[{id,role:"GROUP"}]}}},
  {native:{...native,graph:{...native.graph,revision:"0"}}},
  {channelId:Number(id)},
  {choices:choices.filter(x=>x.label!==choices[1].label)},
  {deviceReady:false}
 ])assert.equal(stripVM.sourceDispatchPlan({authority:"CHANNEL_ID",
  entry:change.entry,hardware,native,channelId:id,choices,
  deviceReady:true,...invalid}).kind,"UNAVAILABLE",JSON.stringify(invalid));
});
test("MV-6 M4 SOURCE dispatch reuses fresh revision but never migrates stale device pins",()=>{
 const choices=[{label:"None",mode:"NONE",left:-1,right:-1},
   {label:"Mono · IN 1",mode:"MONO",left:0,right:-1}];
 const entry=stripVM.sourceQueueSelection({choice:choices[1],choices,
   epoch:"71",acceptedLabel:"None"}).entry;
 const hardware={device:{epoch:"71"},raw:{
  mixerSession:{authority:"M4"},legacyM4:{routingRevision:"29"}}};
 const plan=(override={})=>stripVM.sourceDispatchPlan({
  authority:"M4",entry,hardware,channel:4,choices,deviceReady:true,...override});
 assert.deepEqual(plan(),{kind:"READY",action:"routingSourceSwitch",
  payload:{channel:4,mode:"MONO",left:0,right:-1,deviceEpoch:71,revision:29}});
 assert.equal(plan({hardware:{...hardware,raw:{...hardware.raw,
  legacyM4:{routingRevision:"30"}}}}).payload.revision,30,
  "M4 accepts fresh route revision only on the SAME physical epoch");
 assert.equal(plan({hardware:{...hardware,device:{epoch:"72"}}}).kind,
  "UNAVAILABLE","device switch must discard the old input choice");
 assert.equal(plan({hardware:{...hardware,raw:{...hardware.raw,
  mixerSession:{authority:"CHANNEL_ID"}}}}).kind,"UNAVAILABLE",
  "M4 cannot seize authority from admitted ChannelId PCM");
 for(const override of [
  {channel:10},{channel:-1},{channel:"4"},
  {hardware:{...hardware,raw:{...hardware.raw,legacyM4:{routingRevision:"0"}}}},
  {choices:[choices[0]]},{deviceReady:false}
 ])assert.equal(plan(override).kind,"UNAVAILABLE");
 const pending={requestId:99,channel:4,choice:entry.choice,epoch:71,revision:29};
 const ack={requestId:99,ok:true,state:{ready:true,epoch:71,revision:30,
  hasAcceptedRoute:true,acceptedRoute:{channels:Array.from({length:10},
   (_,i)=>i===4?{mode:"MONO",left:0,right:-1}:null)}}};
 assert.equal(stripVM.sourceAckResult(pending,ack,"M4"),"ACKED");
 assert.equal(stripVM.sourceAckResult(pending,{...ack,requestId:100},"M4"),"IGNORE");
 assert.equal(stripVM.sourceAckResult(pending,{...ack,state:{...ack.state,
  epoch:72}},"M4"),"REJECTED");
 assert.equal(stripVM.sourceAckResult(pending,{...ack,state:{...ack.state,
  acceptedRoute:{channels:Array.from({length:10},(_,i)=>i===4?
   {mode:"MONO",left:2,right:-1}:null)}}},"M4"),"REJECTED");
 const nativePending={requestId:"source-native-1",
  id:"9007199254740993",epoch:"18446744073709551610",
  beforeRevision:"9223372036854775807"};
 const nativeAck={requestId:"source-native-1",
  channelId:"9007199254740993",graphRevision:"9223372036854775808",
  ok:true};
 assert.equal(stripVM.sourceAckResult(nativePending,nativeAck,"CHANNEL_ID"),
  "ACKED");
 assert.equal(stripVM.sourceAckResult(nativePending,
  {...nativeAck,requestId:"source-native-2"},"CHANNEL_ID"),"IGNORE");
 assert.equal(stripVM.sourceAckResult(nativePending,
  {...nativeAck,ok:false},"CHANNEL_ID"),"REJECTED");
 assert.equal(stripVM.sourceAckResult(nativePending,
  {...nativeAck,channelId:"9007199254740994"},"CHANNEL_ID"),"REJECTED");
 assert.equal(stripVM.sourceAckResult(nativePending,
  {...nativeAck,graphRevision:nativePending.beforeRevision},"CHANNEL_ID"),
  "REJECTED");
 assert.equal(stripVM.sourceAckResult(nativePending,
  {...nativeAck,graphRevision:"18446744073709551616"},"CHANNEL_ID"),
  "REJECTED");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/stripVM\.sourceQueueSelection\(/);
 assert.equal((bridge.match(/stripVM\.sourceDispatchPlan\(/g)||[]).length,0,
  "source preflight now belongs only to pure Channel Strip VM");
 assert.equal((bridge.match(/stripVM\.sourceQueueHead\(/g)||[]).length,1,
  "one bridge dispatch head for ChannelId and M4");
 assert.equal((bridge.match(/stripVM\.sourceQueueAckTransition\(/g)||[]).length,1,
   "one immutable ACK state transition for M4 and native ChannelId");
 assert.match(bridge,/pendingNativeSources\.set\(id,decision\.entry\)/);
 assert.match(bridge,/pendingSourceQueue\.set\(channel,decision\.entry\)/);
 assert.match(bridge,/if\(decision\.kind==="CANCEL"\)pendingNativeSources\.delete/);
 assert.match(bridge,/if\(decision\.kind==="CANCEL"\)pendingSourceQueue\.delete/);
 assert.doesNotMatch(stripVM.sourceDispatchPlan.toString(),/backend|document|window/);
});





test("MV-17 SOURCE timeout decision preserves verified PCM and fences stale timers",()=>{
 const decide=(authority,pending,snapshot,requestId=pending?.requestId)=>
   stripVM.sourceQueueTimeoutDecision({authority,pending,snapshot,requestId});
 const id="9007199254740993",epoch="18446744073709551610";
 const native={requestId:"source-native-17",id,epoch,
   beforeRevision:"9223372036854775807",acked:true,
   ackRevision:"9223372036854775808",
   choice:{label:"Stereo IN3/4",mode:"STEREO",left:2,right:3}};
 const accepted={admitted:true,retained:false,device:{epoch},graph:{
   revision:"9223372036854775808",channels:[{id,role:"INPUT",
     source:{sourceFormat:"STEREO",portId:"in:2",portIdRight:"in:3"}}]}};
 const m4={requestId:51,channel:4,epoch:69,revision:12,
   acked:true,ackRevision:"13",
   choice:{label:"Mono IN1",mode:"MONO",left:0,right:-1}};
 const rows=Array.from({length:10},(_,i)=>({role:"INPUT",
   source:i===4?{mode:"MONO",left:0,right:-1}:
     {mode:"NONE",left:-1,right:-1}}));
 const m4Accepted={admitted:false,retained:false,device:{epoch:"69"},
   raw:{legacyM4:{active:true,routeAccepted:true,routingRevision:"13"},
     mixerSession:{authority:"M4",deviceEpoch:"69",routeRevision:"13",channels:rows}}};
 assert.equal(decide("CHANNEL_ID",native,accepted),"SETTLE");
 assert.equal(decide("M4",m4,m4Accepted),"SETTLE");
 for(const [authority,pending,snapshot] of [
   ["CHANNEL_ID",native,accepted],["M4",m4,m4Accepted]
 ]){
   assert.equal(decide(authority,null,snapshot),"IGNORE");
   assert.equal(decide(authority,pending,snapshot,"obsolete-id"),"IGNORE");
   assert.equal(decide(authority,{...pending,acked:false},snapshot),"REFRESH",
     "PCM without correlated ACK is not proof");
   assert.equal(decide(authority,pending,{...snapshot,retained:true}),"REFRESH");
   assert.equal(decide(authority,pending,null),"REFRESH");
 }
 const wrongPin=clone(accepted);
 wrongPin.graph.channels[0].source.portIdRight="in:4";
 assert.equal(decide("CHANNEL_ID",native,wrongPin),"REFRESH");
 const oldRevision=clone(m4Accepted);
 oldRevision.raw.mixerSession.routeRevision="12";
 oldRevision.raw.legacyM4.routingRevision="12";
 assert.equal(decide("M4",m4,oldRevision),"REFRESH");
 assert.equal(decide("INVALID",m4,m4Accepted),"IGNORE");
 assert.deepEqual(m4.choice,{label:"Mono IN1",mode:"MONO",left:0,right:-1},
   "a timeout policy cannot mutate a native source lease");
 assert.doesNotMatch(stripVM.sourceQueueTimeoutDecision.toString(),
   /backend|document\.|window\.|setTimeout|localStorage/);
 const bridge=read("web/approved-native-bridge.js");
 assert.equal((bridge.match(/function handleSourceTimeout\(/g)||[]).length,1);
 assert.equal((bridge.match(/stripVM\.sourceQueueTimeoutDecision\(/g)||[]).length,1);
 assert.match(bridge,/window\.setTimeout\(\(\)=>handleSourceTimeout\(authority,requestId\),8000\)/);
 assert.match(bridge,/if\(decision==="SETTLE"\)\{\s*settleSourceReadback\(authority,snapshot\)/);
 const start=bridge.indexOf("  function handleSourceTimeout(");
 const end=bridge.indexOf("  // MV-15: one UI settlement owner",start);
 assert.ok(start>0&&end>start);
 const body=bridge.slice(start,end);
 assert.match(body,/controller\?\.subscribe\(\)/);
 assert.doesNotMatch(body,/dispatchSourceQueue|dispatchQueuedSource|backend\.emitEvent/,
   "timeout itself cannot replay stale routing requests");
});

test("MV-16 SOURCE ACK lease is immutable; first valid ACK wins, duplicates fenced",()=>{
 const id="9007199254740993",epoch="18446744073709551610";
 const ch={requestId:"source-native-1",id,epoch,
   beforeRevision:"9223372036854775807",acked:false,
   choice:{label:"Mic A",mode:"MONO",left:0,right:-1}};
 const ack={requestId:ch.requestId,channelId:id,
   graphRevision:"9223372036854775808",ok:true};
 const decide=(authority,pending,event)=>
   stripVM.sourceQueueAckTransition({authority,pending,ack:event});
 const accepted=decide("CHANNEL_ID",ch,ack);
 assert.deepEqual(accepted,{kind:"ACKED",pending:{
   ...ch,acked:true,ackRevision:ack.graphRevision}});
 assert.notEqual(accepted.pending,ch);
 assert.equal(ch.acked,false,"VM cannot mutate pending record");
 for(const duplicate of [
   {...ack,ok:false,error:"STALE_NATIVE_SOURCE"},
   {...ack,graphRevision:"9223372036854775810"},
   {...ack,channelId:"9007199254740994"},
   ack
 ])assert.deepEqual(decide("CHANNEL_ID",accepted.pending,duplicate),
   {kind:"IGNORE"},"late/contradictory duplicate cannot release an ACKed lease");
 assert.deepEqual(decide("CHANNEL_ID",ch,{...ack,requestId:"source-native-2"}),
   {kind:"IGNORE"});
 assert.deepEqual(decide("CHANNEL_ID",ch,{...ack,ok:false}),
   {kind:"REJECTED",pending:null});
 assert.deepEqual(decide("CHANNEL_ID",null,ack),{kind:"IGNORE"});

 const m4Pending={requestId:107,channel:4,epoch:69,revision:12,
   acked:false,choice:{label:"Mono IN1",mode:"MONO",left:0,right:-1}};
 const m4Ack={requestId:107,ok:true,state:{ready:true,epoch:69,revision:13,
   hasAcceptedRoute:true,acceptedRoute:{channels:Array.from({length:10},(_,i)=>
     i===4?{mode:"MONO",left:0,right:-1}:null)}}};
 const m4=decide("M4",m4Pending,m4Ack);
 assert.deepEqual(m4,{kind:"ACKED",pending:{...m4Pending,acked:true,
   ackRevision:"13"}});
 assert.equal(m4Pending.acked,false);
 assert.deepEqual(decide("M4",m4.pending,{...m4Ack,ok:false}),
   {kind:"IGNORE"},"late negative ACK cannot invalidate M4 PCM lease");
 assert.deepEqual(decide("M4",m4.pending,{...m4Ack,state:{
   ...m4Ack.state,revision:14}}),{kind:"IGNORE"});
 assert.deepEqual(decide("M4",m4Pending,{...m4Ack,requestId:108}),
   {kind:"IGNORE"});
 assert.deepEqual(decide("M4",m4Pending,{...m4Ack,ok:false}),
   {kind:"REJECTED",pending:null});
 assert.deepEqual(decide("OTHER",m4Pending,m4Ack),
   {kind:"REJECTED",pending:null});
 assert.doesNotMatch(stripVM.sourceQueueAckTransition.toString(),
   /backend|document\.|window\.|setTimeout|AudioContext|localStorage/);
 const bridge=read("web/approved-native-bridge.js");
 assert.equal((bridge.match(/function handleSourceAck\(/g)||[]).length,1);
 assert.equal((bridge.match(/stripVM\.sourceQueueAckTransition\(/g)||[]).length,1);
 assert.equal((bridge.match(/stripVM\.sourceAckResult\(/g)||[]).length,0);
 assert.match(bridge,/handleSourceAck\("CHANNEL_ID",ack\)/);
 assert.match(bridge,/if\(handleSourceAck\("M4",ack\)\)return/);
 assert.match(bridge,/settleSourceReadback\(authority,controller\?\.state\(\)\?\.current\)/);
 assert.equal((bridge.match(/backend\.addEventListener\("arMixSourceSwitchAck"/g)||[]).length,1,
   "legacy CH1 PATCH keeps the one original M4 ACK receiver");
});

test("MV-15 one SOURCE settlement policy is pure and preserves both PCM formats",()=>{
 const nativeId="9007199254740993",epoch="18446744073709551610";
 const rev="9223372036854775808";
 const nativePending={requestId:"source-native-7",id:nativeId,epoch,
   beforeRevision:"9223372036854775807",acked:true,ackRevision:rev,
   choice:{label:"Stereo Mic",mode:"STEREO",left:2,right:3}};
 const nativeSnapshot={admitted:true,retained:false,
   device:{epoch},raw:{mixerSession:{authority:"CHANNEL_ID",deviceEpoch:epoch}},
   graph:{revision:rev,channels:[{id:nativeId,role:"INPUT",
     source:{sourceFormat:"STEREO",portId:"in:2",portIdRight:"in:3"}}]}};
 const m4Pending={requestId:8,channel:4,epoch:71,revision:29,
   acked:true,ackRevision:"30",
   choice:{label:"Mono IN 1",mode:"MONO",left:0,right:-1}};
 const channels=Array.from({length:10},(_,i)=>({id:"m4:"+String(i+1).padStart(2,"0"),
   role:"INPUT",source:i===4?{mode:"MONO",left:0,right:-1}:
     {mode:"NONE",left:-1,right:-1}}));
 const m4Snapshot={admitted:false,retained:false,device:{epoch:"71"},
   raw:{mixerSession:{authority:"M4",deviceEpoch:"71",routeRevision:"30",channels},
     legacyM4:{active:true,routeAccepted:true,routingRevision:"30"}}};
 const settle=(authority,pending,snapshot)=>
   stripVM.sourceQueueReadback({authority,pending,snapshot});
 assert.equal(settle("CHANNEL_ID",nativePending,nativeSnapshot),"CONFIRMED");
 assert.equal(settle("M4",m4Pending,m4Snapshot),"CONFIRMED");
 for(const [authority,pending,snapshot] of [
   ["CHANNEL_ID",nativePending,nativeSnapshot],["M4",m4Pending,m4Snapshot]
 ]){
   assert.equal(settle(authority,null,snapshot),"NONE");
   assert.equal(settle(authority,{...pending,acked:false},snapshot),"PENDING_ACK",
     "independently installed PCM must not bypass the ACK");
   assert.equal(settle(authority,pending,{...snapshot,
     device:{epoch:"99"}}),"STALE","device lease must match exactly");
 }
 const wrong=clone(m4Snapshot);
 wrong.raw.mixerSession.channels[4].source.left=1;
 assert.equal(settle("M4",m4Pending,wrong),"PENDING");
 const stereoWrong=clone(nativeSnapshot);
 stereoWrong.graph.channels[0].source.portIdRight="in:4";
 assert.equal(settle("CHANNEL_ID",nativePending,stereoWrong),"PENDING");
 assert.equal(settle("UNKNOWN",m4Pending,m4Snapshot),"STALE",
   "unrecognized authority cannot invent PCM");
 assert.equal(settle("UNKNOWN",null,null),"NONE");
 const bridge=read("web/approved-native-bridge.js");
 assert.equal((bridge.match(/function settleSourceReadback\(/g)||[]).length,1);
 assert.equal((bridge.match(/stripVM\.sourceQueueReadback\(/g)||[]).length,1);
 assert.doesNotMatch(bridge,/function settleNativeSourceReadback\(/);
 assert.doesNotMatch(bridge,/function settleM4SourceReadback\(/);
 assert.match(bridge,/settleSourceReadback\("CHANNEL_ID",next\)/);
 assert.match(bridge,/settleSourceReadback\("M4",next\)/);
 assert.match(bridge,/settleSourceReadback\(authority,controller\?\.state\(\)\?\.current\)/,
   "one source ACK handler can verify either previously installed PCM snapshot");
 assert.match(bridge,/handleSourceAck\("CHANNEL_ID",ack\)/);
 assert.match(bridge,/if\(handleSourceAck\("M4",ack\)\)return/);
 assert.doesNotMatch(stripVM.sourceQueueReadback.toString(),
   /document\.|window\.|backend|setTimeout|localStorage/);
});

test("MV-14 one pure dispatch head preserves FIFO and exact identity across ChannelId and M4",()=>{
 const id="9007199254740993",id2="9007199254740994";
 const epoch="18446744073709551610";
 const options=[{label:"None",mode:"NONE",left:-1,right:-1},
  {label:"IN 1 mono",mode:"MONO",left:0,right:-1},
  {label:"IN 3/4 stereo",mode:"STEREO",left:2,right:3}];
 const entry=(choice=options[1],at=epoch)=>({epoch:at,choice});
 const hardware={device:{epoch},raw:{mixerSession:{authority:"CHANNEL_ID"},
   legacyM4:{routingRevision:"29"}}};
 const native={admitted:true,retained:false,device:{epoch},
   graph:{revision:"9223372036854775808",rosterRevision:"9223372036854775809",
    channels:[{id,role:"INPUT"},{id:id2,role:"INPUT"}]}};
 const queue=new Map([[id,entry(options[2])],[id2,entry()]]);
 const original=JSON.stringify([...queue]);
 const decide=(changes={})=>stripVM.sourceQueueHead({
   authority:"CHANNEL_ID",queue,hardware,native,choices:options,
   deviceReady:true,...changes});
 assert.deepEqual(decide(),{kind:"SEND",key:id,entry:queue.get(id),
   action:"routingNativeSourcePatch",payload:{channelId:id,deviceEpoch:epoch,
     graphRevision:native.graph.revision,rosterRevision:native.graph.rosterRevision,
     mode:"STEREO",left:2,right:3}});
 assert.equal(JSON.stringify([...queue]),original,"pure selector cannot consume Map");
 assert.equal(decide({active:{requestId:"old"}}).kind,"IDLE");
 assert.equal(decide({native:{...native,admitted:false}}).kind,"WAIT");
 assert.equal(decide({hardware:{...hardware,raw:{
   ...hardware.raw,mixerSession:{authority:"M4"}}}}).kind,"WAIT");
 assert.equal(decide({hardware:{...hardware,device:{epoch:"99"}}}).kind,"DROP");
 assert.equal(decide({choices:[options[0]]}).kind,"DROP");
 const stale=new Map([[id,entry(options[1],"91")],[id2,entry()]]);
 const head=stripVM.sourceQueueHead({authority:"CHANNEL_ID",queue:stale,
   hardware,native,choices:options,deviceReady:true});
 assert.deepEqual(head,{kind:"DROP",key:id});
 assert.equal(stale.size,2,"VM plan may not erase drafts");
 stale.delete(head.key);
 assert.equal(stripVM.sourceQueueHead({authority:"CHANNEL_ID",queue:stale,
   hardware,native,choices:options,deviceReady:true}).kind,"SEND",
   "bad first edit cannot starve another channel");

 const m4Hardware={device:{epoch:"71"},raw:{
   mixerSession:{authority:"NONE"},legacyM4:{routingRevision:"29"}}};
 const m4Queue=new Map([[4,entry(options[1],"71")],[5,entry(options[2],"71")]]);
 const m4=stripVM.sourceQueueHead({authority:"M4",queue:m4Queue,
   hardware:m4Hardware,native:null,choices:options,deviceReady:true});
 assert.deepEqual(m4,{kind:"SEND",key:4,entry:m4Queue.get(4),
   action:"routingSourceSwitch",payload:{
    channel:4,mode:"MONO",left:0,right:-1,deviceEpoch:71,revision:29}},
   "prepared NONE first sound must still dispatch via real M4");
 assert.equal(m4Queue.size,2);
 const crossRealmQueue=require("node:vm").runInNewContext(
   'new Map([[4,{epoch:"71",choice:{label:"IN 1 mono",mode:"MONO",left:0,right:-1}}]])');
 assert.equal(stripVM.sourceQueueHead({authority:"M4",queue:crossRealmQueue,
   hardware:m4Hardware,native:null,choices:options,deviceReady:true}).kind,
   "SEND","cross-realm WebView-like Map must not silently drop physical INPUT");
 assert.deepEqual(stripVM.sourceQueueHead({authority:"M4",queue:m4Queue,
   hardware:m4Hardware,native:null,choices:options,deviceReady:false}),
   {kind:"DROP_ALL"},"old M4 pins must be discarded on device loss");
 assert.equal(stripVM.sourceQueueHead({authority:"M4",queue:m4Queue,
   hardware:{...m4Hardware,raw:{...m4Hardware.raw,
     mixerSession:{authority:"CHANNEL_ID"}}},native,choices:options,
   deviceReady:true}).kind,"DROP");
 assert.equal(stripVM.sourceQueueHead({authority:"INVALID",queue:m4Queue,
   hardware:m4Hardware,choices:options,deviceReady:true}).kind,"WAIT");
 assert.equal(stripVM.sourceQueueHead({authority:"M4",queue:new Map(),
   hardware:m4Hardware,choices:options,deviceReady:true}).kind,"IDLE");

 const bridge=read("web/approved-native-bridge.js");
 const begin=bridge.indexOf("  function dispatchSourceQueue(authority){");
 const end=bridge.indexOf("  // MV-15: one UI settlement owner",begin);
 const body=bridge.slice(begin,end);
 assert.ok(begin>0&&end>begin);
 assert.match(body,/while\(queue\.size\)/);
 assert.match(body,/queue\.delete\(plan\.key\)/);
 assert.match(body,/if\(plan\.kind==="DROP_ALL"\)\{queue\.clear\(\);return;\}/);
 assert.match(body,/stripVM\.sourceQueueRequestLease\(\{/);
 assert.match(body,/setTimeout\(\(\)=>handleSourceTimeout\(authority,requestId\),8000\)/);
 assert.match(bridge,/nativeUiSourcePending=null;[\s\S]*?INPUT timeout/);
 assert.match(body,/backend\.emitEvent\("arMixAction",\{action:plan\.action,requestId/);
 assert.match(body,/controller\?\.subscribe\(\)/);
 assert.match(bridge,/function dispatchNativeSource\(\)\{\s*dispatchSourceQueue\("CHANNEL_ID"\)/);
 assert.match(bridge,/function dispatchQueuedSource\(\)\{\s*dispatchSourceQueue\("M4"\)/);
 assert.equal((bridge.match(/function dispatchSourceQueue\(/g)||[]).length,1);
 assert.equal((body.match(/backend\.emitEvent\("arMixAction",\{action:plan\.action/g)||[]).length,1,
   "one physical INPUT native emit path for both queues");
 assert.doesNotMatch(stripVM.sourceQueueHead.toString(),
   /backend|window\.|document\.|setTimeout|AudioContext/);
});

test("MV-13 M4 input ACK cannot free source queue until independently accepted PCM readback",()=>{
 const choice={label:"Stereo · IN 3/4",mode:"STEREO",left:2,right:3};
 const pending={requestId:501,channel:4,choice,epoch:71,revision:29,acked:false};
 const channels=Array.from({length:10},(_,i)=>({id:"m4:"+String(i+1).padStart(2,"0"),
   role:"INPUT",source:{mode:"NONE",left:-1,right:-1}}));
 const snapshot={admitted:false,retained:false,device:{epoch:"71"},
   raw:{legacyM4:{active:true,routeAccepted:true,routingRevision:"30"},
     mixerSession:{authority:"M4",deviceEpoch:"71",routeRevision:"30",channels}}};
 const check=(p=pending,s=snapshot)=>stripVM.m4SourceReadbackSettlement(p,s);
 assert.equal(check(null),"NONE");
 assert.equal(check(),"PENDING_ACK","fresh PCM values cannot bypass a pending ACK");
 const preparing=clone(snapshot);
 preparing.raw.mixerSession.authority="NONE";
 preparing.raw.mixerSession.channels=[];
 preparing.raw.legacyM4.routeAccepted=false;
 assert.equal(check(pending,preparing),"PENDING_ACK",
   "first M4 source waits while native producer is prepared but not admitted");
 const acked={...pending,acked:true,ackRevision:"30"};
 assert.equal(check(acked,preparing),"PENDING",
   "first-source ACK may race before M4 PCM authority is published");
 assert.equal(check(acked),"PENDING","a matching ACK is still not PCM proof");
 const installed=clone(snapshot);
 installed.raw.mixerSession.channels[4].source={mode:"STEREO",left:2,right:3};
 assert.equal(check(acked,installed),"CONFIRMED");
 assert.deepEqual(snapshot.raw.mixerSession.channels[4].source,
   {mode:"NONE",left:-1,right:-1},"projection must be immutable");
 for(const input of [
   {mode:"STEREO",left:2,right:4},{mode:"MONO",left:2,right:-1},
   {mode:"STEREO",left:3,right:4},{mode:"NONE",left:-1,right:-1}
 ]){
   const mismatch=clone(installed);
   mismatch.raw.mixerSession.channels[4].source=input;
   assert.equal(check(acked,mismatch),"PENDING",JSON.stringify(input));
 }
 for(const [description,change] of [
   ["old callback revision",s=>{s.raw.mixerSession.routeRevision="29";
       s.raw.legacyM4.routingRevision="29";}],
   ["inconsistent legacy revision",s=>{s.raw.legacyM4.routingRevision="31";}],
   ["unprepared PCM",s=>{s.raw.legacyM4.routeAccepted=false;}],
   ["missing native source",s=>{delete s.raw.mixerSession.channels[4].source;}],
   ["wrong lane",s=>{s.raw.mixerSession.channels[4].role="MAIN";}],
 ]){
   const updated=clone(installed);change(updated);
   assert.equal(check(acked,updated),description==="wrong lane"?"STALE":"PENDING",
     description);
 }
 for(const mutate of [
   s=>{s.device.epoch="72";},
   s=>{s.raw.mixerSession.authority="CHANNEL_ID";},
   s=>{s.raw.mixerSession.deviceEpoch="70";},
   s=>{s.retained=true;},
 ]){
   const replaced=clone(installed);mutate(replaced);
   assert.equal(check(acked,replaced),"STALE",
     "device/authority changes cannot confirm the old queued input");
 }
 assert.equal(check({...acked,ackRevision:"29"},installed),"PENDING",
   "same-revision ACK is not later installed PCM");
 assert.equal(check({...acked,ackRevision:"31"},installed),"PENDING",
   "earlier M4 snapshot cannot prove a newer ACK");
 assert.equal(check({...acked,acked:false},installed),"PENDING_ACK");
 const bridge=read("web/approved-native-bridge.js");
 const ackStart=bridge.indexOf('backend.addEventListener("arMixSourceSwitchAck"');
 const ackEnd=bridge.indexOf('backend.addEventListener("arMixM4ControlAck"',ackStart);
 const ackBody=bridge.slice(ackStart,ackEnd);
 assert.match(ackBody,/handleSourceAck\("M4",ack\)/);
 assert.doesNotMatch(ackBody,/nativeUiSourcePending\.acked=true/,
   "in-place M4 ACK mutation must be removed from the native bridge");
 assert.match(bridge,/stripVM\.sourceQueueAckTransition\(\{authority,pending,ack\}\)/);
 assert.match(bridge,/stripVM\.sourceQueueReadback\(\{authority,pending,snapshot:next\}\)/);
 assert.match(bridge,/settleSourceReadback\("M4",next\)/);
 assert.equal((bridge.match(/arMixSourceSwitchAck/g)||[]).length,1,
   "one physical M4 ACK owner, no new transport/controller");
 assert.doesNotMatch(stripVM.m4SourceReadbackSettlement.toString(),
   /window\.|document\.|backend|setTimeout/);
});

test("GUI-4 Channel Strip PRIMARY OUT extracts exact native EdgeId without touching SEND",()=>{
 const source="9007199254740993",bus="9007199254741001",
   main="18446744073709551615";
 const primary="9223372036854775808",send="9223372036854775809";
 const snapshot={admitted:true,retained:false,graph:{
  mainChannelId:main,capabilities:{supportedOps:["APPLY_WIRE_TRANSACTION"]},
  channels:[
   {id:source,role:"INPUT",primaryOutId:bus},
   {id:bus,role:"GROUP"},
   {id:main,role:"MAIN"}],
  edges:[
   {edgeId:primary,enabled:true,primaryOutput:true,from:source,to:bus},
   {edgeId:send,enabled:true,primaryOutput:false,from:source,to:main}]
 }};
 const saved=JSON.stringify(snapshot);
 const none=stripVM.primaryOutMutation(snapshot,source,"NONE",[]);
 assert.deepEqual(none,{kind:"MUTATION",change:{
  op:"REMOVE",from:source,edgeId:primary}});
 assert.equal(none.change.edgeId!==send,true);
 // Old accepted v6 readback lacks primaryOutput, but has exact primaryOutId
 // and unity gain. Never infer PRIMARY from an explicit parallel SEND.
 const legacy=clone(snapshot);
 delete legacy.graph.edges[0].primaryOutput;
 legacy.graph.edges[0].gainLinear=1;
 assert.deepEqual(stripVM.primaryOutMutation(legacy,source,"NONE",[]),none);
 legacy.graph.edges[0].primaryOutput=false;
 assert.equal(stripVM.primaryOutMutation(legacy,source,"NONE",[]).kind,
   "UNAVAILABLE","an explicit SEND can never become a primary OUT");
 legacy.graph.edges[0].primaryOutput="false";
 assert.equal(stripVM.primaryOutMutation(legacy,source,"NONE",[]).kind,
   "UNAVAILABLE","malformed explicit PRIMARY flag cannot authorize removal");
 delete legacy.graph.edges[0].primaryOutput;
 legacy.graph.edges[0].gainLinear=0.9;
 assert.equal(stripVM.primaryOutMutation(legacy,source,"NONE",[]).kind,
   "UNAVAILABLE","unflagged non-unity is not a primary OUT");
 const redirect=stripVM.primaryOutMutation(snapshot,source,"MAIN",[]);
 assert.deepEqual(redirect,{kind:"MUTATION",change:{
  op:"ROUTE_ONLY",from:source,to:main,tap:"POST_FADER",gainLinear:1}});
 const same=stripVM.primaryOutMutation(snapshot,source,"BUS A",[
  {id:bus,label:"BUS A"}]);
 assert.deepEqual(same,{kind:"NO_CHANGE"});
 const other="9007199254741011";
 const cloned=clone(snapshot);
 cloned.graph.channels.push({id:other,role:"GROUP"});
 const toGroup=stripVM.primaryOutMutation(cloned,source,"BUS B",[
  {id:other,label:"BUS B"}]);
 assert.equal(toGroup.change.to,other);
 assert.equal(JSON.stringify(snapshot),saved,
  "pure projection must leave both PRIMARY and parallel SEND untouched");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/stripVM\.primaryOutMutation\(/);
 assert.match(bridge,/controller\.intent\("APPLY_WIRE_TRANSACTION",channelId,/);
 const primaryOutOnly=bridge.slice(bridge.indexOf("function requestPrimaryOut(t,target)"),
  bridge.indexOf("const pendingNativeSources"));
 assert.doesNotMatch(primaryOutOnly,/const edge=current\.graph\.edges\.find/,
  "source-specific PRIMARY edge search must not remain duplicated in bridge");
});
test("GUI-4 primary OUT rejects ambiguous, stale, mismapped or unadmitted native graph",()=>{
 const source="9007199254740993",bus="9007199254741001";
 const main="18446744073709551615";
 const primary="9223372036854775808";
 const base={admitted:true,retained:false,graph:{
  mainChannelId:main,capabilities:{supportedOps:["APPLY_WIRE_TRANSACTION"]},
  channels:[{id:source,role:"INPUT",primaryOutId:bus},
            {id:bus,role:"GROUP"},{id:main,role:"MAIN"}],
  edges:[{edgeId:primary,enabled:true,primaryOutput:true,from:source,to:bus}]
 }};
 const run=(s,t="NONE",targets=[])=>stripVM.primaryOutMutation(s,source,t,targets);
 for(const invalid of [
  {...base,admitted:false},{...base,retained:true},
  {...base,graph:{...base.graph,capabilities:{supportedOps:[]}}},
  {...base,graph:{...base.graph,edges:[...base.graph.edges,
   {...base.graph.edges[0],edgeId:"9223372036854775809"}]}},
  {...base,graph:{...base.graph,edges:[{...base.graph.edges[0],
   edgeId:"9223372036854775809",primaryOutput:false}]}},
  {...base,graph:{...base.graph,channels:[
   {...base.graph.channels[0],role:"MAIN"},...base.graph.channels.slice(1)]}},
  {...base,graph:{...base.graph,mainChannelId:"18446744073709551616"}}
 ])assert.equal(run(invalid).kind,"UNAVAILABLE");
 assert.equal(run(base,"BUS A",[{id:bus,label:"BUS A"},
   {id:main,label:"BUS A"}]).kind,"UNAVAILABLE",
   "ambiguous label must not point to an arbitrary physical destination");
 assert.equal(run(base,"BUS C",[{id:"9007199254741011",
   label:"BUS C"}]).kind,"UNAVAILABLE");
 assert.equal(run(base,"9007199254740993",[]).kind,"UNAVAILABLE");
 assert.equal(stripVM.primaryOutMutation(base,9007199254740993,"MAIN").kind,
  "UNAVAILABLE","rounded numeric ChannelId must fail closed");
 assert.equal(run({...base,graph:{...base.graph,channels:[
  {...base.graph.channels[0],primaryOutId:"18446744073709551616"},
  ...base.graph.channels.slice(1)]}}).kind,"UNAVAILABLE");
});

test("GUI-2 pure FX power readback preserves independent rack/slot/global states and labels",()=>{
 const channel={id:"9007199254740993",fxRackBypassed:false,inserts:[
  {id:"instance-1",bypassed:false,latencySamples:96},
  {id:"instance-2",bypassed:true,latencyChanged:true}
 ]};
 const original=JSON.stringify(channel);
 const normal=fxRackVM.powerReadback(channel,false,"Vocal");
 assert.equal(normal.enabled,true);
 assert.equal(normal.globalEnabled,true);
 assert.equal(normal.chainBypassed,false);
 assert.equal(normal.headerPressed,"true");
 assert.equal(normal.slots[0].effective,true);
 assert.equal(normal.slots[1].effective,false);
 assert.equal(normal.slots[0].latencyTitle,"Plugin delay: 96 samples · compensated");
 assert.match(normal.slots[1].latencyTitle,/Plugin delay changed/);
 assert.equal(normal.slots[0].label,"Bypass only FX insert 1 in Vocal");
 assert.equal(normal.slots[1].title,"Enable only insert 2");
 const rackOff=fxRackVM.powerReadback({...channel,fxRackBypassed:true},false,"Vocal");
 assert.equal(rackOff.enabled,false);
 assert.equal(rackOff.slots[0].bypassed,false,
  "rack bypass must never become an insert bypass");
 assert.equal(rackOff.slots[0].effective,false);
 const globalOff=fxRackVM.powerReadback(channel,true,"Vocal");
 assert.equal(globalOff.globalEnabled,false);
 assert.equal(globalOff.slots[0].effective,false);
 assert.equal(globalOff.slots[0].bypassed,false);
 assert.equal(globalOff.headerPressed,"false");
 assert.equal(JSON.stringify(channel),original,"projection cannot mutate native graph");

 // Missing/incomplete readback, including wrong type, is NOT a default ON.
 assert.equal(fxRackVM.powerReadback({},false,"Vocal"),null);
 assert.equal(fxRackVM.powerReadback(channel,undefined,"Vocal"),null);
 assert.equal(fxRackVM.powerReadback({...channel,
  inserts:[{id:"instance-1",bypassed:"false"}]},false,"Vocal"),null);
 assert.equal(fxRackVM.powerReadback({...channel,
  inserts:[{id:"",bypassed:false}]},false,"Vocal"),null);
});
test("GUI-2 exact mounted FX instance/power identity rejects stale, duplicate or missing rows",()=>{
 const data=fxRackVM.powerReadback({fxRackBypassed:false,inserts:[
  {id:"instance-1",bypassed:false},
  {id:"instance-2",bypassed:true}
 ]},false,"Vocal");
 const good=[
  {index:0,insertId:"instance-1",hasPower:true},
  {index:1,insertId:"instance-2",hasPower:true}
 ];
 assert.equal(fxRackVM.mountedRackMatches(data,good,true),true);
 assert.equal(fxRackVM.mountedRackMatches(data,[good[1],good[0]],true),true,
  "DOM row order is not allowed to substitute native slot identity");
 assert.equal(fxRackVM.mountedRackMatches(data,good,false),false);
 assert.equal(fxRackVM.mountedRackMatches(data,good.slice(0,1),true),false);
 assert.equal(fxRackVM.mountedRackMatches(data,[good[0],good[0]],true),false);
 assert.equal(fxRackVM.mountedRackMatches(data,[good[0],
  {...good[1],insertId:"new-plugin"}],true),false);
 assert.equal(fxRackVM.mountedRackMatches(data,[good[0],
  {...good[1],hasPower:false}],true),false);
 assert.equal(fxRackVM.mountedRackMatches(data,null,true),false);
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/fxRackVM\.powerReadback\(/);
 assert.match(bridge,/fxRackVM\.mountedRackMatches\(/);
 assert.doesNotMatch(bridge,/const effective=enabled&&globalEnabled&&!insert\.bypassed/,
  "old duplicated FX bypass projection must be removed from DOM bridge");
});
test("MV-4 native WIRE translation keeps exact uint64 IDs and one acknowledged dispatcher",()=>{
 const ids=new Map([[1,"9007199254740993"],[2,"18446744073709551615"],
  [3,"9007199254740999"]]);
 const edges=new Map([[41,"9223372036854775808"],[42,"9223372036854775809"]]);
 const before=JSON.stringify([...ids,...edges]);
 const preview=[{type:"CONNECT",sourceId:1,destId:3,mode:"send",levelDb:-12},
  {type:"REMOVE",sourceId:1,edgeId:41}];
 const cmd=wireVM.nativeTransaction(preview,ids,edges);
 assert.equal(cmd.ok,true);
 assert.equal(cmd.source,"9007199254740993");
 assert.deepEqual(cmd.changes,[
  {op:"CONNECT",from:"9007199254740993",to:"9007199254740999",
   tap:"POST_FADER",gainLinear:Math.pow(10,-12/20)},
  {op:"REMOVE",from:"9007199254740993",edgeId:"9223372036854775808"}
 ]);
 assert.equal(JSON.stringify([...ids,...edges]),before);
 assert.equal(preview[0].levelDb,-12,"view gesture must remain unmodified");
 const route=wireVM.nativeTransaction([{type:"ROUTE_ONLY",
  sourceId:1,destId:2}],ids,edges);
 assert.equal(route.ok,true);
 assert.equal(route.changes[0].gainLinear,1);
 assert.equal(route.changes[0].tap,"POST_FADER");
 const retarget=wireVM.nativeTransaction([{type:"RETARGET",
  sourceId:1,destId:3,edgeId:42}],ids,edges);
 assert.deepEqual(retarget.changes,[{op:"RETARGET",from:"9007199254740993",
  to:"9007199254740999",edgeId:"9223372036854775809"}]);
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/wireVM\.nativeTransaction\(ops,nativeByUiId,wireByUiId\)/);
 assert.match(bridge,/wireVM\.compactSendGesture\(ops,wireByUiId\)/);
 assert.match(bridge,/controller\.intent\("APPLY_WIRE_TRANSACTION",source,\{changes\}\)/);
 assert.doesNotMatch(bridge,/const changes=\[\];\s*for \(const op of ops\)/,
  "bridge must not duplicate the moved WIRE command mapper");
});
test("MV-4 reject malformed, missing, duplicate and cross-source native WIRE gestures before PCM intent",()=>{
 const ids=new Map([[1,"9007199254740993"],[2,"18446744073709551615"],
  [3,"9007199254740999"],[4,"9007199254740994"]]);
 const edges=new Map([[41,"9223372036854775808"]]);
 const err=(ops,channels=ids,edgeIds=edges)=>
  wireVM.nativeTransaction(ops,channels,edgeIds).error;
 assert.equal(err(null),"INVALID_WIRE_OPERATION");
 assert.equal(err([]),"INVALID_WIRE_OPERATION");
 assert.equal(err([null]),"INVALID_WIRE_OPERATION");
 assert.equal(err([{type:"RENAME",sourceId:1,destId:2}]),
  "UNSUPPORTED_WIRE_OPERATION");
 assert.equal(err([{type:"CONNECT",sourceId:1,destId:2,mode:"splitPre"}]),
  "UNSUPPORTED_WIRE_OPERATION");
 assert.equal(err([{type:"CONNECT",sourceId:5,destId:2}]),
  "MAPPED_IDENTITY_MISSING");
 assert.equal(err([{type:"CONNECT",sourceId:1,destId:5}]),
  "MAPPED_IDENTITY_MISSING");
 assert.equal(err([{type:"REMOVE",sourceId:1,edgeId:999}]),
  "MAPPED_IDENTITY_MISSING");
 assert.equal(err([{type:"REMOVE",sourceId:1}]),"MAPPED_IDENTITY_MISSING");
 assert.equal(err([{type:"RETARGET",sourceId:1,edgeId:41}]),
  "MAPPED_IDENTITY_MISSING");
 assert.equal(err([{type:"CONNECT",sourceId:1,destId:2,levelDb:NaN}]),
  "INVALID_WIRE_GAIN");
 assert.equal(err([{type:"CONNECT",sourceId:1,destId:2,levelDb:Infinity}]),
  "INVALID_WIRE_GAIN");
 assert.equal(err([{type:"CONNECT",sourceId:1,destId:2,levelDb:"-12"}]),
  "INVALID_WIRE_GAIN");
 assert.equal(err([{type:"CONNECT",sourceId:1,destId:2},
  {type:"CONNECT",sourceId:4,destId:2}]),"MULTI_SOURCE");
 const badIds=new Map(ids);badIds.set(1,9007199254740993);
 assert.equal(err([{type:"CONNECT",sourceId:1,destId:2}],badIds),
  "MAPPED_IDENTITY_MISSING","rounded Number is not a canonical native ID");
 badIds.set(1,"018");
 assert.equal(err([{type:"CONNECT",sourceId:1,destId:2}],badIds),
  "MAPPED_IDENTITY_MISSING","leading-zero identity rejected");
 badIds.set(1,"18446744073709551616");
 assert.equal(err([{type:"CONNECT",sourceId:1,destId:2}],badIds),
  "MAPPED_IDENTITY_MISSING","uint64 overflow rejected");
 assert.equal(wireVM.nativeTransaction([{type:"CONNECT",sourceId:1,destId:2,
  levelDb:-200}],ids,edges).changes[0].gainLinear,Math.pow(10,-60/20));
 assert.equal(wireVM.nativeTransaction([{type:"CONNECT",sourceId:1,destId:2,
  levelDb:300}],ids,edges).changes[0].gainLinear,Math.pow(10,6/20));
});
test("MV-4 SOF draft CONNECT+GAIN becomes one atomic SEND without invented native EdgeId",()=>{
 const ids=new Map([[41,"9223372036854775808"]]);
 const gestures=[{type:"CONNECT",sourceId:1,destId:3,edgeId:undefined},
  {type:"GAIN",sourceId:1,edgeId:999,levelDb:-9}];
 const before=JSON.stringify(gestures);
 const compact=wireVM.compactSendGesture(gestures,ids);
 assert.deepEqual(compact,[{...gestures[0],mode:"send",levelDb:-9}]);
 assert.equal(JSON.stringify(gestures),before,"draft UI is not authoritative");
 assert.notStrictEqual(compact[0],gestures[0]);
 for(const invalid of [
  [{...gestures[0]},{...gestures[1],edgeId:41}],
  [{...gestures[0]},{...gestures[1],sourceId:2}],
  [{...gestures[0]},{...gestures[1],levelDb:NaN}],
  [{...gestures[0]},{...gestures[1],levelDb:-61}],
  [{...gestures[0]},{...gestures[1],levelDb:7}]
 ])assert.strictEqual(wireVM.compactSendGesture(invalid,ids),invalid);
});
test("GUI-3 WIRE selector keeps exact PRIMARY OUT vs parallel SEND identity, even to MAIN",()=>{
 const source="9007199254740993",main="18446744073709551615",bus="9007199254740999";
 const primary={edgeId:"9007199254741000",from:source,to:bus,enabled:true,
  primaryOutput:true,gainLinear:1,tap:"POST_FADER",sendEnabled:true};
 const busOut=wireVM.edgePresentation(primary,bus,main);
 assert.equal(busOut.kind,"primary");assert.equal(busOut.mode,"routeOnly");
 assert.equal(busOut.levelDb,0);
 const mainSend={...primary,edgeId:"9007199254741001",to:main,
  primaryOutput:false,gainLinear:.5,sendEnabled:false};
 const independent=wireVM.edgePresentation(mainSend,main,main);
 assert.equal(independent.kind,"send",
  "parallel SEND to MAIN cannot be promoted by matching primaryOutId");
 assert.equal(independent.mode,"send");
 assert.equal(independent.levelDb,-6);
 assert.equal(independent.sendEnabled,false);
 assert.equal(wireVM.edgePresentation({...primary,gainLinear:.7},bus,main),null,
  "primary OUT is unity; incorrect PCM readback is rejected");
 assert.equal(wireVM.edgePresentation(primary,main,main),null,
  "stale primary destination must not be repainted as verified");
 const old={...primary};delete old.primaryOutput;
 assert.equal(wireVM.edgePresentation(old,bus,main).kind,"primary",
  "legacy readback without explicit primaryOutput keeps existing fallback");
 const split={...mainSend,tap:"SPLIT_PRE",tapStage:"SPLIT_PRE",to:bus};
 assert.equal(wireVM.edgePresentation(split,main,main).mode,"splitPre");
 const updated={...split,tapStage:"PRE_FADER"};
 assert.equal(wireVM.edgePresentation(updated,main,main).nativeTap,"PRE_FADER",
  "actual native post-insert PRE_FADER wins over old PRE_FX tap");
 assert.equal(wireVM.gainDb(0),-60);
 assert.equal(wireVM.gainDb(10),6);
 assert.equal(wireVM.gainDb(-1),null);
});
test("GUI-3 WIRE gain/readback and native edit eligibility are fail-closed by exact EdgeId",()=>{
 const source="9007199254740993",other="9007199254740994",
  edgeId="9223372036854775808",main="18446744073709551615";
 const send={edgeId,from:source,to:main,enabled:true,
  primaryOutput:false,gainLinear:.5,tap:"POST_FADER"};
 const primary={...send,edgeId:"9223372036854775809",primaryOutput:true};
 const graph={capabilities:{supportedOps:["SET_WIRE_GAIN","APPLY_WIRE_TRANSACTION",
  "SET_SEND_ENABLED"]},edges:[send,primary]};
 const snap={admitted:true,retained:false,graph};
 const previous=JSON.stringify(snap);
 assert.equal(wireVM.acceptedParallelEdge(snap,true,source,edgeId,"SET_WIRE_GAIN"),send);
 assert.equal(wireVM.acceptedParallelEdge(snap,true,other,edgeId,"SET_WIRE_GAIN"),null);
 assert.equal(wireVM.acceptedParallelEdge(snap,false,source,edgeId,"SET_WIRE_GAIN"),null);
 assert.equal(wireVM.acceptedParallelEdge({...snap,retained:true},true,
  source,edgeId,"SET_WIRE_GAIN"),null);
 assert.equal(wireVM.acceptedParallelEdge({...snap,admitted:false},true,
  source,edgeId,"SET_WIRE_GAIN"),null);
 assert.equal(wireVM.acceptedParallelEdge(snap,true,source,edgeId,"MISSING_OP"),null);
 assert.equal(wireVM.parallelEdge(graph,source,primary.edgeId),null);
 assert.equal(wireVM.parallelEdge({...graph,edges:[send,send]},source,edgeId),null);
 assert.equal(wireVM.parallelEdge(graph,source,edgeId,other),null);
 const mapping=new Map([[edgeId,42],[primary.edgeId,41]]);
 const pending=new Map([[edgeId,{requestId:"gain-1"}]]);
 const rows=wireVM.gainReadback(graph.edges,mapping,pending);
 assert.deepEqual(rows,[{id:42,pending:true,levelDb:-6}]);
 assert.deepEqual(wireVM.gainReadback(graph.edges,new Map(),pending),[],
  "unknown native EdgeId may never invent a new view-id");
 assert.equal(JSON.stringify(snap),previous,"selectors never mutate native snapshot");
 assert.equal(wireVM.canEditTap(send,{supported:true}),true);
 assert.equal(wireVM.canEditTap(send,{supported:true,pending:true}),false);
 assert.equal(wireVM.canEditTap(send,{supported:false}),false);
 assert.equal(wireVM.canEditTap({...send,tap:"PRE_FX"},{supported:true}),false);
 assert.equal(wireVM.gestureTap("split"),"PRE_FADER");
 assert.equal(wireVM.gestureTap("send"),"POST_FADER");
 assert.equal(wireVM.gestureTap("routeOnly"),null);
 assert.equal(wireVM.sofAvailable(snap,true),true);
 assert.equal(wireVM.sofAvailable({...snap,retained:true},true),false);
 assert.equal(wireVM.sofAvailable(snap,false),false);
 assert.equal(wireVM.sofAvailable({...snap,graph:{...graph,capabilities:{
  supportedOps:["SET_WIRE_GAIN"]}}},true),false);
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/wireVM\.gainReadback\(/);
 assert.match(bridge,/wireVM\.parallelEdge\(/);
 assert.match(bridge,/wireVM\.canEditTap\(/);
 assert.match(bridge,/wireVM\.sofAvailable\(/);
 assert.doesNotMatch(bridge,/const kind=isPrimary\?"primary"/,
  "old WIRE presentation policy must be REMOVED, not copied");
});
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
test("WIRE inspector controls PRIMARY BUS and PARALLEL MAIN by actual EdgeId, with native readback only",()=>{
 const vm=require("node:vm"),{execFileSync}=require("node:child_process");
 const publicHtml=read("site/preview-v2/index.html");
 assert.doesNotMatch(publicHtml,/__ARMIX_NATIVE_M2__/,
  "public approved /preview/ must never own native control authority");
 const html=execFileSync("python3",["-c",
  "from scripts.build_approved_native_gui import native_html; from pathlib import Path; print(native_html(Path('site/preview-v2/index.html').read_text(encoding='utf-8')))"
 ],{
  encoding:"utf8",maxBuffer:10*1024*1024,
  // Windows runner's default Python stdout is often CP1252. The approved
  // GUI includes Unicode symbols (e.g. ◉), so explicitly encode THIS child
  // stream as UTF-8 rather than relying on host code page or a global CI knob.
  env:{...process.env,PYTHONIOENCODING:"utf-8"}
 });
 assert.ok(html.includes("◉"),"native assembled Unicode survives Python stdout on Windows");
 const bridge=read("web/approved-native-bridge.js");
 const builder=read("scripts/build_approved_native_gui.py");
 assert.match(bridge,/function admittedParallelWire\(uiSourceId,uiEdgeId,operation\)/);
 assert.match(bridge,/wireVM\.acceptedParallelEdge\(/);
 assert.match(read("web/approved-wire-view-model.js"),/e\.from===sourceId&&e\.edgeId===edgeId&&e\.primaryOutput===false/);
 assert.match(builder,/getElementById\('wireWsGainText'\)/);
 assert.match(builder,/!row\.pending/);
 const renderStart=html.indexOf("function renderWireWsInspector(){");
 const renderEnd=html.indexOf("function renderWireWorkspace(){",renderStart);
 assert.ok(renderStart>0&&renderEnd>renderStart);
 const primary={id:41,sourceId:1,destId:2,kind:"primary",mode:"routeOnly",
  nativeTap:"POST_FADER",levelDb:0,enabled:true};
 const parallel={id:42,sourceId:1,destId:3,kind:"send",mode:"send",
  nativeTap:"POST_FADER",levelDb:-6,enabled:true};
 const panel={hidden:false,innerHTML:""},readout={textContent:"-6 dB"};
 const inspectorGain={value:"-12",dataset:{wsGain:"42"},disabled:false};
 const observed=[];
 const gain=input=>input?.dataset?.wsGain!==undefined?input:null;
 const tap=input=>input?.dataset?.wsTap!==undefined?input:null;
 const handlers={};
 const scope={
  document:{getElementById(id){
    if(id==="wireWsInspector")return { ...panel, addEventListener(type,fn){handlers[type]=fn},
     get hidden(){return panel.hidden},set hidden(v){panel.hidden=v},
     get innerHTML(){return panel.innerHTML},set innerHTML(v){panel.innerHTML=v}};
    if(id==="wireWsGainText")return readout;
    if(id==="wireWsGain")return inspectorGain;
    return null;
  }},
  window:{__JUCE__:{backend:{}},__ARMIX_NATIVE_M2__:{
    canWireGain(edge,source){return edge===42&&source===1},
    canWireTap(edge,source){return edge===42&&source===1},
    consumeWire(changes){observed.push(changes)}
  }},
  wireWsOpen:true,wireWsInspect:{src:1,dst:2},wireWsRemoveConfirm:null,
  wireWsEditBefore:null,
  wireEdges:[primary,parallel],
  tracks:[{id:1,kind:"audio",name:"Vocal"},{id:2,kind:"bus",name:"Bus A"}],
  master:{id:3,kind:"master",name:"MAIN"},
  wireWsRoutes:()=>[],
  wireWsRouteKind:(src,dst)=>({kind:"wire",edge:src===1&&dst===2?primary:parallel}),
  wireWsLabel:id=>"CH "+id,escapeHtml:s=>String(s),
  wireSourceTrack:id=>({id}),wireGraphCommit(){throw Error("native must not mutate demo graph")},
  wireWsSnapshot(){throw Error("native must not take demo undo snapshot")},
  wireWsRecord(){throw Error("native must not create demo undo history")},
  render(){throw Error("native must not rebuild mixer DOM")},
  renderWireWorkspace(){throw Error("native must not rebuild inspector")},
  say:s=>observed.push(s)
 };
 vm.runInNewContext(html.slice(renderStart,renderEnd),scope);
 vm.runInNewContext("renderWireWsInspector()",scope);
 assert.match(panel.innerHTML,/PRIMARY · UNITY/);
 assert.match(panel.innerHTML,/data-ws-gain="41"[^>]+disabled/);
 scope.wireWsInspect={src:1,dst:3};
 vm.runInNewContext("renderWireWsInspector()",scope);
 assert.match(panel.innerHTML,/data-ws-gain="42"/);
 assert.doesNotMatch(panel.innerHTML,/data-ws-gain="42"[^>]+disabled/);
 assert.match(panel.innerHTML,/POST FADER/);
 const eventStart=html.indexOf("document.getElementById('wireWsInspector').addEventListener('focusin',");
 const eventEnd=html.indexOf("document.getElementById('wireLegend').addEventListener('change',",eventStart);
 assert.ok(eventStart>0&&eventEnd>eventStart);
 vm.runInNewContext(html.slice(eventStart,eventEnd),scope);
 const event=(element,selector)=>({target:{closest:name=>name===selector?element:null}});
 handlers.input(event({dataset:{wsGain:"41"},value:"-10"},"[data-ws-gain]"));
 assert.equal(observed.length,0,"PRIMARY BUS has no independent SEND gain");
 handlers.input(event({dataset:{wsGain:"42"},value:"-12"},"[data-ws-gain]"));
 assert.deepEqual(JSON.parse(JSON.stringify(observed[0])),[{
  type:"GAIN",sourceId:1,edgeId:42,levelDb:-12
 }],"PARALLEL MAIN has the exact writable EdgeId");
 assert.equal(readout.textContent,"PENDING · -12 dB",
  "input gesture is pending; never labels it accepted");
 const picker={dataset:{wsTap:"42"},value:"split"};
 handlers.change(event(picker,"[data-ws-tap]"));
 assert.equal(picker.value,"send","native tap selector waits for independent PCM readback");
 assert.deepEqual(JSON.parse(JSON.stringify(observed[1])),[{
  type:"MODE",sourceId:1,edgeId:42,mode:"split"
 }]);
 assert.match(observed[2],/awaiting accepted native PCM readback/);
 assert.equal(primary.levelDb,0);
 assert.equal(parallel.levelDb,-6,"reference WIRE graph never becomes native routing owner");
 // Execute the native compiled rollback method rather than a mock. It must
 // repair the same focused DOM control from the latest accepted PCM readback.
 // Rejection has no new snapshot: the original level remains -6, not -12.
 const rollbackStart=html.indexOf(" rollbackNativeWireInspector(rows){");
 const rollbackEnd=html.indexOf(" syncVerifiedM4Wires(rows,ids){",rollbackStart);
 assert.ok(rollbackStart>0&&rollbackEnd>rollbackStart,"native rollback method installed exactly once");
 const method=html.slice(rollbackStart,rollbackEnd).trim().replace(/,\s*$/,"");
 const rollback=vm.runInNewContext("({"+method+"})",scope);
 assert.equal(rollback.rollbackNativeWireInspector([{id:42,levelDb:-6}]),true);
 assert.equal(inspectorGain.value,"-6","failed native intent restores accepted gain without replacing the focused input");
 assert.equal(readout.textContent,"UNCHANGED · -6 dB","LCD cannot remain PENDING after a rejected gain");
 assert.equal(parallel.levelDb,-6,"no browser graph edit or optimistic audio state");
 assert.equal(rollback.rollbackNativeWireInspector([{id:42,levelDb:-6}]),false,
  "idempotent failure reconciliation must not repaint a settled LCD");
 // A stale EdgeId cannot silently roll back or write a different route.
 inspectorGain.dataset.wsGain="41";readout.textContent="PENDING · -8 dB";
 assert.equal(rollback.rollbackNativeWireInspector([{id:42,levelDb:-6}]),true);
 assert.equal(inspectorGain.disabled,true);
 assert.equal(readout.textContent,"SEND UNAVAILABLE");
 assert.match(bridge,/rollbackNativeWireInspector\?\.\(nativeWireGainReadback\(state.current\)\)/,
  "native control failure must invoke the same compiled inspector rollback");
});

test("native PRE_FADER WIRE inspector uses one exact SET_TAP PCM intent and accepted readback",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const preview=read("site/preview-v2/index.html");
 assert.match(bridge,/function requestNativeTap\(source,edgeId,tap\)/);
 assert.match(bridge,/ops\[0\]\.type==="MODE"/);
 assert.match(read("web/approved-wire-view-model.js"),
   /op:"SET_TAP",from:sourceId,edgeId,tap:targetTap/);
 assert.match(bridge,/wireVM\.nativeTapPlan\(/);
 assert.match(bridge,/wireVM\.edgePresentation\(/);
 assert.match(read("web/approved-wire-view-model.js"),
   /const tap=TAPS\.has\(edge\.tapStage\)\?edge\.tapStage:edge\.tap/);
 assert.match(preview,/edge\.nativeTap==='PRE_FADER'\?'PRE FADER'/);
 assert.match(preview,/edge\?\.nativeTap==='PRE_FADER'\?'split'/);
 assert.match(preview,/native\?'PRE FADER':'Pre FX'/);
 const s=acceptedFixture(),source=s.graph.channels[0].id;
 const bus={id:"9007199254740999",role:"GROUP",name:"BUS B",processingFormat:"STEREO",
   controls:{faderDb:0,pan:0,mute:false,solo:false},inserts:[],sends:[]};
 s.graph.channels.splice(1,0,bus);
 s.graph.destinations.push({id:bus.id,role:"GROUP"});
 delete s.graph.channels[0].primaryOutId;
 s.graph.edges[0]={edgeId:"9223372036854775808",from:source,to:bus.id,
   tap:"POST_FADER",gainLinear:.5,enabled:true,sendEnabled:true,
   primaryOutput:false,signalKind:"AUDIO"};
 const sent=[],c=protocol.createController({emit:(event,payload)=>sent.push({event,payload})});
 assert.equal(c.onSnapshot(clone(s)),true);
 const change={op:"SET_TAP",from:source,edgeId:s.graph.edges[0].edgeId,tap:"PRE_FADER"};
 const t=c.intent("APPLY_WIRE_TRANSACTION",source,{changes:[change]});
 assert.equal(t.ok,true);
 assert.deepEqual(sent.at(-1).payload.payload.changes,[change]);
 assert.equal(c.state().current.graph.edges[0].tap,"POST_FADER");
 assert.equal(c.onAck({requestId:t.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:s.device.epoch,graphRevision:"6",rosterRevision:"8"}),true);
 assert.equal(c.state().current.graph.edges[0].tap,"POST_FADER",
  "ACK alone cannot repaint a PRE_FADER send");
 const next=clone(s);next.graph.revision="6";next.graph.rosterRevision="8";
 next.graph.edges[0].tap="PRE_FADER";
 assert.equal(c.onSnapshot(next),true);
 assert.equal(c.state().pending,null);
 assert.equal(c.state().current.graph.edges[0].tap,"PRE_FADER");
 assert.equal(c.state().current.graph.edges[0].edgeId,change.edgeId);
 assert.equal(c.state().current.graph.edges[0].gainLinear,.5);
 assert.equal(c.state().current.graph.edges[0].sendEnabled,true);
});

test("native input Gain is a separate ChannelId scalar proven only by ACK plus inputGain readback",()=>{
 const s=acceptedFixture(),id=s.graph.channels[0].id;
 s.graph.capabilities.supportedOps.push("SET_INPUT_GAIN");
 s.graph.channels[0].controls.inputGainDb=0;
 const sent=[],ctl=protocol.createController({emit:(name,value)=>sent.push({name,value})});
 assert.equal(ctl.onSnapshot(s),true);
 const edit=ctl.intent("SET_INPUT_GAIN",id,{valueDb:6});
 assert.equal(edit.ok,true);
 assert.equal(sent.length,1);
 assert.equal(ctl.state().pending?.operation,"SET_INPUT_GAIN");
 assert.equal(ctl.onAck({requestId:edit.requestId,status:"ACCEPTED",
  deviceEpoch:s.device.epoch,graphRevision:s.graph.revision,
  rosterRevision:s.graph.rosterRevision,errorCode:"NONE"}),true);
 // An unchanged FADER is not proof that an INPUT GAIN changed.
 assert.equal(protocol.readbackProvesIntent(ctl.state().current,
  {operation:"SET_INPUT_GAIN",targetChannelId:id,payload:{valueDb:6}}),false);
 const actual=clone(s);actual.graph.channels[0].controls.inputGainDb=6;
 assert.equal(ctl.onSnapshot(actual),true);
 assert.equal(ctl.state().pending,null);
 assert.equal(ctl.state().current.graph.channels[0].controls.faderDb,-6);
 assert.equal(ctl.state().current.graph.channels[0].controls.inputGainDb,6);
 const malformed=clone(actual);malformed.graph.channels[0].controls.inputGainDb="NaN";
 assert.equal(protocol.normalizeSnapshot(malformed),null);
});

test("M5 native FX snapshot rejects malformed/duplicate lease tokens without losing accepted state",()=>{
 const good=acceptedFixture(),input=good.graph.channels[0].id;
 good.graph.channels[0].inserts=[{
   id:"native-lease-A",catalogId:"vendor.fx.catalog",name:"Vendor FX",bypassed:false
 }];
 const sent=[],statuses=[],controller=protocol.createController({
   emit:(event,payload)=>sent.push({event,payload}),
   onStatus:x=>statuses.push(x)
 });
 assert.equal(controller.onSnapshot(good),true);
 const accepted=controller.state().current;
 assert.ok(accepted.admitted);
 const mutations=[
   row=>{row.graph.channels[0].inserts=[null];},
   row=>{row.graph.channels[0].inserts=[{}];},
   row=>{row.graph.channels[0].inserts=[{id:"",catalogId:"vendor.fx.catalog"}];},
   row=>{row.graph.channels[0].inserts=[{id:"native-lease-A",catalogId:""}];},
   row=>{row.graph.channels[0].inserts=[{id:"native-lease-A",catalogId:"vendor.fx.catalog",bypassed:"false"}];},
   row=>{row.graph.channels[0].inserts.push({...row.graph.channels[0].inserts[0]});},
   row=>{row.graph.channels[1].inserts.push({...row.graph.channels[0].inserts[0]});},
   row=>{row.graph.channels[0].inserts=Array.from({length:9},(_,i)=>({
     id:"lease-"+i,catalogId:"vendor.fx.catalog"}));}
 ];
 for(const mutate of mutations){
   const bad=clone(good);
   mutate(bad);
   assert.equal(protocol.normalizeSnapshot(bad),null,
     "malformed instance lease must not be accepted as native PCM graph");
   assert.equal(controller.onSnapshot(bad),false);
   assert.equal(controller.state().current,accepted,
     "rejected FX snapshot must preserve the latest accepted physical graph");
   assert.equal(controller.state().lastError,"INVALID_NATIVE_SNAPSHOT");
   assert.equal(controller.state().pending,null);
   assert.equal(sent.length,0,"no bogus native intent or subscription");
 }
 assert.equal(statuses.at(-1).lastError,"INVALID_NATIVE_SNAPSHOT");
 assert.equal(protocol.readbackProvesIntent(null,{
   operation:"REMOVE_INSERT",targetChannelId:input,payload:{slot:0}
 }),false,"invalid snapshot may never prove ACK");

 // Malformed unsolicited readback during an edit is not a reason to commit or
 // lose the valid pending request; a subsequent real ACK+snapshot may recover.
 const edit=controller.intent("SET_FADER",input,{valueDb:-12});
 assert.equal(edit.ok,true);
 const bad=clone(good);bad.graph.channels[0].inserts=[null];
 assert.equal(controller.onSnapshot(bad),false);
 assert.equal(controller.state().pending?.requestId,edit.requestId);
 assert.equal(controller.state().current,accepted);
 assert.equal(controller.onAck({
   requestId:edit.requestId,status:"ACCEPTED",
   deviceEpoch:good.device.epoch,
   graphRevision:good.graph.revision,
   rosterRevision:good.graph.rosterRevision,errorCode:"NONE"
 }),true);
 const after=clone(good);
 after.graph.channels[0].controls.faderDb=-12;
 assert.equal(controller.onSnapshot(after),true);
 assert.equal(controller.state().lastError,"ACCEPTED_READBACK");
 assert.equal(controller.state().pending,null);
 assert.equal(controller.state().current.graph.channels[0].controls.faderDb,-12);
});
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
  assert.match(muted.nextAction,/Output recovering automatically/);
  assert.doesNotMatch(muted.nextAction,/ARM output|Resume after emergency stop/);
  g0.operatorOutputMuted=true;
  const panic=protocol.diagnoseFirstSound(protocol.normalizeSnapshot(g0),2);
  assert.match(panic.nextAction,/Emergency stop active; use More > Resume after emergency stop/);
  assert.doesNotMatch(panic.nextAction,/recovering automatically/);
  g0.operatorOutputMuted=false;
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
  assert.match(bridge,/deviceVM\.inputChoices\(hardware\)/);
  assert.match(read("web/approved-device-view-model.js"),/d\.enabledInputs\.includes\(p\.port\)/);
  assert.match(bridge,/field==="input"/);
  // Reordering /preview/ strips must NOT change the real hardware lane.
  assert.match(bridge,/const channel=m4Index\(t\)/);
  assert.match(bridge,/const m4TrackIds=Array\.isArray\(scope\?\.tracks\)/);
  assert.match(bridge,/const heldId=uiByNativeId\.get\(ch\.id\)/);
  assert.match(bridge,/!reserved\.has\(m4Id\)&&!nextNativeByUi\.has\(m4Id\)/);
  assert.match(bridge,/if\(nextNativeByUi\.has\(t\.id\)\)return false/);
  assert.match(bridge,/action:"routingSourceSwitch"/);
  assert.match(read("web/approved-channel-strip-view-model.js"),
    /row\?\.left===pending\.choice\.left/);
  assert.match(nativeMain,/autoMainEpoch_!=engine_\.dryRoutingLabEpoch\(\)/);
  assert.match(bridge,/inputSelect\.hidden=true;routeBtn\.hidden=true;armBtn\.hidden=true/);
  assert.match(bridge,/openNativeAudioIO\(\)/);
  assert.match(bridge,/\.armix-audio-shade\[hidden\]\{display:none\}/);
  assert.match(producer,/firstSound->setProperty\("channels",juce::var\(verifiedChannels\)\)/);
  assert.match(producer,/publishAuthoritativeMixerSession/);
  assert.match(nativeMain,/publishAuthoritativeMixerSession\(accepted\)/);
  assert.match(bridge,/const nativeAdmitted=readModel\.kind==="CHANNEL_ID"/);
 assert.match(read("web/approved-native-contract.js"),
   /session\.authority==="CHANNEL_ID"&&next\.admitted===true/);
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
 assert.ok(bridge.includes("stripVM.m4ChannelPatchPlan("));
 assert.ok(read("web/approved-channel-strip-view-model.js").includes('action:"routingChannelPatch"'));
 assert.match(bridge,/const channel=m4Index\(t\)/);
 assert.doesNotMatch(bridge,/data-native-monitor|dataset\.nativeMonitor/);
 assert.match(bridge,/dataset\.nativeEmergencyOutput/);
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
 assert.match(bridge,/fxRackVM\.nativeInsertPlan\(accepted,/);
 assert.match(bridge,/controller\.intent\("LOAD_INSERT",plan\.channelId,plan\.payload\)/);
 assert.match(bridge,/accepted\?\.admitted \|\| mixerSession\(\)\?\.authority==="CHANNEL_ID"/);
 assert.match(lab,/nativeAccepted_\.fxOwners->externalChannel\(\)==target/);
 assert.match(bridge,/pendingFxInsert=\{authority:"CHANNEL_ID"/);
 const fxVM=read("web/approved-fx-rack-view-model.js");
 assert.match(bridge,/fxRackVM\.nativeInsertSettlement\(pendingFxInsert,next,confirmed,/);
 assert.match(fxVM,/fx\.catalogId!==pending\.id/);
 assert.match(fxVM,/confirmed\.requestId!==pending\.requestId/);
 assert.match(translate,/if\(op=="LOAD_INSERT"\)/);
 assert.match(snapshot,/available\.add\(juce::String\("LOAD_INSERT"\)\)/);
 assert.match(main,/engine_\.installNativeVst3\(/);
 assert.match(contract,/if \(operation === "LOAD_INSERT"\)/);
 assert.match(contract,/ch\.inserts\[payload\.slot\]\?\.catalogId===payload\.catalogId/);
 assert.match(lab,/epoch\(\),false,std::move\(fx\),false,true\)/);
 assert.match(pub,/verifiedInternalFxOnly/);
 for(const [start,end] of [
   ["installNativeBuiltInFx(","bool selectNativeMusicTempoSource("],
   ["installNativeVst3(","bool rackFileAllowed("]
 ]){
   const install=engine.slice(engine.indexOf(start),engine.indexOf(end,engine.indexOf(start)));
   assert.ok(install.length>500);
   assert.doesNotMatch(install,/secondaryOutput_\.armed\(\)|!allOutputsDisarmed\(\)/,
     "secondary MAIN copy must not block live insert");
   assert.match(install,/independent_\.active\(\)/);
   assert.match(install,/!directFence_\.permitted\(\)/);
   assert.match(install,/routeReconfiguring_/);
   assert.match(install,/getLatencySamples\(\)>armix::dsp::StereoLatencyDelay::maximumSamples/);
   assert.match(install,/attachNativeInsert\([\s\S]*allOutputsDisarmed\(\)/);
 }
 assert.match(pub,/independentMonitorPublished_ \|\|/);
 assert.match(main,/NATIVE_FX_INSERT/);
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
  assert.ok(fn.includes('controller.intent(plan.operation,plan.channelId,plan.payload)'));
  assert.ok(fn.includes('dispatchM4ChannelPatch(t,plan.operation,plan.target,plan.channel)'));
  assert.ok(fn.indexOf('authority==="CHANNEL_ID"')<
    fn.indexOf('dispatchM4ChannelPatch(t,plan.operation,plan.target,plan.channel)'));
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

test("P0 post-BUS native IN routes NONE MONO STEREO and PLAYER through one PCM owner",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const host=read("src/Main.cpp");
 const engine=read("src/LiveAudioEngine.h");
 const lab=read("src/LiveDryRoutingLab.h");
 assert.match(bridge,/function dispatchNativeSource\(\)/);
 assert.match(read("web/approved-channel-strip-view-model.js"),
  /action:"routingNativeSourcePatch"/);
 assert.match(bridge,/action:plan\.action,requestId/);
 assert.match(bridge,/arMixNativeSourceAck/);
 assert.match(bridge,/pendingNativeSources.set\(id,decision.entry\)/);
 assert.match(host,/if\(action=="routingNativeSourcePatch"\)/);
 assert.match(host,/engine_\.patchNativeInputSource\(ep,rev,roster,/);
 assert.match(lab,/const bool disconnect=!musicPlayer&&!stereo&&\s*physicalLeft==-1&&physicalRight==-1/);
 assert.match(lab,/if\(musicPlayer\) \{\s*inputs\.push_back\(\{channel,-1,-1,true,/);
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
 assert.doesNotMatch(menu,/Confirm Delete Track/);
 assert.match(menu,/if\(track\?\.kind==="audio"\)\{\s*dismissMenu\(\);hooks\.requestDeleteTrack\(track\);return;/);
 assert.match(menu,/Confirm Delete AUX/);
 assert.match(bridge,/newProject\.addEventListener\("click",\(\)=>newSessionButtons\[0\]\.click\(\)\)/);
 assert.match(bridge,/if\(value==="__armix_settings__"\)\{openAdditionalOutput\(\);return;\}/);
 assert.doesNotMatch(bridge,/nativeExtraOutput|nativeFirstSound/);
 assert.match(bridge,/if\(page==="routing"\)mountInlineAdditionalOutput\(\)/);
 assert.match(bridge,/host\.append\(extraControls\)/);
 assert.doesNotMatch(bridge,/__arMixOpenAdditionalOutput/);
 assert.match(menu,/requestDeleteTrack\(track\)/);
 assert.match(menu,/hooks\.orbitTarget\(track,slot\)/);
 assert.ok(bridge.includes('stripVM.rosterMutationPlan('));
 assert.ok(read("web/approved-channel-strip-view-model.js").includes(
   'action:"routingNativeTrackDelete"'));
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
 assert.match(host,/if\(!replay&&translated\.ok\(\)\)[\s\S]*?closeEditorsForNativeRackEdit\(command\);\s*applied=armix::console::executeApprovedNativeControl/);
 assert.match(host,/op!="NATIVE_MOVE_INSERT" && op!="NATIVE_REMOVE_INSERT"/);
 assert.match(host,/item->channelId==from/);
 assert.match(host,/routedEditorOwner_->instanceId==token/);
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
 assert.ok(bridge.includes('dispatchNativeRosterDelete(track,"REMOVE_BUS")'));
 assert.ok(read("web/approved-channel-strip-view-model.js").includes(
   'action:"routingNativeBusDelete"'));
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
 assert.match(bridge,/function requestNativeBusCreate\(track=false,fxBus=false\)/);
 assert.ok(bridge.includes('backend.emitEvent("arMixAction",{action:plan.action,requestId,...plan.payload})'));
 assert.ok(read("web/approved-channel-strip-view-model.js").includes(
   'action:"routingNativeBusCreate"'));
 assert.match(bridge,/target.id==="addBus"/);
 assert.match(bridge,/arMixNativeBusAck/);
 assert.match(bridge,/addBus.disabled=!nativeAdmitted/);
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
 const wire=read("web/approved-wire-view-model.js");
 assert.match(bridge,/wireVM\.edgePresentation\(/);
 assert.match(wire,/const kind=primary\?"primary"/);
 assert.match(wire,/if\(primary&&edge\.gainLinear!==1\)return null/);
 assert.doesNotMatch(bridge,/if \(isMain && edge\.gainLinear!==1\)/);
});
test("P0 WIRE-to-MAIN and channel OUT use one acknowledged native transaction",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const builder=read("scripts/build_approved_native_gui.py");
 const native=read("src/Main.cpp");
 assert.match(bridge,/function requestPrimaryOut\(t,target\)/);
 assert.match(bridge,/session\?\.authority==="CHANNEL_ID"/);
 assert.match(bridge,/controller\.intent\("APPLY_WIRE_TRANSACTION",channelId,/);
 const strip=read("web/approved-channel-strip-view-model.js");
 assert.match(bridge,/stripVM\.primaryOutMutation\(/);
 assert.match(strip,/if\(target==="MAIN"\)to=g\.mainChannelId/);
 assert.match(strip,/op:"ROUTE_ONLY",from:sourceId,to,/);
 assert.match(strip,/op:"REMOVE",from:sourceId,/);
 // Exact PRIMARY EdgeId selection now belongs to pure Channel Strip VM.
 // Existing WIRE MAIN hook and native dispatcher remain in bridge.
 assert.match(strip,/edge\.primaryOutput===true/);
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
 // G-MV32: MAIN cable selection now belongs to the existing WIRE VM;
 // both paths still converge on the one accepted OUT transaction.
 const wirePolicy=read("web/approved-wire-view-model.js");
 assert.match(wirePolicy,/const mainConnect=isMain&&/);
 assert.match(wirePolicy,/const nativePrimary=globalEdges\.length===1/);
 assert.match(bridge,/wireVM\.primaryMainGesturePlan\(current,/);
 assert.match(bridge,/if\(plan\.kind==="ROUTE"\)/);
 assert.match(bridge,/requestPrimaryOut\(t,plan\.target\)/);
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
 assert.match(page,/const FADER_TAPER=Object\.freeze/);
 assert.match(page,/function faderDbFromTravelPercent\(percent,isSend\)/);
 assert.match(page,/faderDbFromTravelPercent\(100\*\(1-frac\),max===6\)/);
 assert.doesNotMatch(nativeBundle,/const FADER_TAPER=Object\.freeze/,
   "A native-only fader law would break /preview/ parity");
 assert.match(bridge,/function previewNativePan\(t,el,event\)/);
 assert.match(bridge,/const width=event\.type==="change"\?18:latched\?22:14/);
 assert.match(bridge,/readout\.textContent=value===0\?"C"/);
 assert.match(bridge,/function requestProcessingFormat\(t\)/);
 assert.ok(bridge.includes('dispatchM4ChannelPatch(t,plan.operation,plan.target,plan.channel)'));
 assert.ok(read("web/approved-channel-strip-view-model.js").includes('operation==="SET_PROCESSING_FORMAT"'));
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
 assert.match(bridge,/fxRackVM\.m4EditorPlan\(current,/);
 assert.match(read("web/approved-fx-rack-view-model.js"),/function m4EditorPlan\(snapshot,/);
 assert.doesNotMatch(bridge,/function verifiedFxFor\(/);
 assert.match(bridge,/function requestM4FxPower\(t,action,button=null\)/);
 assert.match(bridge,/if\(action==="slotPower"\)/);
 assert.match(bridge,/if\(action==="fxOn"\)/);
 assert.doesNotMatch(bridge,/rackSavedBypass/);
 assert.match(bridge,/requestM4FxPower\(t,"fxOn"\)/);
 assert.match(bridge,/requestM4FxPower\(t,"slotPower",btn\)/);
 assert.match(bridge,/requestAdmittedFxPower\(t,action,btn\)/);
 assert.match(bridge,/FX rack unchanged/);
 assert.match(bridge,/pendingM4RackPowers/);
 assert.match(read("web/approved-fx-rack-view-model.js"),/action:"routingFxEditor"/);
 assert.match(bridge,/dispatchPhysicalFxEditor\(plan,lease,"M4"\)/);
 assert.match(bridge,/action:plan\.action,requestId,\.\.\.plan\.payload/);
 assert.match(read("web/approved-fx-rack-view-model.js"),
   /action:"routingFxBypass"/);
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
test("P0 native WIRE MAIN disconnect uses exact PRIMARY preflight, not stale UI cables",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const vm=read("web/approved-wire-view-model.js");
 const bundle=read("scripts/build_approved_native_gui.py");
 assert.match(bridge,/wireVM\.primaryMainGesturePlan\(current,/);
 assert.match(bridge,/edgeId:wireByUiId\.get\(op\.edgeId\)/);
 assert.match(bridge,/if\(plan\.kind==="BLOCK"\)/);
 assert.match(bridge,/requestPrimaryOut\(t,plan\.target\)/);
 assert.doesNotMatch(bridge,/const graphMainRemove=op\.type==="REMOVE"/);
 assert.doesNotMatch(bridge,/const m4MainRemove=op\.type==="REMOVE"/);
 assert.match(vm,/snapshot\?\.admitted===false&&session\?\.authority==="M4"/);
 assert.match(bundle,/get wireEdges\(\)\{return wireEdges;\}/);
});

test("P0 native-only WIRE cables reflect actual M4 MAIN readback, not demo ghosts",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const bundle=read("scripts/build_approved_native_gui.py");
 assert.match(bridge,/if\(readModel\.kind==="M4"\)/);
 assert.match(read("web/approved-native-contract.js"),
   /session\.authority==="M4"&&verifiedM4===true/);
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
 assert.match(native,/const bool requireInput=autoMainRequiresInput_ &&\s*!engine_\.hasVerifiedExternalOutputIntent\(\)/);
  assert.ok(native.includes("const auto fault=engine_.liveOutputArmFault(requireInput);"));
  assert.ok(native.includes("protectKnownFeedbackLoop(fault);"));
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
 assert.match(bridge,/handleSourceTimeout\(authority,requestId\)/);
 assert.match(bridge,/nativeUiSourcePending=null;[\s\S]*?INPUT timeout/);
 assert.match(bridge,/deviceVM\.setupAck\(deviceState,ack\)/);
 assert.match(read("web/approved-device-view-model.js"),/ack\?\.requestId!==state\.pending\.requestId/);
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
 const wireSource=read("web/approved-wire-view-model.js");
 assert.ok(wireSource.includes("op:op.type,from"),
  "native WIRE operation and canonical source belong to pure ViewModel");
 assert.match(source,/wireVM\.nativeTransaction\(ops,nativeByUiId,wireByUiId\)/);
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
test("G-MV21 controller ACK lease is immutable until installed v6 readback",()=>{
 const snap=acceptedFixture(),events=[],paints=[];
 const c=protocol.createController({emit:(name,payload)=>events.push({name,payload}),
   onAccepted:(next,confirmed)=>paints.push({next,confirmed})});
 assert.equal(c.onSnapshot(snap),true);
 const issued=c.intent("SET_FADER","9007199254740993",{valueDb:-12});
 assert.equal(issued.ok,true);
 const ack={requestId:issued.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:snap.device.epoch,graphRevision:"6",rosterRevision:"7"};
 assert.equal(c.onAck(ack),true);
 const n=events.length;
 for(const late of [ack,
   {...ack,status:"REJECTED",errorCode:"STALE_GRAPH"},
   {...ack,graphRevision:"99"},{...ack,graphRevision:"broken"},
   {...ack,deviceEpoch:"18446744073709551609"}]){
   assert.equal(c.onAck(late),false);
   assert.equal(c.state().pending?.requestId,issued.requestId);
   assert.equal(c.state().lastError,"PENDING_ACCEPTED_SNAPSHOT");
   assert.equal(events.length,n,"duplicate ACK cannot resubscribe");
 }
 assert.equal(paints.length,1,"ACK alone cannot repaint");
 const installed=clone(snap);
 installed.graph.revision="6";
 installed.graph.channels[0].controls.faderDb=-12;
 assert.equal(c.onSnapshot(installed),true);
 assert.equal(c.state().pending,null);
 assert.equal(paints.length,2);
 assert.equal(paints[1].confirmed.requestId,issued.requestId);
 assert.equal(c.onAck(ack),false);
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
   __JUCE__:{backend:native},ArSonApprovedNativeContract:protocol,ArSonApprovedDeviceViewModel:deviceVM,ArSonApprovedChannelStripViewModel:stripVM,ArSonApprovedFxRackViewModel:fxRackVM,ArSonApprovedWireViewModel:wireVM,
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
 // WIRE inspector MODE is a legacy visual gesture. It MUST become one
 // exact native SET_TAP EdgeId transaction; rejection leaves the PCM tap intact.
 const tapUi=scope.edges[0].id;
 assert.equal(window.__ARMIX_NATIVE_M2__.consumeWire([{
   type:"MODE",sourceId:sourceUi,edgeId:tapUi,mode:"split"
 }]),true);
 const tapRequest=sent.findLast(x=>x.name==="arMixConsoleIntent").payload;
 assert.equal(tapRequest.operation,"APPLY_WIRE_TRANSACTION");
 assert.deepEqual(JSON.parse(JSON.stringify(tapRequest.payload.changes)),[{
   op:"SET_TAP",from:routed.graph.channels[0].id,
   edgeId:"9223372036854775810",tap:"PRE_FADER"
 }]);
 assert.equal(scope.edges[0].nativeTap,"POST_FADER");
 listeners.get("arMixConsoleAck")({requestId:tapRequest.requestId,status:"REJECTED",
   errorCode:"NATIVE_WIRE_TRANSACTION_REJECTED",deviceEpoch:gained.device.epoch,
   graphRevision:"8",rosterRevision:"8"});
 assert.equal(window.__ARMIX_NATIVE_M2__.state().pending,null);
 assert.equal(scope.edges[0].nativeTap,"POST_FADER",
   "rejected native tap must restore the last accepted PCM stage");
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
 const win={__JUCE__:{backend:native},ArSonApprovedNativeContract:protocol,ArSonApprovedDeviceViewModel:deviceVM,ArSonApprovedChannelStripViewModel:stripVM,ArSonApprovedFxRackViewModel:fxRackVM,ArSonApprovedWireViewModel:wireVM,
  __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},
  setTimeout(){return 0},clearTimeout(){},queueMicrotask(fn){fn();}};
 vm.runInNewContext(read("web/approved-native-bridge.js"),
  {window:win,document:doc,performance:{now:()=>0},console},{timeout:1500});
 const base=acceptedFixture();
 // Production approvedNativeSnapshot publishes this PCM scalar, not CSS.
 base.globalFxBypassed=false;
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
 // One PCM global A/B overrides every displayed FX power without
 // changing either remembered individual bypass preference or DOM nodes.
 const allBypassed=clone(restored);
 allBypassed.globalFxBypassed=true;
 listeners.get("arMixConsoleSnapshot")(allBypassed);
 assert.equal(paints,1,"global FX is a scalar, not a graph rebuild");
 assert.deepEqual(rows.map(r=>r.dataset.fxEffective),["false","false"]);
 assert.deepEqual(rows.map(r=>r.power.getAttribute("aria-pressed")),["false","false"]);
 assert.deepEqual(Array.from(track.slotBypass.slice(0,2)),[false,true]);
 const restoredGlobal=clone(allBypassed);
 restoredGlobal.globalFxBypassed=false;
 listeners.get("arMixConsoleSnapshot")(restoredGlobal);
 assert.equal(paints,1);
 assert.deepEqual(rows.map(r=>r.dataset.fxEffective),["true","false"]);
 const individual=clone(restoredGlobal);
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
 const recovery=read('src/NativeMixerSessionRecovery.h');
 const engine=read('src/LiveAudioEngine.h');
 const route=read('src/LiveDryRoutingLab.h');
 assert.match(main,/Recovery::plan\(doc,\{bindingMode,checkpoint.source,\s*startupOnSavedDevice,mixerSessionRecoveryPending_,nativeAlreadyActive,\s*engine_\.hasPristineStartupMixer\(\),firstBinding\}\)/);
 assert.match(main,/if\(!plan.restoreAllowed\)/);
 assert.match(main,/restoreNativeMixerSession\(std::move\(doc\),states,\s*plan.replacePristineStartup,nullptr,plan.bindingMode\)/);
 assert.match(main,/if\(plan.resumeMain\)\s*requestAutomaticMain/);
 const restore=main.slice(main.indexOf("juce::String restoreSavedMixerSession("),
    main.indexOf("juce::String saveMixerSession("));
 assert.doesNotMatch(restore,/mixerSessionId_=doc.id/);
 assert.match(restore,/if\(!isNativeMixerActive\(\)\)acceptSessionIdentity\(\);/);
 assert.match(restore,/restoreNativeMixerSession\(std::move\(doc\),states,[\s\S]*?return error;\s*\}\s*mixerSessionRecoveryPending_=false;\s*acceptSessionIdentity\(\);/);
 assert.match(recovery,/context.nativeAlreadyActive && fromDisk &&/);
 assert.match(recovery,/Source::head && !doc.outputs.secondaryMain\(\) && doc.resumeMain.value_or\(true\)/);
 assert.match(engine,/dryRoutingLab_\.canReplacePristineStartupMixer\(\)/);
 assert.match(route,/bool canReplacePristineStartupMixer\(\) const noexcept/);
 assert.match(route,/!nativeControlDirty_ && nativeAccepted_\.fxOwners/);
});

test("P0 startup banner never demands legacy Patch CH1 / Arm wizard",()=>{
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/SELECT CHANNEL IN · NATIVE MONITOR AFTER ACK/);
 assert.match(bridge,/next.admitted\?"NO ACTIVE CAPTURE PINS · AUDIO I\/O"/);
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
 assert.match(cpp,/Apply::prepare\(engine_\.devices\(\),args\.getProperty\("config",\{\}\)\)/);
 assert.match(apply,/const bool outputOnly=separate&&r.capture\.isEmpty\(\)/);
 assert.match(apply,/candidate\.useDefaultInputChannels=!outputOnly/);
 assert.match(apply,/if\(outputOnly\)candidate\.inputChannels\.clear\(\)/);
 assert.match(apply,/parsePinMasks\(input,output,plan.originalInputNames.size\(\)/);
 assert.match(apply,/\(outputOnly&&!in\.isZero\(\)\)/);
 assert.match(apply,/out\.countNumberOfSetBits\(\)<2/);
 assert.doesNotMatch(cpp,/opened->getActiveInputChannels\(\)\.countNumberOfSetBits\(\)<1/);
 assert.match(bridge,/No hardware input \(Player \/ FX\)/);
 assert.match(bridge,/view\.inputOptions/);
 assert.match(read("web/approved-device-view-model.js"),
   /type\?\.separate\?\["",\.\.\.\(type\.inputs\|\|\[\]\)\]/);
 assert.doesNotMatch(bridge,/ack\.active\.enabledInputs\.length>0/);
 assert.match(read("web/approved-device-view-model.js"),/Mixer ready · Output muted/);
});


for(const scenario of ["driver","pin-mask"]) test(
 "G-MV20 M4 pending slot FX remains fenced across same-epoch "+scenario+" remap",()=>{
 const vm=require("node:vm"),listeners=new Map(),sent=[],timers=[],microtasks=[];
 const tracks=Array.from({length:10},(_,i)=>({id:i+1,kind:"audio",
   name:"CH "+(i+1),input:"None",output:"Master",format:"mono",
   fx:[],slotBypass:[],fxInsertIds:[]}));
 const scope={tracks,master:{id:50,kind:"master",output:"Master"},
   findTrack:el=>el?.track||null,render(){},stopDemo(){}};
 const backend={emitEvent:(name,payload)=>sent.push({name,payload}),
   addEventListener:(name,fn)=>listeners.set(name,fn)};
 const toast={hidden:true,textContent:""};
 const doc={readyState:"complete",documentElement:{dataset:{}},
   activeElement:null,getElementById:id=>id==="info"?{textContent:""}:
     id==="preview"?{disabled:false}:
     id==="armixNativeToast"?toast:null,
   querySelector:()=>null,addEventListener(){}};
 // MV-19 defense-in-depth: force one controller callback with a rejected
 // read-model candidate. Such a callback must not paint untrusted hardware
 // even if a future controller inadvertently forwards it.
 const guardedProtocol={...protocol,
   createController(options){
     const native=protocol.createController(options);
     return {...native,onSnapshot(raw){
       if(raw?.device?.captureDevice==="Quarantined ASIO"){
         const normalized=protocol.normalizeSnapshot(raw);
         assert.ok(normalized,"rejected read-model candidate must be schema-valid");
         options.onAccepted(normalized);
         return true;
       }
       return native.onSnapshot(raw);
     }};
   },
   selectNativeReadModel(next,verified){
     if(next?.device?.captureDevice==="Quarantined ASIO")
       return {kind:"INVALID"};
     return protocol.selectNativeReadModel(next,verified);
   }};
 const win={__JUCE__:{backend},ArSonApprovedNativeContract:guardedProtocol,
   ArSonApprovedDeviceViewModel:deviceVM,
   ArSonApprovedChannelStripViewModel:stripVM,
   ArSonApprovedFxRackViewModel:fxRackVM,
   ArSonApprovedWireViewModel:wireVM,
   __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},
   setTimeout(fn,ms){timers.push({fn,ms});return timers.length;},
   clearTimeout(){},queueMicrotask(fn){microtasks.push(fn);}};
 vm.runInNewContext(read("web/approved-native-bridge.js"),{
   window:win,document:doc,performance:{now:()=>0},console
 },{timeout:2000});
 const installed=clone(fixtures.g0);
 Object.assign(installed.device,{backend:"ASIO",captureDevice:"Vendor A ASIO",
   playbackDevice:"Vendor A ASIO",enabledInputs:[0,1],
   inputEndpoints:[{id:"in:0",port:0,callback:0,label:"IN 1",active:true},
     {id:"in:1",port:1,callback:1,label:"IN 2",active:true}]});
 const rows=Array.from({length:10},()=>({mode:"NONE",left:-1,right:-1}));
 const fxInstances=Array.from({length:10},(_,i)=>i===4?
   [{catalogId:"armix.builtin.channel-eq.v1",instanceId:"fx-m4-01",
     bypassed:false}]:[]);
 installed.legacyM4={active:true,routeAccepted:true,routingRevision:"13",
   nativeBusStructural:false,fxInstances,
   channelControls:Array.from({length:10},()=>({db:0,pan:0,mute:false,solo:false})),
   channels:rows};
 installed.mixerSession={authority:"M4",deviceEpoch:installed.device.epoch,
   routeRevision:"13",rosterRevision:"0",
   channels:rows.map((source,i)=>({
     id:"m4:"+String(i+1).padStart(2,"0"),ordinal:i,role:"INPUT",
     source,controls:{faderDb:0,pan:0,mute:false,solo:false},
     fxRackBypassed:false
   })),main:{db:0,mute:false,outputArmed:false}};
 const publish=raw=>listeners.get("arMixConsoleSnapshot")(raw);
 const prepared=clone(installed);
 prepared.legacyM4.routeAccepted=false;
 prepared.mixerSession.authority="NONE";
 prepared.mixerSession.routeRevision="0";
 prepared.mixerSession.channels=[];
 publish(prepared);
 publish(installed);
 assert.ok(win.__ARMIX_NATIVE_M2__.inputChoices(tracks[4]).length>1,
   "real verified physical IN must be installed before M4 FX test");
 const click=()=>win.__ARMIX_NATIVE_M2__.consumeAction(
   tracks[4],"slotPower",{dataset:{slot:"0"}});
 const bypasses=()=>sent.filter(x=>x.name==="arMixAction"&&
   x.payload.action==="routingFxBypass").map(x=>x.payload);
 assert.equal(click(),true);
 assert.equal(bypasses().length,1,
   "first M4 slot bypass must dispatch; feedback: "+toast.textContent);
 const first=bypasses()[0];
 // The header uses the SAME M4 authority plan as the insert button.
 const rackClick=()=>win.__ARMIX_NATIVE_M2__.consumeAction(
   tracks[4],"fxOn",{});
 const racks=()=>sent.filter(x=>x.name==="arMixAction"&&
   x.payload.action==="routingFxRackBypass").map(x=>x.payload);
 assert.equal(rackClick(),true);
 assert.equal(racks().length,1,"verified M4 rack gate must dispatch");
 const oldRack=racks()[0];
 assert.equal(oldRack.channel,4);
 assert.equal(oldRack.revision,13);
 assert.equal(rackClick(),true);
 assert.equal(racks().length,1,
   "second rack click cannot escape its unconfirmed native lease");
 const oldRackTimer=timers.filter(t=>t.ms===8000).at(-1);
 assert.ok(oldRackTimer);
 assert.equal(click(),true);
 assert.equal(bypasses().length,1,"same slot is gated by its unresolved native lease");
 const oldTimer=timers.filter(t=>t.ms===5000).at(-1);
 assert.ok(oldTimer);
 // MV-19: rejected candidate has a different physical identity, but neither
 // the active output label nor this legitimate pending M4 FX edit may change.
 const acceptedOutput=scope.master.output;
 const acceptedChoices=win.__ARMIX_NATIVE_M2__.inputChoices(tracks[4])
   .map(x=>x.label).join("|");
 const quarantined=clone(installed);
 quarantined.device.captureDevice="Quarantined ASIO";
 quarantined.device.playbackDevice="Quarantined ASIO";
 quarantined.device.inputEndpoints[0].label="Untrusted IN";
 publish(quarantined);
 assert.equal(scope.master.output,acceptedOutput,
   "invalid candidate cannot repaint master hardware output");
 assert.equal(win.__ARMIX_NATIVE_M2__.inputChoices(tracks[4])
   .map(x=>x.label).join("|"),acceptedChoices,
   "invalid candidate cannot install untrusted physical pin choices");
 assert.equal(click(),true);
 assert.equal(bypasses().length,1,
   "invalid candidate cannot release the pending physical FX lease");
 // Different actual pins OR a different driver on the SAME epoch expires
 // only the old UI lease; native instances are still readback-owned.
 const changed=clone(installed);
 if(scenario==="driver"){
   changed.device.captureDevice="Vendor B ASIO";
   changed.device.playbackDevice="Vendor B ASIO";
 }else{
   changed.device.enabledInputs=[0];
   changed.device.inputEndpoints=changed.device.inputEndpoints.filter(p=>p.port===0);
 }
 changed.legacyM4.routingRevision="14";
 changed.mixerSession.routeRevision="14";
 publish(changed);
 while(microtasks.length)microtasks.shift()();
 assert.equal(click(),true);
 assert.equal(bypasses().length,2,
   "reconfigured hardware cannot leave a slot uneditable for 5 seconds");
 assert.equal(rackClick(),true);
 assert.equal(racks().length,2,
   "reconfigured hardware must release prior M4 rack lease immediately");
 const newRack=racks()[1];
 assert.equal(newRack.revision,14);
 const second=bypasses()[1];
 assert.notEqual(first.requestId,second.requestId);
 assert.equal(second.revision,14,"second bypass uses the newly accepted routing revision");
 listeners.get("arMixRoutingFxBypassAck")({
   requestId:first.requestId,channel:4,slot:0,ok:false,error:"OLD_DEVICE"});
 listeners.get("arMixRoutingFxRackAck")({
   requestId:oldRack.requestId,channel:4,ok:false,
   deviceEpoch:13,routingRevision:13,error:"OLD_DEVICE"});
 oldTimer.fn();oldRackTimer.fn();
 assert.equal(bypasses().length,2,"expired timer cannot affect new driver lease");
 assert.equal(racks().length,2,"expired rack timer cannot retarget new lease");
 // The new request is still protected from a duplicate click until settled.
 assert.equal(click(),true);
 assert.equal(bypasses().length,2,
   "an old ACK/timer cannot release a new driver's pending FX command");
 assert.equal(rackClick(),true);
 assert.equal(racks().length,2,
   "old rack ACK/timer cannot release the new driver's pending rack gate");
 assert.equal(sent.filter(x=>x.name==="arMixAction"&&
   x.payload.action==="monitor").length,0,"no automatic MAIN arm");
});

test("MV-15 real M4 first-sound PCM snapshot ahead of ACK drains queued second INPUT",()=>{
 const vm=require("node:vm"),listeners=new Map(),sent=[],microtasks=[],timers=[];
 const tracks=Array.from({length:10},(_,i)=>({id:i+1,kind:"audio",
   name:"CH "+(i+1),input:"None",output:"Master",format:"mono",
   fx:[],slotBypass:[],fxInsertIds:[]}));
 const scope={tracks,master:{id:50,kind:"master",output:"Master"},
   findTrack:el=>el?.track||null,render(){},stopDemo(){}};
 const backend={emitEvent:(name,payload)=>sent.push({name,payload}),
   addEventListener:(name,fn)=>listeners.set(name,fn)};
 const doc={readyState:"complete",documentElement:{dataset:{}},
   activeElement:null,getElementById:id=>id==="info"?{textContent:""}:
     id==="preview"?{disabled:false}:null,
   querySelector:()=>null,addEventListener(){}};
 const win={__JUCE__:{backend},ArSonApprovedNativeContract:protocol,
   ArSonApprovedDeviceViewModel:deviceVM,
   ArSonApprovedChannelStripViewModel:stripVM,
   ArSonApprovedFxRackViewModel:fxRackVM,
   ArSonApprovedWireViewModel:wireVM,
   __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},
   setTimeout(fn,ms){timers.push({fn,ms});return timers.length;},
   clearTimeout(){},queueMicrotask(fn){microtasks.push(fn);}};
 vm.runInNewContext(read("web/approved-native-bridge.js"),{
   window:win,document:doc,performance:{now:()=>0},console
 },{timeout:1500});
 const boot=clone(fixtures.g0);
 Object.assign(boot.device,{backend:"Windows Audio",
   captureDevice:"Real microphone",playbackDevice:"Speakers",
   enabledInputs:[0,1],
   inputEndpoints:[
     {id:"in:0",port:0,callback:0,label:"IN 1",active:true},
     {id:"in:1",port:1,callback:1,label:"IN 2",active:true}
   ]});
 boot.legacyM4={active:true,routeAccepted:false,routingRevision:"12"};
 boot.mixerSession={authority:"NONE",deviceEpoch:boot.device.epoch,
   routeRevision:"0",rosterRevision:"0",channels:[],main:{outputArmed:false}};
 listeners.get("arMixConsoleSnapshot")(boot);
 const choose=(channel,label)=>win.__ARMIX_NATIVE_M2__.consumeField({
   type:"change",target:{track:tracks[channel],dataset:{field:"input"},value:label}
 });
 assert.equal(choose(4,"Mono · IN 1 [IN 1]"),true);
 assert.equal(choose(5,"Mono · IN 2 [IN 2]"),true);
 const sources=()=>sent.filter(x=>x.name==="arMixAction"&&
   x.payload.action==="routingSourceSwitch").map(x=>x.payload);
 assert.equal(sources().length,1,"one in-flight M4 source, second is queued");
 const first=sources()[0];
 assert.equal(first.revision,12);
 const independentlyInstalled=clone(boot);
 const actual=Array.from({length:10},(_,i)=>({
   mode:i===4?"MONO":"NONE",left:i===4?0:-1,right:-1,main:true
 }));
 independentlyInstalled.legacyM4={
   active:true,routeAccepted:true,routingRevision:"13",nativeBusStructural:false,
   channelControls:Array.from({length:10},()=>({
     db:0,pan:0,mute:false,solo:false
   })),
   channels:actual
 };
 independentlyInstalled.mixerSession={
   authority:"M4",deviceEpoch:"69",routeRevision:"13",rosterRevision:"0",
   channels:actual.map((source,i)=>({
     id:"m4:"+String(i+1).padStart(2,"0"),ordinal:i,role:"INPUT",
     source,controls:{faderDb:0,pan:0,mute:false,solo:false}
   })),
   main:{db:0,mute:false,outputArmed:false}
 };
 // The installed PCM callback can be observed before the correlated ACK.
 listeners.get("arMixConsoleSnapshot")(independentlyInstalled);
 while(microtasks.length)microtasks.shift()();
 assert.equal(sources().length,1,
   "correct independent PCM must still wait for correlated first ACK");
 listeners.get("arMixSourceSwitchAck")({
   requestId:first.requestId,ok:true,state:{
     ready:true,epoch:69,revision:13,hasAcceptedRoute:true,
     acceptedRoute:{channels:actual}
   }
 });
 // MV-15: ACK can verify the already-published PCM immediately and
 // schedule the queued second INPUT with its fresh revision (13).
 while(microtasks.length)microtasks.shift()();
 assert.equal(sources().length,2,
   "a valid native PCM readback followed by ACK releases the queued channel");
 assert.equal(sources()[1].channel,5);
 assert.equal(sources()[1].left,1);
 assert.equal(sources()[1].revision,13);
 assert.equal(sources()[1].deviceEpoch,69);
 // MV-16: keep a third channel queued while the SECOND transaction has
 // received a valid ACK, followed by a contradictory late reject.
 assert.equal(choose(6,"Mono · IN 1 [IN 1]"),true);
 const second=sources()[1];
 const acceptedAt14=actual.map((row,i)=>i===5?
   {mode:"MONO",left:1,right:-1,main:true}:row);
 const secondPositive={requestId:second.requestId,ok:true,state:{
   ready:true,epoch:69,revision:14,hasAcceptedRoute:true,
   acceptedRoute:{channels:acceptedAt14}}};
 listeners.get("arMixSourceSwitchAck")(secondPositive);
 assert.equal(sources().length,2,"ACK alone never submits the third INPUT");
 listeners.get("arMixSourceSwitchAck")({
   requestId:second.requestId,ok:false,error:"LATE_NEGATIVE_ACK"});
 while(microtasks.length)microtasks.shift()();
 assert.equal(sources().length,2,
   "contradictory duplicate cannot release the M4 source queue before PCM");
 const installed14=clone(independentlyInstalled);
 installed14.legacyM4.routingRevision="14";
 installed14.legacyM4.channels=acceptedAt14;
 installed14.mixerSession.routeRevision="14";
 installed14.mixerSession.channels[5].source=acceptedAt14[5];
 listeners.get("arMixConsoleSnapshot")(installed14);
 while(microtasks.length)microtasks.shift()();
 assert.equal(sources().length,3,
   "PCM readback plus original positive ACK releases exactly one queued INPUT");
 assert.equal(sources()[2].channel,6);
 assert.equal(sources()[2].left,0);
 assert.equal(sources()[2].revision,14);
 // Late ACKs from already-settled leases cannot alter another channel.
 listeners.get("arMixSourceSwitchAck")({
   requestId:second.requestId,ok:false,error:"EXPIRED_ACK"});
 assert.equal(sources().length,3);
 // MV-17: third INPUT has no ACK and its request timer expires.
 // A fourth INPUT cannot use the previously cached M4 route revision.
 assert.equal(choose(7,"Mono · IN 2 [IN 2]"),true);
 const third=sources()[2];
 const timeout=timers.filter(t=>t.ms===8000).at(-1);
 assert.ok(timeout,"SOURCE must register one bounded timeout");
 const beforeRefresh=sent.filter(x=>x.name==="arMixConsoleSubscribe").length;
 timeout.fn();
 while(microtasks.length)microtasks.shift()();
 assert.equal(sources().length,3,"timeout must not dispatch on cached revision");
 assert.ok(sent.filter(x=>x.name==="arMixConsoleSubscribe").length>beforeRefresh,
   "timeout resubscribes to obtain native PCM instead of retrying blindly");
 listeners.get("arMixSourceSwitchAck")({
   requestId:third.requestId,ok:true,state:{
     ready:true,epoch:69,revision:15,hasAcceptedRoute:true,
     acceptedRoute:{channels:acceptedAt14}}
 });
 assert.equal(sources().length,3,"late expired ACK cannot confirm a timed-out source");
 const installed15=clone(installed14);
 installed15.legacyM4.routingRevision="15";
 installed15.mixerSession.routeRevision="15";
 const thirdRow={mode:"MONO",left:0,right:-1,main:true};
 installed15.legacyM4.channels[6]=thirdRow;
 installed15.mixerSession.channels[6].source=thirdRow;
 listeners.get("arMixConsoleSnapshot")(installed15);
 while(microtasks.length)microtasks.shift()();
 assert.equal(sources().length,4,
   "next queued INPUT must wait for a new independently accepted snapshot");
 assert.equal(sources()[3].channel,7);
 assert.equal(sources()[3].left,1);
 assert.equal(sources()[3].revision,15);
 assert.equal(sources()[3].deviceEpoch,69);
 timeout.fn();
 assert.equal(sources().length,4,
   "repeated old timeout may not affect the next channel lease");
 assert.equal(sent.filter(x=>x.name==="arMixAction"&&
   x.payload.action==="monitor").length,0,
   "UI must never auto-arm MAIN as a side effect of SOURCE ACK");
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
  __JUCE__:{backend},ArSonApprovedNativeContract:protocol,ArSonApprovedDeviceViewModel:deviceVM,ArSonApprovedChannelStripViewModel:stripVM,ArSonApprovedFxRackViewModel:fxRackVM,ArSonApprovedWireViewModel:wireVM,
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
 assert.deepEqual(renderedChoices.at(-1),["None","Mono · IN 1 [IN 1]","Music Player"],
  "the verified physical pin and virtual Player are discoverable before admission");
 // Showing a virtual source must not imply that the unadmitted M4 producer
 // can patch it; no synthetic browser PCM or MAIN arm is permitted.
 const rejectPlayer=win.__ARMIX_NATIVE_M2__.consumeField({
  type:"change",target:{dataset:{field:"input"},value:"Music Player",track:tracks[4]}
 });
 assert.equal(rejectPlayer,true);
 assert.equal(sent.some(x=>x.name==="arMixAction"&&
   x.payload.action==="routingSourceSwitch"),false);
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
 assert.deepEqual([...win.__ARMIX_NATIVE_M2__.inputChoices(tracks[0])],["None","Music Player"],
  "Player remains discoverable even when capture pins disappear; admission still rejects it");
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
 const win={__JUCE__:{backend},ArSonApprovedNativeContract:protocol,ArSonApprovedDeviceViewModel:deviceVM,ArSonApprovedChannelStripViewModel:stripVM,ArSonApprovedFxRackViewModel:fxRackVM,ArSonApprovedWireViewModel:wireVM,
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
 // MV-7: a later positive ACK must NOT drain a third channel edit until the
 // exact new source pins and native graph revision are observed separately.
 assert.equal(choose(scope.tracks[0],"Mono · Mic A [IN 1]"),true);
 const secondCommand=sourceCommands()[1];
 listeners.get("arMixNativeSourceAck")({
   requestId:secondCommand.payload.requestId,channelId:second.id,
   graphRevision:"6",ok:true
 });
 assert.equal(sourceCommands().length,2,"ACK is not installed PCM readback");
 listeners.get("arMixConsoleSnapshot")(base);
 while(microtasks.length)microtasks.shift()();
 assert.equal(sourceCommands().length,2,
   "unchanged pre-ACK PCM must hold next queued SOURCE");
 const installed=clone(base);
 installed.graph.revision="6";
 installed.graph.channels.find(ch=>ch.id===second.id).source={
   sourceFormat:"MONO",portId:"in:1"
 };
 installed.mixerSession.routeRevision="6";
 listeners.get("arMixConsoleSnapshot")(installed);
 while(microtasks.length)microtasks.shift()();
 assert.equal(sourceCommands().length,3,
   "installed source readback unlocks only one next queued edit");
 assert.equal(sourceCommands()[2].payload.channelId,first.id);
 assert.equal(sourceCommands()[2].payload.graphRevision,"6");
});
test("M3/V60 Windows bundle dispatches exactly one fader release through native ACK and fences FX reorder",()=>{
 const builder=read("scripts/build_approved_native_gui.py");
 const preview=read("site/preview-v2/index.html");
 const bridge=read("web/approved-native-bridge.js");
 assert.ok(preview.includes("const g=faderGesture;faderGesture=null;"));
 assert.equal(preview.split("handleField({target:g.input,type:'change',altKey:!!ev.altKey});").length-1,1);
 assert.ok(builder.includes("window.__ARMIX_NATIVE_M2__?.consumeField?.(event);return;"));
 assert.ok(!builder.includes("consumeField?.({target:g.input,type:'change'})"),
   "V60 source already emits the final change; native must not double-publish");
 assert.ok(preview.includes("faderDbFromTravelPercent(100*(1-frac),max===6)"));
 assert.ok(preview.includes("faderPercent(0,max===6)/100"));
 assert.ok(!builder.includes("const FADER_TAPER=Object.freeze"),
   "Both visual surfaces must use one Pages taper, not another native renderer");
 assert.ok(builder.includes("function fxNativeTransactionRequired"));
 assert.ok(builder.includes("__ARMIX_NATIVE_SCOPE__"));
 assert.ok(bridge.includes("scope.installGraph(nextTracks.map(commitTrack),commitTrack(nextMaster),edges)"));
 assert.ok(bridge.indexOf("validateCandidateGraph(nextTracks,nextMaster.id,edges)")<bridge.indexOf("const commitTrack="),
   "validate the complete graph before mutating the previously installed strip models");
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
 assert.match(read("web/approved-device-view-model.js"),/action:"audioDeviceApply"/);
 assert.match(js,/backend\.emitEvent\("arMixAction",begun\.request\)/);
 assert.match(read("web/approved-device-view-model.js"),/d\.inputEndpoints\.filter\(p=>/);
 assert.match(read("web/approved-device-view-model.js"),/d\.enabledInputs\.includes\(p\.port\)/);
 assert.match(js,/deviceVM\.inputChoices\(hardware\)/);
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
 assert.equal(c.onSnapshot(wrong),false,'duplicate live token is invalid native schema');
 assert.equal(c.state().lastError,'INVALID_NATIVE_SNAPSHOT');
 assert.equal(c.state().pending?.requestId,request.requestId,
   'invalid transport snapshot must not consume the accepted native ACK');
 const wrongEffect=clone(t.base);
 wrongEffect.graph.revision='6';wrongEffect.graph.rosterRevision='8';
 wrongEffect.graph.channels[0].inserts.reverse();
 assert.equal(c.onSnapshot(wrongEffect),false,
   'valid but wrong FX order cannot masquerade as MOVE');
 assert.equal(c.state().lastError,'ACK_READBACK_EFFECT_MISMATCH');
 assert.equal(c.state().pending,null);
});

test("ImmerSIB opens the exact native ChannelId HWND, survives VST3 editor, no success-only toast",()=>{
 const bridge=read("web/approved-native-bridge.js"),host=read("src/Main.cpp");
 const editorVM=read("web/approved-fx-rack-view-model.js");
 assert.match(bridge,/fxRackVM\.editorNavigationPlan\(current,/);
 assert.match(editorVM,/view\.raw\?\.mixerSession\?\.authority==="CHANNEL_ID"/);
 assert.match(bridge,/fxRackVM\.editorSurfacePlan\(current,editor,/);
 assert.match(bridge,/surface\.kind==="NATIVE_HOST"/);
 assert.match(editorVM,/if\(lease\.authority==="CHANNEL_ID"\)return \{kind:"NATIVE_HOST"\};/);
 assert.match(bridge,/openChannelIdFxEditor\(editor\)/);
 assert.match(bridge,/fxRackVM\.nativeEditorDispatchPlan\(current,lease,/);
 assert.match(bridge,/dispatchPhysicalFxEditor\(plan,lease,"CHANNEL_ID"\)/);
 assert.match(bridge,/surface\.kind==="LOGICAL_VENDOR"/);
 assert.match(editorVM,/if\(lease\.authority==="LOGICAL"\)return \{kind:"LOGICAL_VENDOR"\};/);
 assert.match(bridge,/action:"logicalVendorFxEditor"/);
 assert.doesNotMatch(bridge,/else status\("Native VST3 editor opened"\)/);
 assert.match(host,/shownWindow=dedicatedImmerSIB\?immersibEditorWindow_:editorWindow_/);
 assert.match(host,/shownOwner=dedicatedImmerSIB\?immersibEditorOwner_:routedEditorOwner_/);
 assert.match(host,/shownWindow->present\(\)/);
 assert.match(host,/immersibEditorWindow_\.reset\(\)/);
 assert.match(host,/!engine_\.retainsRoutedFxEditor\(immersibEditorEpoch_,immersibEditorOwner_\)/);
});

test("native vendor editor retention follows current PCM ownership, not inactive M4 slots",()=>{
 const main=read('src/Main.cpp'),lab=read('src/LiveDryRoutingLab.h');
 assert.match(main,/engine_\.retainsRoutedFxEditor\(routedEditorEpoch_,routedEditorOwner_\)/);
 assert.match(main,/shownEpoch=retainedEpoch/);
 assert.match(main,/retainedEpoch=ep/);
 assert.match(lab,/nativeAccepted_\.fxOwners->retainsEditor\(owner\)/);
 const start=main.indexOf('if(owner && owner->processor)');
 assert.ok(start>=0,'routed FX editor source must exist');
 const editor=main.slice(start,main.indexOf('catch(const std::exception& ex)',start));
 assert.ok(editor.includes('shownOwner==owner'),
   'reopen only exact current accepted PCM owner');
 assert.ok(editor.includes('shownWindow->present()'),
   'same FX instance must be focused instead of recreated');
 const fresh=editor.slice(editor.indexOf('} else try {'));
 assert.ok(fresh.includes('closePluginEditor();') &&
   fresh.indexOf('closePluginEditor();')<fresh.indexOf('createHostedAudioEditor(*owner->processor)'),
   'DIFFERENT instance must destroy old window before asking for new editor');
 const change=main.slice(main.indexOf('void changeListenerCallback'),main.indexOf('void timerCallback()',main.indexOf('void changeListenerCallback')));
 assert.doesNotMatch(change,/if\(routedEditorOwner_\)closePluginEditor/);
});


test("M5 logical-only editor proves the exact ACK revision and full FX/WIRE effect",()=>{
 const id="9007199254740993",main="9007199254740994";
 const initial={
   revision:"10",mainChannelId:main,
   channels:[
     {id,role:"INPUT",name:"Vocal",processingFormat:"MONO",primaryOutId:main,
       controls:{faderDb:-6,pan:0,mute:false,solo:false},fxRackBypassed:false,
       inserts:[{id:"fx-slot-0",catalogId:"vendor.a",bypassed:false},
                {id:"fx-slot-1",catalogId:"vendor.b",bypassed:false}]},
     {id:main,role:"MAIN",name:"MAIN",processingFormat:"STEREO",
       controls:{faderDb:0,pan:0,mute:false,solo:false},inserts:[]}
   ],
   edges:[{edgeId:"101",from:id,to:main,primaryOutput:true,
     enabled:true,tap:"POST_FADER",gainLinear:1}]
 };
 const originalCatalogs=["vendor.a","vendor.b"];
 const beforeEdges=clone(initial.edges);
 const next=()=>{const graph=clone(initial);graph.revision="11";return graph;};
 const pending=(operation,payload={},extras={})=>({
   operation,channelId:id,payload,ackRevision:"11",
   beforeChannelExists:true,beforeInsertCatalogs:originalCatalogs,
   beforeEdges,...extras
 });
 const proves=(p,g)=>protocol.logicalReadbackProves(p,g);
 let g=next();g.channels[0].inserts.push({id:"fx-slot-2",catalogId:"vendor.a",bypassed:false});
 assert.equal(proves(pending("ADD_FX_REFERENCE",{catalogId:"vendor.a"}),g),true);
 g=next();assert.equal(proves(pending("ADD_FX_REFERENCE",{catalogId:"vendor.a"}),g),false,
   "existing instance of same catalog cannot masquerade as new insert");
 g=next();g.channels[0].inserts.splice(0,1);
 assert.equal(proves(pending("REMOVE_FX_REFERENCE",{slot:0,beforeCount:2}),g),true);
 g=next();g.channels[0].inserts.splice(1,1);
 assert.equal(proves(pending("REMOVE_FX_REFERENCE",{slot:0,beforeCount:2}),g),false,
   "identical rack length with wrong slot removed is not accepted");
 g=next();g.channels[0].controls.faderDb=-12;
 assert.equal(proves(pending("SET_FADER",{valueDb:-12}),g),true);
 assert.equal(proves(pending("SET_FADER",{valueDb:"-12"}),g),false);
 assert.equal(proves(pending("SET_FADER",{valueDb:-12}, {ackRevision:"12"}),g),false,
   "same effect at wrong revision is not proof of this ACK");
 assert.equal(proves(pending("SET_FADER",{valueDb:-12}),{revision:"11"}),false);
 g=next();g.channels[0].controls.mute=true;
 assert.equal(proves(pending("SET_MUTE",{enabled:true}),g),true);
 assert.equal(proves(pending("SET_MUTE",{enabled:"true"}),g),false);
 g=next();g.channels[0].inserts[1].bypassed=true;
 assert.equal(proves(pending("SET_FX_SLOT_BYPASS",{slot:1,enabled:true}),g),true);
 assert.equal(proves(pending("SET_FX_SLOT_BYPASS",{slot:2,enabled:true}),g),false);
 g=next();g.channels[0].fxRackBypassed=true;
 assert.equal(proves(pending("SET_FX_RACK_BYPASS",{enabled:true}),g),true);
 g=next();g.channels[0].name="New Vocal";
 assert.equal(proves(pending("RENAME_CHANNEL",{name:"New Vocal"}),g),true);
 g=next();g.channels[0].processingFormat="STEREO";
 assert.equal(proves(pending("SET_PROCESSING_FORMAT",{format:"STEREO"}),g),true);
 // Offline parallel SEND editing must be proved from an independent
 // post-ACK readback, not inferred from a successful disk-commit ACK.
 const send={edgeId:"102",from:id,to:"9007199254740999",primaryOutput:false,
   enabled:true,sendEnabled:true,tap:"POST_FADER",tapStage:"POST_FADER",
   gainLinear:.5};
 const prior=[...beforeEdges,send];
 const sendPending=(op,payload)=>pending(op,payload,{beforeEdges:prior});
 g=next();g.edges.push({...send,tap:"SPLIT_PRE",tapStage:"PRE_FADER"});
 assert.equal(proves(sendPending("SET_SEND_TAP",{edgeId:"102",tap:"PRE_FADER"}),g),true);
 assert.equal(proves(sendPending("SET_SEND_TAP",{edgeId:"102",tap:"POST_FADER"}),g),false,
   "ACK alone cannot prove PRE/POST without changed tapStage");
 assert.equal(proves(sendPending("SET_SEND_TAP",{edgeId:"101",tap:"PRE_FADER"}),g),false,
   "MAIN primary cable never accepts a parallel tap");
 g=next();g.edges.push({...send,tap:"SPLIT_PRE",tapStage:"POST_FADER"});
 assert.equal(proves(sendPending("SET_SEND_TAP",{edgeId:"102",tap:"PRE_FADER"}),g),false,
   "old PRE_FX marker must not masquerade as post-FX PRE_FADER");
 g=next();g.edges.push({...send,to:main,tapStage:"PRE_FADER"});
 assert.equal(proves(sendPending("SET_SEND_TAP",{edgeId:"102",tap:"PRE_FADER"}),g),false,
   "a redirected EdgeId cannot prove the requested SEND tap");
 g=next();g.edges.push({...send,sendEnabled:false});
 assert.equal(proves(sendPending("SET_SEND_ENABLED",{edgeId:"102",enabled:false}),g),true);
 assert.equal(proves(sendPending("SET_SEND_ENABLED",{edgeId:"102",enabled:true}),g),false);
 assert.equal(proves(sendPending("SET_SEND_ENABLED",{edgeId:"101",enabled:false}),g),false,
   "primary output power is not a SEND ON/OFF command");
 g=next();g.channels.push({id:"9007199254740995",role:"GROUP"});
 assert.equal(proves(pending("CREATE_GROUP",{},{
   channelId:null,createdId:"9007199254740995"}),g),true);
 assert.equal(proves(pending("CREATE_GROUP",{},{
   channelId:null,createdId:"9007199254740996"}),g),false);
 g=next();g.channels.shift();g.edges=[];
 assert.equal(proves(pending("DELETE_CHANNEL"),g),true);
 assert.equal(proves(pending("DELETE_CHANNEL",{},{
   beforeChannelExists:false}),g),false);
 const wire=change=>pending("WIRE_TRANSACTION",{changes:[change]});
 g=next();g.channels[0].primaryOutId=undefined;g.edges=[];
 assert.equal(proves(wire({op:"DISCONNECT",from:id}),g),true);
 assert.equal(proves(wire({op:"REMOVE",from:id,edgeId:"101"}),g),true);
 assert.equal(proves(wire({op:"REMOVE",from:id,edgeId:"999"}),g),false);
 g=next();g.edges.push({edgeId:"102",from:id,to:main,primaryOutput:false,
   enabled:true,tap:"POST_FADER",gainLinear:.5});
 assert.equal(proves(wire({op:"CONNECT",from:id,to:main,gainLinear:.5}),g),true);
 assert.equal(proves(wire({op:"CONNECT",from:id,to:main,gainLinear:.8}),g),false);
 assert.equal(proves(wire({op:"CONNECT",from:id,to:main}),next()),false,
   "existing edge to the destination is not evidence of a new send");
 g=next();g.edges[0].to="9007199254740996";
 assert.equal(proves(wire({op:"RETARGET",from:id,edgeId:"101",to:"9007199254740996"}),g),true);
 assert.equal(proves(wire({op:"RETARGET",from:id,edgeId:"101",to:main}),next()),false);
 assert.equal(proves(wire({op:"ROUTE_ONLY",from:id,to:main}),next()),true);
 assert.equal(proves(wire({op:"ROUTE_ONLY",from:id,to:"9007199254740996"}),next()),false);
 assert.equal(proves(pending("WIRE_TRANSACTION",{changes:[
   {op:"ROUTE_ONLY",from:id,to:main},{op:"CONNECT",from:id,to:main}
 ]}),next()),false,"JUCE accepts one offline WIRE change, never just changes[0]");
 assert.equal(proves(pending("WIRE_TRANSACTION",{changes:[
   {op:"ROUTE_ONLY",from:main,to:main}
 ]}),next()),false,"cross-channel readback cannot prove another ChannelId");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/protocol\.logicalReadbackProves\(p,offline\.graph\)/);
 assert.doesNotMatch(bridge,/function logicalReadbackProves\(/);
 assert.match(bridge,/beforeInsertCatalogs:/);
 assert.match(bridge,/beforeEdges:/);
});


test("M5 native presentation policy is pure and preserves editable ChannelId identity rules",()=>{
 const n=protocol.validPresentationName,c=protocol.validPresentationColor,
       i=protocol.validPresentationIcon;
 for(const valid of ["Vocal","Channel 07","Mic 🎙","Guitar","X".repeat(30)])
   assert.equal(n(valid),true);
 for(const invalid of [""," X","X ","X".repeat(31),"line\nbreak","name\twith tab",null,123])
   assert.equal(n(invalid),false);
 for(const valid of ["#abcdef","#AABBCC","#012345"])
   assert.equal(c(valid),true);
 for(const invalid of ["abc","#fff","#abcdefghijkl","#xyzxyz",undefined,23])
   assert.equal(c(invalid),false);
 for(const valid of ["🎙","♫","♬","⊞","◎"])
   assert.equal(i(valid),true);
 for(const invalid of ["🐙","A",null,0,"🎙♫"])
   assert.equal(i(invalid),false);
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/const legalName=protocol\.validPresentationName/);
 assert.match(bridge,/const legalColor=protocol\.validPresentationColor/);
 assert.match(bridge,/const legalIcon=protocol\.validPresentationIcon/);
 assert.match(bridge,/const operatorMessage=protocol\.operatorMessage/);
 assert.doesNotMatch(bridge,/const operatorMessage = message =>/);
});

test("M5 native operator status renders useful actions without exposing internal QA state",()=>{
 const f=protocol.operatorMessage;
 const pairs=[
  ["HARDWARE_FAT_PENDING","Mixer ready"],
  ["NATIVE PCM GRAPH pending","Mixer ready"],
  ["FAT PENDING; output muted","Output muted"],
  ["FAT FAILED; output enabled","Output enabled"],
  ["DEVICE_DISCONNECTED","Audio device unavailable · Open Audio I/O"],
  ["Native routing rejected","Routing not changed · Check the connection and try again"],
  ["NATIVE: STALE_NATIVE_WIRE_GRAPH","Routing not changed · Check the connection and try again"],
  ["Native routing failed","Routing not changed · Check the connection and try again"],
  ["Native VST3 plugin rejected","Effect change failed · Previous settings kept"],
  ["WIRE applying, waiting for graph revision","Updating channel routing…"],
  ["Native VST3 plugin loading","Applying effect…"],
  ["native FX rejected","Effect change failed · Previous settings kept"],
  ["INVALID_NATIVE_SNAPSHOT","Change not applied · Previous mixer settings kept"],
  ["ACK_READBACK_EFFECT_MISMATCH","Change not applied · Previous mixer settings kept"],
  ["PENDING_NATIVE_ACK","Applying mixer changes…"],
  ["Mixer input accepted","Mixer input accepted"],
  ["",""],
  ["  User visible message  ","User visible message"]
 ];
 for(const [raw,expected] of pairs)
   assert.equal(f(raw),expected,`status ${JSON.stringify(raw)} should stay operator-safe`);
 // No network, DOM, snapshot or runtime can be accessed by this pure helper.
 const state=acceptedFixture(),before=clone(state);
 f("Native routing rejected");f("PENDING_NATIVE_ACK");
 assert.deepEqual(state,before);
 assert.ok(protocol.normalizeSnapshot(state).admitted);
});


test("O2 independent per-track external ASIO transactions and clear rejected ACK",()=>{
 const src=read("web/approved-native-bridge.js");
 const builder=read("scripts/build_approved_native_gui.py");
 assert.match(src,/const pendingExternal=new Map\(\)/);
 assert.match(src,/pendingExternal\.has\(id\)/);
 assert.match(src,/pendingExternal\.set\(id,/);
 assert.match(src,/pendingExternal\.delete\(id\)/);
 assert.match(src,/select\?\.removeAttribute\("aria-busy"\)/);
 assert.match(src,/if\(!pendingExternal\.has\(id\)\)select\.removeAttribute/);
 const vm=read("web/approved-channel-strip-view-model.js");
 assert.match(vm,/SHARED with /);
 assert.match(vm,/signals SUM/);
 assert.match(src,/stripVM\.externalOutputPlan\(/);
 const page=read("site/preview-v2/index.html");
 const css=read("site/preview-v2/assets/mixer-section-controls.css");
 assert.match(page,/external-out-state/);
 assert.match(css,/external-out-state/);
 assert.doesNotMatch(builder,/class=\\\"route-field external-out-field/,
   "Native should never append its own EXT DOM");
});

test("O2 SOURCE ONLY confirmed ASIO EXTERNAL OUT auto-arms without a second Monitor click",()=>{
 const native=read("src/Main.cpp"),engine=read("src/LiveAudioEngine.h");
 const routing=read("src/LiveDryRoutingLab.h");
 const start=native.indexOf('if(action=="routingExternalOutput")');
 const end=native.indexOf('if(action=="routingChannelPatch")',start);
 assert.ok(start>=0&&end>start,"real native external-output action is present");
 const control=native.slice(start,end);
 assert.match(control,/error=engine_\.setPhysicalExternalOutput/);
 assert.match(control,/if\(left>=0 && !operatorOutputMuted_ && !engine_\.isMonitorEnabled\(\)\)\s*requestAutomaticMain\(false\)/);
 assert.match(control,/emitApprovedNativeSnapshot\(\); \/\/ callback readback proves LIVE/);
 assert.match(native,/operatorOutputMuted_=!arm; \/\/ an explicit user control/);
 assert.match(native,/if\(operatorOutputMuted_\)\{autoMainEpoch_=0;return;\}/);
 assert.match(native,/autoMainRequiresInput_ &&\s*!engine_\.hasVerifiedExternalOutputIntent\(\)/);
 assert.match(engine,/bool hasVerifiedExternalOutputIntent\(\) const noexcept/);
 assert.match(routing,/externalOutputEpoch_!=epoch\(\)/);
 assert.match(routing,/externalOutputVerified_\.empty\(\)/);
 assert.doesNotMatch(control,/setMonitorEnabled\(true\)|setOutputMuted\(false\)/,
     "Control delegates one-shot automatic output acceptance, not blind arm");
});

test("O2 external OUT never reports LIVE while driver output is muted",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const css=read("site/preview-v2/assets/mixer-section-controls.css");
 const projection=read("web/approved-device-view-model.js");
 assert.match(projection,/const installed=row\?\.status==="LIVE"&&match/);
 assert.match(projection,/snapshot\?\.device\?\.outputArmed===true\?"LIVE":"MUTED"/);
 assert.match(projection,/state==="MUTED"\?"ASIO pair connected/);
 assert.match(bridge,/deviceVM\.externalPresentation\(next,row,choices\)/);
 assert.match(bridge,/stripVM\.externalPending\(pending,next\?\.device\?\.epoch,state,row\)/);
 assert.match(read("web/approved-channel-strip-view-model.js"),
   /state==="LIVE"\|\|state==="MUTED"/);
 assert.match(bridge,/EXTERNAL OUT patched · hardware output muted/);
 assert.match(css,/external-out-state\[data-status="MUTED"\]/);
});

test("O2 SHARED output means two or more ChannelIds, not one",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const projection=read("web/approved-device-view-model.js");
 assert.match(projection,/shared>1\?" · SHARED "\+shared\+" SOURCES"/);
 assert.match(projection,/const shared=Array\.isArray\(snapshot\?\.raw\?\.externalOutputs\)/);
 assert.doesNotMatch(projection,/shared\?" · SHARED "\+shared/);
 assert.match(bridge,/deviceVM\.outputChoices\(hardware\)/);
});

test("O3 OUTPUTS overview reflects real ASIO 8-pin ownership and callback status",()=>{
  const state=acceptedFixture(),voice=state.graph.channels[0].id;
  state.device.backend="ASIO";
  state.device.outputArmed=true;
  state.device.enabledOutputs=[0,1,2,3,4,5,6,7];
  state.device.outputEndpoints=state.device.enabledOutputs.map(n=>({
    id:"out:"+n,port:n,callback:n,label:"ASIO "+(n+1),active:true
  }));
  const group=(id,name)=>({id,name,role:"GROUP",processingFormat:"STEREO",
    controls:{faderDb:0,pan:0,mute:false,solo:false},inserts:[],sends:[]});
  state.graph.channels.splice(1,0,group("9007199254740994","Music Bus"),
    group("9007199254740995","Monitor Bus"));
  state.physicalMainOutputs=[0,1];
  state.physicalMonitorOutputs=[2,3];
  state.externalOutputs=[
    {channelId:voice,physicalLeft:4,physicalRight:5,status:"LIVE"},
    {channelId:"9007199254740994",physicalLeft:6,physicalRight:7,status:"LIVE"},
    {channelId:"9007199254740995",physicalLeft:6,physicalRight:7,status:"LIVE"}
  ];
  const read=()=>protocol.physicalOutputOverview(protocol.normalizeSnapshot(state));
  let out=read();
  assert.equal(out.length,4);
  assert.deepEqual(out.map(row=>row.pair),["OUT 1/2","OUT 3/4","OUT 5/6","OUT 7/8"]);
  assert.equal(out[0].role,"MAIN");
  assert.equal(out[1].role,"MONITOR");
  assert.equal(out[2].status,"LIVE");
  assert.equal(out[2].owners,"Vocal");
  assert.equal(out[2].shared,1,"single source must never be called SHARED");
  assert.equal(out[3].status,"LIVE");
  assert.equal(out[3].shared,2);
  assert.match(out[3].owners,/Music Bus \+ Monitor Bus/);
  state.device.outputArmed=false;
  out=read();
  assert.equal(out[0].status,"MUTED");
  assert.equal(out[2].status,"MUTED");
  assert.equal(out[3].status,"MUTED");
  assert.equal(out[3].shared,2,"verified patch ownership persists through mute");
  state.device.outputArmed=true;
  state.externalOutputs[2].status="PENDING";
  out=read();
  assert.equal(out[3].shared,1,"pending patch is not already installed PCM");
  state.externalOutputs[1].status="NEEDS_REMAP";
  out=read();
  assert.equal(out[3].status,"PENDING");
  assert.equal(out[3].shared,0);
  state.device.outputEndpoints[7].active=false;
  out=read();
  assert.equal(out.find(row=>row.role==="EXTERNAL"&&row.pair==="OUT 7/8").status,
    "NEEDS_REMAP","disabled physical pin can never be LIVE");
  state.device.backend="WASAPI";
  assert.deepEqual(read(),[],"ASIO patch view must not invent WASAPI pairs");
});

test("O3 OUTPUTS stays in one existing Audio I/O with safe, read-only DOM",()=>{
  const bridge=read("web/approved-native-bridge.js");
  assert.match(bridge,/protocol\.physicalOutputOverview\(next\)/);
  assert.match(bridge,/paintPhysicalOutputs\(next\)/);
  assert.match(bridge,/paintPhysicalOutputs\(hardware\)/);
  assert.match(bridge,/class="armix-output-overview" data-audio-output-overview/);
  assert.match(bridge,/class="armix-output-heading"/);
  assert.doesNotMatch(bridge,/<details data-audio-output-overview/,
    "read-only OUTPUTS must not create a second Advanced disclosure");
  assert.match(bridge,/data-audio-output-matrix role="list"/);
  assert.match(bridge,/owner\.textContent=patch\.role/);
  assert.doesNotMatch(bridge,/owner\.innerHTML=patch/);
});

test("Guitar FX picker binding is real, visible and matches native installed registry",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const catalog=read("src/NativeBuiltInFx.h");
 const processor=read("engine/builtin_fx/GuitarRigProcessor.h");
 const host=read("src/Main.cpp");
 const guiBuilder=read("scripts/build_approved_native_gui.py");
 const id="armix.builtin.guitar-rig.v1";
 const start=bridge.indexOf("let nativeBuiltins=Object.freeze([");
 const end=bridge.indexOf("const presentFxCatalog=",start);
 assert.ok(start>0 && end>start,"compiled native catalog exists");
 const builtinSlice=bridge.slice(start,end);
 assert.ok(builtinSlice.includes('id:"'+id+'",name:"Guitar FX Rig"'));
 assert.ok(builtinSlice.includes('category:"Guitar",format:"Native",source:"builtin"'));
 assert.ok(processor.includes('catalogIdentity="'+id+'"'),
           "native processor ID matches UI catalog");
 assert.ok(catalog.includes('GuitarRigProcessor::catalogIdentity,"Guitar FX Rig","Guitar",true'),
           "C++ native registry advertises exactly the same visible FX");
 assert.ok(host.includes('for(const auto& fx:armix::builtin_fx::nativeBuiltInFx)'));
 assert.ok(host.includes('entry->setProperty("showInPicker",fx.showInPicker)'));
 assert.ok(host.includes('setProperty("builtIns",juce::var(builtIns))'));
 assert.ok(bridge.includes("if(Array.isArray(catalog.builtIns))"));
 assert.ok(bridge.includes("nativeBuiltins=catalog.builtIns.filter"));
 assert.ok(bridge.includes("const presentFxCatalog=()=>scope?.setNativeCatalog?.("));
 assert.ok(guiBuilder.includes("setNativeCatalog(rows)"));
 assert.ok(guiBuilder.includes("renderPluginCatalog();renderFastPicker();"));
 assert.ok(bridge.includes('fxRackVM.nativeInsertPlan(accepted,'));
 assert.ok(bridge.includes('controller.intent("LOAD_INSERT",plan.channelId,plan.payload)'));
 assert.ok(bridge.includes('pendingFxInsert={authority:"CHANNEL_ID"'));
});

test("SOF double-click and target selection never commandeer MAIN fader",()=>{
  const html=read("site/preview-v2/index.html");
  const bridge=read("web/approved-native-bridge.js");
  const bundle=read("scripts/build_approved_native_gui.py");
  assert.match(html,/function selectedSofDestination\(\)/);
  assert.match(html,/sofTargetId=target\?\.id\?\?null/);
  assert.match(html,/const send=sofEnabled&&isSofSource\(t\)/);
  assert.match(html,/fader\.dispatchEvent\(new Event\('change'/);
  assert.match(bundle,/get sofTargetId\(\)\{return sofTargetId;\}/);
  assert.match(bridge,/x\.id===scope\.sofTargetId/);
  const start=bridge.indexOf("consumeDoubleClick(event,t)");
  const end=bridge.indexOf("applyIdentity:",start);
  assert.ok(start>0&&end>start,"native delegate must own double-click");
  const handler=bridge.slice(start,end);
  assert.ok(handler.indexOf("scope?.sofEnabled") <
            handler.indexOf('dispatchControlCommand'),
    "SOF must intercept fader reset BEFORE unified native control routing");
  assert.match(handler,/requestWireGain\(\{sourceId:t\.id,edgeId:id,levelDb:0\}\)/);
  assert.match(handler,/intent\("SET_INPUT_GAIN",t,\{valueDb:0\}\)/);
});

test("MVVM device selector projects only the verified native physical input pins",()=>{
  const hardware={device:{backend:"ASIO",enabledInputs:[0,1,3],
    inputEndpoints:[
      {port:0,label:"Mic 1",active:true},{port:1,label:"Mic 2",active:true},
      {port:2,label:"Disabled",active:true},{port:3,label:"Mic 4",active:true},
      {port:4,label:"Inactive",active:false}]},raw:{}};
  const original=JSON.stringify(hardware);
  assert.deepEqual(deviceVM.activeInputPins(hardware).map(p=>p.port),[0,1,3]);
  const choices=deviceVM.inputChoices(hardware);
  assert.deepEqual(choices.map(x=>[x.mode,x.left,x.right]),[
    ["NONE",-1,-1],["MONO",0,-1],["MONO",1,-1],["MONO",3,-1],["STEREO",0,1]
  ]);
  assert.equal(deviceVM.inputLabel(hardware,{mode:"MONO",left:3,right:-1}),
    "Mono · Mic 4 [IN 4]");
  assert.equal(deviceVM.inputLabel(hardware,{mode:"STEREO",left:0,right:1}),
    "Stereo · IN 1 + 2 · Mic 1 / Mic 2");
  assert.equal(deviceVM.inputLabel(hardware,{mode:"STEREO",left:1,right:2}),"None");
  assert.equal(deviceVM.inputLabel(hardware,{mode:"BUS"}),"Internal BUS (read-only)");
  assert.equal(JSON.stringify(hardware),original,"view must not mutate native readback");
  assert.deepEqual(deviceVM.inputChoices({device:{inputEndpoints:[],enabledInputs:[]}}),
    [{label:"None",mode:"NONE",left:-1,right:-1}]);
  assert.deepEqual(deviceVM.activeInputPins({device:{inputEndpoints:hardware.device.inputEndpoints}}),[]);
});
test("MVVM external outputs never offer MAIN/MONITOR pins, disabled pairs, or WASAPI copies",()=>{
  const snapshot={device:{backend:"ASIO",enabledOutputs:[0,1,2,3,4,5,7,8],
    outputArmed:false,outputEndpoints:Array.from({length:9},(_,port)=>({
      port,label:"Physical "+(port+1),active:port!==6
    }))},raw:{physicalMainOutputs:[0,1],physicalMonitorOutputs:[4,5],
    externalOutputs:[{physicalLeft:2,physicalRight:3},{physicalLeft:2,physicalRight:3}]}};
  const before=JSON.stringify(snapshot);
  const choices=deviceVM.outputChoices(snapshot);
  assert.equal(choices.length,2);
  assert.deepEqual(choices[0],{label:"None",left:-1,right:-1});
  assert.deepEqual([choices[1].left,choices[1].right],[2,3]);
  assert.match(choices[1].label,/SHARED 2 SOURCES/);
  assert.equal(deviceVM.outputChoices({...snapshot,device:{...snapshot.device,
    backend:"WASAPI"}}).length,1);
  assert.equal(JSON.stringify(snapshot),before,"output projection must be read-only");
});
test("MVVM external readback needs verified pair and output-arm, never ACK-only LIVE",()=>{
  const snapshot={device:{backend:"ASIO",outputArmed:false,
    outputEndpoints:[{port:2,label:"OUT L",active:true},{port:3,label:"OUT R",active:true}],
    enabledOutputs:[2,3]},raw:{physicalMainOutputs:[],physicalMonitorOutputs:[],externalOutputs:[]}};
  const choices=deviceVM.outputChoices(snapshot);
  const installed={status:"LIVE",physicalLeft:2,physicalRight:3};
  const muted=deviceVM.externalPresentation(snapshot,installed,choices);
  assert.equal(muted.state,"MUTED");
  assert.equal(muted.marker,"MUTED");
  assert.equal(muted.value,choices[1].label);
  assert.equal(deviceVM.externalPresentation({...snapshot,
    device:{...snapshot.device,outputArmed:true}},installed,choices).state,"LIVE");
  assert.equal(deviceVM.externalPresentation(snapshot,{...installed,status:"PENDING"},choices).state,
    "PENDING");
  const invalid=deviceVM.externalPresentation(snapshot,
    {...installed,physicalLeft:6,physicalRight:7},choices);
  assert.equal(invalid.state,"NEEDS_REMAP");
  assert.match(invalid.value,/Needs Remap/);
  assert.equal(deviceVM.externalPresentation(snapshot,null,choices).state,"NONE");
  assert.equal(deviceVM.externalPresentation(null,installed).state,"NEEDS_REMAP");
});
test("MVVM native bridge uses the one device read model, not retired web/mixer-io.js",()=>{
  const bridge=read("web/approved-native-bridge.js");
  const build=read("scripts/build_approved_native_gui.py");
  assert.match(bridge,/deviceVM\.inputChoices\(hardware\)/);
  assert.match(bridge,/deviceVM\.externalPresentation\(next,row,choices\)/);
  assert.match(bridge,/deviceVM\.outputChoices\(hardware\)/);
  assert.doesNotMatch(bridge,/const monoLabel=|const hardwareOutputPairs=\(\)=>\{/);
  assert.match(build,/assets\/approved-device-view-model\.js/);
  assert.match(build,/web" \/ "approved-device-view-model\.js"/);
  assert.doesNotMatch(bridge,/AudioContext|AudioWorklet|createPluginInstance/);
});


// Device Setup MVVM: the same native /preview/ bundle owns one transient UI
// transaction, but only installed schema-v6 readback may make it accepted.
function nativeDeviceSetupFixture() {
  const active={backend:"ASIO",captureDevice:"ASIO Test",playbackDevice:"ASIO Test",
    deviceGeneration:12,open:true,authorized:true,rate:48000,buffer:256,
    enabledInputs:[0],enabledOutputs:[0,1]};
  const inventory={active,types:[{name:"ASIO",separate:false,
    inputs:["ASIO Test"],outputs:["ASIO Test"]}]};
  const draft={backend:"ASIO",capture:"ASIO Test",playback:"ASIO Test",
    inputPins:[0],outputPins:[0,1],rate:48000,buffer:256};
  const begin=requestId=>deviceVM.setupBegin(deviceVM.setupInitial(),{
    inventory,draft,requestId,baselineEpoch:"12"});
  const ack=(requestId,overrides={})=>({
    requestId,ok:true,state:"ACCEPTED_LIVE",
    active:{...active,deviceGeneration:13},mixerBound:true,
    outputMuted:false,...overrides});
  const readback={raw:{schemaVersion:6},retained:false,admitted:true,
    productLive:true,device:{epoch:"13",backend:active.backend,
      captureDevice:active.captureDevice,playbackDevice:active.playbackDevice,
      sampleRate:48000,bufferFrames:256,enabledInputs:[0],
      enabledOutputs:[0,1],outputArmed:true}};
  return {active,inventory,draft,begin,ack,readback};
}
test("Device Setup VM requires matching ACK AND later installed v6 readback, never ACK-only LIVE",()=>{
  const f=nativeDeviceSetupFixture();
  const begun=f.begin("approved-io-42");
  assert.ok(begun);assert.equal(begun.request.action,"audioDeviceApply");
  assert.equal(begun.request.expectedDeviceGeneration,12);
  assert.equal(begun.request.config.backend,"ASIO");
  assert.equal(begun.request.config.sampleRate,48000);
  const waiting=deviceVM.setupAck(begun.state,f.ack("approved-io-42"));
  assert.equal(waiting.phase,"VERIFYING");
  assert.equal(waiting.pending.requestId,"approved-io-42");
  assert.equal(waiting.message,"");
  const beforeAckDevice={...f.readback,device:{...f.readback.device,epoch:"12"}};
  assert.equal(deviceVM.setupReadback(waiting,beforeAckDevice),waiting,
    "matching old hardware cannot prove the later Apply ACK");
  assert.equal(deviceVM.setupReadback(waiting,{...f.readback,device:{
    ...f.readback.device,bufferFrames:128}}),waiting,
    "rate/buffer mismatch never proves installed device");
  const ready=deviceVM.setupReadback(waiting,f.readback);
  assert.equal(ready.phase,"ACCEPTED");
  assert.equal(ready.pending,null);
  assert.match(ready.message,/Output live/);
  assert.equal(deviceVM.setupReadback(waiting,{...f.readback,device:{
    ...f.readback.device,epoch:"14"}}).phase,"ACCEPTED",
    "routing epoch may advance beyond JUCE's generation");
  assert.equal(deviceVM.setupAck(ready,f.ack("approved-io-42")),ready,
    "late ACK cannot resurrect a finished request");
});
test("Device Setup VM fails closed for device loss, unadmitted graph, stale epoch and stale ACK",()=>{
  const f=nativeDeviceSetupFixture();
  const waiting=deviceVM.setupAck(f.begin("approved-io-5").state,f.ack("approved-io-5"));
  const deviceOnly=deviceVM.setupReadback(waiting,{
    ...f.readback,admitted:false,productLive:false});
  assert.equal(deviceOnly.phase,"ACCEPTED",
    "the real device may be ready before a ChannelId PCM graph is admitted");
  assert.match(deviceOnly.message,/route not yet verified/i);
  assert.doesNotMatch(deviceOnly.message,/Mixer ready|Output live/);
  const lost=deviceVM.setupReadback(waiting,{...f.readback,
    device:{...f.readback.device,epoch:"14",sampleRate:0,enabledOutputs:[]}});
  assert.equal(lost.phase,"RECOVERY_REQUIRED");
  assert.equal(lost.pending,null);
  const stale=deviceVM.setupReadback(waiting,{...f.readback,
    device:{...f.readback.device,epoch:"11"}});
  assert.equal(stale.phase,"STALE");
  assert.equal(deviceVM.setupAck(waiting,f.ack("approved-io-4")),waiting,
    "old request ID cannot change the current transaction");
  assert.equal(deviceVM.setupReadback(waiting,{...f.readback,retained:true}).phase,
    "RECOVERY_REQUIRED");
});
test("Device Setup VM rejects driver negative ACK, safe rollback, wrong endpoint and absent generation",()=>{
  const f=nativeDeviceSetupFixture();
  const begun=f.begin("approved-io-6");
  const fail=deviceVM.setupAck(begun.state,{requestId:"approved-io-6",ok:false,
    state:"REJECTED",error:"Driver refused",active:f.active});
  assert.equal(fail.phase,"REJECTED");
  assert.match(fail.message,/Driver refused/);
  const rollback=deviceVM.setupAck(begun.state,{requestId:"approved-io-6",ok:false,
    state:"RESTORED_MUTED",error:"Driver refused",active:f.active});
  assert.equal(rollback.phase,"REJECTED");
  assert.match(rollback.message,/Previous device restored/);
  assert.deepEqual(f.inventory.active.enabledOutputs,[0,1],
    "refused apply cannot mutate the installed output pins in the UI");
  const unverified=deviceVM.setupAck(begun.state,f.ack("approved-io-6",{
    active:{...f.active,deviceGeneration:13,authorized:false}}));
  assert.equal(unverified.phase,"REJECTED");
  const wrongEndpoint=deviceVM.setupAck(begun.state,f.ack("approved-io-6",{
    active:{...f.active,deviceGeneration:13,playbackDevice:"Another ASIO"}}));
  assert.equal(wrongEndpoint.phase,"REJECTED",
    "same requestId cannot approve another physical output");
  const staleGeneration=deviceVM.setupAck(begun.state,f.ack("approved-io-6",{
    active:{...f.active,deviceGeneration:11}}));
  assert.equal(staleGeneration.phase,"REJECTED",
    "same requestId cannot approve an older driver generation");
  const accepted=deviceVM.setupAck(begun.state,f.ack("approved-io-6"));
  assert.equal(deviceVM.setupReadback(accepted,{...f.readback,
    device:{...f.readback.device,playbackDevice:"Unexpected"}}).phase,"VERIFYING");
  assert.equal(deviceVM.setupBegin(deviceVM.setupInitial(),{
    inventory:{...f.inventory,active:{...f.active,deviceGeneration:null}},
    draft:f.draft,requestId:"invalid"}),null);
});
test("Device Setup VM times out both before and after ACK; late events never apply",()=>{
  const f=nativeDeviceSetupFixture();
  const begun=f.begin("approved-io-7");
  assert.equal(deviceVM.setupBegin(begun.state,{inventory:f.inventory,
    draft:f.draft,requestId:"approved-io-8"}),null);
  const timed=deviceVM.setupTimeout(begun.state,"approved-io-7");
  assert.equal(timed.phase,"TIMEOUT");
  assert.equal(deviceVM.setupAck(timed,f.ack("approved-io-7")),timed);
  const verifying=deviceVM.setupAck(begun.state,f.ack("approved-io-7"));
  const staleTimer=deviceVM.setupTimeout(verifying,"approved-io-6");
  assert.equal(staleTimer,verifying);
  assert.equal(deviceVM.setupTimeout(verifying,"approved-io-7").phase,"TIMEOUT");
});
test("Device Setup VM retains offline .smix and never promotes rollback to output LIVE",()=>{
  const f=nativeDeviceSetupFixture();
  const document={schema:"arsonmix.native.logical-mixer.v1",revision:"18446744073709551615",
    channels:[{id:"9007199254740993"}],outputArmed:false};
  const frozen=JSON.stringify(document);
  const begun=f.begin("approved-io-9");
  const awaiting=deviceVM.setupAck(begun.state,f.ack("approved-io-9"));
  const offline=deviceVM.setupReadback(awaiting,{...f.readback,retained:true,
    raw:{schemaVersion:6,logicalMixer:document},admitted:false,productLive:false});
  assert.equal(offline.phase,"RECOVERY_REQUIRED");
  assert.equal(JSON.stringify(document),frozen);
  assert.equal(deviceVM.setupAck(begun.state,{requestId:"approved-io-9",
    ok:false,state:"RESTORED_LIVE",active:f.active}).phase,"REJECTED");
  const text=read("web/approved-native-bridge.js");
  assert.match(text,/deviceVM\.setupReadback\(deviceState,next\)/);
  assert.match(text,/deviceVM\.setupAck\(deviceState,ack\)/);
  const setup=text.slice(text.indexOf("  // Compact DAW-style device preferences"),
    text.indexOf("  function installFirstSoundMenu()"));
  assert.doesNotMatch(setup,/const accepted=ack\.ok===true/);
  assert.doesNotMatch(text,/devicePanel\.dataset\.pendingRequest/);
  assert.equal((text.match(/backend\.emitEvent\("arMixAction",begun\.request\)/g)||[]).length,1);
});


// MV-2: native Device Setup dropdown/pin draft is ONE pure presentation
// projection; the old approved bridge now only reads it and paints the DOM.
function nativeDeviceDraftFixture() {
  const f=nativeDeviceSetupFixture();
  const active={...f.active,inputNames:["Mic 1","Mic 2"],
    outputNames:["L","R","AUX L","AUX R"],
    mainOutputs:[0,1],enabledInputs:[0],enabledOutputs:[0,1],
    rates:[48000,96000],buffers:[128,256]};
  return {active,types:[
    {name:"ASIO",separate:false,
      inputs:["ASIO Test","ASIO4ALL v2"],outputs:["ASIO Test","ASIO4ALL v2"]},
    {name:"Windows Audio",separate:true,
      inputs:["Mic"],outputs:["Speakers"]}
  ]};
}
test("MV-2 native Device Setup initial draft and verified select options are pure",()=>{
  const inventory=nativeDeviceDraftFixture(),prior=JSON.stringify(inventory);
  const draft=deviceVM.setupDraft(inventory),untouched=JSON.stringify(draft);
  assert.equal(draft.backend,"ASIO");
  assert.equal(draft.capture,"ASIO Test");
  assert.equal(draft.playback,"ASIO Test");
  assert.deepEqual(draft.inputPins,[0]);
  const view=deviceVM.setupPresentation(inventory,draft);
  assert.equal(view.same,true);
  assert.equal(view.canApply,true);
  assert.equal(view.canAssignMain,true);
  assert.deepEqual(view.backendOptions,["ASIO","Windows Audio"]);
  assert.deepEqual(view.rateOptions,["0","48000","96000"]);
  assert.deepEqual(view.bufferOptions,["0","128","256"]);
  assert.deepEqual(view.outputPins,[0,1]);
  assert.match(view.activeText,/MAIN L \/ R/);
  assert.equal(view.asio4allHint,false);
  assert.equal(JSON.stringify(draft),untouched);
  assert.equal(JSON.stringify(inventory),prior,
    "view must not mutate JUCE active inventory or .smix");
  assert.equal(deviceVM.setupDraft(null),null);
});
test("MV-2 Device Setup backend/capture switches preserve output-only and duplex policy",()=>{
  const inventory=nativeDeviceDraftFixture();
  const from=deviceVM.setupDraft(inventory);
  const external=deviceVM.setupEditDraft(from,inventory,{field:"backend",value:"Windows Audio"});
  assert.equal(external.backend,"Windows Audio");
  assert.equal(external.capture,"Mic");
  assert.equal(external.playback,"Speakers");
  assert.equal(external.rate,0);assert.equal(external.buffer,0);
  assert.equal(external.inputPins,null);assert.equal(external.outputPins,null);
  let outputOnly=deviceVM.setupEditDraft(external,inventory,{field:"capture",value:""});
  const view=deviceVM.setupPresentation(inventory,outputOnly);
  assert.equal(view.same,false);
  assert.deepEqual(view.inputOptions,["","Mic"]);
  assert.deepEqual(view.rateOptions,["0"]);
  assert.equal(view.canApply,true,"no hardware input allowed for separate backend");
  assert.equal(view.canAssignMain,false);
  const request=deviceVM.setupBegin(deviceVM.setupInitial(),{
    inventory,draft:outputOnly,requestId:"approved-io-output-only"});
  assert.ok(request);assert.equal(request.request.config.captureDevice,"");
  assert.deepEqual(request.request.config.enabledInputs,[]);
  assert.deepEqual(request.request.config.enabledOutputs,[]);
  assert.equal(deviceVM.setupEditDraft(outputOnly,inventory,{
    field:"capture",value:"Ghost microphone"}),outputOnly);
  assert.equal(deviceVM.setupEditDraft(outputOnly,inventory,{
    field:"backend",value:"Unknown backend"}),outputOnly);
  const invalidPair={...from,playback:"ASIO4ALL v2"};
  assert.equal(deviceVM.setupPresentation(inventory,invalidPair).canApply,false);
  assert.equal(deviceVM.setupBegin(deviceVM.setupInitial(),{
    inventory,draft:invalidPair,requestId:"bad-pair"}),null);
  assert.equal(from.backend,"ASIO","original selected device not mutated");
});
test("MV-2 Device Setup pin/rate edits are immutable, bounded and generation-scoped",()=>{
  const inventory=nativeDeviceDraftFixture();
  const original=deviceVM.setupDraft(inventory);
  const extra=deviceVM.setupEditDraft(original,inventory,{
    side:"output",index:2,checked:true});
  assert.deepEqual(extra.outputPins,[0,1,2]);
  assert.deepEqual(original.outputPins,[0,1]);
  const pair1=deviceVM.setupEditDraft(extra,inventory,{side:"output",index:0,checked:false});
  assert.deepEqual(pair1.outputPins,[1,2]);
  assert.equal(deviceVM.setupPresentation(inventory,pair1).canAssignMain,true);
  assert.equal(deviceVM.setupEditDraft(pair1,inventory,{
    side:"output",index:63,checked:true}),pair1,"phantom jack cannot be drafted");
  assert.equal(deviceVM.setupEditDraft(pair1,inventory,{
    side:"output",index:1.5,checked:true}),pair1);
  assert.equal(deviceVM.setupEditDraft(pair1,inventory,{
    side:"output",index:-1,checked:true}),pair1);
  assert.equal(deviceVM.setupEditDraft(pair1,inventory,{
    field:"rate",value:"44100"}),pair1,"unsupported rate stays unchanged");
  const rate=deviceVM.setupEditDraft(pair1,inventory,{field:"rate",value:"96000"});
  const buffer=deviceVM.setupEditDraft(rate,inventory,{field:"buffer",value:"128"});
  assert.equal(buffer.rate,96000);assert.equal(buffer.buffer,128);
  const begun=deviceVM.setupBegin(deviceVM.setupInitial(),{
    inventory,draft:buffer,requestId:"approved-io-pins"});
  assert.deepEqual(begun.request.config.enabledOutputs,[1,2]);
  assert.equal(begun.request.config.sampleRate,96000);
  assert.equal(begun.request.config.bufferSize,128);
  const stale={...inventory,active:{...inventory.active,deviceGeneration:15,
    captureDevice:"Absent ASIO"}};
  assert.equal(deviceVM.setupPresentation(stale,buffer).same,false);
  assert.equal(deviceVM.setupEditDraft(buffer,stale,{
    side:"output",index:3,checked:true}),buffer);
  assert.deepEqual(stale.active.enabledOutputs,[0,1],
    "draft never alters output permission or physical pin masks");
});
test("MV-2 render uses the existing one ViewModel and keeps native transport authority",()=>{
  const bridge=read("web/approved-native-bridge.js");
  const setup=bridge.slice(bridge.indexOf("  // Compact DAW-style device preferences"),
    bridge.indexOf("  function installFirstSoundMenu()"));
  assert.match(setup,/deviceVM\.setupDraft\(deviceInventory\)/);
  assert.match(setup,/deviceVM\.setupEditDraft\(deviceChoice,deviceInventory,change\)/);
  assert.match(setup,/deviceVM\.setupPresentation\(deviceInventory,deviceChoice,deviceState\)/);
  assert.doesNotMatch(setup,/deviceChoice\.(backend|capture|playback|inputPins|outputPins|rate|buffer)\s*=/);
  assert.doesNotMatch(setup,/const selected=types\.find/);
  assert.equal((setup.match(/backend\.emitEvent\("arMixAction",begun\.request\)/g)||[]).length,1);
  const vmText=read("web/approved-device-view-model.js");
  assert.doesNotMatch(vmText,/AudioContext|AudioWorklet|document\.|window\.|localStorage/);
  const inventory=nativeDeviceDraftFixture();
  assert.equal(deviceVM.setupPresentation(inventory,deviceVM.setupDraft(inventory),{
    ...deviceVM.setupInitial(),remapGeneration:inventory.active.deviceGeneration}).canReusePins,true);
});


// GUI-1: native source/OUT Channel Strip presentation has zero DOM or
// mixer-owned state. One tested model is used by native readback & hook UI.
test("GUI-1 Channel Strip output labels preserve uint64 identity, reserved names and source-only",()=>{
  const large="18446744073709551615",dup="9007199254740993";
  const channels=[
    {id:"11",role:"INPUT",name:"Vocal"},
    {id:large,role:"GROUP",name:"Master"},
    {id:dup,role:"GROUP",name:"Drums"},
    {id:"17",role:"FX_RETURN",name:"Drums"},
    {id:"99",role:"MAIN",name:"MAIN"}];
  const targets=stripVM.outputTargets(channels,"11",row=>row.name);
  assert.deepEqual(targets,[
    {id:large,label:"Master ["+large+"]"},
    {id:dup,label:"Drums ["+dup+"]"},
    {id:"17",label:"Drums [17]"}]);
  assert.equal(stripVM.resolvePrimarySelection("Master",targets),"MAIN");
  assert.equal(stripVM.resolvePrimarySelection("SOURCE ONLY (No Main)",targets),"NONE");
  assert.equal(stripVM.resolvePrimarySelection("Drums ["+dup+"]",targets),
    "Drums ["+dup+"]");
  assert.equal(stripVM.resolvePrimarySelection("Drums",targets),null,
    "ambiguous human names cannot silently route another channel");
  assert.equal(stripVM.resolvePrimarySelection("Unknown",targets),null);
  const output=stripVM.primaryChoices({track:{kind:"audio",output:"Master"},
    authority:"CHANNEL_ID",targets});
  assert.deepEqual(output.slice(0,2),["Master","SOURCE ONLY (No Main)"]);
  assert.ok(output.includes("Master ["+large+"]"));
  assert.equal(JSON.stringify(channels).includes(large),true);
});
test("GUI-1 Channel Strip source, physical output and MAIN pickers fail closed on loss",()=>{
  const t={kind:"audio",input:"None",output:"Master",
    externalOutput:"Needs Remap · OUT 3/4"};
  const pins=[{label:"None"},{label:"Mono · Mic 1"}];
  assert.deepEqual(stripVM.inputChoices(t,pins,false,false,
    "Internal BUS (read-only)"),["None","Mono · Mic 1"]);
  const bus={...t,input:"Internal BUS (read-only)"};
  assert.ok(stripVM.inputChoices(bus,pins,false,false,"Internal BUS (read-only)")
    .includes("Internal BUS (read-only)"));
  assert.deepEqual(stripVM.inputChoices(bus,pins,true,false,
    "Internal BUS (read-only)"),["Internal BUS (read-only)"],
    "retained hardware cannot propose new physical IN");
  assert.deepEqual(stripVM.inputChoices({kind:"bus",input:"Mic 1"},pins,
    false,false,"Internal BUS (read-only)"),["None"]);
  assert.deepEqual(stripVM.externalChoices(t,[{label:"None"},{label:"OUT 5/6"}]),
    ["None","OUT 5/6","Needs Remap · OUT 3/4"]);
  assert.deepEqual(stripVM.externalChoices({kind:"master"},[]),["None"]);
  assert.deepEqual(stripVM.primaryChoices({track:{kind:"master",output:"Master"},
    retained:true,playbackDevice:"Old ASIO"}),["Audio connection lost"]);
  assert.deepEqual(stripVM.primaryChoices({track:{kind:"master"},
    logical:true}),["Choose audio output"]);
  assert.deepEqual(stripVM.primaryChoices({track:t,authority:"M4",targets:[
    {id:"9",label:"BUS A"}]}),["Master","SOURCE ONLY (No Main)"],
    "M4 never advertises ChannelId BUS routing");
});
test("GUI-1 Channel Strip input and PRIMARY OUT require installed native readback",()=>{
  const channels=[{id:"1",role:"INPUT",primaryOutId:"100",
    source:{portId:"in:0",sourceFormat:"MONO"}},
    {id:"100",role:"MAIN",name:"MAIN"}];
  const read=stripVM.readbackBindings({native:{retained:false},
    channel:channels[0],channels,targets:[],
    inputLabel:p=>p.left===0?"Mono · Mic 1":"None"});
  assert.deepEqual(read,{input:"Mono · Mic 1",output:"Master"});
  const offline=stripVM.readbackBindings({native:{retained:true},
    channel:{...channels[0],primaryOutId:null,
      source:{bindingState:"NEEDS_REMAP",portId:"in:0"}},
    channels,targets:[],inputLabel:()=>{throw Error("no phantom pin");}});
  assert.deepEqual(offline,{input:"Audio connection lost",
    output:"SOURCE ONLY (No Main)"});
  const invalid=stripVM.readbackBindings({native:{retained:false},
    channel:{...channels[0],source:{portId:"in:9007199254740993"}},
    channels,targets:[],inputLabel:()=>{throw Error("unsafe port index");}});
  assert.equal(invalid.input,"None");
});

test("MV-5 EXTERNAL OUT preflight preserves exact native channel identity and warns on shared ASIO pins",()=>{
 const id="9007199254740993",other="9007199254741000";
 const native={admitted:true,retained:false,device:{epoch:"31"},
  graph:{channels:[{id,role:"INPUT"}]}};
 const hardware={device:{epoch:"31",backend:"ASIO",enabledOutputs:[2,3,4,5],
  outputEndpoints:[2,3,4,5].map(port=>({port,active:true}))},
  raw:{externalRevision:"9",physicalMainOutputs:[0,1],physicalMonitorOutputs:[],
   externalOutputs:[{channelId:other,physicalLeft:2,physicalRight:3}]}};
 const choices=[{label:"None",left:-1,right:-1},
  {label:"OUT 3/4",left:2,right:3},{label:"OUT 5/6",left:4,right:5}];
 const before=JSON.stringify([native,hardware,choices]);
 const plan=stripVM.externalOutputPlan({native,hardware,channelId:id,
  selectedLabel:"OUT 3/4",currentLabel:"None",choices,channelName:"Vocal"});
 assert.deepEqual(plan,{kind:"READY",left:2,right:3,epoch:"31",revision:"9",
  requiresConfirmation:true,sharedCount:1,confirmation:
  "Enable ASIO OUT 3/4 for Vocal? SHARED with 1 other source(s); "+
  "signals SUM. Check gains. Check amplifier/speakers. INTERNAL OUT stays unchanged."});
 const remove=stripVM.externalOutputPlan({native,hardware,channelId:id,
  selectedLabel:"None",currentLabel:"OUT 3/4",choices,channelName:"Vocal"});
 assert.deepEqual([remove.kind,remove.left,remove.right,remove.requiresConfirmation],
  ["READY",-1,-1,false],"None does not silently repatch internal WIRE");
 assert.equal(stripVM.externalOutputPlan({native,hardware,channelId:id,
  selectedLabel:"OUT 3/4",currentLabel:"OUT 3/4",choices}).kind,"NO_CHANGE");
 assert.equal(JSON.stringify([native,hardware,choices]),before,
  "pure preflight cannot mutate accepted device inventory or graph");
 const bridge=read("web/approved-native-bridge.js");
 assert.ok(bridge.includes("stripVM.externalOutputPlan("));
 assert.ok(bridge.includes('backend.emitEvent("arMixAction",{action:"routingExternalOutput"'));
 assert.equal((bridge.match(/action:"routingExternalOutput"/g)||[]).length,1);
 assert.ok(!bridge.includes("SHARED with "),
  "shared output policy must no longer be duplicated in native bridge");
 const strip=read("web/approved-channel-strip-view-model.js");
 assert.doesNotMatch(strip,/backend\.emitEvent|window\.confirm|document\.|localStorage/);
});
test("MV-5 EXTERNAL OUT preflight fails closed on stale, ambiguous or prohibited physical pins",()=>{
 const id="9007199254740993",other="9007199254740994";
 const native={admitted:true,retained:false,device:{epoch:"31"},
  graph:{channels:[{id,role:"GROUP"}]}};
 const hardware={device:{epoch:"31",backend:"ASIO",enabledOutputs:[2,3],
  outputEndpoints:[{port:2,active:true},{port:3,active:true}]},
  raw:{externalRevision:"9",physicalMainOutputs:[0,1],
   physicalMonitorOutputs:[],externalOutputs:[]}};
 const choices=[{label:"None",left:-1,right:-1},{label:"OUT 3/4",left:2,right:3}];
 const plan=(changes={})=>stripVM.externalOutputPlan({native,hardware,
  channelId:id,selectedLabel:"OUT 3/4",currentLabel:"None",choices,
  channelName:"Vocal",...changes});
 assert.equal(plan().kind,"READY");
 for(const invalid of [
  {pending:true},{native:{...native,admitted:false}},
  {native:{...native,retained:true}},
  {native:{...native,device:{epoch:"30"}}},
  {channelId:Number(id)},{channelId:other},
  {native:{...native,graph:{channels:[{id,role:"MAIN"}]}}},
  {choices:[...choices,choices[1]]},
  {choices:[{label:"OUT 3/4",left:4,right:5}]},
  {choices:[{label:"OUT 3/4",left:3,right:4}]},
  {choices:[{label:"OUT 3/4",left:NaN,right:3}]},
  {hardware:{...hardware,device:{...hardware.device,backend:"WASAPI"}}},
  {hardware:{...hardware,device:{...hardware.device,enabledOutputs:[2]}}},
  {hardware:{...hardware,device:{...hardware.device,
    outputEndpoints:[{port:2,active:true},{port:3,active:false}]}}},
  {hardware:{...hardware,raw:{...hardware.raw,physicalMonitorOutputs:[3]}}},
  {hardware:{...hardware,raw:{...hardware.raw,externalRevision:"18446744073709551616"}}},
  {hardware:{...hardware,device:{...hardware.device,epoch:"32"}}}
 ])assert.equal(plan(invalid).kind,"UNAVAILABLE",JSON.stringify(invalid));
});

test("GUI-1 external OUT pending waits for matching ACK and later readback",()=>{
  const p={requestId:"ext-1",epoch:"19",left:2,right:3,acked:false};
  const row={physicalLeft:2,physicalRight:3};
  assert.equal(stripVM.externalPending(p,"19","LIVE",row),"PENDING",
    "installed-looking row cannot clear command without correlated ACK");
  const ack={...p,acked:true};
  assert.equal(stripVM.externalPending(ack,"19","LIVE",
    {...row,physicalRight:5}),"PENDING");
  assert.equal(stripVM.externalPending(ack,"18","LIVE",row),"STALE",
    "new device epoch invalidates pending output");
  assert.equal(stripVM.externalPending(ack,"19","MUTED",row),"CONFIRMED");
  assert.equal(stripVM.externalPending(ack,"19","LIVE",null),"PENDING");
  assert.equal(stripVM.externalPending({...ack,left:-1,right:-1},
    "19","NONE",null),"CONFIRMED","remove requires observed absent row");
  assert.equal(stripVM.externalPending(null,"19","LIVE",row),"NONE");
});
test("GUI-1 packaged native bridge uses one strip selector and one route dispatcher",()=>{
  const s=read("web/approved-native-bridge.js");
  const model=read("web/approved-channel-strip-view-model.js");
  assert.match(s,/stripVM\.readbackBindings\(/);
  assert.match(s,/stripVM\.externalPending\(/);
  assert.match(s,/stripVM\.inputChoices\(/);
  assert.match(s,/stripVM\.primaryChoices\(/);
  assert.match(s,/stripVM\.resolvePrimarySelection\(/);
  assert.doesNotMatch(s,/const source=nativeIdFor\(t\),reserved=/);
  assert.doesNotMatch(s,/const destination=nativeOutputTargets\(t\)\.find\(ch=>ch.label===el.value\)/);
  assert.doesNotMatch(model,/backend\.emitEvent|AudioContext|AudioWorklet|document\.|localStorage|createPluginInstance/);
  assert.equal((s.match(/backend\.emitEvent\("arMixAction",\{action:"routingExternalOutput"/g)||[]).length,1);
});
test("MV-2 merged #767 retains one selector; #768 unique duplex/one-pin guards are forward-ported",()=>{
  const inventory={active:{open:true,authorized:true,backend:"ASIO",
    captureDevice:"Duplex A",playbackDevice:"Duplex A",deviceGeneration:12,
    enabledInputs:[0],enabledOutputs:[0,1],inputNames:["Mic"],
    outputNames:["L","R"],rates:[48000],buffers:[256]},
    types:[{name:"ASIO",separate:false,inputs:["Duplex A","Duplex B"],
      outputs:["Duplex A","Duplex B"]}]};
  const draft=deviceVM.setupDraft(inventory);
  const changed=deviceVM.setupEditDraft(draft,inventory,{field:"playback",value:"Duplex B"});
  assert.equal(changed.capture,"Duplex B");
  assert.equal(changed.playback,"Duplex B");
  assert.deepEqual(changed.outputPins,null);
  assert.equal(changed.rate,0);
  assert.equal(deviceVM.setupPresentation(inventory,changed).canApply,true);
  assert.equal(deviceVM.setupBegin(deviceVM.setupInitial(),
    {inventory,draft:changed,requestId:"gui1-duplex"}).request.config.captureDevice,"Duplex B");
  const onePin=deviceVM.setupEditDraft(draft,inventory,
    {side:"output",index:1,checked:false});
  assert.equal(deviceVM.setupPresentation(inventory,onePin).canApply,false);
  assert.equal(deviceVM.setupBegin(deviceVM.setupInitial(),
    {inventory,draft:onePin,requestId:"gui1-one"}),null);
  const reset={...draft,outputPins:null,inputPins:null};
  const recovered=deviceVM.setupBegin(deviceVM.setupInitial(),
    {inventory,draft:reset,requestId:"gui1-fallback"});
  assert.deepEqual(recovered.request.config.enabledOutputs,[0,1]);
});

test("GUI-2 native FX catalog projection keeps exact M4 processor identity",()=>{
 const rows=Array(8).fill(null);
 rows[1]={instanceId:"18446744073709551615",catalogId:"eq.a",bypassed:true};
 rows[3]={instanceId:"9007199254740993",catalogId:"unknown",bypassed:false};
 const before=JSON.stringify(rows);
 const result=fxRackVM.m4Presentation(rows,new Map([["eq.a",{name:"EQ One"}]]),true);
 assert.equal(result.fxOn,false);assert.equal(result.slots.length,8);
 assert.deepEqual(result.slots[1].instance,{id:"18446744073709551615",name:"EQ One"});
 assert.deepEqual(result.slots[3].instance,{id:"9007199254740993",name:"VST3 Loaded"});
 assert.equal(result.slots[0].instance,null);
 assert.equal(fxRackVM.m4Presentation(rows,new Map(),null).fxOn,null);
 assert.equal(JSON.stringify(rows),before,"M4 metadata never edits native FX");
 const forged=rows.slice();forged[1]={catalogId:"eq.a",bypassed:false};
 assert.equal(fxRackVM.m4Presentation(forged,new Map(),false),null);
});
test("GUI-2 native FX pending settlement requires ACK, exact lease and epoch",()=>{
 const epoch="18446744073709551612";
 const ch={id:"9007199254740993",fxRackBypassed:false,
   inserts:[{id:"18446744073709551615",bypassed:true}]};
 const next={device:{epoch},graph:{channels:[ch]}};
 const p={requestId:"fx42",channelId:ch.id,kind:"insert",
   insertId:"18446744073709551615",bypassed:true,epoch,acked:false};
 assert.equal(fxRackVM.nativePowerSettlement(p,next),"WAIT");
 assert.equal(fxRackVM.nativePowerSettlement({...p,acked:true},next),"CONFIRMED");
 assert.equal(fxRackVM.nativePowerSettlement({...p,acked:true},
   {...next,device:{epoch:"18446744073709551613"}}),"STALE");
 assert.equal(fxRackVM.nativePowerSettlement({...p,acked:true},
   {...next,graph:{channels:[{...ch,inserts:[{id:"new",bypassed:true}]}]}}),"WAIT");
 assert.equal(fxRackVM.nativePowerSettlement({...p,acked:true,kind:"rack",bypassed:false},next),"CONFIRMED");
 assert.equal(fxRackVM.nativePowerSettlement(null,next),"NONE");
});
test("G-MV28 M4 rack settlement needs exact full installed snapshot, not an old session",()=>{
 const session={authority:"M4",deviceEpoch:"17",routeRevision:"8",
   channels:Array.from({length:10},()=>({fxRackBypassed:true}))};
 const snapshot={admitted:false,retained:false,device:{epoch:"17"},raw:{
   mixerSession:session,legacyM4:{active:true,routeAccepted:true,
     routingRevision:"8"}}};
 const p={channel:0,epoch:"17",revision:"8",bypassed:true,acked:false};
 const settle=(pending=p,view=snapshot)=>fxRackVM.m4RackSettlement(pending,view);
 assert.equal(settle(),"WAIT");
 assert.equal(settle({...p,acked:true}),"CONFIRMED");
 assert.equal(settle({...p,acked:true},{...snapshot,raw:{...snapshot.raw,
   mixerSession:{...session,routeRevision:"9"}}}),"STALE");
 assert.equal(settle({...p,acked:true},{...snapshot,raw:{...snapshot.raw,
   mixerSession:{...session,authority:"CHANNEL_ID"}}}),"STALE");
 assert.equal(settle({...p,acked:true},{...snapshot,raw:{...snapshot.raw,
   legacyM4:{...snapshot.raw.legacyM4,routingRevision:"9"}}}),"STALE",
   "stale legacy M4 revision cannot confirm an earlier physical rack gate");
 assert.equal(settle({...p,acked:true},{...snapshot,retained:true}),"STALE");
 assert.equal(settle({...p,acked:true},{...snapshot,admitted:true}),"STALE");
 assert.equal(settle({...p,acked:true},{...snapshot,raw:{...snapshot.raw,
   legacyM4:{...snapshot.raw.legacyM4,routeAccepted:false}}}),"STALE");
 assert.equal(settle({...p,acked:true},{...snapshot,raw:{...snapshot.raw,
   mixerSession:{...session,channels:[
     {...session.channels[0],fxRackBypassed:false},
     ...session.channels.slice(1)]}}}),"WAIT");
 assert.equal(settle(null),"NONE");
 const source=read("web/approved-native-bridge.js");
 assert.ok(source.includes("fxRackVM.m4Presentation("));
 assert.ok(source.includes("fxRackVM.nativePowerSettlement("));
 assert.match(source,/fxRackVM\.m4RackSettlement\(p,next,/);
 assert.doesNotMatch(source,/const actual=p\.kind==="rack"\?/);
});

test("G-MV28 pure M4 FX power plan owns exact rack/slot authority, lease and IPC envelope",()=>{
 const rows=Array.from({length:10},()=>({fxRackBypassed:false}));
 const instances=Array.from({length:10},(_,i)=>i===4?
   [{instanceId:"m4-vst-lease",catalogId:"vendor.fx",bypassed:false}]:[]);
 const m={active:true,routeAccepted:true,nativeBusStructural:false,
   routingRevision:"12",fxInstances:instances};
 const session={authority:"M4",deviceEpoch:"31",routeRevision:"12",
   channels:rows};
 const snapshot={admitted:false,retained:false,device:{epoch:"31"},raw:{
   mixerSession:session,legacyM4:m}};
 const plan=(action="fxOn",extra={},view=snapshot)=>fxRackVM.m4PowerPlan(view,{
   action,channel:4,slot:action==="slotPower"?0:null,verifiedM4:true,...extra});
 const before=JSON.stringify(snapshot);
 const rack=plan();
 assert.equal(rack.kind,"READY");
 assert.equal(rack.action,"routingFxRackBypass");
 assert.equal(rack.key,4);
 assert.equal(rack.timeoutMs,8000);
 assert.deepEqual(rack.pending,{channel:4,epoch:"31",revision:"12",
   acked:false,bypassed:true});
 assert.deepEqual(rack.payload,{channel:4,deviceEpoch:31,revision:12,value:true});
 const slot=plan("slotPower");
 assert.equal(slot.kind,"READY");
 assert.equal(slot.action,"routingFxBypass");
 assert.equal(slot.key,"4:0");
 assert.equal(slot.timeoutMs,5000);
 assert.deepEqual(slot.pending,{channel:4,epoch:"31",revision:"12",
   acked:false,slot:0,token:"m4-vst-lease",bypass:true});
 assert.deepEqual(slot.payload,{channel:4,slot:0,instanceId:"m4-vst-lease",
   deviceEpoch:31,revision:12,value:true});
 const unavailable=(a="fxOn",extra={},view=snapshot)=>{
   assert.equal(plan(a,extra,view).kind,"UNAVAILABLE");};
 unavailable("fxOn",{verifiedM4:false});
 unavailable("fxOn",{offline:true});
 unavailable("fxOn",{pending:true});
 unavailable("fxOn",{channel:10});
 unavailable("fxOn",{channel:-1});
 unavailable("fxOn",{slot:0});
 unavailable("slotPower",{slot:-1});
 unavailable("slotPower",{slot:8});
 unavailable("slotPower",{pending:true});
 unavailable("slotPower",{channel:5});
 unavailable("slotPower",{slot:0}, {...snapshot,raw:{...snapshot.raw,
   legacyM4:{...m,fxInstances:instances.map((xs,i)=>i===4?
     [{...xs[0],instanceId:""}]:xs)}}});
 unavailable("fxOn",{}, {...snapshot,admitted:true});
 unavailable("fxOn",{}, {...snapshot,retained:true});
 unavailable("fxOn",{}, {...snapshot,device:{epoch:"32"}});
 unavailable("fxOn",{}, {...snapshot,raw:{...snapshot.raw,
   mixerSession:{...session,authority:"CHANNEL_ID"}}});
 unavailable("slotPower",{}, {...snapshot,raw:{...snapshot.raw,
   mixerSession:{...session,authority:"NONE"}}});
 unavailable("fxOn",{}, {...snapshot,raw:{...snapshot.raw,
   mixerSession:{...session,routeRevision:"13"}}});
 unavailable("fxOn",{}, {...snapshot,raw:{...snapshot.raw,
   legacyM4:{...m,routingRevision:"13"}}});
 unavailable("fxOn",{}, {...snapshot,raw:{...snapshot.raw,
   legacyM4:{...m,active:false}}});
 unavailable("fxOn",{}, {...snapshot,raw:{...snapshot.raw,
   legacyM4:{...m,routeAccepted:false}}});
 unavailable("fxOn",{}, {...snapshot,raw:{...snapshot.raw,
   legacyM4:{...m,nativeBusStructural:true}}});
 unavailable("fxOn",{}, {...snapshot,device:{epoch:"9007199254740993"},raw:{
   ...snapshot.raw,mixerSession:{...session,deviceEpoch:"9007199254740993"}}});
 unavailable("fxOn",{}, {...snapshot,raw:{...snapshot.raw,
   legacyM4:{...m,routingRevision:"9007199254740993"},
   mixerSession:{...session,routeRevision:"9007199254740993"}}});
 assert.equal(fxRackVM.m4RackAckResult(null,{}),"IGNORE");
 const pending={requestId:"rack-m4-42",...rack.pending};
 const ack={requestId:"rack-m4-42",channel:4,deviceEpoch:31,
   routingRevision:12,bypassed:true,ok:true};
 assert.equal(fxRackVM.m4RackAckResult(pending,{...ack,requestId:"old"}),"IGNORE");
 assert.equal(fxRackVM.m4RackAckResult(pending,ack),"ACKED");
 for(const faulty of [{channel:5},{deviceEpoch:32},{routingRevision:13},
   {bypassed:false},{ok:false}])
   assert.equal(fxRackVM.m4RackAckResult(pending,{...ack,...faulty}),"REJECTED");
 assert.equal(JSON.stringify(snapshot),before,"pure plan must never mutate native readback");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/fxRackVM\.m4PowerPlan\(/);
 assert.match(bridge,/fxRackVM\.m4RackAckResult\(/);
 assert.doesNotMatch(bridge,/function requestNativeFxPower\(/);
 assert.doesNotMatch(bridge,/function requestM4RackPower\(/);
 assert.equal((bridge.match(/function requestM4FxPower\(/g)||[]).length,1);
 assert.equal((bridge.match(/action:"routingFxBypass"/g)||[]).length,0,
   "native M4 FX command envelope must be owned by the pure power plan");
});


test("MV-7 deferred SEND gain requires unique exact installed EdgeId",()=>{
 const source="9007199254740993",edgeId="9007199254740997";
 const snap={admitted:true,retained:false,identity:"A|17",graph:{edges:[
   {enabled:true,primaryOutput:false,from:source,edgeId,to:"2",gainLinear:0.5}]}};
 const edit={identity:snap.identity,source,gain:0.75};
 const before=JSON.stringify(snap);
 assert.deepEqual(wireVM.gainDispatchPlan(snap,edgeId,edit),
   {kind:"DISPATCH",source,edgeId,gainLinear:0.75});
 assert.equal(wireVM.gainDispatchPlan(snap,edgeId,{...edit,gain:0.5}).kind,"CONFIRMED");
 const fail=(s=snap,e=edgeId,d=edit)=>assert.equal(
   wireVM.gainDispatchPlan(s,e,d).kind,"ROLLBACK");
 fail({...snap,admitted:false});fail({...snap,retained:true});
 fail({...snap,identity:"B|17"});fail(snap,edgeId,{...edit,identity:"B|17"});
 fail(snap,Number(edgeId));fail(snap,"0");fail(snap,edgeId,{...edit,source:Number(source)});
 for(const g of [NaN,Infinity,-1,4.01])fail(snap,edgeId,{...edit,gain:g});
 fail({...snap,graph:{edges:[...snap.graph.edges,...snap.graph.edges]}});
 fail({...snap,graph:{edges:[{...snap.graph.edges[0],primaryOutput:true}]}});
 fail({...snap,graph:{edges:[{...snap.graph.edges[0],gainLinear:NaN}]}});
 assert.equal(JSON.stringify(snap),before);
});
test("MV-7 WIRE status only retries one same-epoch initial CONNECT",()=>{
 const state={pending:null,lastError:"STALE_NATIVE_WIRE_GRAPH",current:{device:{epoch:"71"}}};
 const attempt={epoch:"71",retries:0,ops:[{type:"CONNECT",sourceId:1,destId:11}]};
 const plan=(s=state,a=attempt,start=null)=>
   wireVM.nativeRecoveryPlan(s,a,start).kind;
 assert.equal(plan(),"REBASE_CONNECTION");
 assert.equal(plan({...state,lastError:"STALE_NATIVE_CONTROL_REVISION"}),"REBASE_CONNECTION");
 assert.equal(plan({...state,pending:{requestId:"r"}}),"PENDING");
 assert.equal(plan({...state,lastError:"ACCEPTED_READBACK"}),"ACCEPTED");
 assert.equal(plan({...state,lastError:"NONE"}),"IDLE");
 assert.equal(plan({...state,lastError:"NATIVE_NOT_SUBSCRIBED"}),"IDLE");
 for(const err of ["INVALID_NATIVE_ACK","UNKNOWN_CHANNEL_ID",
   "NATIVE_ACK_OR_READBACK_TIMEOUT","ACK_READBACK_EFFECT_MISMATCH"])
   assert.equal(plan({...state,lastError:err}),"ROLLBACK");
 assert.equal(plan({...state,current:{device:{epoch:"72"}}}),"ROLLBACK");
 assert.equal(plan(state,{...attempt,epoch:"72"}),"ROLLBACK");
 assert.equal(plan(state,{...attempt,epoch:71}),"ROLLBACK");
 assert.equal(plan(state,{...attempt,retries:1}),"ROLLBACK");
 assert.equal(plan(state,{...attempt,ops:[{type:"REMOVE"}]}),"ROLLBACK");
 assert.equal(plan(state,{...attempt,ops:[{type:"RETARGET"}]}),"ROLLBACK");
 assert.equal(plan(state,{...attempt,ops:[{type:"CONNECT"},{type:"CONNECT"}]}),"ROLLBACK");
 assert.equal(plan(state,null,{retries:1}),"FLUSH_STARTUP_RETRY");
 assert.equal(plan({...state,lastError:"INVALID_NATIVE_ACK"},null,{retries:1}),"ROLLBACK");
});
test("MV-7 bridge delegates WIRE decisions, preserves only one native controller",()=>{
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/wireVM\.gainDispatchPlan\(/);
 assert.match(bridge,/wireVM\.nativeRecoveryPlan\(/);
 assert.match(bridge,/controller\.intent\("SET_WIRE_GAIN",plan\.source/);
 assert.match(bridge,/controller = protocol\.createController\(/);
 assert.doesNotMatch(bridge,/const failed=!state\.pending/);
 assert.doesNotMatch(read("web/approved-wire-view-model.js"),
   /backend\.emitEvent|window\.setTimeout|document\.querySelector/);
});

test("MV-8 FX native insert preflight keeps exact ChannelId, slot and installed roster",()=>{
  const id="9007199254740993",id2="9007199254740995";
  const snapshot={admitted:true,retained:false,device:{epoch:"73"},graph:{
    revision:"55",capabilities:{supportedOps:["LOAD_INSERT","MOVE_INSERT","REMOVE_INSERT"]},
    channels:[{id,role:"INPUT",inserts:[{id:"fx-1",catalogId:"eq"}]},
      {id:id2,role:"GROUP",inserts:[]}]}};
  const before=JSON.stringify(snapshot);
  const ask={channelId:id,slot:1,catalogId:"vendor.vst3"};
  assert.deepEqual(fxRackVM.nativeInsertPlan(snapshot,ask),{
    kind:"READY",channelId:id,payload:{catalogId:"vendor.vst3",slot:1}});
  const unavailable=(snap=snapshot,params=ask)=>
    assert.equal(fxRackVM.nativeInsertPlan(snap,params).kind,"UNAVAILABLE");
  unavailable(snapshot,{...ask,pending:true});
  unavailable(snapshot,{...ask,slot:0});unavailable(snapshot,{...ask,slot:2});
  unavailable(snapshot,{...ask,slot:8});unavailable(snapshot,{...ask,slot:1.5});
  unavailable(snapshot,{...ask,channelId:Number(id)});
  unavailable(snapshot,{...ask,channelId:"18446744073709551616"});
  unavailable(snapshot,{...ask,catalogId:""});
  unavailable({...snapshot,admitted:false});unavailable({...snapshot,retained:true});
  unavailable({...snapshot,graph:{...snapshot.graph,capabilities:{supportedOps:[]}}});
  unavailable({...snapshot,graph:{...snapshot.graph,channels:[
    snapshot.graph.channels[0],{...snapshot.graph.channels[0]}]}});
  unavailable({...snapshot,graph:{...snapshot.graph,channels:[
    snapshot.graph.channels[0],
    {id:id2,role:"GROUP",inserts:[{id:"fx-1",catalogId:"eq"}]}]}});
  assert.equal(JSON.stringify(snapshot),before);
});
test("MV-8 FX move/remove preflight preserves exact instance and destination",()=>{
  const a="9007199254740993",b="9007199254740995";
  const base={admitted:true,retained:false,graph:{capabilities:{
    supportedOps:["MOVE_INSERT","REMOVE_INSERT"]},
    channels:[{id:a,role:"INPUT",inserts:[{id:"lease-a"},{id:"lease-b"}]},
      {id:b,role:"GROUP",inserts:[]}]}};
  const request={channelId:a,slot:0,instanceId:"lease-a",
    move:true,destinationId:b,position:0};
  assert.deepEqual(fxRackVM.nativeEditPlan(base,request),{
    kind:"READY",operation:"MOVE_INSERT",channelId:a,
    payload:{slot:0,instanceId:"lease-a",toChannelId:b,toSlot:0}});
  assert.deepEqual(fxRackVM.nativeEditPlan(base,{channelId:a,slot:1,
    instanceId:"lease-b"}),{kind:"READY",operation:"REMOVE_INSERT",
    channelId:a,payload:{slot:1,instanceId:"lease-b"}});
  assert.equal(fxRackVM.nativeEditPlan(base,{...request,destinationId:a,
    position:0}).kind,"NO_CHANGE");
  const bad=(s=base,args=request)=>assert.equal(
    fxRackVM.nativeEditPlan(s,args).kind,"UNAVAILABLE");
  bad(base,{...request,slot:1});
  bad(base,{...request,instanceId:"vendor.vst3"});
  bad(base,{...request,destinationId:"0"});
  bad(base,{...request,destinationId:null}); // lost mapping never deletes FX
  bad(base,{...request,destinationId:Number(b)});
  bad(base,{...request,move:false}); // stray target cannot become REMOVE
  bad(base,{...request,move:"yes"});
  bad(base,{...request,position:1});
  bad(base,{...request,position:-1});
  bad({...base,retained:true});
  bad({...base,graph:{...base.graph,channels:[
    base.graph.channels[0],{...base.graph.channels[1],
    inserts:[{id:"lease-a"}]}]}});
  bad({...base,graph:{...base.graph,capabilities:{supportedOps:[]}}});
});
test("MV-8 FX inserted editor requires correlated controller proof and unique lease",()=>{
  const ch="9007199254740993",cat="vendor.vst3";
  const pending={authority:"CHANNEL_ID",requestId:"r-7",channelId:ch,
    id:cat,slot:0,epoch:"73"};
  const snapshot={admitted:true,retained:false,device:{epoch:"73"},graph:{
    channels:[{id:ch,role:"INPUT",inserts:[
      {id:"live-instance-a",catalogId:cat,name:"Vendor VST3"}]}]}};
  const ack={requestId:"r-7",operation:"LOAD_INSERT",targetChannelId:ch,
    payload:{catalogId:cat,slot:0}};
  assert.deepEqual(fxRackVM.nativeInsertSettlement(pending,snapshot,ack),{
    kind:"CONFIRMED",channelId:ch,slot:0,catalogId:cat,
    instanceId:"live-instance-a",name:"Vendor VST3"});
  assert.equal(fxRackVM.nativeInsertSettlement(pending,snapshot,null).kind,"WAIT");
  assert.equal(fxRackVM.nativeInsertSettlement(pending,snapshot,
    {...ack,requestId:"r-8"}).kind,"WAIT");
  assert.equal(fxRackVM.nativeInsertSettlement(pending,snapshot,
    {...ack,targetChannelId:"71"}).kind,"WAIT");
  assert.equal(fxRackVM.nativeInsertSettlement(pending,snapshot,
    {...ack,payload:{catalogId:"different",slot:0}}).kind,"WAIT");
  assert.equal(fxRackVM.nativeInsertSettlement(pending,
    {...snapshot,device:{epoch:"74"}},ack).kind,"STALE");
  assert.equal(fxRackVM.nativeInsertSettlement(pending,
    {...snapshot,admitted:false},ack).kind,"WAIT");
  assert.equal(fxRackVM.nativeInsertSettlement(pending,
    {...snapshot,graph:{channels:[...snapshot.graph.channels,
      {id:"91",role:"GROUP",inserts:[
        {id:"live-instance-a",catalogId:cat}]}]}},ack).kind,"WAIT");
  assert.equal(fxRackVM.nativeInsertSettlement(pending,
    {...snapshot,graph:{channels:[{id:ch,role:"INPUT",inserts:[
      {id:"live-instance-b",catalogId:"different"}]}]}},ack).kind,"WAIT");
  assert.deepEqual(fxRackVM.nativeInsertSettlement(null,snapshot,ack),{kind:"NONE"});
});
test("MV-8 FX native bypass plan and ACK are independent of global and insert states",()=>{
  const channelId="9007199254740993";
  const snapshot={admitted:true,retained:false,device:{epoch:"73"},graph:{
    revision:"22",channels:[{id:channelId,role:"INPUT",fxRackBypassed:false,
      inserts:[{id:"lease-1",bypassed:true},{id:"lease-2",bypassed:false}]}]}};
  const rack=fxRackVM.nativePowerPlan(snapshot,{channelId,action:"fxOn"});
  assert.equal(rack.kind,"READY");
  assert.equal(rack.pending.bypassed,true);
  assert.equal(rack.action,"routingNativeFxRackBypass");
  assert.equal(rack.payload.revision,"22");
  const slot=fxRackVM.nativePowerPlan(snapshot,{channelId,action:"slotPower",slot:0});
  assert.equal(slot.kind,"READY");
  assert.equal(slot.pending.insertId,"lease-1");
  assert.equal(slot.pending.bypassed,false);
  assert.equal(slot.payload.value,false);
  assert.equal(fxRackVM.nativePowerPlan(snapshot,{channelId,action:"slotPower",slot:2}).kind,"UNAVAILABLE");
  assert.equal(fxRackVM.nativePowerPlan(snapshot,{channelId,action:"slotPower",slot:0,
    pending:true}).kind,"UNAVAILABLE");
  const p={...slot.pending,requestId:"fx-17"};
  assert.equal(fxRackVM.nativePowerAckResult(p,{requestId:"other",ok:true}),"IGNORE");
  assert.equal(fxRackVM.nativePowerAckResult(p,{requestId:"fx-17",ok:true,
    channelId,bypassed:false,insertId:"lease-1"}),"ACKED");
  assert.equal(fxRackVM.nativePowerAckResult(p,{requestId:"fx-17",ok:true,
    channelId,bypassed:false,insertId:"replaced"}),"REJECTED");
  assert.equal(fxRackVM.nativePowerAckResult(p,{requestId:"fx-17",ok:false,
    channelId,bypassed:false}),"REJECTED");
  assert.equal(fxRackVM.nativePowerAckResult(null,{}),"IGNORE");
});
test("MV-8 FX bridge delegates preflight and settlement to existing pure FX VM",()=>{
  const bridge=read("web/approved-native-bridge.js");
  assert.match(bridge,/fxRackVM\.nativeInsertPlan\(/);
  assert.match(bridge,/fxRackVM\.nativeInsertSettlement\(/);
  assert.match(bridge,/fxRackVM\.nativeEditPlan\(/);
  assert.match(bridge,/move:!!target/);
  assert.match(bridge,/fxRackVM\.nativePowerPlan\(/);
  assert.match(bridge,/fxRackVM\.nativePowerAckResult\(/);
  assert.match(bridge,/controller\.intent\(plan\.channelId,|controller\.intent\("LOAD_INSERT",plan\.channelId/);
  assert.match(bridge,/controller\.intent\(decision\.operation,decision\.channelId/);
  assert.doesNotMatch(read("web/approved-fx-rack-view-model.js"),
    /backend\.emitEvent|document\.querySelector|setTimeout\(/);
});

test("MV-9B Global FX requires one admitted native/M4 owner, ACK and accepted epoch readback",()=>{
 const epoch="9007199254740993",revision="9007199254740995";
 const current={admitted:true,retained:false,device:{epoch},graph:{
   revision,capabilities:{callbackOwnsChannelIds:true}},raw:{
   globalFxBypassed:false,mixerSession:{
     authority:"CHANNEL_ID",deviceEpoch:epoch}}};
 const before=JSON.stringify(current);
 const context=fxRackVM.globalPowerContext(current,false,false);
 assert.deepEqual(context,{authority:"CHANNEL_ID",epoch,revision});
 assert.deepEqual(fxRackVM.globalPowerPlan(current,context),{
   kind:"READY",bypassed:true,action:"routingGlobalFxBypass",
   payload:{deviceEpoch:epoch,revision,value:true}});
 const pending={requestId:"global-fx-7",authority:"CHANNEL_ID",
   epoch,revision,bypassed:true,acked:false};
 const settled=(p=pending,s=current,c=context)=>
   fxRackVM.globalPowerSettlement(p,s,c);
 assert.equal(settled(),"WAIT");
 assert.equal(settled({...pending,acked:true}),"WAIT");
 assert.equal(settled({...pending,acked:true},
   {...current,raw:{...current.raw,globalFxBypassed:true}}),"CONFIRMED");
 assert.equal(settled({...pending,acked:true},current,
   {...context,epoch:"9007199254740996"}),"STALE");
 assert.equal(settled({...pending,acked:true},current,
   {...context,revision:"9007199254740996"}),"STALE");
 assert.equal(settled({...pending,acked:true},current,
   {...context,authority:"M4"}),"STALE");
 assert.equal(settled({...pending,acked:true},current,null),"STALE");
 assert.equal(settled(null),"NONE");
 const unavailable=(s=current,m4=false,offline=false)=>
   assert.equal(fxRackVM.globalPowerContext(s,m4,offline),null);
 unavailable({...current,admitted:false});
 unavailable({...current,retained:true});
 unavailable({...current,device:{epoch:"71"}});
 unavailable({...current,raw:{...current.raw,mixerSession:{
   authority:"M4",deviceEpoch:epoch}}});
 unavailable(current,false,true);
 unavailable({...current,graph:{...current.graph,revision:12}});
 const m4={admitted:false,retained:false,device:{epoch:"21"},graph:{revision:"0"},
   raw:{globalFxBypassed:false,legacyM4:{routingRevision:"17"},
     mixerSession:{authority:"M4",deviceEpoch:"21",routeRevision:"17"}}};
 assert.deepEqual(fxRackVM.globalPowerContext(m4,true,false),
   {authority:"M4",epoch:"21",revision:"17"});
 assert.equal(fxRackVM.globalPowerContext(m4,false,false),null);
 assert.equal(fxRackVM.globalPowerContext({...m4,
   raw:{...m4.raw,legacyM4:{routingRevision:"18"}}},true,false),null);
 assert.equal(fxRackVM.globalPowerPlan(current,null).kind,"UNAVAILABLE");
 assert.equal(fxRackVM.globalPowerPlan(current,context,true).kind,"UNAVAILABLE");
 assert.equal(fxRackVM.globalPowerPlan({...current,raw:{...current.raw,
   globalFxBypassed:"false"}},context).kind,"UNAVAILABLE");
 assert.equal(JSON.stringify(current),before,"pure selector must not mutate native state");
});
test("MV-9B Global FX ACK never commits without matching request and independent readback",()=>{
 const pending={requestId:"global-fx-8",bypassed:true};
 assert.equal(fxRackVM.globalPowerAckResult(null,{}),"IGNORE");
 assert.equal(fxRackVM.globalPowerAckResult(pending,{requestId:"old",ok:true,
   bypassed:true}),"IGNORE");
 assert.equal(fxRackVM.globalPowerAckResult(pending,{requestId:"global-fx-8",
   ok:false,bypassed:true}),"REJECTED");
 assert.equal(fxRackVM.globalPowerAckResult(pending,{requestId:"global-fx-8",
   ok:true,bypassed:false}),"REJECTED");
 assert.equal(fxRackVM.globalPowerAckResult(pending,{requestId:"global-fx-8",
   ok:true,bypassed:true}),"ACKED");
});
test("MV-9B M4 FX slot requires exact lease, route revision and independent accepted bypass",()=>{
 const snapshot={admitted:false,retained:false,device:{epoch:"31"},raw:{
   mixerSession:{authority:"M4",deviceEpoch:"31",routeRevision:"12"},
   legacyM4:{routingRevision:"12",fxInstances:[
     [{instanceId:"fx-one",bypassed:true,catalogId:"x"}]]}}};
 const pending={requestId:"fxpower-6",channel:0,slot:0,token:"fx-one",
   bypass:true,epoch:"31",revision:"12",acked:false};
 const settle=(p=pending,s=snapshot)=>fxRackVM.m4SlotPowerSettlement(p,s);
 assert.equal(settle(),"WAIT","ACK is required even if bypass matches");
 assert.equal(settle({...pending,acked:true}),"CONFIRMED");
 assert.equal(settle({...pending,acked:true},
   {...snapshot,raw:{...snapshot.raw,legacyM4:{...snapshot.raw.legacyM4,
     fxInstances:[[{instanceId:"fx-one",bypassed:false}]]}}}),"WAIT");
 assert.equal(settle({...pending,acked:true},
   {...snapshot,raw:{...snapshot.raw,legacyM4:{...snapshot.raw.legacyM4,
     fxInstances:[[{instanceId:"replaced",bypassed:true}]]}}}),"STALE");
 assert.equal(settle({...pending,acked:true},
   {...snapshot,device:{epoch:"32"}}),"STALE");
 assert.equal(settle({...pending,acked:true},
   {...snapshot,raw:{...snapshot.raw,mixerSession:{
     ...snapshot.raw.mixerSession,routeRevision:"13"}}}),"STALE");
 assert.equal(settle({...pending,acked:true},
   {...snapshot,raw:{...snapshot.raw,mixerSession:{
     ...snapshot.raw.mixerSession,authority:"CHANNEL_ID"}}}),"STALE");
 assert.equal(settle({...pending,acked:true},{...snapshot,admitted:true}),"STALE");
 assert.equal(settle(null),"NONE");
 assert.equal(fxRackVM.m4SlotPowerAckResult(null,{}),"IGNORE");
 assert.equal(fxRackVM.m4SlotPowerAckResult(pending,{requestId:"late",
   channel:0,slot:0,ok:true}),"IGNORE");
 assert.equal(fxRackVM.m4SlotPowerAckResult(pending,{requestId:"fxpower-6",
   channel:1,slot:0,ok:true}),"REJECTED");
 assert.equal(fxRackVM.m4SlotPowerAckResult(pending,{requestId:"fxpower-6",
   channel:0,slot:1,ok:true}),"REJECTED");
 assert.equal(fxRackVM.m4SlotPowerAckResult(pending,{requestId:"fxpower-6",
   channel:0,slot:0,ok:false}),"REJECTED");
 assert.equal(fxRackVM.m4SlotPowerAckResult(pending,{requestId:"fxpower-6",
   channel:0,slot:0,ok:true}),"ACKED");
});
test("MV-9B bridge uses only accepted native snapshots for global FX, fences M4 slot ACK",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const vm=read("web/approved-fx-rack-view-model.js");
 for(const name of ["globalPowerContext","globalPowerPlan","globalPowerAckResult",
   "globalPowerSettlement","m4SlotPowerAckResult","m4SlotPowerSettlement"])
   assert.match(bridge,new RegExp("fxRackVM\\."+name+"\\("));
 assert.match(bridge,/const current=controller\?\.state\(\)\?\.current;\s*const button=document\.getElementById\?\.\("globalFxToggle"\)/);
 assert.match(bridge,/onAccepted:\(next,confirmed\)=>\{[\s\S]*?onNativeReadback\(next,confirmed\);/);
 assert.match(bridge,/fxRackVM\.nativeEditorAckResult\(pendingFxEditorAck,null,accepted,/);
 assert.match(bridge,/if\(fresh\)p\.lease=fresh;/);
 assert.match(bridge,/pendingFxEditorAck=null;\s*nativeToast\("FX editor unavailable · accepted instance changed"\);/);
 assert.match(bridge,/const apply=\(\)=>controller\.onSnapshot\(snapshot\);/);
 assert.doesNotMatch(bridge,/controller\.onSnapshot\(snapshot\);syncGlobalFxReadback/);
 assert.doesNotMatch(bridge,/const actual=snapshot\?\.globalFxBypassed;/);
 assert.match(bridge,/if\(verdict==="REJECTED"\)\{\s*pendingSlotPowers\.delete\(key\);/);
 assert.match(bridge,/}\s*else pending\.acked=true;/);
 assert.doesNotMatch(vm,/backend\.emitEvent|document\.querySelector|window\.setTimeout/);
 assert.match(bridge,/controller = protocol\.createController\(/);
});

test("MV-9B Global FX ignores rejected post-ACK snapshot after a pre-ACK value race",()=>{
 const vm=require("node:vm"),listeners=new Map(),sent=[],timers=[];
 let seq=101,wire=300,viewCalls=0;
 const scope={
   tracks:[],master:{id:1,name:"Master"},colors:["#456789"],
   wireGraph:{validateCandidateGraph:()=>({ok:true})},
   make:(name,kind,color)=>({id:seq++,name,kind,color,selected:false}),
   render(){},installGraph(tracks,master,edges){
     this.tracks=tracks;this.master=master;this.edges=edges;
   },allocateWireId:()=>wire++,displayMeterDbfs(){},stopDemo(){}
 };
 const backend={emitEvent:(name,payload)=>sent.push({name,payload}),
   addEventListener:(name,fn)=>listeners.set(name,fn)};
 const button={disabled:false,attrs:new Map(),title:"",
   setAttribute(name,val){this.attrs.set(name,String(val));},
   getAttribute(name){return this.attrs.get(name);}};
 const doc={readyState:"complete",documentElement:{dataset:{}},
   getElementById:id=>id==="globalFxToggle"?button:null,
   querySelector(){return null;},addEventListener(){}};
 const win={__JUCE__:{backend},
   ArSonApprovedNativeContract:protocol,
   ArSonApprovedDeviceViewModel:deviceVM,
   ArSonApprovedChannelStripViewModel:stripVM,
   ArSonApprovedFxRackViewModel:fxRackVM,
   ArSonApprovedWireViewModel:wireVM,
   __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},
   setTimeout(fn,ms){timers.push({fn,ms});return timers.length},clearTimeout(){},
   queueMicrotask(fn){fn();},
   __ARMIX_SET_GLOBAL_FX_VIEW__(){viewCalls++;}
 };
 vm.runInNewContext(read("web/approved-native-bridge.js"),
   {window:win,document:doc,performance:{now:()=>0},console},{timeout:1500});
 const initial=acceptedFixture();
 initial.globalFxBypassed=false;
 initial.graph.channels[0].inserts=[{id:"geq-live",catalogId:"armix.builtin.graphic-eq31.v1",
   name:"Graphic EQ 31",bypassed:false},
   {id:"reverb-legacy",catalogId:"armix.builtin.vocal-reverb.v1",
    name:"Vocal Reverb",bypassed:false},
   {id:"imersib-exact",catalogId:"armix.builtin.imersip.v1",
    name:"ImmerSIB",bypassed:true}];
 initial.mixerSession={authority:"CHANNEL_ID",deviceEpoch:initial.device.epoch,
   routeRevision:initial.graph.revision,rosterRevision:initial.graph.rosterRevision,
   channels:initial.graph.channels,main:{db:0,mute:false}};
 listeners.get("arMixConsoleSnapshot")(initial);
 assert.equal(scope.tracks[0].fx[1],"Reverb-Sis","old saved rack label follows stable catalog identity");
 assert.equal(scope.tracks[0].fxInsertIds[1].id,"reverb-legacy");
 // Real bridge consumeAction: a BYPASSED ImmerSIB is navigation, not a mutation.
 const editorRequests=()=>sent.filter(x=>x.payload?.action==="routingFxEditor");
 assert.equal(win.__ARMIX_NATIVE_M2__.consumeAction(scope.tracks[0],"openPlugin",
   {dataset:{slot:"2"}}),true);
 assert.equal(editorRequests().length,1);
 const opened=editorRequests()[0].payload;
 assert.equal(opened.instanceId,"imersib-exact");
 assert.equal(opened.channelId,initial.graph.channels[0].id);
 assert.equal(opened.slot,2);
 listeners.get("arMixRoutingFxEditorAck")({requestId:opened.requestId,
   instanceId:opened.instanceId,ok:true});
 assert.equal(win.__ARMIX_NATIVE_M2__.consumeAction(scope.tracks[0],"openPlugin",
   {dataset:{slot:"2"}}),true);
 assert.equal(editorRequests().length,2);
 const reopened=editorRequests()[1].payload;
 assert.equal(reopened.instanceId,opened.instanceId);
 assert.notEqual(reopened.requestId,opened.requestId);
 listeners.get("arMixRoutingFxEditorAck")({requestId:reopened.requestId,
   instanceId:reopened.instanceId,ok:true});
 assert.equal(win.__ARMIX_NATIVE_GLOBAL_FX__.toggle(),true);
 const dispatched=sent.findLast(x=>x.payload?.action==="routingGlobalFxBypass");
 assert.ok(dispatched);
 assert.equal(dispatched.payload.value,true);
 assert.equal(button.getAttribute("aria-busy"),"true");
 // Native value has advanced before its ACK: no false completion yet.
 const premature=clone(initial);premature.globalFxBypassed=true;
 listeners.get("arMixConsoleSnapshot")(premature);
 assert.equal(button.getAttribute("aria-busy"),"true");
 const beforeInvalid=viewCalls;
 listeners.get("arMixGlobalFxAck")({requestId:dispatched.payload.requestId,
   ok:true,bypassed:true});
 listeners.get("arMixConsoleSnapshot")({schemaVersion:-1,
   globalFxBypassed:true});
 assert.equal(button.getAttribute("aria-busy"),"true",
   "malformed post-ACK envelope must never unlock Global FX");
 assert.equal(viewCalls,beforeInvalid,
   "rejected snapshot must never confirm cached pre-ACK value");
 // Only a later fully accepted native snapshot settles Global FX.
 listeners.get("arMixConsoleSnapshot")(clone(premature));
 assert.equal(button.getAttribute("aria-busy"),"false");
 assert.equal(button.disabled,false);
 assert.ok(viewCalls>beforeInvalid);
 // Embedded GEQ recovery uses the exact same global transaction, with no
 // local slot/rack mutation, and cannot invert an already enabled global gate.
 const hooks=win.__ARMIX_NATIVE_M2__,track=scope.tracks[0];
 const action={dataset:{slot:"0"}};
 const requests=()=>sent.filter(x=>x.payload?.action==="routingGlobalFxBypass");
 assert.equal(hooks.consumeAction(track,"globalFxOn",{dataset:{slot:"7"}}),true);
 assert.equal(requests().length,1);
 assert.equal(hooks.consumeAction(track,"globalFxOn",action),true);
 assert.equal(requests().length,2);
 const enable=requests().at(-1).payload;
 assert.equal(enable.value,false);
 assert.equal(enable.deviceEpoch,initial.device.epoch);
 assert.equal(enable.revision,initial.graph.revision);
 hooks.consumeAction(track,"globalFxOn",action);
 assert.equal(requests().length,2,"duplicate click cannot race pending global ACK");
 listeners.get("arMixGlobalFxAck")({requestId:enable.requestId,ok:true,bypassed:false});
 assert.equal(button.getAttribute("aria-busy"),"true");
 listeners.get("arMixConsoleSnapshot")(clone(initial));
 assert.equal(button.getAttribute("aria-busy"),"false");
 hooks.consumeAction(track,"globalFxOn",action);
 assert.equal(requests().length,2,"stale enable click must not bypass FX again");
 // Transport failure must release only this transaction without waiting for
 // another snapshot or replaying a toggle against an unconfirmed state.
 assert.equal(win.__ARMIX_NATIVE_GLOBAL_FX__.toggle(),true);
 const rejected=requests().at(-1).payload;
 listeners.get("arMixGlobalFxAck")({requestId:rejected.requestId,ok:false,error:"busy"});
 assert.equal(button.disabled,false,"rejection must immediately unlock the control");
 assert.equal(button.getAttribute("aria-busy"),"false");
 assert.equal(requests().length,3,"never replay rejected intent");
 assert.equal(win.__ARMIX_NATIVE_GLOBAL_FX__.toggle(),true);
 const timeout=requests().at(-1).payload;
 const expire=timers.filter(x=>x.ms===8000).at(-1).fn;
 expire();
 assert.equal(button.disabled,false,"timeout must not depend on another snapshot");
 assert.equal(button.getAttribute("aria-busy"),"false");
 assert.equal(requests().length,4);
 assert.equal(win.__ARMIX_NATIVE_GLOBAL_FX__.toggle(),true);
 const retry=requests().at(-1).payload;
 assert.notEqual(retry.requestId,timeout.requestId);
 expire();
 listeners.get("arMixGlobalFxAck")({requestId:timeout.requestId,ok:false});
 assert.equal(button.disabled,true,"obsolete timeout/ACK must not unlock newer request");
 listeners.get("arMixGlobalFxAck")({requestId:retry.requestId,ok:false});
 assert.equal(button.disabled,false);
});

test("MV-11 channel appearance projection rejects partial, stale and forged identities",()=>{
 const big="9007199254740993",maximum="18446744073709551615";
 const snap={device:{epoch:"51"},graph:{revision:"21",rosterRevision:"5",
   channels:[{id:big,name:"Vox"},{id:maximum,name:"MAIN"}]},raw:{mixerSession:{
     authority:"CHANNEL_ID"},channelAppearance:{schema:"arsonmix.native.channel-appearance.v1",
     revision:"0",readOnly:false,entries:[{id:big,name:"Voice",color:"#aabbcc",icon:"🎙"}]}}};
 const before=JSON.stringify(snap),project=x=>stripVM.identityReadback(x,protocol);
 const p=project(snap);
 assert.deepEqual(p,{kind:"VALID",revision:"0",writable:true,
   entries:[{id:big,name:"Voice",color:"#aabbcc",icon:"🎙"}]});
 assert.equal(project({...snap,raw:{...snap.raw,channelAppearance:{
   ...snap.raw.channelAppearance,readOnly:true}}}).writable,false);
 for(const invalid of [
   {...snap,graph:{...snap.graph,channels:[snap.graph.channels[0],snap.graph.channels[0]]}},
   {...snap,raw:{...snap.raw,channelAppearance:{...snap.raw.channelAppearance,
     revision:"18446744073709551616"}}},
   {...snap,raw:{...snap.raw,channelAppearance:{...snap.raw.channelAppearance,
     entries:[...snap.raw.channelAppearance.entries,...snap.raw.channelAppearance.entries]}}},
   {...snap,raw:{...snap.raw,channelAppearance:{...snap.raw.channelAppearance,
     entries:[{id:"9007199254740994",name:"Ghost",color:"#aabbcc",icon:"🎙"}]}}},
   {...snap,raw:{...snap.raw,channelAppearance:{...snap.raw.channelAppearance,
     entries:[{id:big,name:"Voice",color:"wrong",icon:"🎙"}]}}},
   {...snap,raw:{...snap.raw,channelAppearance:{...snap.raw.channelAppearance,
     entries:[{id:Number(big),name:"Voice",color:"#aabbcc",icon:"🎙"}]}}},
   {...snap,raw:{...snap.raw,channelAppearance:{...snap.raw.channelAppearance,
     entries:[{id:big,name:"Voice",color:"#aabbcc",icon:"<script>"}]}}}
 ])assert.equal(project(invalid).kind,"INVALID");
 assert.equal(JSON.stringify(snap),before);
});
test("MV-11 native identity plan is one exact ChannelId and no optimistic PCM mutation",()=>{
 const a="9007199254740993",b="18446744073709551615";
 const native={admitted:true,retained:false,device:{epoch:"71"},graph:{
   revision:"101",rosterRevision:"13",
   channels:[{id:a,name:"Vox"},{id:b,name:"Keys"}]},
   raw:{mixerSession:{authority:"CHANNEL_ID"}}};
 const presentation=new Map([[a,{name:"Vox",color:"#eeeeee",icon:"🎙"}]]);
 const input={native,channelId:a,rawName:"  Studio    Vox ",color:"#aabbcc",
   icon:"🎙",appearance:presentation,appearanceRevision:"0",
   appearanceWritable:true,valid:protocol};
 const before=JSON.stringify(native);
 const plan=stripVM.identityEditPlan(input);
 assert.equal(plan.kind,"READY");
 assert.equal(plan.pending.name,"Studio Vox");
 assert.equal(plan.payload.name,"Studio Vox");
 assert.equal(plan.payload.deviceEpoch,"71");
 assert.equal(plan.payload.graphRevision,"101");
 assert.equal(plan.payload.rosterRevision,"13");
 assert.equal(plan.payload.appearanceRevision,"0");
 assert.equal(plan.payload.channelId,a);
 const kind=change=>stripVM.identityEditPlan({...input,...change}).kind;
 assert.equal(kind({channelId:Number(a)}),"UNAVAILABLE");
 assert.equal(kind({channelId:"18446744073709551616"}),"UNAVAILABLE");
 assert.equal(kind({channelId:"15"}),"UNAVAILABLE");
 assert.equal(kind({rawName:"Keys"}),"DUPLICATE");
 assert.equal(kind({rawName:"  "}),"INVALID_NAME");
 assert.equal(kind({color:"f00"}),"INVALID_FIELDS");
 assert.equal(kind({icon:"<img>"}),"INVALID_FIELDS");
 assert.equal(kind({pending:true}),"BUSY");
 assert.equal(kind({appearanceWritable:false}),"UNAVAILABLE");
 assert.equal(kind({appearanceRevision:"18446744073709551616"}),"UNAVAILABLE");
 assert.equal(kind({native:{...native,admitted:false,retained:false}}),"UNAVAILABLE");
 assert.equal(kind({native:{...native,raw:{mixerSession:{authority:"M4"}}}}),"UNAVAILABLE");
 assert.equal(kind({native:{...native,device:{epoch:"0"}}}),"UNAVAILABLE");
 assert.equal(kind({native:{...native,graph:{...native.graph,revision:"0"}}}),"UNAVAILABLE");
 assert.equal(kind({native:{...native,graph:{...native.graph,rosterRevision:"0"}}}),"UNAVAILABLE");
 assert.equal(kind({native:{...native,retained:true,admitted:false}}),"READY",
   "physical remap may retain native cosmetic editing, without PCM admission");
 assert.equal(JSON.stringify(native),before,"pure selector never mutates accepted graph");
});
test("MV-11 logical-only channel rename stays a saved draft, not a physical appearance request",()=>{
 const channelId="9007199254740993";
 const logical={readOnly:false,graph:{channels:[{id:channelId,name:"Vox"},
   {id:"9007199254740995",name:"Bass"}]}};
 const p={logical,channelId,rawName:" New    Vox ",valid:protocol};
 assert.deepEqual(stripVM.identityEditPlan(p),{
   kind:"LOGICAL",channelId,name:"New Vox",operation:"RENAME_CHANNEL",
   payload:{name:"New Vox"}});
 assert.equal(stripVM.identityEditPlan({...p,rawName:"bass"}).kind,"DUPLICATE");
 // Native workspace cosmetic aliases are not the offline .smix name owner.
 const staleNativeAlias=new Map([["9007199254740995",{
   name:"From Old Native",color:"#eeeeee",icon:"🎹"}]]);
 assert.equal(stripVM.identityEditPlan({...p,rawName:"From Old Native",
   appearance:staleNativeAlias}).kind,"LOGICAL");
 assert.equal(stripVM.identityEditPlan({...p,rawName:"Bass",
   appearance:staleNativeAlias}).kind,"DUPLICATE");
 assert.equal(stripVM.identityEditPlan({...p,logical:{...logical,readOnly:true}}).kind,"UNAVAILABLE");
 assert.equal(stripVM.identityEditPlan({...p,pending:true}).kind,"UNAVAILABLE");
});
test("MV-11 channel identity requires ACK and the subsequent same-lease appearance readback",()=>{
 const id="9007199254740993";
 const snap={admitted:true,retained:false,device:{epoch:"100"},graph:{
   revision:"200",rosterRevision:"300",channels:[{id,name:"Original"}]},
   raw:{mixerSession:{authority:"CHANNEL_ID"}}};
 const row={id,name:"Studio Vox",color:"#112233",icon:"🎙"};
 const projected={kind:"VALID",revision:"71",writable:true,entries:[row]};
 const pending={requestId:"appearance-9",channelId:id,name:row.name,
   color:row.color,icon:row.icon,deviceEpoch:"100",graphRevision:"200",
   rosterRevision:"300",beforeAppearanceRevision:"70",acked:false};
 const settle=(p=pending,s=snap,v=projected)=>
   stripVM.identitySettlement(p,s,v);
 assert.equal(settle(),"WAIT","pre-ACK matching value cannot confirm");
 assert.equal(settle({...pending,acked:true}),"CONFIRMED");
 assert.equal(settle({...pending,acked:true},snap,
   {...projected,revision:"69"}),"STALE");
 assert.equal(settle({...pending,acked:true},snap,
   {...projected,revision:"18446744073709551616"}),"WAIT");
 assert.equal(settle({...pending,acked:true},{...snap,device:{epoch:"101"}}),"STALE");
 assert.equal(settle({...pending,acked:true},{...snap,graph:{...snap.graph,revision:"201"}}),"STALE");
 assert.equal(settle({...pending,acked:true},{...snap,graph:{...snap.graph,rosterRevision:"301"}}),"STALE");
 assert.equal(settle({...pending,acked:true},{...snap,raw:{mixerSession:{
   authority:"M4"}}}),"STALE");
 assert.equal(settle({...pending,acked:true},{...snap,retained:true,admitted:false}),
   "CONFIRMED","retained native document may still be the appearance authority");
 assert.equal(settle({...pending,acked:true},snap,
   {...projected,entries:[{...row,icon:"🎹"}]}),"WAIT");
 assert.equal(settle({...pending,acked:true},snap,{kind:"INVALID"}),"WAIT");
 assert.equal(settle(null),"NONE");
 assert.equal(stripVM.identityAckResult(pending,{requestId:"wrong",ok:true}),"IGNORE");
 assert.equal(stripVM.identityAckResult(pending,{requestId:"appearance-9",ok:false}),"REJECTED");
 assert.equal(stripVM.identityAckResult(pending,{requestId:"appearance-9",ok:true}),"ACKED");
});
test("MV-11 legacy WebView channel aliases cannot overwrite genuine native identity",()=>{
 const id="9007199254740993",other="9007199254740995";
 const source={admitted:true,retained:false,device:{epoch:"90"},graph:{
   revision:"99",rosterRevision:"19",channels:[
     {id,name:"Vocal"},{id:other,name:"Bass"}]},
   raw:{mixerSession:{authority:"CHANNEL_ID"},channelAppearance:{
     revision:"4"}}};
 const old={name:"Old Vocal",color:"#112233",icon:"🎙"};
 const params={snapshot:source,id,identity:old,appearance:new Map(),
   writable:true,pending:false,deferred:new Set(),valid:protocol};
 const send=stripVM.legacyIdentityDecision(params);
 assert.equal(send.kind,"SEND");
 assert.equal(send.plan.payload.channelId,id);
 assert.equal(send.plan.pending.beforeAppearanceRevision,"4");
 const got=changed=>stripVM.legacyIdentityDecision({...params,...changed}).kind;
 assert.equal(got({appearance:new Map([[id,old]])}),"RETIRE");
 assert.equal(got({appearance:new Map([[id,{...old,name:"Native Edit"}]])}),"DEFER");
 assert.equal(got({deferred:new Set([id])}),"SKIP");
 assert.equal(got({writable:false}),"SKIP");
 assert.equal(got({pending:true}),"SKIP");
 assert.equal(got({snapshot:{...source,raw:{...source.raw,mixerSession:{
   authority:"M4"}}}}),"SKIP");
 assert.equal(got({identity:{...old,name:"Bass"}}),"CONFLICT");
});
test("MV-11 bridge delegates identity decisions and retains one native host/control path",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const vm=read("web/approved-channel-strip-view-model.js");
 for(const name of ["identityReadback","identityEditPlan","identityAckResult",
   "identitySettlement","legacyIdentityDecision"])
   assert.match(bridge,new RegExp("stripVM\\."+name+"\\("));
 assert.match(bridge,/backend\.addEventListener\("arMixChannelIdentityAck"/);
 assert.match(bridge,/backend\.emitEvent\("arMixChannelIdentitySet"/);
 assert.match(bridge,/controller = protocol\.createController\(/);
 assert.doesNotMatch(vm,/backend\.emitEvent|document\.getElementById|window\.setTimeout|localStorage/);
 assert.doesNotMatch(bridge,/const liveIds=new Set\(next\.graph\.channels/);
 assert.doesNotMatch(bridge,/if\(ack\.ok!==true\)\{\s*if\(pendingIdentity\.migration\)/);
});


test("G-MV29 M4 OUT and M/ST pure policy denies stale authority, unsafe tokens, and busy source",()=>{
 const rows=Array.from({length:10},(_,i)=>({ordinal:i,role:"INPUT",
   primaryOut:"NONE",source:{main:false,channelFormat:"MONO"}}));
 const sources=rows.map(x=>({...x.source}));
 const session={authority:"M4",deviceEpoch:"21",routeRevision:"12",channels:rows};
 const legacy={active:true,routeAccepted:true,nativeBusStructural:false,
   routingRevision:"12",channels:sources};
 const snapshot={admitted:false,retained:false,device:{epoch:"21"},
   raw:{mixerSession:session,legacyM4:legacy}};
 const plan=(operation,target,extra={},raw=snapshot)=>
   stripVM.m4ChannelPatchPlan(raw,{operation,target,channel:4,
     verifiedM4:true,deviceReady:true,...extra});
 const baseline=JSON.stringify(snapshot);
 const out=plan("SET_PRIMARY_OUT","MAIN");
 assert.equal(out.kind,"READY");
 assert.equal(out.action,"routingChannelPatch");
 assert.deepEqual(out.payload,{operation:"SET_PRIMARY_OUT",channel:4,
   target:"MAIN",deviceEpoch:21,revision:12});
 assert.deepEqual(out.pending,{channel:4,operation:"SET_PRIMARY_OUT",target:"MAIN",
   epoch:"21",baseRevision:"12",acked:false});
 const fmt=plan("SET_PROCESSING_FORMAT","STEREO");
 assert.equal(fmt.kind,"READY");
 assert.equal(fmt.action,out.action);
 assert.deepEqual(fmt.payload,{operation:"SET_PROCESSING_FORMAT",channel:4,
   target:"STEREO",deviceEpoch:21,revision:12});
 assert.equal(plan("SET_PRIMARY_OUT","NONE").kind,"NO_CHANGE");
 assert.equal(plan("SET_PROCESSING_FORMAT","MONO").kind,"NO_CHANGE");
 const deny=(op="SET_PRIMARY_OUT",target="MAIN",extra={},raw=snapshot)=>
   assert.equal(plan(op,target,extra,raw).kind,"UNAVAILABLE");
 deny("SET_PRIMARY_OUT","bus");
 deny("SET_PROCESSING_FORMAT","SIDE");
 deny("DELETE","MAIN");
 for(const extra of [{verifiedM4:false},{deviceReady:false},{pending:true},
   {sourcePending:true},{channel:-1},{channel:10}])deny("SET_PRIMARY_OUT","MAIN",extra);
 for(const raw of [
   {...snapshot,admitted:true},{...snapshot,retained:true},
   {...snapshot,device:{epoch:"22"}},
   {...snapshot,device:{epoch:"9007199254740993"},raw:{...snapshot.raw,
     mixerSession:{...session,deviceEpoch:"9007199254740993"}}},
   {...snapshot,raw:{...snapshot.raw,mixerSession:{...session,authority:"CHANNEL_ID"}}},
   {...snapshot,raw:{...snapshot.raw,legacyM4:{...legacy,active:false}}},
   {...snapshot,raw:{...snapshot.raw,legacyM4:{...legacy,routeAccepted:false}}},
   {...snapshot,raw:{...snapshot.raw,legacyM4:{...legacy,nativeBusStructural:true}}},
   {...snapshot,raw:{...snapshot.raw,legacyM4:{...legacy,routingRevision:"13"}}},
   {...snapshot,raw:{...snapshot.raw,mixerSession:{...session,
     channels:rows.slice(0,9)}}},
   {...snapshot,raw:{...snapshot.raw,legacyM4:{...legacy,
     channels:sources.slice(0,9)}}},
   {...snapshot,raw:{...snapshot.raw,mixerSession:{...session,
     channels:rows.map((r,i)=>i===4?{...r,primaryOut:"MAIN"}:r)}}}
 ])deny("SET_PRIMARY_OUT","MAIN",{},raw);
 deny("SET_PROCESSING_FORMAT","STEREO",{}, {...snapshot,raw:{...snapshot.raw,
   legacyM4:{...legacy,channels:sources.map((s,i)=>i===4?
     {...s,channelFormat:undefined}:s)}}});
 assert.equal(JSON.stringify(snapshot),baseline,"ViewModel cannot mutate installed PCM");
});
test("G-MV29 M4 channel patch ACK and accepted readback are independently correlated",()=>{
 const rows=Array.from({length:10},(_,i)=>({ordinal:i,role:"INPUT",
   source:{main:false,channelFormat:"MONO"},primaryOut:"NONE"}));
 const sources=rows.map(x=>({...x.source}));
 const sess={authority:"M4",deviceEpoch:"21",routeRevision:"12",channels:rows};
 const m={active:true,routeAccepted:true,nativeBusStructural:false,
   routingRevision:"12",channels:sources};
 const old={admitted:false,retained:false,device:{epoch:"21"},
   raw:{mixerSession:sess,legacyM4:m}};
 const p={requestId:7002,channel:4,operation:"SET_PRIMARY_OUT",target:"MAIN",
   epoch:"21",baseRevision:"12",acked:false};
 const ack={requestId:7002,channel:4,ok:true,
   state:{epoch:21,revision:13,acceptedRoute:{channels:[]}}};
 const ackResult=(a)=>stripVM.m4ChannelPatchAckResult(p,a);
 assert.equal(ackResult({...ack,requestId:1}),"IGNORE");
 assert.equal(ackResult({...ack,channel:5}),"REJECTED");
 assert.equal(ackResult({...ack,ok:false}),"REJECTED");
 assert.equal(ackResult({...ack,state:{epoch:22}}),"REJECTED");
 assert.equal(ackResult(ack),"ACKED");
 const check=(pending,view)=>stripVM.m4ChannelPatchSettlement(pending,view);
 assert.equal(check(p,old),"WAIT");
 assert.equal(check({...p,acked:true},old),"WAIT");
 const changed=rows.map((row,i)=>i===4?
   {...row,primaryOut:"MAIN",source:{...row.source,main:true}}:row);
 const changedSources=sources.map((s,i)=>i===4?{...s,main:true}:s);
 const next={...old,raw:{mixerSession:{...sess,routeRevision:"13",channels:changed},
   legacyM4:{...m,routingRevision:"13",channels:changedSources}}};
 assert.equal(check(p,next),"WAIT");
 assert.equal(check({...p,acked:true},next),"CONFIRMED");
 for(const bad of [{...next,retained:true},{...next,admitted:true},
   {...next,device:{epoch:"22"}},
   {...next,raw:{...next.raw,mixerSession:{...next.raw.mixerSession,
     authority:"CHANNEL_ID"}}},
   {...next,raw:{...next.raw,legacyM4:{...next.raw.legacyM4,
     routingRevision:"14"}}},
   {...next,raw:{...next.raw,legacyM4:{...next.raw.legacyM4,
     channels:sources}}}])
   assert.equal(check({...p,acked:true},bad),"STALE");
 const notYet={...next,raw:{mixerSession:{...next.raw.mixerSession,channels:rows},
   legacyM4:{...next.raw.legacyM4,channels:sources}}};
 assert.equal(check({...p,acked:true},notYet),"WAIT");
 const noRevision={...old,raw:{mixerSession:{...sess,channels:changed},
   legacyM4:{...m,channels:changedSources}}};
 assert.equal(check({...p,acked:true},noRevision),"WAIT");
 const format={...p,operation:"SET_PROCESSING_FORMAT",target:"STEREO"};
 const newFmt=changed.map((row,i)=>i===4?{...row,
   source:{...row.source,channelFormat:"STEREO"}}:row);
 const newFmtLegacy=changedSources.map((row,i)=>i===4?
   {...row,channelFormat:"STEREO"}:row);
 assert.equal(check({...format,acked:true},{...next,raw:{
   mixerSession:{...next.raw.mixerSession,channels:newFmt},
   legacyM4:{...next.raw.legacyM4,channels:newFmtLegacy}}}),"CONFIRMED");
 assert.equal(check({...format,acked:true},next),"WAIT");
 const bridge=read("web/approved-native-bridge.js");
 for(const name of ["m4ChannelPatchPlan","m4ChannelPatchAckResult",
   "m4ChannelPatchSettlement"])assert.ok(bridge.includes("stripVM."+name+"("));
 assert.ok(bridge.includes("reconcileM4ChannelPatchReadback(next)"));
 assert.ok(bridge.includes("action:plan.action,requestId,...plan.payload"));
 assert.equal(bridge.includes("const row=ack?.state?.acceptedRoute?.channels"),false);
});


test("G-MV29 real M4 bridge holds OUT and M/ST pending until accepted PCM, including late ACK/timer",()=>{
 const vm=require("node:vm"),listeners=new Map(),sent=[],timers=[],microtasks=[];
 const tracks=Array.from({length:10},(_,i)=>({
   id:i+1,kind:"audio",name:"CH "+(i+1),input:"None",
   output:"SOURCE ONLY (No Main)",format:"mono",fx:[],slotBypass:[],fxInsertIds:[]}));
 const master={id:50,kind:"master",output:"Master"};
 const scope={tracks,master,findTrack:el=>el?.track||null,
   stopDemo(){},render(){}};
 const backend={emitEvent:(name,payload)=>sent.push({name,payload}),
   addEventListener:(name,fn)=>listeners.set(name,fn)};
 const menu={dataset:{field:"output"},value:"Master",track:tracks[4],
   attrs:{},setAttribute(k,v){this.attrs[k]=v;},
   removeAttribute(k){delete this.attrs[k];}};
 const doc={readyState:"complete",documentElement:{dataset:{}},
   activeElement:null,
   getElementById:id=>id==="info"?{textContent:""}:
     id==="preview"?{disabled:false}:
     id==="armixNativeToast"?{hidden:true,textContent:""}:null,
   querySelector:q=>q.includes('select[data-field="output"]')?menu:null,
   addEventListener(){}};
 const win={__JUCE__:{backend},ArSonApprovedNativeContract:protocol,
   ArSonApprovedDeviceViewModel:deviceVM,
   ArSonApprovedChannelStripViewModel:stripVM,
   ArSonApprovedFxRackViewModel:fxRackVM,
   ArSonApprovedWireViewModel:wireVM,
   __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},
   setTimeout(fn,ms){timers.push({fn,ms});return timers.length;},
   clearTimeout(){},queueMicrotask(fn){microtasks.push(fn);}};
 vm.runInNewContext(read("web/approved-native-bridge.js"),{
   window:win,document:doc,performance:{now:()=>0},console},{timeout:2000});
 const installed=clone(fixtures.g0);
 Object.assign(installed.device,{backend:"ASIO",captureDevice:"Test Driver ASIO",
   playbackDevice:"Test Driver ASIO",enabledInputs:[0,1],
   inputEndpoints:[{id:"in:0",port:0,callback:0,label:"IN 1",active:true},
     {id:"in:1",port:1,callback:1,label:"IN 2",active:true}]});
 const rows=Array.from({length:10},(_,i)=>({
   mode:"NONE",left:-1,right:-1,main:false,channelFormat:"MONO"}));
 const controls=Array.from({length:10},()=>({
   db:0,pan:0,mute:false,solo:false}));
 installed.legacyM4={active:true,routeAccepted:true,nativeBusStructural:false,
   routingRevision:"13",fxInstances:Array.from({length:10},()=>[]),
   channelControls:controls,channels:rows};
 const makeSession=(src=rows,rev="13")=>({
   authority:"M4",deviceEpoch:installed.device.epoch,routeRevision:rev,
   rosterRevision:"0",channels:src.map((row,i)=>({
     id:"m4:"+String(i+1).padStart(2,"0"),ordinal:i,role:"INPUT",
     source:{...row},primaryOut:row.main?"MAIN":"NONE",
     controls:{faderDb:0,pan:0,mute:false,solo:false},
     fxRackBypassed:false
   })),main:{db:0,mute:false,outputArmed:false}
 });
 installed.mixerSession=makeSession();
 const publish=v=>listeners.get("arMixConsoleSnapshot")(v);
 const none=clone(installed);
 none.legacyM4.routeAccepted=false;
 none.mixerSession.authority="NONE";
 none.mixerSession.routeRevision="0";
 none.mixerSession.channels=[];
 publish(none);publish(installed);
 const actions=()=>sent.filter(e=>e.name==="arMixAction"&&
   e.payload.action==="routingChannelPatch").map(e=>e.payload);
 const formatClick=()=>win.__ARMIX_NATIVE_M2__.consumeAction(
   tracks[4],"format",{dataset:{}});
 assert.equal(formatClick(),true);
 assert.equal(actions().length,1,"verified M4 M/ST dispatches");
 const first=actions()[0];
 assert.equal(first.operation,"SET_PROCESSING_FORMAT");
 assert.equal(first.target,"STEREO");
 assert.equal(first.revision,13);
 formatClick();
 assert.equal(actions().length,1,"same in-flight format is fenced");
 // ACK is real but has no right to clear pending without readback.
 listeners.get("arMixChannelPatchAck")({
   requestId:first.requestId,channel:4,ok:true,state:{epoch:Number(installed.device.epoch),
     revision:14,acceptedRoute:{channels:[]}}});
 formatClick();
 assert.equal(actions().length,1,
   "even ACK claiming formatted output cannot unlock without readback");
 publish(installed);formatClick();
 assert.equal(actions().length,1,"unchanged accepted revision must not settle ACK");
 const newFormat=clone(installed);
 newFormat.legacyM4.routingRevision="14";
 newFormat.legacyM4.channels[4].channelFormat="STEREO";
 newFormat.mixerSession=makeSession(newFormat.legacyM4.channels,"14");
 publish(newFormat);
 // Now its native producer and session agree on a strictly newer format.
 formatClick();
 assert.equal(actions().length,2,"independent native readback releases format lease");
 const second=actions()[1];
 assert.equal(second.operation,"SET_PROCESSING_FORMAT");
 assert.equal(second.target,"MONO");
 const firstTimer=timers.filter(x=>x.ms===8000).at(-2);
 if(firstTimer)firstTimer.fn();
 listeners.get("arMixChannelPatchAck")({
   requestId:first.requestId,channel:4,ok:false,error:"OLD_DEVICE"});
 formatClick();
 assert.equal(actions().length,2,
   "superseded ACK and timeout cannot release second format request");
 // A failed second ACK releases the lease but must not fabricate a value.
 listeners.get("arMixChannelPatchAck")({
   requestId:second.requestId,channel:4,ok:false,error:"REJECTED"});
 // Source remains readback-owned STEREO; change OUT using same adapter.
 menu.value="Master";
 win.__ARMIX_NATIVE_M2__.consumeField({type:"change",target:menu});
 assert.equal(actions().length,3,"M4 primary OUT uses same native adapter");
 const out=actions()[2];
 assert.equal(out.operation,"SET_PRIMARY_OUT");
 assert.equal(out.target,"MAIN");
 assert.equal(menu.attrs["aria-busy"],"true");
 listeners.get("arMixChannelPatchAck")({
   requestId:out.requestId,channel:4,ok:true,state:{
     epoch:Number(installed.device.epoch),revision:15}});
 assert.equal(menu.attrs["aria-busy"],"true",
   "ACK alone must retain pending UI gate");
 const newOut=clone(newFormat);
 newOut.legacyM4.routingRevision="15";
 newOut.legacyM4.channels[4].main=true;
 newOut.mixerSession=makeSession(newOut.legacyM4.channels,"15");
 publish(newOut);
 assert.equal(menu.attrs["aria-busy"],undefined,
   "accepted M4 readback clears busy once native PCM proves OUT");
 assert.equal(menu.value,"Master");
 assert.equal(sent.filter(e=>e.name==="arMixAction"&&
   e.payload.action==="monitor").length,0,
   "no output monitor auto-arm in GUI refactor");
});


test("G-MV30 exact ChannelId roster create/delete policy, including safe bootstrap",()=>{
 const epoch="18446744073709551610",input="9007199254740993",
   bus="9007199254740994",fx="9007199254740995",main="9007199254740996";
 const rows=[{id:input,role:"INPUT"},{id:bus,role:"GROUP"},
   {id:fx,role:"FX_RETURN"},{id:main,role:"MAIN"}];
 const graph={revision:"5",rosterRevision:"7",channels:rows,
   capabilities:{supportedOps:["CREATE_CHANNEL","CREATE_GROUP",
     "DELETE_CHANNEL","REMOVE_BUS"]}};
 const session={authority:"CHANNEL_ID",deviceEpoch:epoch,
   routeRevision:"5",rosterRevision:"7"};
 const accepted={admitted:true,retained:false,device:{epoch},graph,
   raw:{mixerSession:session}};
 const hardware={device:{epoch}};
 const before=JSON.stringify(accepted);
 const p=(operation,channelId=null,override={},state=accepted)=>
   stripVM.rosterMutationPlan(state,{
     operation,channelId,hardware,deviceReady:true,...override
   });
 const cases=[["CREATE_CHANNEL",null,"routingNativeTrackCreate","INPUT"],
   ["CREATE_GROUP",null,"routingNativeBusCreate","GROUP"],
   ["DELETE_CHANNEL",input,"routingNativeTrackDelete","INPUT"],
   ["REMOVE_BUS",bus,"routingNativeBusDelete","GROUP"],
   ["REMOVE_BUS",fx,"routingNativeBusDelete","FX_RETURN"]];
 for(const [operation,id,action,role] of cases){
   const plan=p(operation,id);
   assert.equal(plan.kind,"READY",operation);
   assert.equal(plan.action,action);
   assert.equal(plan.payload.deviceEpoch,epoch);
   assert.equal(plan.payload.graphRevision,"5");
   assert.equal(plan.payload.rosterRevision,"7");
   assert.equal(plan.pending.operation,operation);
   if(id){assert.equal(plan.payload.channelId,id);
     assert.equal(plan.pending.id,id);assert.equal(plan.pending.role,role);}
   else assert.deepEqual(plan.pending.beforeIds,rows.map(x=>x.id));
 }
 for(const args of [
   ["DELETE_CHANNEL",bus],["REMOVE_BUS",input],["REMOVE_BUS",main],
   ["CREATE_BUS"],["DELETE_CHANNEL","7"],
   ["DELETE_CHANNEL",9007199254740993]
 ])assert.equal(p(...args).kind,"UNAVAILABLE");
 for(const options of [{deviceReady:false},{controllerPending:true},
   {structuralPending:true}])
   assert.equal(p("CREATE_CHANNEL",null,options).kind,"UNAVAILABLE");
 for(const state of [
   {...accepted,admitted:false},
   {...accepted,retained:true},
   {...accepted,device:{epoch:"12"}},
   {...accepted,raw:{mixerSession:{...session,authority:"M4"}}},
   {...accepted,raw:{mixerSession:{...session,routeRevision:"6"}}},
   {...accepted,raw:{mixerSession:{...session,rosterRevision:"8"}}},
   {...accepted,graph:{...graph,revision:"0"}},
   {...accepted,graph:{...graph,rosterRevision:"0"}},
   {...accepted,graph:{...graph,capabilities:{supportedOps:[]}}}
 ])assert.equal(p("CREATE_GROUP",null,{},state).kind,"UNAVAILABLE");
 const duplicate={...accepted,graph:{...graph,channels:rows.concat(rows[0])}};
 assert.equal(p("DELETE_CHANNEL",input,{},duplicate).kind,"UNAVAILABLE");
 const silent={admitted:false,retained:false,device:{epoch},raw:{
   mixerSession:{authority:"NONE",deviceEpoch:epoch},
   legacyM4:{active:true,routeAccepted:false,routingRevision:"13"}}};
 const bootstrap=p("CREATE_CHANNEL",null,{},silent);
 assert.equal(bootstrap.kind,"READY");
 assert.deepEqual(bootstrap.payload,{deviceEpoch:epoch,bootstrapRevision:"13"});
 assert.equal(bootstrap.pending.bootstrap,true);
 assert.equal(p("CREATE_GROUP",null,{},silent).kind,"UNAVAILABLE");
 assert.equal(p("DELETE_CHANNEL",input,{},silent).kind,"UNAVAILABLE");
 for(const legacy of [
   {...silent.raw.legacyM4,active:false},
   {...silent.raw.legacyM4,routeAccepted:true},
   {...silent.raw.legacyM4,routingRevision:"9007199254740993"}
 ])assert.equal(p("CREATE_CHANNEL",null,{}, {...silent,raw:{...silent.raw,
   legacyM4:legacy}}).kind,"UNAVAILABLE");
 assert.equal(JSON.stringify(accepted),before,
   "pure roster policy cannot mutate native mixer or UI graph");
});
test("G-MV30 native roster ACK, concurrent lanes and exact later graph+roster proof",()=>{
 const epoch="18446744073709551610",src="9007199254740993",
   newId="9007199254740994",bus="9007199254740995",main="9007199254740996";
 const channels=[{id:src,role:"INPUT"},{id:bus,role:"GROUP"},{id:main,role:"MAIN"}];
 const graph={revision:"5",rosterRevision:"7",channels,
   capabilities:{supportedOps:["CREATE_CHANNEL","CREATE_GROUP",
     "DELETE_CHANNEL","REMOVE_BUS"]}};
 const session={authority:"CHANNEL_ID",deviceEpoch:epoch,
   routeRevision:"5",rosterRevision:"7"};
 const base={admitted:true,retained:false,device:{epoch},graph,
   raw:{mixerSession:session}};
 const plan=(operation,channelId=null)=>stripVM.rosterMutationPlan(base,{
   operation,channelId,hardware:{device:{epoch}},deviceReady:true});
 const acked=(p,ack)=>stripVM.rosterAckResult(p,ack);
 const settle=(p,s)=>stripVM.rosterReadbackSettlement(p,s);
 const create={requestId:"bus-81",...plan("CREATE_GROUP").pending};
 const createAck={requestId:create.requestId,ok:true,
   createdId:newId,graphRevision:"6"};
 assert.equal(acked(create,{...createAck,requestId:"bus-80"}),"IGNORE");
 assert.equal(acked(create,{...createAck,ok:false}),"REJECTED");
 assert.equal(acked(create,{...createAck,graphRevision:"5"}),"REJECTED");
 assert.equal(acked(create,{...createAck,createdId:bus}),"REJECTED",
   "replayed ID of an existing channel can never prove new BUS");
 assert.equal(acked(create,{...createAck,createdId:"9007199254740993.1"}),"REJECTED");
 assert.equal(acked(create,createAck),"ACKED");
 const pCreate={...create,acked:true,ackRevision:"6",createdId:newId};
 assert.equal(settle(create,base),"WAIT");
 assert.equal(settle(pCreate,base),"WAIT",
   "ACK does not unlock a BUS without independently accepted PCM");
 const newlyCreated=channels.concat({id:newId,role:"GROUP"});
 const advanced=(rows=newlyCreated,revision="6",rosterRevision="8")=>({
   ...base,graph:{...graph,revision,rosterRevision,channels:rows},
   raw:{mixerSession:{...session,routeRevision:revision,rosterRevision}}
 });
 assert.equal(settle(pCreate,advanced()),"CONFIRMED");
 const grouped={...pCreate,selection:[src]};
 assert.equal(settle(grouped,advanced()),"WAIT","Created bus alone cannot prove reroute");
 assert.equal(settle(grouped,advanced(newlyCreated.map(ch=>ch.id===src?{...ch,primaryOutId:newId}:ch))),"CONFIRMED");
 assert.equal(settle(create,advanced()),"WAIT","readback-before-ACK does not finish");
 assert.equal(settle(pCreate,advanced(newlyCreated,"6","7")),"WAIT",
   "roster revision must advance beyond request baseline");
 assert.equal(settle(pCreate,advanced(newlyCreated,"5","8")),"WAIT",
   "graph revision must advance beyond request baseline");
 assert.equal(settle(pCreate,advanced(channels)),"WAIT",
   "new revision alone cannot synthesize missing createdId");
 assert.equal(settle(pCreate,advanced(channels.concat({id:newId,role:"INPUT"}))),
   "WAIT","requested native BUS must have installed GROUP role");
 assert.equal(settle(pCreate,{...advanced(),retained:true}),"STALE");
 assert.equal(settle(pCreate,{...advanced(),device:{epoch:"22"}}),"STALE");
 assert.equal(settle(pCreate,{...advanced(),raw:{mixerSession:{
   ...session,authority:"M4",routeRevision:"6",rosterRevision:"8"}}}),"STALE");
 assert.equal(settle(pCreate,{...advanced(),raw:{mixerSession:{
   ...session,routeRevision:"6",rosterRevision:"7"}}}),"STALE");
 assert.equal(settle(pCreate,advanced(newlyCreated,"4","8")),"STALE",
   "a regressed native graph cannot close an old transaction");
 const remove={requestId:"bus-delete-82",...plan("REMOVE_BUS",bus).pending};
 const removeAck={requestId:remove.requestId,channelId:bus,ok:true,
   graphRevision:"6"};
 assert.equal(acked(remove,{...removeAck,channelId:src}),"REJECTED");
 assert.equal(acked(remove,{...removeAck,requestId:"bus-delete-1"}),"IGNORE");
 assert.equal(acked(remove,removeAck),"ACKED");
 const pRemove={...remove,acked:true,ackRevision:"6"};
 assert.equal(settle(pRemove,advanced(channels.filter(x=>x.id!==bus))),"CONFIRMED");
 assert.equal(settle(pRemove,advanced(channels)),"WAIT");
 assert.equal(settle(remove,advanced(channels.filter(x=>x.id!==bus))),"WAIT");
 const inputDelete={requestId:"track-delete-83",...plan("DELETE_CHANNEL",src).pending};
 assert.equal(acked(inputDelete,{...removeAck,requestId:inputDelete.requestId,
   channelId:src}),"ACKED");
 assert.equal(settle({...inputDelete,acked:true,ackRevision:"6"},
   advanced(channels.filter(x=>x.id!==src))),"CONFIRMED");
 const silent={admitted:false,retained:false,device:{epoch},
   raw:{mixerSession:{authority:"NONE",deviceEpoch:epoch},
   legacyM4:{active:true,routeAccepted:false,routingRevision:"13"}}};
 const bootstrap=stripVM.rosterMutationPlan(silent,{
   operation:"CREATE_CHANNEL",hardware:{device:{epoch}},deviceReady:true});
 const bPending={requestId:"track-99",...bootstrap.pending};
 assert.equal(acked(bPending,{requestId:"track-99",ok:true,
   createdId:newId,graphRevision:"6"}),"ACKED");
 assert.equal(settle({...bPending,acked:true,ackRevision:"6",
   createdId:newId},silent),"WAIT","first track stays pending while native admits");
 const installedInput=advanced(channels.concat({id:newId,role:"INPUT"}));
 assert.equal(settle({...bPending,acked:true,ackRevision:"6",
   createdId:newId},installedInput),"CONFIRMED");
 const bridge=read("web/approved-native-bridge.js");
 for(const symbol of ["rosterMutationPlan","rosterAckResult","rosterReadbackSettlement"])
   assert.ok(bridge.includes("stripVM."+symbol+"("),
     "bridge must use one pure roster authority: "+symbol);
 assert.ok(bridge.includes('if(verdict==="ACKED")'));
 assert.ok(bridge.includes('finishNativeTrack(controller?.state()?.current)'));
 assert.equal(bridge.includes("else pendingBusCreate=null"),false,
   "BUS ACK cannot release the mutation before accepted readback");
 assert.equal(bridge.includes('action:"routingNativeBusDelete"'),false,
   "routing action comes from one pure transaction plan");
});


test("G-MV30 first native ChannelId projection enables Add Track without a second callback",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const success=bridge.indexOf("flags.productionCallbackLive=next.productLive;flags.acceptedGraphConnected=true;");
 assert.ok(success>=0);
 const after=bridge.slice(success,success+500);
 assert.ok(after.includes("syncAddTrack(next);"),
   "native button should re-evaluate only AFTER successful graph projection");
 assert.ok(after.indexOf("syncAddTrack(next);")<
   after.indexOf("syncEmergencyOutputControl(next.device);"));
 const before=bridge.indexOf("if (!projectNativeGraphWithoutFocusLoss(next))",success-500);
 assert.ok(before>=0&&before<success,
   "never enable a native Add before the authoritative ChannelId graph projects");
});


test("G-MV31 pure WIRE PRE/POST plans bind exact send identities, never PRIMARY/MAIN/mystery EdgeId",()=>{
 const src="9007199254740993",other="9007199254740994",
   bus="9007199254740995",fx="9007199254740996",
   main="18446744073709551615",id="9223372036854775809",
   otherId="9223372036854775810",mainId="9223372036854775811",
   primaryId="9223372036854775812";
 const send={edgeId:id,from:src,to:bus,enabled:true,primaryOutput:false,
   tap:"POST_FADER",sendEnabled:true,gainLinear:0.5};
 const fxSend={...send,edgeId:otherId,to:fx};
 const mainSend={...send,edgeId:mainId,to:main};
 const primary={...send,edgeId:primaryId,primaryOutput:true,gainLinear:1};
 const graph={mainChannelId:main,channels:[
   {id:src,role:"INPUT"},{id:other,role:"INPUT"},
   {id:bus,role:"GROUP"},{id:fx,role:"FX_RETURN"},
   {id:main,role:"MAIN"}],edges:[send,mainSend,primary],
   capabilities:{supportedOps:["APPLY_WIRE_TRANSACTION","SET_SEND_ENABLED"]}};
 const snap={admitted:true,retained:false,graph};
 const before=JSON.stringify(snap);
 const tap=(edgeId=id,targetTap="PRE_FADER",extra={},state=snap)=>
   wireVM.nativeTapPlan(state,{sourceId:src,edgeId,targetTap,
     projected:true,...extra});
 const expected={kind:"DISPATCH",operation:"APPLY_WIRE_TRANSACTION",
   source:src,payload:{changes:[{op:"SET_TAP",from:src,edgeId:id,tap:"PRE_FADER"}]}};
 assert.deepEqual(tap(),expected);
 assert.deepEqual(tap(id,"POST_FADER"),{kind:"NO_CHANGE"});
 assert.deepEqual(tap(id,null),{kind:"AVAILABLE",tap:"POST_FADER"});
 assert.equal(wireVM.channelTapEdge(snap,{sourceId:src}).edgeId,id,
   "parallel MAIN and PRIMARY do not steal channel PRE/POST fallback");
 assert.equal(wireVM.channelTapEdge(snap,{sourceId:src,selectedEdgeId:id}).edgeId,id);
 assert.equal(wireVM.channelTapEdge(snap,{sourceId:src,selectedEdgeId:primaryId}),null);
 assert.equal(wireVM.channelTapEdge(snap,{sourceId:src,selectedEdgeId:mainId}),null);
 assert.equal(wireVM.channelTapEdge(snap,{sourceId:src,selectedEdgeId:otherId}),null);
 assert.equal(wireVM.channelTapEdge(snap,{sourceId:other}),null);
 const two={...snap,graph:{...graph,edges:[...graph.edges,fxSend]}};
 assert.equal(wireVM.channelTapEdge(two,{sourceId:src}),null,
   "two SENDs require explicit identity, not a guessed first match");
 assert.equal(wireVM.channelTapEdge(two,{sourceId:src,selectedEdgeId:otherId}).edgeId,otherId);
 assert.equal(wireVM.channelTapEdge(two,{sourceId:src,selectedEdgeId:"invalid"}),null);
 for(const [edgeId,target,extra,state] of [
   [primaryId,"PRE_FADER",{},snap],
   [id,"NO_TAP",{},snap],
   [id,"PRE_FADER",{projected:false},snap],
   [id,"PRE_FADER",{pending:true},snap],
   [id,"PRE_FADER",{}, {...snap,admitted:false}],
   [id,"PRE_FADER",{}, {...snap,retained:true}],
   [id,"PRE_FADER",{}, {...snap,graph:{...graph,capabilities:{supportedOps:[]}}}],
   [id,"PRE_FADER",{}, {...snap,graph:{...graph,edges:[...graph.edges,send]}}],
   [id,"PRE_FADER",{}, {...snap,graph:{...graph,channels:graph.channels.filter(ch=>ch.id!==src)}}],
   [id,"PRE_FADER",{}, {...snap,graph:{...graph,edges:[{...send,from:other},mainSend,primary]}}]
 ])assert.equal(tap(edgeId,target,extra,state).kind,"UNAVAILABLE");
 for(const wrong of ["0","-1","9007199254740993.1","18446744073709551616",
   9223372036854775808])
   assert.equal(tap(wrong).kind,"UNAVAILABLE");
 assert.equal(wireVM.nativeTapPlan(snap,{sourceId:src,
   edgeId:undefined,targetTap:"PRE_FADER",projected:true}).kind,"UNAVAILABLE",
   "missing EdgeId must not be replaced by the test helper default");
 assert.equal(JSON.stringify(snap),before,"pure selectors cannot mutate live PCM graph");
});
test("G-MV31 SOF exact BUS/FX send selection and ON/OFF fail closed under pending or owner change",()=>{
 const src="9007199254740993",other="9007199254740994",
   bus="9007199254740995",fx="9007199254740996",
   main="18446744073709551615",edgeId="9223372036854775809";
 const rows=[{id:src,role:"INPUT"},{id:other,role:"INPUT"},
   {id:bus,role:"GROUP"},{id:fx,role:"FX_RETURN"},{id:main,role:"MAIN"}];
 const edge={edgeId,from:src,to:bus,enabled:true,primaryOutput:false,
   tap:"POST_FADER",sendEnabled:true,gainLinear:1};
 const graph={channels:rows,mainChannelId:main,edges:[edge],
   capabilities:{supportedOps:["SET_SEND_ENABLED","APPLY_WIRE_TRANSACTION",
     "SET_WIRE_GAIN"]}};
 const snap={admitted:true,retained:false,graph};
 const sof=(sourceId=src,destinationId=bus,state=snap)=>
   wireVM.sofSendEdge(state,{sourceId,destinationId});
 assert.equal(sof(),edge);
 assert.equal(sof(src,fx),null,"FX return without a wire is not an SOF route");
 assert.equal(sof(src,main),null,"SOF cannot control a parallel MAIN send");
 assert.equal(sof(other,bus),null);
 assert.equal(sof(src,src),null);
 assert.equal(sof(src,bus,{...snap,retained:true}),null);
 assert.equal(sof(src,bus,{...snap,admitted:false}),null);
 assert.equal(sof(src,bus,{...snap,graph:{...graph,edges:[edge,edge]}}),null);
 assert.equal(sof(src,bus,{...snap,graph:{...graph,edges:[
   {...edge,primaryOutput:true}]}}),null);
 assert.equal(sof(src,bus,{...snap,graph:{...graph,edges:[{
   ...edge,from:other}]}}),null);
 assert.equal(sof(src,fx,{...snap,graph:{...graph,edges:[{
   ...edge,to:fx}]}}).edgeId,edgeId);
 const enabled=(overrides={},state=snap)=>wireVM.nativeSendTogglePlan(state,{
   sourceId:src,edgeId,projected:true,...overrides});
 assert.deepEqual(enabled(),{kind:"DISPATCH",operation:"SET_SEND_ENABLED",
   source:src,payload:{edgeId,enabled:false}});
 assert.equal(enabled({}, {...snap,graph:{...graph,edges:[{
   ...edge,sendEnabled:false}]}}).payload.enabled,true);
 for(const item of [
   [{projected:false},snap],[{pending:true},snap],[{}, {...snap,admitted:false}],
   [{}, {...snap,retained:true}],[{edgeId:"0"},snap],
   [{sourceId:other},snap],
   [{}, {...snap,graph:{...graph,edges:[{...edge,sendEnabled:null}]}}],
   [{}, {...snap,graph:{...graph,edges:[{...edge,primaryOutput:true}]}}],
   [{}, {...snap,graph:{...graph,capabilities:{supportedOps:["SET_WIRE_GAIN"]}}}]
 ])assert.equal(enabled(item[0],item[1]).kind,"UNAVAILABLE");
 assert.equal(wireVM.nativeTapPlan(snap,{sourceId:src,edgeId,
   projected:true}).kind,"AVAILABLE",
   "SOF PRE/POST and inspector share one exact EdgeId policy");
 const bridge=read("web/approved-native-bridge.js");
 for(const fn of ["sofSendEdge","channelTapEdge","nativeSendTogglePlan","nativeTapPlan"])
   assert.match(bridge,new RegExp("wireVM\\."+fn+"\\("));
 assert.equal((bridge.match(/function requestNativeTap\(/g)||[]).length,1,
   "SOF + WIRE inspector use one native transaction sender");
 assert.ok(bridge.includes('controller.intent(plan.operation,plan.source,plan.payload)'));
 assert.ok(bridge.includes('intent(plan.operation,t,plan.payload)'));
 assert.doesNotMatch(bridge,/const sends=current\.graph\.edges\.filter\(/,
   "native channel tapping no longer duplicates graph selection policy");
 assert.doesNotMatch(read("web/approved-wire-view-model.js"),
   /backend\.emitEvent|document\.querySelector|window\.setTimeout/,
   "VM is deterministic and has no independent dispatcher");
});

test("G-MV32 PRIMARY MAIN WIRE is one exact ChannelId gesture with safe SEND fallthrough",()=>{
 const source="9007199254740993",main="18446744073709551615",
   bus="9007199254740995",primaryId="9223372036854775809",
   sendId="9223372036854775810";
 const primary={edgeId:primaryId,from:source,to:main,enabled:true,
   primaryOutput:true,gainLinear:1};
 const send={...primary,edgeId:sendId,primaryOutput:false,gainLinear:0.5};
 const graph={mainChannelId:main,channels:[
   {id:source,role:"INPUT",primaryOutId:main},
   {id:bus,role:"GROUP"},{id:main,role:"MAIN"}],
   edges:[primary,send],capabilities:{supportedOps:["APPLY_WIRE_TRANSACTION"]}};
 const native={admitted:true,retained:false,graph};
 const uiEdges=[{id:41,sourceId:8,destId:0,kind:"primary",enabled:true},
   {id:42,sourceId:8,destId:0,kind:"send",enabled:true}];
 const remove={type:"REMOVE",sourceId:8,edgeId:41};
 const plan=(snapshot=native,overrides={})=>
   wireVM.primaryMainGesturePlan(snapshot,{
     op:remove,mainUiId:0,sourceId:source,edgeId:primaryId,uiEdges,
     ...overrides});
 const baseline=JSON.stringify(native);
 assert.deepEqual(plan(),{kind:"ROUTE",target:"NONE"});
 // Legacy admitted v6 graphs may omit the explicit PRIMARY flag.
 assert.deepEqual(plan({...native,graph:{...graph,edges:[
   {...primary,primaryOutput:undefined},send]}}),{kind:"ROUTE",target:"NONE"});
 assert.deepEqual(plan({...native,graph:{...graph,edges:[
   {...primary,primaryOutput:false},send]}}),{kind:"BLOCK"});
 assert.deepEqual(plan(native,{op:{type:"ROUTE_ONLY",sourceId:8,destId:0}}),
   {kind:"ROUTE",target:"MAIN"});
 for(const mode of [undefined,"postFader","routeOnly"])
   assert.deepEqual(plan(native,{op:{type:"CONNECT",sourceId:8,destId:0,mode}}),
     {kind:"ROUTE",target:"MAIN"});
 assert.deepEqual(plan(native,{op:{type:"CONNECT",sourceId:8,destId:0,mode:"send"}}),
   {kind:"PASS"},"parallel send to MAIN must stay a parallel send");
 assert.deepEqual(plan(native,{op:{type:"REMOVE",sourceId:8,edgeId:42},
   edgeId:sendId}),{kind:"PASS"},"parallel MAIN SEND is not the PRIMARY cable");
 assert.deepEqual(plan(native,{op:{type:"RETARGET",sourceId:8,edgeId:41,destId:9}}),
   {kind:"PASS"});
 for(const [label,snapshot,opts] of [
   ["retained",{...native,retained:true},{}],
   ["unadmitted",{...native,admitted:false},{}],
   ["changed PRIMARY OUT",{...native,graph:{...graph,channels:[
     {...graph.channels[0],primaryOutId:bus},...graph.channels.slice(1)]}},{}],
   ["missing PRIMARY",{...native,graph:{...graph,edges:[send]}},{}],
   ["duplicate producer EdgeId",{...native,graph:{...graph,edges:[
     ...graph.edges,{...send,edgeId:primaryId,from:bus}]}},{}],
   ["wrong native source",native,{sourceId:bus}],
   ["noncanonical EdgeId",native,{edgeId:"18446744073709551616"}],
   ["rounded JS EdgeId",native,{edgeId:9223372036854776000}],
   ["missing UI PRIMARY",native,{uiEdges:[]}],
   ["duplicate UI PRIMARY",native,{uiEdges:[uiEdges[0],uiEdges[0]]}],
   ["retired source role",{...native,graph:{...graph,channels:[
     {...graph.channels[0],role:"MAIN"},...graph.channels.slice(1)]}},{}],
 ])assert.deepEqual(plan(snapshot,opts),{kind:"BLOCK"},label);
 assert.equal(JSON.stringify(native),baseline,"pure policy cannot modify PCM");
});

test("G-MV32 M4 PRIMARY cable verifies exact owner, epoch, lane, route and UI identity",()=>{
 const legacy={routingRevision:"12",channels:Array.from({length:10},(_,i)=>({
   main:i===3,channelFormat:"STEREO"}))};
 const session={authority:"M4",deviceEpoch:"5",routeRevision:"12",
   channels:Array.from({length:10},(_,i)=>({
     role:"INPUT",ordinal:i,source:{main:i===3}}))};
 const native={admitted:false,retained:false,device:{epoch:"5"},
   raw:{mixerSession:session,legacyM4:legacy}};
 const op={type:"REMOVE",sourceId:8,edgeId:41};
 const uiEdges=[{id:41,sourceId:8,destId:0,kind:"primary",enabled:true}];
 const plan=(snapshot=native,overrides={})=>
   wireVM.primaryMainGesturePlan(snapshot,{op,mainUiId:0,
     uiEdges,m4Verified:true,m4Channel:3,...overrides});
 assert.deepEqual(plan(),{kind:"ROUTE",target:"NONE"});
 for(const [name,snapshot,overrides] of [
   ["M4 not verified",native,{m4Verified:false}],
   ["wrong lane",native,{m4Channel:2}],
   ["stale device epoch",{...native,device:{epoch:"6"}},{}],
   ["stale revision",{...native,raw:{...native.raw,
     mixerSession:{...session,routeRevision:"11"}}},{}],
   ["switch to ChannelId",{...native,raw:{...native.raw,
     mixerSession:{...session,authority:"CHANNEL_ID"}}},{}],
   ["retained device",{...native,retained:true},{}],
   ["lost output",{...native,raw:{...native.raw,legacyM4:{
     ...legacy,channels:legacy.channels.map((row,i)=>
       i===3?{...row,main:false}:row)}}},{}],
   ["wrong UI cable",native,{uiEdges:[{...uiEdges[0],destId:12}]}],
 ])assert.deepEqual(plan(snapshot,overrides),{kind:"BLOCK"},name);
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/wireVM\.primaryMainGesturePlan\(current,/);
 assert.match(bridge,/m4Verified:m4Verified\(\),m4Channel:m4Index\(t\)/);
 assert.equal((bridge.match(/function requestPrimaryOut\(/g)||[]).length,1,
   "same one OUT dispatcher serves strip selector and WIRE MAIN");
 assert.doesNotMatch(read("web/approved-wire-view-model.js"),
   /backend\.emitEvent|document\.querySelector|window\.setTimeout/,
   "pure ViewModel never becomes a second PCM controller");
});

test("G-MV30 M4 insert preflight requires one exact physical producer and next empty slot",()=>{
 const snapshot={admitted:false,retained:false,device:{epoch:"42"},raw:{
   mixerSession:{authority:"M4",deviceEpoch:"42",routeRevision:"10",
     channels:Array.from({length:10},()=>({role:"INPUT"}))},
   legacyM4:{active:true,routeAccepted:true,nativeBusStructural:false,
     routingRevision:"10",fxInstances:Array.from({length:10},()=>[])}
 }};
 const opts={channel:2,slot:0,catalogId:"com.vendor.delay",verifiedM4:true};
 const p=fxRackVM.m4InsertPlan(snapshot,opts);
 assert.equal(p.kind,"READY");
 assert.deepEqual(p.payload,{id:"com.vendor.delay",channel:2,slot:0,
   deviceEpoch:42,revision:10});
 assert.deepEqual(p.pending,{authority:"M4",id:"com.vendor.delay",
   channel:2,slot:0,epoch:"42",revision:"10",acked:false});
 for(const changed of [
   {verifiedM4:false},{offline:true},{pending:true},{channel:10},
   {channel:-1},{slot:8},{slot:1},{catalogId:""}
 ])assert.deepEqual(fxRackVM.m4InsertPlan(snapshot,{...opts,...changed}),
   {kind:"UNAVAILABLE"});
 for(const change of [
   s=>{s.admitted=true;},s=>{s.retained=true;},
   s=>{s.raw.mixerSession.authority="CHANNEL_ID";},
   s=>{s.raw.legacyM4.active=false;},
   s=>{s.raw.legacyM4.routeAccepted=false;},
   s=>{s.raw.legacyM4.nativeBusStructural=true;},
   s=>{s.raw.mixerSession.routeRevision="11";},
   s=>{s.device.epoch="9007199254740993";s.raw.mixerSession.deviceEpoch=s.device.epoch;},
   s=>{s.raw.legacyM4.fxInstances[2][0]={catalogId:"already-installed"};}
 ]){
   const next=clone(snapshot);change(next);
   assert.equal(fxRackVM.m4InsertPlan(next,opts).kind,"UNAVAILABLE");
 }
 const ordered=clone(snapshot);
 ordered.raw.legacyM4.fxInstances[2].push({instanceId:"fx-original",catalogId:"first",bypassed:false});
 assert.equal(fxRackVM.m4InsertPlan(ordered,{...opts,slot:1}).kind,"READY");
});
test("G-MV30 M4 insert ACK is not installed FX; only accepted producer readback opens editor",()=>{
 const p={authority:"M4",id:"com.vendor.delay",requestId:"fx-95",channel:2,
   slot:0,epoch:"42",revision:"10",acked:false};
 const snap=(revision="10",entry=null)=>{
   const fx=Array.from({length:10},()=>[]);if(entry)fx[2][0]=entry;
   return {admitted:false,retained:false,device:{epoch:"42"},raw:{
     mixerSession:{authority:"M4",deviceEpoch:"42",routeRevision:revision},
     legacyM4:{active:true,routeAccepted:true,nativeBusStructural:false,
       routingRevision:revision,fxInstances:fx}}};
 };
 const genuine={catalogId:p.id,instanceId:"native-instance-77",bypassed:false};
 const forged={requestId:p.requestId,ok:true,channel:2,slot:0,state:{
   epoch:"42",revision:"999",fxInstances:Array.from({length:10},()=>[])
 }};
 forged.state.fxInstances[2][0]=genuine;
 assert.equal(fxRackVM.m4InsertAckResult(p,forged),"ACKED");
 assert.equal(fxRackVM.m4InsertReadbackSettlement(p,snap()).kind,"WAIT");
 assert.equal(fxRackVM.m4InsertReadbackSettlement({...p,acked:true},snap()).kind,"WAIT");
 assert.equal(fxRackVM.m4InsertReadbackSettlement(p,snap("11",genuine)).kind,"WAIT",
   "producer readback before ACK cannot open a plugin editor");
 assert.deepEqual(fxRackVM.m4InsertReadbackSettlement({...p,acked:true},snap("11",genuine)),
   {kind:"CONFIRMED",channel:2,slot:0,instanceId:genuine.instanceId,
    epoch:42,revision:11});
 assert.equal(fxRackVM.m4InsertReadbackSettlement({...p,acked:true},
   snap("10",genuine)).kind,"WAIT","cached same revision cannot prove a structural insert");
 assert.equal(fxRackVM.m4InsertReadbackSettlement({...p,acked:true},snap("11")).kind,"WAIT");
 assert.equal(fxRackVM.m4InsertAckResult(p,{...forged,requestId:"fx-94"}),"IGNORE");
 assert.equal(fxRackVM.m4InsertAckResult(p,{...forged,channel:3}),"REJECTED");
 assert.equal(fxRackVM.m4InsertAckResult(p,{...forged,slot:1}),"REJECTED");
 assert.equal(fxRackVM.m4InsertAckResult(p,{...forged,ok:false}),"REJECTED");
 assert.equal(fxRackVM.m4InsertReadbackSettlement({...p,acked:true},
   snap("11",{...genuine,catalogId:"other"})).kind,"STALE");
 const wrongOwner=snap("11",genuine);wrongOwner.raw.mixerSession.authority="CHANNEL_ID";
 assert.equal(fxRackVM.m4InsertReadbackSettlement({...p,acked:true},wrongOwner).kind,"STALE");
 const unplugged=snap("11",genuine);unplugged.device.epoch="43";
 assert.equal(fxRackVM.m4InsertReadbackSettlement({...p,acked:true},unplugged).kind,"STALE");
 const stale=snap("9",genuine);
 assert.equal(fxRackVM.m4InsertReadbackSettlement({...p,acked:true},stale).kind,"STALE");
});
test("G-MV30 bridge has one M4 insert IPC, and never paints or opens from ack.state",()=>{
 const bridge=read("web/approved-native-bridge.js"),
   source=read("web/approved-fx-rack-view-model.js");
 assert.match(bridge,/fxRackVM\.m4InsertPlan\(accepted,/);
 assert.match(bridge,/fxRackVM\.m4InsertAckResult\(pendingFxInsert,ack\)/);
 assert.match(bridge,/fxRackVM\.m4InsertReadbackSettlement\(p,snapshot,/);
 assert.match(bridge,/reconcileM4FxInsertReadback\(next\)/);
 assert.equal((bridge.match(/action:"routingFxInsert"/g)||[]).length,0,
   "bridge must emit one ViewModel-planned action, not a duplicated raw dispatcher");
 const ack=bridge.split('backend.addEventListener("arMixRoutingFxInsertAck",ack=>{')[1]
   ?.split('backend.addEventListener("arMixCatalog",catalog=>{')[0];
 assert.ok(ack);
 assert.doesNotMatch(ack,/ack\?\.state|paintM4FxInstances\(|openRoutedFxEditor\(/);
 assert.doesNotMatch(source,/document\.|window\.|backend\.emitEvent|setTimeout/);
});

// G-MV31: automatic native/logical FX editor navigation must never cross a
// session generation between confirmed readback and the queued microtask.
test("G-MV31 ChannelId FX editor action is bound to physical graph, roster, device and instance",()=>{
  const raw=gmv20AcceptedGraph();
  raw.mixerSession={authority:"CHANNEL_ID",deviceEpoch:raw.device.epoch,
    routeRevision:raw.graph.revision,rosterRevision:raw.graph.rosterRevision};
  raw.graph.channels[0].inserts=[{id:"instance-native-22",
    catalogId:"vendor.fx-22",name:"Delay",bypassed:false}];
  const snap=protocol.normalizeSnapshot(raw);
  assert.ok(snap?.admitted,"must start from genuinely normalized native snapshot");
  const id=snap.graph.channels[0].id,
    hw=protocol.nativeHardwareConfiguration(snap.device);
  const open=fxRackVM.editorNavigationPlan(snap,{
    channelId:id,slot:0,hardwareConfiguration:hw});
  assert.equal(open.kind,"OPEN");
  assert.equal(open.lease.authority,"CHANNEL_ID");
  assert.equal(open.lease.channelId,id);
  assert.equal(open.lease.instanceId,"instance-native-22");
  assert.equal(fxRackVM.editorNavigationLeaseMatches(snap,open.lease,
    {hardwareConfiguration:hw}),true);
  for(const edit of [
    s=>{s.device.epoch="9999";s.raw.mixerSession.deviceEpoch="9999";},
    s=>{s.graph.revision="998";},
    s=>{s.graph.rosterRevision="997";},
    s=>{s.graph.channels[0].inserts[0].id="instance-replacement";},
    s=>{s.graph.channels[0].inserts[0].catalogId="another.vendor";},
    s=>{s.raw.mixerSession.authority="M4";},
    s=>{s.retained=true;s.admitted=false;},
    s=>{s.graph.channels[0].inserts=[];},
    s=>{s.graph.channels.push({...s.graph.channels[0]});},
  ]){
    const next=clone(snap);edit(next);
    assert.equal(fxRackVM.editorNavigationLeaseMatches(next,open.lease,
      {hardwareConfiguration:hw}),false);
  }
  assert.equal(fxRackVM.editorNavigationLeaseMatches(snap,open.lease,
    {hardwareConfiguration:hw+":new-buffer"}),false,
    "same-epoch device configuration replacement must revoke HWND navigation");
  assert.equal(fxRackVM.editorNavigationPlan(snap,{
    channelId:id,slot:1,hardwareConfiguration:hw}).kind,"PICKER");
  assert.equal(fxRackVM.editorNavigationPlan(snap,{
    channelId:id,slot:0,hardwareConfiguration:null}).kind,"UNAVAILABLE");
  assert.equal(fxRackVM.editorNavigationPlan(snap,{
    channelId:"9007199254740993",slot:0,hardwareConfiguration:hw}).kind,"UNAVAILABLE");
});
test("G-MV31 .smix editor navigation is scoped to exact workspace, graph, and FX lease",()=>{
  const ch={id:"9007199254740993",role:"INPUT",
    inserts:[{id:"logical-instance",catalogId:"catalog.test"}]};
  const base={logical:true,admitted:false,retained:false,workspaceToken:"work-a",
    device:{epoch:"0"},
    graph:{revision:"5",rosterRevision:"5",channels:[ch]}};
  const params={channelId:ch.id,slot:0};
  const open=fxRackVM.editorNavigationPlan(base,params);
  assert.equal(open.kind,"OPEN");
  assert.equal(open.lease.authority,"LOGICAL");
  assert.equal(open.lease.workspaceToken,"work-a");
  assert.equal(open.lease.deviceEpoch,null);
  assert.equal(fxRackVM.editorNavigationLeaseMatches(base,open.lease),true);
  const changed=clone(base);
  changed.workspaceToken="work-b";
  assert.equal(fxRackVM.editorNavigationLeaseMatches(changed,open.lease),false);
  changed.workspaceToken="work-a";changed.graph.revision="6";
  assert.equal(fxRackVM.editorNavigationLeaseMatches(changed,open.lease),false);
  const physical=clone(base);physical.logical=false;physical.admitted=true;
  assert.equal(fxRackVM.editorNavigationLeaseMatches(physical,open.lease),false);
  const offline=clone(base);offline.retained=true;
  assert.equal(fxRackVM.editorNavigationPlan(offline,params).kind,"UNAVAILABLE");
  const empty=clone(base);empty.graph.channels[0].inserts=[];
  assert.equal(fxRackVM.editorNavigationPlan(empty,params).kind,"PICKER");
});
test("G-MV31 manual M4 editor must read one admitted FX slot from current host",()=>{
  const snap={admitted:false,retained:false,device:{epoch:"55"},raw:{
    mixerSession:{authority:"M4",deviceEpoch:"55",routeRevision:"12",
      channels:Array.from({length:10},()=>({role:"INPUT"}))},
    legacyM4:{active:true,routeAccepted:true,nativeBusStructural:false,
      routingRevision:"12",fxInstances:Array.from({length:10},()=>[])}
  }};
  snap.raw.legacyM4.fxInstances[4][0]={
    instanceId:"verified-m4-fx",catalogId:"vendor.m4",bypassed:false};
  const args={channel:4,slot:0,verifiedM4:true};
  assert.deepEqual(fxRackVM.m4EditorPlan(snap,args),{kind:"OPEN",
    channel:4,slot:0,instanceId:"verified-m4-fx",deviceEpoch:55,revision:12});
  assert.equal(fxRackVM.m4EditorPlan(snap,{...args,slot:1}).kind,"PICKER");
  for(const delta of [
    {verifiedM4:false},{offline:true},{channel:-1},{slot:8}
  ])assert.equal(fxRackVM.m4EditorPlan(snap,{...args,...delta}).kind,"UNAVAILABLE");
  for(const change of [
    s=>{s.raw.mixerSession.authority="CHANNEL_ID";},
    s=>{s.raw.legacyM4.routingRevision="13";},
    s=>{s.device.epoch="56";},
    s=>{s.raw.legacyM4.routeAccepted=false;},
    s=>{s.raw.legacyM4.nativeBusStructural=true;},
    s=>{s.raw.legacyM4.fxInstances[4][0].instanceId="";}
  ]){
    const v=clone(snap);change(v);
    assert.equal(fxRackVM.m4EditorPlan(v,args).kind,"UNAVAILABLE");
  }
});
test("G-MV31 bridge delegates deferred and manually opened FX editors to one pure plan",()=>{
  const bridge=read("web/approved-native-bridge.js");
  assert.match(bridge,/fxRackVM\.editorNavigationPlan\(verified,/);
  assert.match(bridge,/fxRackVM\.editorNavigationPlan\(current,/);
  assert.match(bridge,/fxRackVM\.editorNavigationLeaseMatches\(current,lease,/);
  assert.match(bridge,/track&&nativeIdFor\(track\)===channelId/);
  assert.match(bridge,/fxRackVM\.m4EditorPlan\(current,/);
  assert.doesNotMatch(bridge,/function verifiedFxFor\(/);
  assert.match(bridge,/scope\?\.closeNativePicker\?\.\(\)/);
  assert.match(bridge,/openVerifiedInsertedFx\(p\.channelId,p\.slot,p\.catalogId,inserted\.id\)/);
  assert.doesNotMatch(read("web/approved-fx-rack-view-model.js"),
    /backend\.emitEvent|document\.|window\.|setTimeout/);
});


test("G-MV33 exact M4 FX host admission and hardware-scoped transaction settlement",()=>{
 const device={epoch:"31",sampleRate:48000,bufferFrames:256,
   enabledInputs:[0,1],enabledOutputs:[0,1]};
 const token=protocol.nativeHardwareConfiguration(device);
 assert.equal(typeof token,"string");
 const base={admitted:false,retained:false,device,raw:{
   mixerSession:{authority:"M4",deviceEpoch:"31",routeRevision:"12",
     channels:Array.from({length:10},()=>({fxRackBypassed:false}))},
   legacyM4:{active:true,routeAccepted:true,nativeBusStructural:false,
     routingRevision:"12",fxInstances:Array.from({length:10},(_,i)=>i===4?
       [{instanceId:"fx-old",catalogId:"vendor.old",bypassed:false}]:[])}
 }};
 const opts={channel:4,verifiedM4:true,hardwareConfiguration:token};
 const rack=fxRackVM.m4PowerPlan(base,{...opts,action:"fxOn"});
 const power=fxRackVM.m4PowerPlan(base,{...opts,action:"slotPower",slot:0});
 const insert=fxRackVM.m4InsertPlan(base,{...opts,slot:1,catalogId:"vendor.new"});
 const editor=fxRackVM.m4EditorPlan(base,{...opts,slot:0});
 assert.deepEqual([rack.kind,power.kind,insert.kind,editor.kind],
   ["READY","READY","READY","OPEN"]);
 for(const plan of [rack,power,insert])
   assert.equal(plan.pending.hardwareConfiguration,token);
 assert.equal(editor.instanceId,"fx-old");
 for(const badSlot of [undefined,null,"0",-1,8,NaN]){
   assert.equal(fxRackVM.m4InsertPlan(base,{...opts,slot:badSlot,
     catalogId:"vendor.new"}).kind,"UNAVAILABLE");
   assert.equal(fxRackVM.m4EditorPlan(base,{...opts,slot:badSlot}).kind,
     "UNAVAILABLE");
   assert.equal(fxRackVM.m4PowerPlan(base,{...opts,slot:badSlot,
     action:"slotPower"}).kind,"UNAVAILABLE");
 }
 for(const delta of [
   {verifiedM4:false},{offline:true},{hardwareConfiguration:null},
   {hardwareConfiguration:""},{channel:10}
 ]){
   const input={...opts,...delta};
   assert.equal(fxRackVM.m4PowerPlan(base,{...input,action:"fxOn"}).kind,
     "UNAVAILABLE");
   assert.equal(fxRackVM.m4InsertPlan(base,{...input,slot:1,
     catalogId:"vendor.new"}).kind,"UNAVAILABLE");
   assert.equal(fxRackVM.m4EditorPlan(base,{...input,slot:0}).kind,
     "UNAVAILABLE");
 }
 for(const change of [
   s=>{s.admitted=true;},s=>{s.retained=true;},
   s=>{s.raw.mixerSession.authority="CHANNEL_ID";},
   s=>{s.raw.mixerSession.routeRevision="13";},
   s=>{s.raw.legacyM4.active=false;},
   s=>{s.raw.legacyM4.routeAccepted=false;},
   s=>{s.raw.legacyM4.nativeBusStructural=true;},
   s=>{s.raw.legacyM4.routingRevision="9007199254740993";},
   s=>{s.device.epoch="32";}
 ]){
   const changed=clone(base);change(changed);
   assert.equal(fxRackVM.m4PowerPlan(changed,{...opts,action:"fxOn"}).kind,"UNAVAILABLE");
   assert.equal(fxRackVM.m4InsertPlan(changed,{...opts,slot:1,
     catalogId:"vendor.new"}).kind,"UNAVAILABLE");
   assert.equal(fxRackVM.m4EditorPlan(changed,{...opts,slot:0}).kind,"UNAVAILABLE");
 }
 const remapped=clone(base);remapped.device.enabledInputs=[2,3];
 const newer=protocol.nativeHardwareConfiguration(remapped.device);
 assert.notEqual(newer,token);
 remapped.raw.mixerSession.channels[4].fxRackBypassed=true;
 remapped.raw.legacyM4.fxInstances[4][0].bypassed=true;
 assert.equal(fxRackVM.m4RackSettlement(
   {...rack.pending,acked:true},remapped,newer),"STALE");
 assert.equal(fxRackVM.m4SlotPowerSettlement(
   {...power.pending,acked:true},remapped,newer),"STALE");
 const installed=clone(remapped);
 installed.raw.legacyM4.routingRevision="13";
 installed.raw.mixerSession.routeRevision="13";
 installed.raw.legacyM4.fxInstances[4][1]={
   instanceId:"fx-new",catalogId:"vendor.new",bypassed:false};
 assert.equal(fxRackVM.m4InsertReadbackSettlement(
   {...insert.pending,acked:true},installed,newer).kind,"STALE");
 assert.equal(fxRackVM.m4RackSettlement(
   {...rack.pending,acked:true},base,token),"WAIT");
 const acceptedRack=clone(base);
 acceptedRack.raw.mixerSession.channels[4].fxRackBypassed=true;
 assert.equal(fxRackVM.m4RackSettlement(
   {...rack.pending,acked:true},acceptedRack,token),"CONFIRMED");
 const acceptedPower=clone(base);
 acceptedPower.raw.legacyM4.fxInstances[4][0].bypassed=true;
 assert.equal(fxRackVM.m4SlotPowerSettlement(
   {...power.pending,acked:true},acceptedPower,token),"CONFIRMED");
 const acceptedInsert=clone(base);
 acceptedInsert.raw.legacyM4.routingRevision="13";
 acceptedInsert.raw.mixerSession.routeRevision="13";
 acceptedInsert.raw.legacyM4.fxInstances[4][1]={
   instanceId:"fx-new",catalogId:"vendor.new",bypassed:false};
 assert.equal(fxRackVM.m4InsertReadbackSettlement(
   {...insert.pending,acked:true},acceptedInsert,token).kind,"CONFIRMED");
 assert.equal(fxRackVM.m4InsertReadbackSettlement(
   {...insert.pending,acked:false},acceptedInsert,token).kind,"WAIT");
 assert.equal(fxRackVM.m4InsertReadbackSettlement(
   {...insert.pending,acked:true},acceptedInsert,null).kind,"STALE");
});
test("G-MV33 late duplicate negative ACK cannot discard verified FX lease",()=>{
 const rack={requestId:"rack-1",channel:4,epoch:"31",revision:"12",
   bypassed:true,acked:true};
 const rackAck={requestId:"rack-1",channel:4,deviceEpoch:31,
   routingRevision:12,bypassed:true,ok:false};
 assert.equal(fxRackVM.m4RackAckResult(rack,rackAck),"IGNORE");
 assert.equal(fxRackVM.m4RackAckResult({...rack,acked:false},rackAck),"REJECTED");
 const power={requestId:"power-1",channel:4,slot:0,acked:true};
 const powerAck={requestId:"power-1",channel:4,slot:0,ok:false};
 assert.equal(fxRackVM.m4SlotPowerAckResult(power,powerAck),"IGNORE");
 assert.equal(fxRackVM.m4SlotPowerAckResult({...power,acked:false},powerAck),
   "REJECTED");
 const insert={authority:"M4",requestId:"insert-1",channel:4,slot:1,acked:true};
 const insertAck={requestId:"insert-1",channel:4,slot:1,ok:false,
   state:{fxInstances:[{instanceId:"fake"}]}};
 assert.equal(fxRackVM.m4InsertAckResult(insert,insertAck),"IGNORE");
 assert.equal(fxRackVM.m4InsertAckResult({...insert,acked:false},insertAck),
   "REJECTED");
 const source=read("web/approved-fx-rack-view-model.js");
 const bridge=read("web/approved-native-bridge.js");
 assert.equal((source.match(/function m4FxHost\(/g)||[]).length,1);
 assert.equal((source.match(/const host=m4FxHost\(snapshot,/g)||[]).length,3);
 for(const name of ["m4PowerPlan","m4InsertPlan","m4EditorPlan"])
   assert.match(source,new RegExp("function "+name+"\\(snapshot,"));
 assert.match(bridge,/hardwareConfiguration:protocol\.nativeHardwareConfiguration\(current\?\.device\)/);
 assert.match(bridge,/hardwareConfiguration:protocol\.nativeHardwareConfiguration\(accepted\?\.device\)/);
 assert.match(bridge,/fxRackVM\.m4RackSettlement\(p,next,/);
 assert.match(bridge,/fxRackVM\.m4SlotPowerSettlement\(p,next,/);
 assert.match(bridge,/fxRackVM\.m4InsertReadbackSettlement\(p,snapshot,/);
 assert.doesNotMatch(source,/backend\.emitEvent|document\.|window\.|setTimeout/);
});


test("G-MV34 exact physical ChannelId FX admission + global/power/insert readback",()=>{
 const device={epoch:"55",sampleRate:48000,bufferFrames:256,
   enabledInputs:[0,1],enabledOutputs:[0,1]};
 const hardwareConfiguration=protocol.nativeHardwareConfiguration(device),
   channelId="9007199254740993";
 const view={admitted:true,retained:false,device,graph:{
   revision:"81",rosterRevision:"45",
   capabilities:{callbackOwnsChannelIds:true,supportedOps:["LOAD_INSERT"]},
   channels:[{id:channelId,role:"INPUT",fxRackBypassed:false,
     inserts:[{id:"fx-one",catalogId:"vendor.one",bypassed:false}]}]},
   raw:{globalFxBypassed:false,
     mixerSession:{authority:"CHANNEL_ID",deviceEpoch:"55"}}};
 const o={channelId,hardwareConfiguration};
 const context=fxRackVM.globalPowerContext(view,false,false,hardwareConfiguration);
 const rack=fxRackVM.nativePowerPlan(view,{...o,action:"fxOn"});
 const slot=fxRackVM.nativePowerPlan(view,{...o,action:"slotPower",slot:0});
 const insert=fxRackVM.nativeInsertPlan(view,{...o,slot:1,catalogId:"vendor.two"});
 assert.deepEqual([rack.kind,slot.kind,insert.kind],["READY","READY","READY"]);
 assert.equal(context.hardwareConfiguration,hardwareConfiguration);
 assert.equal(rack.pending.hardwareConfiguration,hardwareConfiguration);
 assert.equal(slot.pending.rosterRevision,"45");
 for(const invalid of [null,"",5]){
   assert.equal(fxRackVM.globalPowerContext(view,false,false,invalid),null);
   assert.equal(fxRackVM.nativePowerPlan(view,{...o,hardwareConfiguration:invalid,
     action:"fxOn"}).kind,"UNAVAILABLE");
   assert.equal(fxRackVM.nativeInsertPlan(view,{...o,hardwareConfiguration:invalid,
     slot:1,catalogId:"vendor.two"}).kind,"UNAVAILABLE");
 }
 for(const mutate of [
   v=>{v.admitted=false;},v=>{v.retained=true;},
   v=>{v.raw.mixerSession.authority="M4";},
   v=>{v.raw.mixerSession.deviceEpoch="56";},
   v=>{v.graph.capabilities.callbackOwnsChannelIds=false;},
   v=>{v.graph.rosterRevision="0";}
 ]){
   const altered=clone(view);mutate(altered);
   assert.equal(fxRackVM.nativePowerPlan(altered,{...o,action:"fxOn"}).kind,
     "UNAVAILABLE");
   assert.equal(fxRackVM.nativeInsertPlan(altered,{...o,slot:1,
     catalogId:"vendor.two"}).kind,"UNAVAILABLE");
 }
 const g={requestId:"g",...context,bypassed:true,acked:true};
 const r={requestId:"r",...rack.pending,acked:true};
 const s={requestId:"s",...slot.pending,acked:true};
 const ip={authority:"CHANNEL_ID",requestId:"i",id:"vendor.two",
   channelId,slot:1,epoch:"55",hardwareConfiguration};
 const confirm={requestId:"i",operation:"LOAD_INSERT",targetChannelId:channelId,
   payload:{catalogId:"vendor.two",slot:1}};
 const read=clone(view);
 read.raw.globalFxBypassed=true;
 read.graph.channels[0].fxRackBypassed=true;
 read.graph.channels[0].inserts[0].bypassed=true;
 assert.equal(fxRackVM.globalPowerSettlement(g,read,context),"CONFIRMED");
 assert.equal(fxRackVM.nativePowerSettlement(r,read,hardwareConfiguration),
   "CONFIRMED");
 assert.equal(fxRackVM.nativePowerSettlement(s,read,hardwareConfiguration),
   "CONFIRMED");
 assert.equal(fxRackVM.nativePowerSettlement({...r,acked:false},
   read,hardwareConfiguration),"WAIT");
 const loaded=clone(read);
 loaded.graph.channels[0].inserts[1]={id:"fx-two",catalogId:"vendor.two",
   bypassed:false,name:"Vendor Two"};
 loaded.graph.revision="82";loaded.graph.rosterRevision="46";
 assert.equal(fxRackVM.nativeInsertSettlement(ip,loaded,confirm,
   hardwareConfiguration).kind,"CONFIRMED");
 assert.equal(fxRackVM.nativePowerSettlement(r,loaded,hardwareConfiguration),
   "STALE","changed FX roster retires old bypass gesture");
 const remap=clone(read);
 remap.device.enabledInputs=[2,3];
 const remapId=protocol.nativeHardwareConfiguration(remap.device);
 assert.notEqual(remapId,hardwareConfiguration);
 assert.equal(fxRackVM.globalPowerSettlement(g,remap,
   {...context,hardwareConfiguration:remapId}),"STALE");
 assert.equal(fxRackVM.nativePowerSettlement(r,remap,remapId),"STALE");
 assert.equal(fxRackVM.nativePowerSettlement(s,remap,remapId),"STALE");
 assert.equal(fxRackVM.nativeInsertSettlement(ip,loaded,confirm,
   remapId).kind,"STALE");
 assert.equal(fxRackVM.nativeInsertSettlement(ip,loaded,confirm,
   null).kind,"STALE");
 assert.equal(fxRackVM.nativePowerSettlement(r,read,null),"STALE");
});
test("G-MV34 native/global positive FX ACK wins across duplicate negative ACK",()=>{
 const native={requestId:"n",channelId:"9007199254740993",
   kind:"rack",bypassed:true,acked:true};
 const global={requestId:"g",bypassed:true,acked:true};
 const nAck={requestId:"n",channelId:native.channelId,ok:false,bypassed:true};
 const gAck={requestId:"g",ok:false,bypassed:true};
 assert.equal(fxRackVM.nativePowerAckResult(native,nAck),"IGNORE");
 assert.equal(fxRackVM.globalPowerAckResult(global,gAck),"IGNORE");
 assert.equal(fxRackVM.nativePowerAckResult({...native,acked:false},nAck),
   "REJECTED");
 assert.equal(fxRackVM.globalPowerAckResult({...global,acked:false},gAck),
   "REJECTED");
 const source=read("web/approved-fx-rack-view-model.js"),
   bridge=read("web/approved-native-bridge.js");
 assert.equal((source.match(/function nativePhysicalFxReady\(/g)||[]).length,1);
 assert.match(bridge,/fxRackVM\.nativePowerSettlement\(pending,next,hardwareConfiguration\)/);
 assert.match(bridge,/fxRackVM\.nativeInsertSettlement\(pendingFxInsert,next,confirmed,/);
 assert.match(bridge,/const hardwareConfiguration=protocol\.nativeHardwareConfiguration\(accepted\?\.device\)/);
 assert.match(bridge,/pendingGlobalFx=\{requestId,bypassed:plan\.bypassed,acked:false,\.\.\.ctx\}/);
 assert.doesNotMatch(source,/backend\.emitEvent|document\.|window\.|setTimeout/);
});

test("G-MV35 queued M4 FX editor dispatch rejects replaced accepted PCM owner",()=>{
 const device={epoch:"55",sampleRate:48000,bufferFrames:256,
   enabledInputs:[0,1],enabledOutputs:[0,1]};
 const hardwareConfiguration=protocol.nativeHardwareConfiguration(device);
 const snap={admitted:false,retained:false,device,raw:{
   mixerSession:{authority:"M4",deviceEpoch:"55",routeRevision:"12",
     channels:Array.from({length:10},()=>({role:"INPUT"}))},
   legacyM4:{active:true,routeAccepted:true,nativeBusStructural:false,
     routingRevision:"12",fxInstances:Array.from({length:10},()=>[])}
 }};
 snap.raw.legacyM4.fxInstances[4][0]={instanceId:"installed-4",
   catalogId:"vendor.alpha",bypassed:false};
 const opts={channel:4,slot:0,instanceId:"installed-4",
   deviceEpoch:55,revision:12,verifiedM4:true,hardwareConfiguration};
 const initial=JSON.stringify(snap);
 const plan=fxRackVM.m4EditorDispatchPlan(snap,opts);
 assert.equal(plan.kind,"READY");
 assert.deepEqual(plan.payload,{channel:4,slot:0,instanceId:"installed-4",
   deviceEpoch:55,revision:12});
 assert.equal(plan.action,"routingFxEditor");
 assert.equal(Object.isFrozen(plan.lease),true);
 assert.equal(plan.lease.catalogId,"vendor.alpha");
 const matches=(s=snap,c=hardwareConfiguration)=>
   fxRackVM.m4EditorDispatchLeaseMatches(s,plan.lease,{
     verifiedM4:true,hardwareConfiguration:c});
 assert.equal(matches(),true);
 assert.equal(matches(snap,"changed"),false);
 for(const mutation of [
   s=>{s.device.epoch="56";},
   s=>{s.device.enabledOutputs=[2,3];},
   s=>{s.raw.legacyM4.routeAccepted=false;},
   s=>{s.raw.legacyM4.active=false;},
   s=>{s.raw.legacyM4.nativeBusStructural=true;},
   s=>{s.raw.legacyM4.routingRevision="13";s.raw.mixerSession.routeRevision="13";},
   s=>{s.raw.mixerSession.authority="CHANNEL_ID";},
   s=>{s.raw.mixerSession.deviceEpoch="56";},
   s=>{s.raw.legacyM4.fxInstances[4][0].instanceId="replacement";},
   s=>{s.raw.legacyM4.fxInstances[4][0].catalogId="vendor.beta";},
   s=>{s.raw.legacyM4.fxInstances[4][0]=null;},
   s=>{s.retained=true;},
   s=>{s.admitted=true;}
 ]) {
   const next=clone(snap);mutation(next);
   const physical=protocol.nativeHardwareConfiguration(next.device);
   assert.equal(matches(next,physical),false);
 }
 for(const corrupted of [
   {...opts,slot:-1},{...opts,slot:8},{...opts,deviceEpoch:56},
   {...opts,revision:13},{...opts,instanceId:"wrong"},
   {...opts,verifiedM4:false},{...opts,offline:true},
   {...opts,hardwareConfiguration:""},
   {...opts,hardwareConfiguration:null}
 ])assert.equal(fxRackVM.m4EditorDispatchPlan(snap,corrupted).kind,"UNAVAILABLE");
 assert.equal(fxRackVM.m4EditorDispatchLeaseMatches(snap,
   {...plan.lease,unexpected:true},{verifiedM4:true,hardwareConfiguration}),false);
 assert.equal(fxRackVM.m4EditorDispatchLeaseMatches(snap,null,{
   verifiedM4:true,hardwareConfiguration}),false);
 assert.equal(JSON.stringify(snap),initial,"pure editor guard never mutates readback");
});
test("G-MV35 native MOVE/REMOVE FX demands installed ChannelId hardware owner",()=>{
 const device={epoch:"31",sampleRate:48000,bufferFrames:256,
   enabledInputs:[0,1],enabledOutputs:[0,1]};
 const hardwareConfiguration=protocol.nativeHardwareConfiguration(device);
 const source="9007199254740993",target="9007199254740995";
 const snapshot={admitted:true,retained:false,device,raw:{
   mixerSession:{authority:"CHANNEL_ID",deviceEpoch:"31"}},graph:{
     revision:"45",rosterRevision:"18",
     capabilities:{callbackOwnsChannelIds:true,supportedOps:[
       "MOVE_INSERT","REMOVE_INSERT"]},
     channels:[{id:source,role:"INPUT",inserts:[
       {id:"one",catalogId:"vendor.fx",bypassed:false}]},
       {id:target,role:"GROUP",inserts:[]}]
 }};
 const opts={channelId:source,slot:0,instanceId:"one",hardwareConfiguration};
 assert.deepEqual(fxRackVM.nativeEditPlan(snapshot,opts),{
   kind:"READY",operation:"REMOVE_INSERT",channelId:source,
   payload:{slot:0,instanceId:"one"}});
 assert.deepEqual(fxRackVM.nativeEditPlan(snapshot,{
   ...opts,move:true,destinationId:target,position:0}),{
   kind:"READY",operation:"MOVE_INSERT",channelId:source,
   payload:{slot:0,instanceId:"one",toChannelId:target,toSlot:0}});
 for(const badHardware of [null,"",123]){
   assert.equal(fxRackVM.nativeEditPlan(snapshot,{
     ...opts,hardwareConfiguration:badHardware}).kind,"UNAVAILABLE");
 }
 for(const mutation of [
   s=>{s.admitted=false;},s=>{s.retained=true;},
   s=>{s.raw.mixerSession.authority="M4";},
   s=>{s.raw.mixerSession.deviceEpoch="32";},
   s=>{s.graph.capabilities.callbackOwnsChannelIds=false;},
   s=>{s.graph.rosterRevision="0";},
   s=>{s.graph.revision="0";},
   s=>{s.graph.channels[0].inserts[0].id="replacement";}
 ]) {
   const current=clone(snapshot);mutation(current);
   assert.equal(fxRackVM.nativeEditPlan(current,opts).kind,"UNAVAILABLE");
   assert.equal(fxRackVM.nativeEditPlan(current,{...opts,move:true,
     destinationId:target,position:0}).kind,"UNAVAILABLE");
 }
 const bridge=read("web/approved-native-bridge.js"),
       pure=read("web/approved-fx-rack-view-model.js");
 assert.equal((pure.match(/function m4EditorDispatchPlan\(/g)||[]).length,1);
 assert.equal((pure.match(/function m4EditorDispatchLeaseMatches\(/g)||[]).length,1);
 assert.match(bridge,/fxRackVM\.m4EditorDispatchPlan\(accepted,/);
 assert.match(bridge,/fxRackVM\.m4EditorDispatchLeaseMatches\(current,lease,/);
 assert.match(bridge,/window\.queueMicrotask\(\(\)=>\{/);
 assert.match(bridge,/hardwareConfiguration:protocol\.nativeHardwareConfiguration\(current\?\.device\)/);
 assert.match(bridge,/fxRackVM\.nativeEditPlan\(current,/);
 assert.doesNotMatch(pure,/backend\.emitEvent|document\.|window\.|setTimeout/);
});

test("G-MV36 ChannelId vendor FX editor dispatch retains exactly one physical instance lease",()=>{
 const channelId="9007199254740993",epoch="31",revision="45",rosterRevision="18";
 const device={epoch,sampleRate:48000,bufferFrames:256,
   enabledInputs:[0,1],enabledOutputs:[0,1]};
 const hardwareConfiguration=protocol.nativeHardwareConfiguration(device);
 const snap={admitted:true,retained:false,device,raw:{
   mixerSession:{authority:"CHANNEL_ID",deviceEpoch:epoch}},graph:{
     revision,rosterRevision,capabilities:{callbackOwnsChannelIds:true},
     channels:[{id:channelId,role:"INPUT",inserts:[
       {id:"vst3-instance",catalogId:"vendor.alpha",bypassed:false}]}]
 }};
 const original=JSON.stringify(snap);
 const navigation=fxRackVM.editorNavigationPlan(snap,{
   channelId,slot:0,hardwareConfiguration});
 assert.equal(navigation.kind,"OPEN");
 assert.equal(navigation.lease.authority,"CHANNEL_ID");
 const plan=(snapshot=snap,lease=navigation.lease,hw=hardwareConfiguration)=>
   fxRackVM.nativeEditorDispatchPlan(snapshot,lease,{hardwareConfiguration:hw});
 assert.deepEqual(plan(),{kind:"READY",action:"routingFxEditor",payload:{
   channelId,slot:0,instanceId:"vst3-instance",deviceEpoch:epoch,revision}});
 assert.equal(Object.isFrozen(navigation.lease),true);
 for(const bad of [null,{}, {...navigation.lease,authority:"LOGICAL"},
   {...navigation.lease,workspaceToken:"other"}, {...navigation.lease,slot:1}])
   assert.equal(plan(snap,bad).kind,"UNAVAILABLE");
 for(const mutate of [
   s=>{s.device.epoch="32";},s=>{s.device.enabledInputs=[2,3];},
   s=>{s.raw.mixerSession.authority="M4";},
   s=>{s.raw.mixerSession.deviceEpoch="32";},
   s=>{s.admitted=false;},s=>{s.retained=true;},
   s=>{s.graph.revision="46";},s=>{s.graph.rosterRevision="19";},
   s=>{s.graph.capabilities.callbackOwnsChannelIds=false;},
   s=>{s.graph.channels[0].inserts[0].id="replacement";},
   s=>{s.graph.channels[0].inserts[0].catalogId="vendor.beta";},
   s=>{s.graph.channels[0].inserts=[];}
 ]){
   const next=clone(snap);mutate(next);
   assert.equal(plan(next,navigation.lease,
     protocol.nativeHardwareConfiguration(next.device)).kind,"UNAVAILABLE");
 }
 assert.equal(plan(snap,navigation.lease,"wrong-device").kind,"UNAVAILABLE");
 assert.equal(fxRackVM.nativeEditorDispatchPlan(snap,navigation.lease,{
   hardwareConfiguration,offline:true}).kind,"UNAVAILABLE");
 assert.equal(JSON.stringify(snap),original,"pure editor plan never writes PCM");
});
test("G-MV36 correlated FX HWND ACK guards request, device, roster, and plugin instance",()=>{
 const channelId="9007199254740993",epoch="51",revision="46";
 const device={epoch,sampleRate:48000,bufferFrames:256,
   enabledInputs:[0,1],enabledOutputs:[0,1]};
 const hardwareConfiguration=protocol.nativeHardwareConfiguration(device);
 const native={admitted:true,retained:false,device,raw:{
   mixerSession:{authority:"CHANNEL_ID",deviceEpoch:epoch}},graph:{
     revision,rosterRevision:"19",capabilities:{callbackOwnsChannelIds:true},
     channels:[{id:channelId,role:"INPUT",inserts:[
       {id:"native-fx-8",catalogId:"vendor.vst3"}]}]
 }};
 const nav=fxRackVM.editorNavigationPlan(native,{
   channelId,slot:0,hardwareConfiguration});
 assert.equal(nav.kind,"OPEN");
 const pending={authority:"CHANNEL_ID",requestId:"fx-editor-7",lease:nav.lease};
 const verdict=(p=pending,ack=null,snapshot=native,hw=hardwareConfiguration)=>
   fxRackVM.nativeEditorAckResult(p,ack,snapshot,{
     hardwareConfiguration:hw,verifiedM4:true});
 assert.equal(verdict(null,{requestId:pending.requestId,
   instanceId:"native-fx-8",ok:true}),"IGNORE");
 assert.equal(verdict(),"IGNORE");
 assert.equal(verdict(pending,{requestId:"old",instanceId:"native-fx-8",
   ok:false}),"IGNORE");
 assert.equal(verdict(pending,{requestId:pending.requestId,
   instanceId:"wrong",ok:true}),"REJECTED");
 assert.equal(verdict(pending,{requestId:pending.requestId,
   instanceId:"native-fx-8",ok:false}),"REJECTED");
 assert.equal(verdict(pending,{requestId:pending.requestId,
   instanceId:"native-fx-8",ok:true}),"ACKED");
 assert.equal(verdict(pending,{instanceId:"native-fx-8",ok:true}),"IGNORE");
 assert.equal(fxRackVM.nativeEditorAckResult(pending,null,native,{
   hardwareConfiguration,offline:true}),"STALE");
 for(const mutate of [
   s=>{s.graph.rosterRevision="20";},
   s=>{s.graph.revision="47";},
   s=>{s.graph.channels[0].inserts[0].id="retired";},
   s=>{s.raw.mixerSession.authority="M4";},
   s=>{s.device.enabledOutputs=[2,3];}
 ]){
   const updated=clone(native);mutate(updated);
   const hw=protocol.nativeHardwareConfiguration(updated.device);
   assert.equal(verdict(pending,null,updated,hw),"STALE");
   assert.equal(verdict(pending,{requestId:pending.requestId,
     instanceId:"native-fx-8",ok:true},updated,hw),"STALE");
 }
 const m4Device={epoch:"55",sampleRate:48000,bufferFrames:256,
   enabledInputs:[0,1],enabledOutputs:[0,1]};
 const m4Hw=protocol.nativeHardwareConfiguration(m4Device);
 const m4={admitted:false,retained:false,device:m4Device,raw:{
   mixerSession:{authority:"M4",deviceEpoch:"55",routeRevision:"12",
     channels:Array.from({length:10},()=>({role:"INPUT"}))},
   legacyM4:{active:true,routeAccepted:true,nativeBusStructural:false,
     routingRevision:"12",fxInstances:Array.from({length:10},()=>[])}
 }};
 m4.raw.legacyM4.fxInstances[4][0]={
   instanceId:"m4-fx",catalogId:"vendor.m4",bypassed:false};
 const accepted=fxRackVM.m4EditorDispatchPlan(m4,{
   channel:4,slot:0,instanceId:"m4-fx",deviceEpoch:55,revision:12,
   verifiedM4:true,hardwareConfiguration:m4Hw});
 assert.equal(accepted.kind,"READY");
 const m4Pending={requestId:"fx-editor-9",authority:"M4",lease:accepted.lease};
 const m4Result=(p=m4Pending,ack=null,snapshot=m4,hw=m4Hw)=>
   fxRackVM.nativeEditorAckResult(p,ack,snapshot,{
     verifiedM4:true,hardwareConfiguration:hw});
 assert.equal(m4Result(),"IGNORE");
 assert.equal(m4Result(m4Pending,{requestId:"fx-editor-7",
   instanceId:"m4-fx",ok:true}),"IGNORE");
 assert.equal(m4Result(m4Pending,{requestId:"fx-editor-9",
   instanceId:"m4-fx",ok:true}),"ACKED");
 const replacement=clone(m4);
 replacement.raw.legacyM4.fxInstances[4][0].catalogId="other";
 assert.equal(m4Result(m4Pending,null,replacement),"STALE");
 assert.equal(m4Result(m4Pending,null,m4,"old-hardware"),"STALE");
 assert.equal(m4Result({...m4Pending,authority:"LOGICAL"}),"STALE");
});
test("G-MV36 one correlated native editor transport and JUCE echo, no duplicate toast",()=>{
 const bridge=read("web/approved-native-bridge.js"),
   main=read("src/Main.cpp"),viewmodel=read("web/approved-fx-rack-view-model.js");
 assert.match(main,/if \(action == "routingFxEditor"\) \{/);
 assert.match(main,/ack->setProperty\("requestId",args\.getProperty\("requestId",juce::var\(\)\)\);/);
 assert.match(main,/ack->setProperty\("instanceId",args\.getProperty\("instanceId",\{\}\)\);/);
 assert.match(bridge,/function dispatchPhysicalFxEditor\(plan,lease,authority,retried=false\)/);
 assert.match(bridge,/backend\.emitEvent\("arMixAction",\{action:plan\.action,requestId,\.\.\.plan\.payload\}\);/);
 assert.match(bridge,/dispatchPhysicalFxEditor\(plan,lease,"M4"\)/);
 assert.match(bridge,/dispatchPhysicalFxEditor\(plan,lease,"CHANNEL_ID"\)/);
 assert.match(bridge,/if\(logicalView\|\|fxRackVM\.nativeEditorDispatchPlan\(current,lease,/);
 assert.match(bridge,/pendingFxEditorAck=null; \/\/ expire old native HWND/);
 assert.match(bridge,/fxRackVM\.nativeEditorAckResult\(pendingFxEditorAck,ack,accepted,/);
 assert.match(bridge,/if\(verdict==="IGNORE"\)return; \/\/ obsolete, duplicate or unrelated ACK/);
 assert.match(bridge,/fxRackVM\.nativeEditorAckResult\(pendingFxEditorAck,null,accepted,/);
 assert.doesNotMatch(bridge,/if\(ack\?\.ok!==true\)\s*nativeToast\(ack\?\.error\|\|/);
 assert.doesNotMatch(viewmodel,/backend\.emitEvent|document\.|window\.|setTimeout/);
});

test("G-MV37 one physical ChannelId M/ST policy validates exact accepted input",()=>{
 const id="9007199254740993",hw="asio|48000|256|0,1|0,1";
 const graph={revision:"45",rosterRevision:"8",
   capabilities:{callbackOwnsChannelIds:true,
     supportedOps:["SET_PROCESSING_FORMAT"]},
   channels:[{id,role:"INPUT",processingFormat:"MONO"}]};
 const native={admitted:true,retained:false,logical:false,
   device:{epoch:"17"},raw:{mixerSession:{
     authority:"CHANNEL_ID",deviceEpoch:"17"}},graph};
 const get=(snapshot=native,args={})=>stripVM.processingFormatPlan(snapshot,{
   authority:"CHANNEL_ID",channelId:id,hardwareConfiguration:hw,...args});
 assert.deepEqual(get(),{kind:"READY",authority:"CHANNEL_ID",
   operation:"SET_PROCESSING_FORMAT",channelId:id,
   payload:{format:"STEREO"},target:"STEREO"});
 const stereo=clone(native);stereo.graph.channels[0].processingFormat="STEREO";
 assert.equal(get(stereo).target,"MONO");
 for(const h of ["",null,0])
   assert.equal(get(native,{hardwareConfiguration:h}).kind,"UNAVAILABLE");
 for(const mutation of [
   s=>{s.admitted=false;},s=>{s.retained=true;},s=>{s.logical=true;},
   s=>{s.raw.mixerSession.authority="M4";},
   s=>{s.raw.mixerSession.deviceEpoch="18";},
   s=>{s.device.epoch="0";},s=>{s.graph.revision="0";},
   s=>{s.graph.rosterRevision="0";},
   s=>{s.graph.capabilities.callbackOwnsChannelIds=false;},
   s=>{s.graph.capabilities.supportedOps=[];},
   s=>{s.graph.channels[0].role="GROUP";},
   s=>{s.graph.channels[0].processingFormat="UNKNOWN";},
   s=>{s.graph.channels.push({...s.graph.channels[0]});}
 ]){const snapshot=clone(native);mutation(snapshot);
   assert.equal(get(snapshot).kind,"UNAVAILABLE");}
 assert.equal(get(native,{pending:true}).kind,"UNAVAILABLE");
});
test("G-MV37 .smix M/ST stays offline and workspace-scoped",()=>{
 const id="9007199254740993";
 const saved={logical:true,admitted:false,retained:false,readOnly:false,
   workspaceToken:"workspace-A",graph:{
     capabilities:{supportedOps:["SET_PROCESSING_FORMAT"]},
     channels:[{id,role:"INPUT",processingFormat:"STEREO"}]}};
 const get=(snapshot=saved,args={})=>stripVM.processingFormatPlan(snapshot,{
   authority:"LOGICAL",channelId:id,...args});
 assert.deepEqual(get(),{kind:"READY",authority:"LOGICAL",
   operation:"SET_PROCESSING_FORMAT",channelId:id,
   payload:{format:"MONO"},target:"MONO"});
 for(const mutation of [
   s=>{s.logical=false;},s=>{s.admitted=true;},s=>{s.retained=true;},
   s=>{s.readOnly=true;},s=>{s.workspaceToken="";},
   s=>{s.graph.capabilities.supportedOps=[];},
   s=>{s.graph.channels[0].processingFormat=null;},
   s=>{s.graph.channels.push({...s.graph.channels[0]});}
 ]){const snapshot=clone(saved);mutation(snapshot);
   assert.equal(get(snapshot).kind,"UNAVAILABLE");}
 assert.equal(get(saved,{pending:true}).kind,"UNAVAILABLE");
});
test("G-MV37 M4 M/ST reuses exact existing routed channel patch",()=>{
 const sessionChannels=Array.from({length:10},(_,i)=>({
   role:"INPUT",ordinal:i,source:{main:false,channelFormat:"MONO"},
   primaryOut:"NONE"}));
 const legacyChannels=sessionChannels.map(()=>({main:false,channelFormat:"MONO"}));
 const snapshot={admitted:false,retained:false,device:{epoch:"21"},raw:{
   mixerSession:{authority:"M4",deviceEpoch:"21",routeRevision:"12",
     channels:sessionChannels},
   legacyM4:{active:true,routeAccepted:true,nativeBusStructural:false,
     routingRevision:"12",channels:legacyChannels}}};
 const opts={authority:"M4",channel:4,verifiedM4:true,deviceReady:true,
   hardwareConfiguration:"physical-lease"};
 const plan=stripVM.processingFormatPlan(snapshot,opts);
 assert.equal(plan.kind,"READY");
 assert.equal(plan.operation,"SET_PROCESSING_FORMAT");
 assert.equal(plan.action,"routingChannelPatch");
 assert.equal(plan.target,"STEREO");
 assert.equal(plan.payload.channel,4);
 for(const change of [
   {verifiedM4:false},{deviceReady:false},{pending:true},
   {sourcePending:true},{hardwareConfiguration:null},
   {hardwareConfiguration:""},{channel:10}
 ])assert.equal(stripVM.processingFormatPlan(snapshot,{...opts,...change}).kind,
    "UNAVAILABLE");
 const wrong=clone(snapshot);
 wrong.raw.mixerSession.channels[4].source.channelFormat="STEREO";
 assert.equal(stripVM.processingFormatPlan(wrong,opts).kind,"UNAVAILABLE");
 const bridge=read("web/approved-native-bridge.js");
 const section=bridge.slice(bridge.indexOf("function requestProcessingFormat(t)"),
   bridge.indexOf("function previewNativePan(t,el,event)"));
 assert.match(section,/stripVM\.processingFormatPlan\(snapshot,/);
 assert.match(section,/logicalIntent\(plan\.operation,plan\.channelId,plan\.payload\)/);
 assert.match(section,/controller\.intent\(plan\.operation,plan\.channelId,plan\.payload\)/);
 assert.match(section,/dispatchM4ChannelPatch\(t,plan\.operation,plan\.target,plan\.channel\)/);
 assert.doesNotMatch(section,/const target=.*processingFormat|channelFormat===/);
});

test("G-MV38 editor surface routing has one allowlisted browser owner; future FX remain native",()=>{
  const channelId="9007199254740993",epoch="53";
  const device={epoch,sampleRate:48000,bufferFrames:256,
    enabledInputs:[0,1],enabledOutputs:[0,1]};
  const hardwareConfiguration=protocol.nativeHardwareConfiguration(device);
  const view={admitted:true,retained:false,device,raw:{
    mixerSession:{authority:"CHANNEL_ID",deviceEpoch:epoch}},graph:{
    revision:"72",rosterRevision:"11",
    capabilities:{callbackOwnsChannelIds:true},
    channels:[{id:channelId,role:"INPUT",inserts:[
      {id:"native-fx-instance",catalogId:"vendor.vst3"}]}]}};
  const project=(catalogId,snapshot=view,hw=hardwareConfiguration)=>{
    const fx=snapshot.graph.channels[0].inserts[0];
    fx.catalogId=catalogId;
    const nav=fxRackVM.editorNavigationPlan(snapshot,{channelId,slot:0,
      hardwareConfiguration:hw});
    assert.equal(nav.kind,"OPEN");
    return {lease:nav.lease,result:fxRackVM.editorSurfacePlan(snapshot,nav.lease,
      {hardwareConfiguration:hw})};
  };
  const allowlisted=[
    ["armix.builtin.channel-eq.v1","ArSonMixNativeEq"],
    ["armix.builtin.graphic-eq31.v1","ArSonMixGraphicEq31"],
    ["armix.builtin.channel-compressor.v1","ArSonMixNativeCompressor"],
    ["armix.builtin.arson-harmony.v1","ArSonMixNativeHarmony"],
    ["armix.builtin.vocal-throw.v1","ArSonMixNativeVocalThrow"],
    ["armix.builtin.8d-sonku.v1","ArSonMixNativeSonKu"]
  ];
  for(const [id,windowKey] of allowlisted){
    const {result}=project(id);
    assert.deepEqual(result,{kind:"BROWSER",windowKey});
  }
  for(const id of ["vendor.vst3",
                   "armix.builtin.immersib.v1","armix.builtin.vocal-delay.v2","__proto__"]){
    assert.deepEqual(project(id).result,{kind:"NATIVE_HOST"},
      "non-allowlisted catalogs cannot choose an arbitrary browser host");
  }
  const {lease}=project("vendor.vst3");
  assert.deepEqual(fxRackVM.editorSurfacePlan(view,lease,{
    hardwareConfiguration:"wrong-hardware"}),{kind:"UNAVAILABLE"});
  const stale=JSON.parse(JSON.stringify(view));
  stale.graph.revision="73";
  assert.deepEqual(fxRackVM.editorSurfacePlan(stale,lease,{
    hardwareConfiguration}),{kind:"UNAVAILABLE"});
  const offline=JSON.parse(JSON.stringify(view));
  offline.logical=true;offline.admitted=false;offline.workspaceToken="saved-workspace";
  offline.raw.mixerSession.authority="LOGICAL";
  const logicalLease=fxRackVM.editorNavigationPlan(offline,{
    channelId,slot:0}).lease;
  assert.deepEqual(fxRackVM.editorSurfacePlan(offline,logicalLease),
    {kind:"LOGICAL_VENDOR"});
  offline.graph.channels[0].inserts[0].catalogId="armix.builtin.vocal-delay.v2";
  const delayLogical=fxRackVM.editorNavigationPlan(offline,{channelId,slot:0}).lease;
  assert.deepEqual(fxRackVM.editorSurfacePlan(offline,delayLogical),{kind:"LOGICAL_VENDOR"});
  assert.equal(fxRackVM.nativeEditorDispatchPlan(offline,delayLogical,{offline:true}).kind,"UNAVAILABLE");
  offline.graph.channels[0].inserts[0].catalogId="armix.builtin.arson-harmony.v1";
  const builtInLogical=fxRackVM.editorNavigationPlan(offline,{
    channelId,slot:0}).lease;
  assert.deepEqual(fxRackVM.editorSurfacePlan(offline,builtInLogical),
    {kind:"BROWSER",windowKey:"ArSonMixNativeHarmony"},
    "existing offline built-in editor remains available");
  assert.deepEqual(fxRackVM.editorSurfacePlan(null,lease),
    {kind:"UNAVAILABLE"});
  const bridge=read("web/approved-native-bridge.js");
  assert.match(bridge,/fxRackVM\.editorSurfacePlan\(current,editor,/);
  assert.match(bridge,/const host=window\[surface\.windowKey\]/);
  assert.match(bridge,/openChannelIdFxEditor\(editor\)/);
  assert.match(bridge,/action:"logicalVendorFxEditor"/);
  assert.doesNotMatch(bridge,/if\(editor\.catalogId==='armix\.builtin/);
});

test("UX-1 footer live metrics: exact driver hints, CPU App, last RT and process RAM",()=>{
  const input={
    footerHealthSequence:17,deviceGeneration:4,dryRoutingLab:{epoch:7},
    connected:true,directRouteAuthorized:true,adaptiveClockActive:false,
    rate:48000,periodUs:5000,driverReportedInputLatencyFrames:240,
    driverReportedOutputLatencyFrames:480,latencySamples:240,
    footerProcessCpuPercent:4.16,footerProcessWorkingSetMiB:318,
    callbackDurationUs:400,buffer:240
  };
  const projected=protocol.footerHealthFromLive(input);
  assert.equal(projected.latency,"LAT ~20.0 ms",
    "do not add the separate 240-frame buffer twice");
  assert.equal(projected.performance,"CPU App 4.2% · RT last 8% · RAM 318 MiB");
  assert.equal(projected.deviceGeneration,"4");
  assert.equal(projected.routingEpoch,"7");
  assert.equal(protocol.footerHealthFromLive({...input,dryRoutingLab:null}).routingEpoch,null);
  assert.equal(projected.sequence,17);
  assert.match(projected.provenance,/NOT_LOOPBACK/);
  assert.equal(protocol.footerHealthFromLive({...input,footerHealthSequence:undefined}),null);
  assert.equal(protocol.footerHealthFromLive({...input,rate:0}).latency,"LAT —");
  assert.equal(protocol.footerHealthFromLive({...input,driverReportedInputLatencyFrames:null}).latency,"LAT —");
  assert.equal(protocol.footerHealthFromLive({...input,adaptiveClockActive:true}).latency,"LAT —");
  assert.equal(protocol.footerHealthFromLive({...input,connected:false}).latency,"LAT —");
  assert.equal(protocol.footerHealthFromLive({...input,directRouteAuthorized:false}).latency,"LAT —");
  assert.equal(protocol.footerHealthFromLive({...input,footerProcessCpuPercent:null,
    footerProcessWorkingSetMiB:null,callbackDurationUs:null}).performance,
    "CPU App — · RT — · RAM —",
    "missing telemetry must NEVER be converted to a reassuring 0%");
  assert.equal(protocol.footerHealthFromLive({...input,footerProcessCpuPercent:101}).performance,
    "CPU App — · RT last 8% · RAM 318 MiB");
  assert.equal(protocol.footerHealthFromLive({...input,footerProcessCpuPercent:NaN}).performance,
    "CPU App — · RT last 8% · RAM 318 MiB");
});
test("UX-1 health strip uses only already-available 1 Hz host samples and existing status event",()=>{
  const host=read("src/Main.cpp");
  const bridge=read("web/approved-native-bridge.js");
  const page=read("site/preview/mixer.html");
  assert.match(host,/sampleFooterHostCpuPercent\(\)/);
  assert.match(host,/::GetProcessTimes/);
  assert.match(host,/timerTicks_ % 12 == 0[\s\S]*?footerHealthSequence/);
  assert.match(host,/evidence\.memory\.processWorkingSetMiB/);
  assert.match(host,/browser_\.emitEventIfBrowserIsVisible \("arMixStatus", state\)/);
  assert.match(bridge,/backend\.addEventListener\("arMixStatus", live=>/);
  assert.match(bridge,/protocol\.footerHealthFromLive\(live\)/);
  assert.match(bridge,/paintAudioHealthFooter\(hardware\)/);
  assert.match(bridge,/lastFooterHealth\.routingEpoch===String\(d\?\.epoch\)/);
  assert.match(bridge,/warningText==="OUTPUT ENABLED"/);
  assert.doesNotMatch(bridge,/setInterval\([^)]*footer|setTimeout\([^)]*footer/i);
  assert.match(page,/html\.armix-native-desktop \.status-bar \.warning\[data-telemetry="true"\]/);
});

test("UX-1 calm status colors use genuine app/process and callback budget evidence",()=>{
  const base={
    footerHealthSequence:9,deviceGeneration:9,
    connected:true,directRouteAuthorized:true,adaptiveClockActive:false,
    rate:48000,periodUs:5000,driverReportedInputLatencyFrames:48,
    driverReportedOutputLatencyFrames:96,latencySamples:0,
    footerProcessCpuPercent:4.0,footerProcessWorkingSetMiB:240,
    callbackDurationUs:200
  };
  const read=x=>protocol.footerHealthFromLive({...base,...x}).metrics;
  assert.deepEqual(Object.keys(read({})),["cpu","rt","ram"]);
  assert.equal(read({}).cpu.tone,"normal");
  assert.equal(read({}).rt.tone,"normal");
  assert.equal(read({}).ram.tone,"neutral");
  assert.equal(read({footerProcessCpuPercent:75}).cpu.tone,"attention");
  assert.equal(read({footerProcessCpuPercent:90}).cpu.tone,"critical");
  assert.equal(read({callbackDurationUs:4000}).rt.tone,"attention");
  assert.equal(read({callbackDurationUs:5000}).rt.tone,"critical");
  assert.equal(read({footerProcessCpuPercent:null}).cpu.tone,"unknown");
  assert.equal(read({callbackDurationUs:null}).rt.tone,"unknown");
  assert.equal(read({adaptiveClockActive:true}).rt.tone,"unknown");
  assert.equal(read({footerProcessWorkingSetMiB:null}).ram.tone,"unknown");
  assert.equal(read({footerProcessWorkingSetMiB:9000}).ram.tone,"neutral",
    "RAM MiB has no machine-specific limit; never color by arbitrary size");
  assert.equal(read({footerProcessCpuPercent:99}).cpu.label,"CPU App 99.0%");
  const bridge=require("node:fs").readFileSync("web/approved-native-bridge.js","utf8");
  const page=require("node:fs").readFileSync(
    "site/preview-v2/assets/mixer-section-controls.css","utf8");
  // The retired site/preview/mixer.html is not part of the packaged Pages or
  // native WebView2 GUI; asserting it produced a false-green UI status test.
  const canonical=require("node:fs").readFileSync(
    "site/preview-v2/index.html","utf8");
  assert.match(canonical,/assets\/mixer-section-controls\.css/);
  assert.match(bridge,/function paintFooterMetrics\(warning,health\)/);
  assert.doesNotMatch(bridge,/item\.setAttribute\("aria-hidden","true"\)/,
    "numeric status values stay available to assistive technology");
  assert.match(bridge,/warning\.replaceChildren\(nodes\.cpu,nodes\.rt,nodes\.ram\)/);
  assert.match(bridge,/label\.dataset\.telemetry!==latencyMode/);
  assert.match(bridge,/warning\.dataset\.telemetry!==healthMode/);
  assert.match(bridge,/warning\.firstElementChild!==footerMetricNodes\.cpu/);
  assert.doesNotMatch(bridge,/requestAnimationFrame\([^)]*paintFooterMetrics|setInterval\([^)]*paintFooterMetrics/);
  for(const key of ["cpu","rt","ram"])
    assert.match(page,new RegExp('data-metric="'+key+'"'));
  for(const tone of ["normal","attention","critical","unknown"])
    assert.match(page,new RegExp('data-tone="'+tone+'"'));
  const geometry=require("node:fs").readFileSync(
    "site/preview-v2/assets/mixer-chrome-material.css","utf8");
  assert.match(geometry,/\.status-bar\{height:20px!important;min-height:20px!important/);
});


test("P0 PRIMARY WIRE remains unity, never aliases channel fader",()=>{
  const primary={edgeId:"92",from:"8",to:"100",enabled:true,
    primaryOutput:true,gainLinear:1};
  const parallel={edgeId:"93",from:"8",to:"100",enabled:true,
    primaryOutput:false,gainLinear:0.5};
  const channel={id:"8",role:"INPUT",primaryOutId:"100",
    controls:{faderDb:-11.5}};
  const accepted={admitted:true,retained:false,graph:{
    capabilities:{supportedOps:["SET_FADER","SET_WIRE_GAIN"]},
    channels:[channel],edges:[primary,parallel]}};
  const plan=wireVM.primaryChannelLevelPlan(accepted,true,"8","92");
  assert.deepEqual(plan,{kind:"READY",source:"8",levelDb:-11.5});
  assert.deepEqual(wireVM.primaryChannelLevelPlan(accepted,true,"8","93"),
    {kind:"UNAVAILABLE"},"parallel send cannot become a channel fader");
  assert.deepEqual(wireVM.primaryChannelLevelPlan(accepted,false,"8","92"),
    {kind:"UNAVAILABLE"},"unprojected graph cannot authorize PRIMARY control");
  assert.deepEqual(wireVM.primaryChannelLevelPlan({...accepted,retained:true},
    true,"8","92"),{kind:"UNAVAILABLE"});
  assert.deepEqual(wireVM.primaryChannelLevelPlan({
    ...accepted,graph:{...accepted.graph,
      edges:[{...primary,gainLinear:.5},parallel]}
    },true,"8","92"),{kind:"UNAVAILABLE"},"native PRIMARY must stay unity");
  const readback=wireVM.gainReadback(accepted.graph.edges,
    new Map([["92",8],["93",9]]),new Map(),accepted.graph.channels);
  assert.deepEqual(readback,[{id:8,pending:false,levelDb:0},
    {id:9,pending:false,levelDb:-6}],
    "PRIMARY stays unity, SEND has independent gain");
  const bridge=read("web/approved-native-bridge.js");
  assert.doesNotMatch(bridge,/primaryChannelLevelPlan\(current,/);
  assert.doesNotMatch(bridge,/controller.intent\("SET_FADER",primary.source,/);
  assert.match(bridge,/PRIMARY OUT · fixed unity/);
});
test("P0 native player INPUT advertised before pin admission; F11 window command survives VM boot",()=>{
  const bridge=read("web/approved-native-bridge.js");
  assert.match(bridge,/return physical.some\(p=>p\?\.mode==="PLAYER"\)\?physical:/);
  assert.match(bridge,/if\(choice\?\.mode==="PLAYER"&&!nativeOwner\)/);
  assert.match(bridge,/window.addEventListener\("keydown"/);
  assert.match(bridge,/backend.emitEvent\("arMixWindowFullscreen",\{operation:"TOGGLE"\}\)/);
  const popup=read("web/approved-floating-fx-bridge.js");
  assert.match(popup,/gate==="rack"/);
  assert.match(popup,/gate==="global"/);
  assert.doesNotMatch(popup,/crypto.randomUUID\(/);
  const geq=read("site/preview-v2/assets/mixer-native-geq31.js");
  assert.match(geq,/GLOBAL FX BYPASSED/);
  assert.match(geq,/ENABLE FX RACK/);
  const host=read("src/Main.cpp");
  assert.match(host,/event=="arMixGlobalFxAck"/);
  assert.match(host,/if\(gate=="rack"\|\|gate=="global"\)/);
});


test("P0 WIRE LVL uses independent incoming sum scalar regardless of selected wire",()=>{
  const receiver={id:"8",role:"INPUT",controls:{faderDb:-8.1,wireSumGainDb:0},primaryOutId:"100"};
  const ch=[receiver,{id:"3",role:"INPUT",controls:{faderDb:-4}},
    {id:"7",role:"INPUT",controls:{faderDb:-6}}];
  const e1={edgeId:"301",from:"3",to:"8",enabled:true,primaryOutput:false};
  const e2={edgeId:"302",from:"7",to:"8",enabled:true,primaryOutput:false};
  const graph={capabilities:{supportedOps:["SET_FADER","SET_WIRE_GAIN","SET_WIRE_SUM_GAIN"]},
    channels:ch,edges:[e1,e2]};
  const snapshot={admitted:true,retained:false,graph};
  assert.deepEqual(wireVM.channelSumLevelPlan(snapshot,true,"8",-12),
    {kind:"READY",source:"8",levelDb:-12,wireCount:2});
  assert.deepEqual(wireVM.channelSumLevelPlan({
    ...snapshot,graph:{...graph,edges:[e2]}},true,"8",-12),
    {kind:"READY",source:"8",levelDb:-12,wireCount:1});
  for(const bad of [
    {...snapshot,retained:true},{...snapshot,admitted:false},
    {...snapshot,graph:{...graph,edges:[]}}
  ])assert.equal(wireVM.channelSumLevelPlan(bad,true,"8",-12).kind,"UNAVAILABLE");
  assert.equal(wireVM.channelSumLevelPlan(snapshot,true,"7",3).kind,"UNAVAILABLE");
  const bridge=read("web/approved-native-bridge.js");
  assert.match(bridge,/field==="wireChannelLevel"/);
  assert.match(bridge,/requestLiveWireSumGain\(t,db,event.type==="change"\)/);
  const assembled=read("scripts/build_approved_native_gui.py");
  assert.match(assembled,/if\(!isMain&&connected\)/);
  assert.match(assembled,/data-field=\\"wireChannelLevel\\"/);
  assert.match(assembled,/incoming WIRE sum level/);
});

test("P0 input GAIN sends bounded latest native intents during pointermove",()=>{
  const bridge=read("web/approved-native-bridge.js");
  assert.match(bridge,/latestInputGainDrafts=new Map\(\)/);
  assert.match(bridge,/requestLiveInputGain\(t,n,false\)/);
  assert.match(bridge,/requestLiveInputGain\(t,n,true\)/);
  assert.match(bridge,/controller\.state\(\)\?\.pending\|\|pendingLogical/);
  assert.match(bridge,/latestInputGainDrafts\.clear\(\)/);
  assert.match(bridge,/__ARMIX_NATIVE_GAIN_CANCEL__/);
  const host=read("src/Main.cpp");
  assert.match(host,/setKioskModeComponent\(this,false\)/);
  assert.match(host,/setKioskModeComponent\(nullptr,false\)/);
});


test("P0 Audio I/O help reacts to explicit failed operator intent without auto-ARM",()=>{
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/function recommendAudioRepair\(context\)/);
 assert.match(bridge,/if\(!deviceReady\(\)\)recommendAudioRepair\("INPUT unavailable"\)/);
 const editor=bridge.slice(bridge.indexOf('if(action==="openPlugin"){'),bridge.indexOf('if(action==="globalFxOn"){'));
 assert.match(editor,/status\("FX editor waiting for the native mixer workspace"\)/);
 assert.doesNotMatch(editor,/recommendAudioRepair|openNativeAudioIO/);
 assert.match(editor,/requestRetainedFxAction\(t,slot,"openPlugin",\{recenter\}\)/);
 assert.match(bridge,/requestRetainedFxAction\(t,Number\(btn\?\.dataset\.slot\),"slotPower"\)/);
 assert.match(bridge,/requestRetainedFxAction\(t,0,"fxOn"\)/);
 assert.match(bridge,/window\.setTimeout\(\(\)=>\{/);
 assert.match(bridge,/openNativeAudioIO\(\); \/\/ explicit repair prompt/);
 assert.match(bridge,/if\(now-lastAudioRepairAt<1600\)return true/);
 assert.match(bridge,/data-audio-tab="routing"/);
 assert.match(bridge,/selectAudioSetupPage\(tab\.dataset\.audioTab\)/);
 assert.match(bridge,/if\(!tab\|\|!dialog\|\|deviceBusy\(\)\|\|tab\.disabled\|\|loopbackPending\)return/);
 assert.match(bridge,/deviceVM\.setupRoutingReady\(deviceInventory,deviceChoice,deviceState\)/);
 assert.match(bridge,/Windows Audio uses the selected stereo playback endpoint/);
 assert.doesNotMatch(bridge,/recommendAudioRepair\([^)]*\)\s*;\s*backend\.emitEvent\("arMixAction",\{action:"monitor"/);
 const css=read("site/preview-v2/assets/mixer-contextual-buttons.css");
 assert.match(css,/#wireNavigatorToggle,#wireMatrixToggle/);
 assert.match(css,/#mixContext/);
 assert.match(css,/inset:-3px;pointer-events:auto/);
});


test("P0 real-time gain pointer cancellation compensates native PCM through one owner",()=>{
 const page=read("site/preview-v2/index.html");
 assert.match(page,/__ARMIX_NATIVE_GAIN_CANCEL__\?\.\(g\.input,g\.start\)/);
 assert.match(page,/__ARMIX_NATIVE_GAIN_RESTORE__\?\.\(\)/);
 assert.match(page,/wireChannelLevel','trim','sendLevel'/);
});


test("P0 Audio I/O repair never steals retained offline FX editor recovery",()=>{
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/if\(field==="input"&&event.type==="change"\)\s*recommendAudioRepair\("INPUT audio device disconnected"\)/);
 assert.match(bridge,/editorView\?\.admitted!==true&&\s*editorView\?\.retained!==true\)\{/);
 assert.match(bridge,/retainedFxOfflineOpen/);
 assert.match(bridge,/pendingRetainedEditor=\{/);
});


test("P0 receiver SUM WIRE disables on retained epoch, then reenables on accepted PCM",()=>{
 const vm=read("web/approved-native-bridge.js");
 const asm=read("scripts/build_approved_native_gui.py");
 assert.match(vm,/canWireChannelLevel\(uiChannelId\)/);
 assert.match(vm,/function syncReceiverSumLevelAdmission\(next\)/);
 assert.match(vm,/const ready=Number\.isSafeInteger\(ui\)&&plan\.kind==="READY"/);
 assert.match(vm,/if\(input\.disabled===ready\)input\.disabled=!ready/);
 assert.match(asm,/wireEditable=window\.__ARMIX_NATIVE_M2__\?\.canWireChannelLevel\?\.\(t\.id\)===true/);
 assert.match(asm,/wireEditable\?'':' disabled'/);
});


test("P0 native receiver LVL re-admits only after projected device recovery",()=>{
 const bridge=read("web/approved-native-bridge.js");
 const cold=read("tests/approved_native_cold_start_smoke.py");
 assert.match(bridge,/function syncReceiverSumLevelAdmission\(next\)/);
 assert.match(bridge,/flags\.productionCallbackLive=next\.productLive;flags\.acceptedGraphConnected=true;\s*syncReceiverSumLevelAdmission\(next\)/);
 assert.match(bridge,/if\s*\(transientPlan\.discardRetained\)/);
 assert.match(bridge,/syncReceiverSumLevelAdmission\(next\);\s*stopDemo\(\)/);
 assert.match(bridge,/channelSumLevelPlan\(next,flags\.acceptedGraphConnected,/);
 assert.match(cold,/['"]CREATE_CHANNEL['"], ['"]SET_FADER['"], ['"]SET_WIRE_SUM_GAIN['"]/);
 assert.match(cold,/\.wire-gain'\)\.is_disabled\(\)/);
 assert.match(cold,/\.wire-gain'\)\.is_enabled\(\)/);
});

test("P0 routing commands remain usable in More at all desktop widths",()=>{
 const base=read("site/preview-v2/assets/mixer-chrome-material.css");
 const ux=read("site/preview-v2/assets/mixer-contextual-buttons.css");
 const page=read("site/preview-v2/index.html");
 // Immutable hardware baseline is preserved; the UX owner controls the move.
 assert.match(base,/\.more-tools-panel \.wire-overflow\{display:none!important\}/);
 assert.match(ux,/\.app \.more-tools-panel \.wire-overflow\{display:block!important\}/);
 assert.match(ux,/#wireNavigatorToggle,#wireMatrixToggle/);
 assert.match(page,/data-wire-open="routes"/);
 assert.match(page,/data-wire-open="matrix"/);
 assert.match(page,/data-wire-open="config"/);
});


test("P0 offline picker is presentation-only until selection and Apply restores edited graph before MAIN",()=>{
 const bridge=read("web/approved-native-bridge.js"),native=read("src/Main.cpp"),engine=read("src/LiveAudioEngine.h");
 assert.match(bridge,/if\(empty\)\{\s*scope\?\.openFastPicker\?\.\(t,slot,btn\);\s*return true;/);
 assert.match(bridge,/requestRetainedFxAction\(track,slot,"insertFx",\{selectedCatalogId:id\}\)/);
 const handoff=bridge.slice(bridge.indexOf("function requestRetainedFxAction("),bridge.indexOf("function logicalIntent("));
 assert.match(handoff,/current\?\.retained!==true/);
 assert.match(handoff,/if\(pendingRetainedEditor\)\{/);
 assert.match(handoff,/deviceEpoch:current\.device\.epoch,graphRevision:current\.graph\.revision/);
 assert.match(handoff,/selectedCatalogId:options\.selectedCatalogId/);
 assert.doesNotMatch(handoff,/arMixLogicalIntent|arMixConsoleIntent|openNativeAudioIO/);
 assert.match(bridge,/nativeHooks\.insertPlugin\(held\.selectedCatalogId/);
 assert.match(native,/if\(pendingRetainedFxCapture_ \|\| retainedOfflineFxActive_\)return; \/\/ never arm stale retained PCM/);
 assert.match(native,/engine_\.finishManualAudioSetup\(false,true\)/);
 assert.match(native,/OFFLINE_MIXER_REBIND_ACCEPTED/);
 assert.match(engine,/if\(deferRetainedRebind\)return true/);
});


test("P2 offline GAIN coalesces released values until durable ACK and exact readback",()=>{
 const vm=require("node:vm"),sent=[],listeners=new Map(),microtasks=[];
 let nextId=100,nextEdgeId=200;
 const scope={tracks:[],master:{id:1,name:"Master"},colors:["#568ead"],
   wireGraph:{validateCandidateGraph:()=>({ok:true})},
   make:(name,kind,color)=>({id:nextId++,name,kind,color}),
   render(){},findTrack(){return this.tracks[0];},displayMeterDbfs(){},
   allocateWireId:()=>nextEdgeId++,installGraph(tracks,master,edges){
     this.tracks=tracks;this.master=master;this.edges=edges;
   },stopDemo(){}};
 const backend={emitEvent:(name,payload)=>sent.push({name,payload}),
   addEventListener:(name,fn)=>listeners.set(name,fn)};
 const doc={readyState:"complete",documentElement:{dataset:{}},
   getElementById:()=>null,querySelector:()=>null,addEventListener(){}};
 const win={__JUCE__:{backend},ArSonApprovedNativeContract:protocol,
   ArSonApprovedDeviceViewModel:deviceVM,ArSonApprovedChannelStripViewModel:stripVM,
   ArSonApprovedFxRackViewModel:fxRackVM,ArSonApprovedWireViewModel:wireVM,
   __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},setTimeout(){return 0;},
   clearTimeout(){},queueMicrotask(fn){microtasks.push(fn);}};
 vm.runInNewContext(read("web/approved-native-bridge.js"),{window:win,document:doc,
   performance:{now:()=>0},console},{timeout:1500});
 const graph=acceptedFixture().graph;
 graph.capabilities.callbackOwnsChannelIds=false;
 graph.capabilities.supportedOps=["SET_INPUT_GAIN","SET_FADER"];
 graph.channels[0].controls.inputGainDb=0;
 graph.edges[0].primaryOutput=true;
 const snapshot=(revision,db,workspaceToken="offline-gain")=>{
   const s=clone(fixtures.g0),g=clone(graph);
   g.revision=revision;g.rosterRevision=revision;g.channels[0].controls.inputGainDb=db;
   s.mixerSession={authority:"NONE",deviceEpoch:s.device.epoch};
   s.logicalMixer={schema:"arsonmix.native.logical-mixer.v1",authority:"LOGICAL_ONLY",
     workspaceToken,readOnly:false,revision,physicalOutputAdmitted:false,outputArmed:false,graph:g};
   return s;
 };
 const emit=(name,payload)=>{listeners.get(name)(payload);while(microtasks.length)microtasks.shift()();};
 const commands=()=>sent.filter(x=>x.name==="arMixLogicalIntent").map(x=>x.payload);
 const gain=(db,type)=>win.__ARMIX_NATIVE_M2__.consumeField({type,target:{
   dataset:{field:"trim"},value:String(db),closest:()=>null}});
 const ack=(cmd,revision)=>emit("arMixLogicalAck",{requestId:cmd.requestId,
   status:"ACCEPTED",errorCode:"NONE",revision});
 emit("arMixConsoleSnapshot",snapshot("10",0));
 assert.equal(scope.tracks[0].nativeGainAvailable,true);
 for(const value of [1,2,3])gain(value,"input");
 assert.equal(commands().length,0,"no disk writes while offline dragging");
 gain(3,"change");
 const first=commands()[0];assert.equal(first.payload.valueDb,3);
 for(const value of [4,5,6])gain(value,"input");
 gain(6,"change");gain(9,"change");
 assert.equal(commands().length,1);
 ack(first,"11");
 assert.equal(commands().length,1,"ACK without independent readback cannot drain");
 emit("arMixConsoleSnapshot",snapshot("11",3));
 assert.equal(commands().length,2);
 const last=commands()[1];
 assert.equal(last.baseRevision,"11");assert.equal(last.payload.valueDb,9);
 assert.equal(scope.tracks[0].nativeGainAvailable,true,"offline readback keeps knob usable");
 assert.equal(scope.tracks[0].trim,3,"pending target never commits model optimistically");
 // Cancel while a prior released edit is pending: restore the pointer-down value.
 win.__ARMIX_NATIVE_GAIN_CANCEL__({},3);
 ack(last,"12");emit("arMixConsoleSnapshot",snapshot("12",9));
 assert.equal(commands().length,3);const cancel=commands()[2];
 assert.equal(cancel.payload.valueDb,3);assert.equal(cancel.baseRevision,"12");
 ack(cancel,"13");emit("arMixConsoleSnapshot",snapshot("13",3));
 assert.equal(scope.tracks[0].trim,3);
 gain(7,"change");const old=commands().at(-1);gain(10,"change");
 emit("arMixConsoleSnapshot",snapshot("1",0,"another-workspace"));
 ack(old,"14");
 assert.equal(commands().length,4,"queued GAIN must never follow replacement workspace");
 assert.equal(scope.tracks[0].trim,0);
});

test("P0 automatic MAIN recovery: no header mute toggle, explicit emergency stop is separate",()=>{
 const bridge=read("web/approved-native-bridge.js"),native=read("src/Main.cpp");
 assert.doesNotMatch(bridge,/dataset\.nativeMonitor|data-native-monitor|toolbar\.insertBefore\(monitor/);
 assert.match(bridge,/menu\.append\(caption,setup,muteBtn,\.\.\.newSessionButtons,archiveButton/);
 assert.match(bridge,/audioDiagnosticTools\.append\(hardwareNote,inspect,tone,mark,report,advanced\)/);
 assert.match(bridge,/querySelector\("\.armix-audio-diagnostics"\)\.append\(audioDiagnosticTools\)/);
 assert.doesNotMatch(bridge,/menu\.append\([^;]*(?:hardwareNote|healthTitle|tone,mark,report|advanced)/);
 assert.match(bridge,/Resume after emergency stop/);
 assert.match(bridge,/EMERGENCY STOP · MORE → RESUME/);
 const start=native.indexOf("void serviceAutomaticMain()"),end=native.indexOf("void requestAutomaticMain",start);
 const control=native.slice(start,end);
 assert.match(control,/if\(operatorOutputMuted_\)\{autoMainEpoch_=0;return;\}/);
 assert.match(control,/deviceApply_\.busy\(\)/);
 assert.match(control,/directCallbackStalled_/);
 assert.match(control,/if\(autoMainEpoch_==0\)return/);
 assert.doesNotMatch(control,/autoMainEpoch_=engine_\.dryRoutingLabEpoch\(\)/);
 assert.match(control,/!engine_\.directRouteAuthorized\(\)/);
 assert.match(control,/!engine_\.hasBoundMixerRoute\(\)/);
 assert.match(control,/engine_\.hasRetainedUnboundMixer\(\)/);
 assert.match(control,/engine_\.liveOutputArmFault\(requireInput\)/);
 assert.match(native,/if\(error\.isEmpty\(\)&&!sameOutputs\)\s*engine\.setMonitorEnabled\(false\)/);
 const audio=read("src/LiveAudioEngine.h");
 const begin=audio.indexOf("if(wanted==Route::mainOutputTone)"),finish=audio.indexOf("if(currentRoute==Route::mainOutputTone)",begin);
 assert.ok(begin>=0&&finish>begin);
 const tone=audio.slice(begin,finish);
 assert.match(tone,/setOutputMuted\(false\)/);
 assert.doesNotMatch(tone,/setMonitorEnabled\(true\)/);
 assert.doesNotMatch(control,/setMasterMuted\(false\)|openSelectedAudioDevice|setAudioDeviceSetup/);
});

// P0: navigation cannot be mistaken for a parameter ACK; overlay is accepted-state only.
test("P0 editor refresh and bypass projection preserve exact owner and gate hierarchy",()=>{
 const id="9007199254740993",device={epoch:"51",sampleRate:48000,bufferFrames:256,
  enabledInputs:[0,1],enabledOutputs:[0,1]};
 const hardwareConfiguration=protocol.nativeHardwareConfiguration(device);
 const view={admitted:true,retained:false,device,raw:{globalFxBypassed:false,
  mixerSession:{authority:"CHANNEL_ID",deviceEpoch:"51"}},graph:{
   revision:"46",rosterRevision:"19",capabilities:{callbackOwnsChannelIds:true},
   channels:[{id,role:"INPUT",fxRackBypassed:false,inserts:[
    {id:"fx-one",catalogId:"armix.builtin.8d-sonku.v1",bypassed:false}]}]}};
 const options={hardwareConfiguration};
 const lease=fxRackVM.editorNavigationPlan(view,{channelId:id,slot:0,...options}).lease;
 const state=()=>fxRackVM.editorBypassPresentation(view,lease,options);
 assert.equal(state(),"ON");
 view.graph.revision="47";
 assert.equal(fxRackVM.refreshEditorNavigationLease(view,lease,options).graphRevision,"47");
 view.graph.channels[0].inserts[0].bypassed=true;assert.equal(state(),"SLOT");
 view.graph.channels[0].fxRackBypassed=true;assert.equal(state(),"RACK");
 view.raw.globalFxBypassed=true;assert.equal(state(),"GLOBAL");
 view.raw.globalFxBypassed=false;assert.equal(state(),"RACK");
 view.graph.channels[0].fxRackBypassed=false;assert.equal(state(),"SLOT");
 view.graph.channels[0].inserts[0].id="replacement";assert.equal(state(),"UNAVAILABLE");
 assert.equal(fxRackVM.refreshEditorNavigationLease(view,lease,options),null);
 assert.equal(protocol.operatorMessage("VST3 editor · refresh native device and FX instance"),
  "FX editor unavailable · Reopen from FX Rack");
 assert.equal(protocol.operatorMessage("Native FX editor opened"),"FX editor opened");
 assert.notEqual(protocol.operatorMessage("Native FX editor pending"),"Effect settings updated");
});

test("P0 bounded editor retry stays on same owner and native failure class",()=>{
 const b=read("web/approved-native-bridge.js"),m=read("src/Main.cpp");
 assert.match(b,/pending\.retried\|\|pending\.retryScheduled/);
 assert.match(b,/if\(pendingFxEditorAck!==pending\)return/);
 assert.match(b,/ack\?\.retryable===true&&retryPhysicalFxEditor\(pending\)/);
 assert.match(b,/fresh,"CHANNEL_ID",true\);/);
 assert.match(m,/retryable=false; \/\/ plugin\/window failures/);
 assert.match(m,/ack->setProperty\("retryable",!opened&&retryable\)/);
});

test("P0 native busy guard and all physical presentations produce explicit receipts",()=>{
 const m=read("src/Main.cpp"),w=read("src/NativeOwnedFloatingWindow.h");
 assert.match(m,/const bool outputIntent=action=="monitor"&&args\.getProperty\("value",\{\}\)\.isBool\(\)/);
 assert.match(m,/if\(deviceApply_\.busy\(\) && action!="audioDeviceApply" && !outputIntent\)\{/);
 const monitor=m.split('else if (action == "monitor" && value.isBool())')[1].split('else if (action == "dryTest"')[0];
 assert.match(monitor,/if \(arm && deviceApply_\.busy\(\)\)/);
 assert.match(monitor,/operatorOutputMuted_=!arm/);
 assert.match(monitor,/arMixAudioArmAck/);
 assert.match(m,/emitRoutingFxEditorReceipt\(args,false,true,"FX editor unavailable: Audio I\/O is applying"\)/);
 assert.match(m,/opened=shownWindow->presentationConfirmed\(\)/);
 assert.match(m,/opened=\(\*extra\)->window->presentationConfirmed\(\)/);
 assert.match(m,/opened=createdWindow&&createdWindow->presentationConfirmed\(\)/);
 assert.match(w,/win32::ensureOwnedPopupVisible/);
 assert.equal(protocol.operatorMessage("This VST3 has no native editor"),
  "FX editor unavailable · Reopen from FX Rack");
});

// DMX native source/target roles: no terminal sends, no guessed IDs.
test("DMX SOF selects exact independent FX return to monitor edge",()=>{
 const fx="9007199254740993",mon="9007199254740994",main="18446744073709551615";
 const edge={edgeId:"9223372036854775809",from:fx,to:mon,enabled:true,
   primaryOutput:false,tap:"PRE_FADER",sendEnabled:true,gainLinear:.125};
 const graph={channels:[{id:fx,role:"FX_RETURN"},{id:mon,role:"MONITOR"},
   {id:main,role:"MAIN"}],mainChannelId:main,edges:[edge],
   capabilities:{supportedOps:["SET_WIRE_GAIN","SET_SEND_ENABLED","APPLY_WIRE_TRANSACTION"]}};
 const snap={admitted:true,graph};
 const find=(sourceId,destinationId,state=snap)=>wireVM.sofSendEdge(state,{sourceId,destinationId});
 assert.equal(find(fx,mon),edge);
 assert.equal(find(fx,mon,{...snap,retained:true}),null);
 assert.equal(find(fx,mon,{...snap,graph:{...graph,edges:[edge,edge]}}),null);
 assert.equal(find(fx,mon,{...snap,graph:{...graph,edges:[{...edge,primaryOutput:true}]}}),null);
 assert.equal(find(fx,main),null);
 assert.equal(find(mon,fx,{...snap,graph:{...graph,edges:[{...edge,from:mon,to:fx}]}}),null);
 assert.equal(find(fx,fx),null);
 assert.equal(wireVM.sofSourceRole("MONITOR"),false);
 assert.equal(wireVM.sofTargetRole("MONITOR"),true);
 assert.equal(wireVM.sofTargetRole("MAIN"),false);
});

test("DMX conversion ACK needs a new actual wet return and preserved source identity",()=>{
 const setup=()=>{
  const base=acceptedFixture(),source=base.graph.channels.find(c=>c.role==="INPUT");
  const main=base.graph.channels.find(c=>c.role==="MAIN");
  base.graph.mainChannelId=main.id;source.primaryOutId=main.id;
  source.inserts=[{id:"fx-existing",catalogId:"test.eq",name:"EQ"},
    {id:"fx-reverb",catalogId:"armix.builtin.vocal-reverb.v1",name:"Reverb-Sis"}];
  base.graph.capabilities.supportedOps.push("CONVERT_REVERB_RETURN");
  const c=protocol.createController({emit(){}});
  assert.ok(c.onSnapshot(base));return {base,source,main,c};
 };
 const run=(corrupt)=>{
  const {base,source,main,c}=setup();
  assert.equal(c.intent("CONVERT_REVERB_RETURN",source.id,{slot:0,instanceId:"fx-existing"}).ok,false);
  assert.equal(c.intent("CONVERT_REVERB_RETURN",source.id,{slot:1,instanceId:"stale"}).ok,false);
  const r=c.intent("CONVERT_REVERB_RETURN",source.id,{slot:1,instanceId:"fx-reverb"});
  assert.ok(r.ok);
  c.onAck({requestId:r.requestId,status:"ACCEPTED",errorCode:"NONE",
    deviceEpoch:base.device.epoch,graphRevision:"6",rosterRevision:"8"});
  assert.equal(c.state().current.graph.channels.find(x=>x.id===source.id).inserts.length,2);
  const next=clone(base);
  next.graph.revision="6";next.graph.rosterRevision="8";
  next.graph.channels.find(x=>x.id===source.id).inserts.pop();
  const wet=clone(source);wet.id="9007199254740999";wet.role="FX_RETURN";
  wet.name="Reverb-Sis Return";wet.inserts=[{id:"fx-wet",catalogId:"armix.builtin.vocal-reverb-return.v1",name:"Reverb-Sis Return"}];
  next.graph.channels.push(wet);
  next.graph.edges.push({edgeId:"9223372036854775991",from:source.id,to:wet.id,
    signalKind:"AUDIO",enabled:true,primaryOutput:false,gainLinear:1,tap:"POST_FADER"});
  if(corrupt)corrupt(next,wet);
  return {ok:c.onSnapshot(next),c,wet};
 };
 assert.ok(run(null).ok);
 assert.equal(run((n,w)=>{w.inserts[0].catalogId="armix.builtin.vocal-reverb.v1";}).ok,false);
 assert.equal(run(n=>{n.graph.edges.pop();}).ok,false);
});


test("RTR closes affected inline FX editors before rack mutation",()=>{
 const b=read("web/approved-native-bridge.js");
 const edit=b.slice(b.indexOf("function editNativeFx("),b.indexOf("function requestM4FxPower("));
 assert.ok(edit.indexOf('decision.kind!=="READY"')<edit.indexOf("inlineFxClip?.closeRackEditors("));
 assert.ok(edit.indexOf("inlineFxClip?.closeRackEditors(")<edit.indexOf("controller.intent("));
 assert.match(b,/editorOwners\.set\(root,\{key,lease\}\)/);
 assert.match(b,/affected\.has\(String\(owner\.lease\.channelId\)\)/);
 assert.match(b,/bypassOwners\.delete\(root\)/);
});


test("RTR live New Project requires verified archive before native replacement",()=>{
 const m=read("src/Main.cpp"),w=read("src/NativeMixerSessionSaveWorker.h"),b=read("web/approved-native-bridge.js");
 const reset=m.slice(m.indexOf("void createLiveSession("),m.indexOf("void createOfflineSession("));
 assert.match(reset,/liveNewSessionLeaseMatches\(args\)/);
 assert.ok(reset.indexOf("mixerSessionWorker_.save(")<reset.indexOf("restoreNativeMixerSession("));
 assert.match(reset,/Codec::encode\(current\)!=expected/);
 assert.match(reset,/setMonitorEnabled\(false\)/);
 assert.doesNotMatch(reset,/setMonitorEnabled\(true\)/);
 assert.match(w,/verifyExportArchive_/);
 assert.match(w,/Session archive readback mismatch/);
 assert.match(b,/action:"nativeMixerNewLive",confirmed:true/);
});


test("M3 MONITOR timing requires admitted physical pairs, disarm and observed revision",()=>{
  const raw=gmv20AcceptedGraph();
  raw.device.backend="ASIO";raw.device.outputArmed=false;
  raw.device.enabledOutputs=[0,1,4,5];
  raw.device.outputEndpoints=[0,1,4,5].map((port,callback)=>({port,callback,id:"out:"+port,label:"Out "+port,active:true}));
  raw.physicalMainOutputs=[0,1];raw.physicalMonitorOutputs=[4,5];
  raw.physicalBusOutputObserved=true;raw.operatorOutputMuted=true;raw.physicalBusSecondaryArmed=false;
  raw.monitorTiming={policy:"GLOBAL_ALIGNED",mainSamples:1456,monitorSamples:1456,potentialSaving:1152};
  const read=()=>protocol.monitorTimingView(protocol.normalizeSnapshot(raw));
  assert.equal(read()?.editable,true);
  raw.physicalBusSecondaryArmed=true;assert.equal(read()?.editable,false);
  raw.physicalBusSecondaryArmed=false;raw.physicalBusOutputObserved=false;assert.equal(read()?.editable,false);
  raw.physicalBusOutputObserved=true;
  raw.monitorTiming.policy="INDEPENDENT_MONITOR";raw.monitorTiming.monitorSamples=304;
  assert.equal(read()?.monitorSamples,304);
  raw.monitorTiming.monitorSamples=305;assert.equal(read(),null);
  raw.monitorTiming.monitorSamples=304;raw.physicalMonitorOutputs=[1,5];assert.equal(read(),null);
});


// RTR M4 read-only Smart Performance Assistant: exact device/epoch, 1 Hz
// cumulative bucket deltas, provenance, and no automated DSP/device changes.
const m4Accepted=(armed=true)=>({
  admitted:true,retained:false,
  device:{epoch:"42",sampleRate:48000,bufferFrames:128,outputArmed:armed},
  graph:{revision:"9"}
});
const m4Live=(sequence,buckets,extra={})=>({
  connected:true,directRouteAuthorized:true,deviceGeneration:42,adaptiveClockActive:false,
  performanceRenderToken:"11",performanceRenderEpoch:"42",performanceRenderRevision:"9",
  dryRoutingLab:{epoch:42,nativeBus:{graphRevision:"9"}},
  rate:48000,buffer:128,periodUs:2666.667,nativeFxGraphRevision:"9",
  footerHealthSequence:sequence,callbackBudgetBuckets:buckets,...extra
});
const m4Buckets=(values={})=>Array.from({length:13},(_,i)=>values[i]||0);
test("M4 first 1 Hz sample cannot manufacture green status or inferred xrun",()=>{
  const first=protocol.smartPerformanceView(m4Live(1,m4Buckets({9:100})),m4Accepted());
  assert.equal(first.state,"COLLECTING");
  assert.equal(first.headroom,null);
  assert.equal(first.callbacks,null);
  assert.equal(first.over,null);
  assert.equal(first.cursor.epoch,"42");
  assert.equal(first.recommendations.some(x=>/at least 64/.test(x)),true);
});
test("M4 computes per-second histogram delta and conservative bucket-ceiling headroom",()=>{
  const first=protocol.smartPerformanceView(m4Live(1,m4Buckets({9:500})),m4Accepted());
  const next=protocol.smartPerformanceView(m4Live(2,m4Buckets({9:500,10:100})),m4Accepted(),first.cursor);
  assert.equal(next.state,"TIGHT"); // index 10 is <=85% budget
  assert.equal(next.headroom.p99,"85%");
  assert.equal(next.headroom.margin,"15%");
  assert.equal(next.over,0);
  assert.equal(next.callbacks,100);
  assert.equal(next.recommendations[0].includes("Limited"),true);
});
test("M4 labels current over-budget host renders as pressure, not confirmed crackle",()=>{
  const first=protocol.smartPerformanceView(m4Live(5,m4Buckets()),m4Accepted());
  const next=protocol.smartPerformanceView(
    m4Live(6,m4Buckets({8:90,12:10})),m4Accepted(),first.cursor);
  assert.equal(next.state,"PRESSURE");
  assert.equal(next.headroom.p99,">100%");
  assert.equal(next.over,10);
  assert.equal(next.callbacks,100);
  assert.match(next.recommendations[0],/Render deadline exceeded/);
  assert.equal(next.headroom.confidence.includes("NOT_XRUN"),true);
});
test("M4 rejects stale device, retained graph, broken interval and counter reset",()=>{
  const first=protocol.smartPerformanceView(m4Live(1,m4Buckets({6:64})),m4Accepted());
  assert.equal(protocol.smartPerformanceView(m4Live(2,m4Buckets({6:128})),
    {...m4Accepted(),device:{...m4Accepted().device,epoch:"43"}},first.cursor).state,"UNAVAILABLE");
  assert.equal(protocol.smartPerformanceView(m4Live(2,m4Buckets({6:128})),
    {...m4Accepted(),retained:true},first.cursor).state,"UNAVAILABLE");
  assert.equal(protocol.smartPerformanceView(m4Live(2,m4Buckets({6:63})),
    m4Accepted(),first.cursor).state,"COLLECTING");
  assert.equal(protocol.smartPerformanceView(m4Live(2,[1,2]),m4Accepted(),first.cursor).state,"UNAVAILABLE");
  assert.equal(protocol.smartPerformanceView(m4Live(2,m4Buckets({6:128}),
    {dryRoutingLab:{epoch:42,nativeBus:{graphRevision:"10"}}}),m4Accepted(),first.cursor).state,"UNAVAILABLE");
  assert.equal(protocol.smartPerformanceView(m4Live(2,m4Buckets({6:128})),
    {...m4Accepted(),graph:{revision:"10"}},first.cursor).state,"UNAVAILABLE");
  assert.equal(protocol.smartPerformanceView(m4Live(1,m4Buckets({6:128})),
    m4Accepted(),first.cursor).state,"COLLECTING");
});
test("M4 distinguishes scheduling suspicion and a historic sampled FX peak",()=>{
  const first=protocol.smartPerformanceView(m4Live(1,m4Buckets()),m4Accepted());
  const next=protocol.smartPerformanceView(m4Live(2,m4Buckets({7:80}),{
    renderPressureWindowValid:true,renderPressureWithinThenOnTime:52,
    renderPressureWithinThenLate:12,renderPressureOverThenOnTime:0,
    renderPressureOverThenLate:0,nativeFxWorkloadWorstPermille:913,
    nativeFxWorkloadWorstName:"dearVR",nativeFxWorkloadWorstSampledOnly:true,
    nativeFxWorkloadReviews:[{name:"dearVR",channel:"CH1_EXTERNAL",
      recommendation:"REVIEW_BLOCK_BUDGET",sampledOnly:true,
      windowValid:true,windowMeanPermille:290}]
  }),m4Accepted(),first.cursor);
  assert.equal(next.state,"OBSERVED_MARGIN");
  assert.equal(next.fxPeak.percent,"91.3%");
  assert.equal(next.fxPeak.sampled,true);
  assert.equal(next.reviews[0].window,"29.0%");
  assert.match(next.recommendations.join(" "),/driver \/ OS scheduling/);
  assert.match(next.recommendations.join(" "),/historical and sampled/);
});
test("M4 reports signed-off graph PDC with no implied zero device latency",()=>{
  const first=protocol.smartPerformanceView(m4Live(1,m4Buckets()),m4Accepted());
  const next=protocol.smartPerformanceView(m4Live(2,m4Buckets({7:64}),{
    nativeLatencyPlanValid:true,nativeLatencyPlanAlignedSamples:1456,
    nativeLatencyPlanHasMonitor:true,nativeLatencyPlanMonitorOutputSamples:0,
    nativeLatencyPlanMonitorPolicy:"INDEPENDENT_MONITOR",
    nativeMaxInsertLatencySamples:1152,nativeMaxInsertLatencyName:"Harmonice",
    nativeLatentBypassRequestedCount:1
  }),m4Accepted(),first.cursor);
  assert.equal(next.latency.main,"30.33 ms");
  assert.equal(next.latency.monitor,"0.00 ms");
  assert.equal(next.latency.policy,"Independent singer monitor");
  assert.match(next.recommendations.join(" "),/save then deactivate explicitly/);
});
test("M4 refuses green while disarmed, discards invalid FX fields and uses a single native status transport",()=>{
  const first=protocol.smartPerformanceView(m4Live(1,m4Buckets()),m4Accepted(false));
  const next=protocol.smartPerformanceView(m4Live(2,m4Buckets({5:100}),{
    nativeFxWorkloadReviews:[{name:"<img src=x onerror=alert(1)>",recommendation:"BAD"}]
  }),m4Accepted(false),first.cursor);
  assert.equal(next.state,"MUTED");
  assert.equal(next.reviews.length,0);
  assert.equal(next.recommendations[0].startsWith("Outputs disarmed"),true);
  const bridge=read("web/approved-native-bridge.js");
  assert.match(bridge,/protocol.smartPerformanceView\(live,hardware,lastM4Performance\?\.cursor\)/);
  assert.match(bridge,/row.textContent=value; \/\/ Plugin names are data/);
  assert.match(bridge,/data-audio-tab="performance"/);
  assert.doesNotMatch(bridge,/action:"autoBypassForM4"|action:"autoBufferIncreaseForM4"/);
});

test("M6 continuous live evidence excludes skipped samples, mute cycles, diagnostic callbacks and wrong scope",()=>{
  const first=protocol.smartPerformanceView(m4Live(1,m4Buckets()),m4Accepted());
  const next=(seq,extra={},accepted=m4Accepted(),prior=first.cursor)=>
    protocol.smartPerformanceView(m4Live(seq,m4Buckets({7:100}),extra),accepted,prior);
  assert.equal(next(2).state,"OBSERVED_MARGIN");
  for(const extra of [{performanceRenderToken:"12"},{performanceRenderToken:null},
    {performanceRenderEpoch:"43"},{performanceRenderRevision:"10"}])
    assert.equal(next(2,extra).headroom,null);
  assert.equal(next(3).headroom,null);
  assert.equal(next(2,{adaptiveClockActive:true}).state,"UNAVAILABLE");
  const muted=next(2,{},m4Accepted(false));
  assert.equal(muted.state,"MUTED");assert.equal(muted.headroom,null);
  assert.equal(next(3,{},m4Accepted(),muted.cursor).headroom,null);
  assert.equal(next(2,{renderPressureWindowValid:true,renderPressureWithinThenLate:65,
    renderPressureWithinThenOnTime:0,renderPressureOverThenLate:0,renderPressureOverThenOnTime:0}).correlation,null);
});

test("M6 uses actual routing epoch and installed native graph, not device-start or legacy revision counters",()=>{
  const extra={deviceGeneration:21,nativeFxGraphRevision:"81"};
  const first=protocol.smartPerformanceView(m4Live(1,m4Buckets(),extra),m4Accepted());
  const second=protocol.smartPerformanceView(m4Live(2,m4Buckets({7:100}),extra),m4Accepted(),first.cursor);
  assert.equal(second.state,"OBSERVED_MARGIN");
  assert.equal(protocol.smartPerformanceView(m4Live(2,m4Buckets({7:100}),{
    dryRoutingLab:{epoch:21,nativeBus:{graphRevision:"9"}}}),m4Accepted(),first.cursor).state,"UNAVAILABLE");
  assert.equal(protocol.smartPerformanceView(m4Live(2,m4Buckets({7:100}),{
    dryRoutingLab:{epoch:42,nativeBus:{graphRevision:"81"}}}),m4Accepted(),first.cursor).state,"UNAVAILABLE");
});


test("RTR integration preserves monitor timing, Additional Output, performance and rack editor lifetime",()=>{
  const b=read("web/approved-native-bridge.js"),m=read("src/Main.cpp");
  assert.match(b,/data-monitor-timing-toggle/);
  assert.match(b,/data-audio-extra-output/);
  assert.match(b,/data-audio-tab="performance"/);
  const rack=b.slice(b.indexOf("function requestRackFile("),b.indexOf("const nativeHooks="));
  assert.doesNotMatch(rack,/closeRackEditors|retirePendingRackEditor/);
  const receipt=b.slice(b.indexOf('backend.addEventListener("arMixFxRackFileResult"'),b.indexOf('backend.addEventListener("arMixNotice"'));
  assert.ok(receipt.indexOf('inlineFxClip?.closeRackEditors([pending.channelId])')>receipt.indexOf('receipt.phase==="COMPLETED"'));
  assert.match(receipt,/receipt\?\.requestId!==pending\.requestId/);
  assert.match(receipt,/receipt\.channelId!==pending\.channelId/);
  const live=m.slice(m.indexOf("bool canCreateLiveSession("),m.indexOf("bool liveNewSessionLeaseMatches("));
  assert.match(live,/!fxRackFileChooser_/);
});


test("intentional rack edits retire only affected pending native HWND feedback",()=>{
 const b=read("web/approved-native-bridge.js");
 const f=b.slice(b.indexOf("function retirePendingRackEditor("),b.indexOf("function reconcileM4FxInsertReadback("));
 assert.match(f,/pending.authority==="CHANNEL_ID"/);
 assert.match(f,/channelIds.some\(id=>String\(id\)===String\(pending.lease\?\.channelId\)\)/);
 assert.match(f,/pendingFxEditorAck=null/);
 const edit=b.slice(b.indexOf("function editNativeFx("),b.indexOf("function requestM4FxPower("));
 assert.ok(edit.indexOf("retirePendingRackEditor(")>edit.indexOf('decision.kind!=="READY"'));
 assert.ok(edit.indexOf("retirePendingRackEditor(")<edit.indexOf("controller.intent("));
 assert.match(b,/retirePendingRackEditor\(\[pending.channelId\]\)/);
 assert.match(b,/retirePendingRackEditor\(\);\s*inlineFxClip\?\.closeRackEditors\(\)/);
});

test("DMX sole ASIO pair belongs to MONITOR; MAIN readback names only the secondary sink",()=>{
  const raw=gmv20AcceptedGraph();
  raw.device.backend="ASIO";raw.device.outputArmed=false;
  raw.device.enabledOutputs=[4,5];
  raw.device.outputEndpoints=[4,5].map((port,callback)=>({port,callback,id:"out:"+port,label:"Out "+port,active:true}));
  raw.physicalMainOutputs=[];raw.physicalMonitorOutputs=[4,5];
  raw.secondaryMainOnly=true;raw.secondaryMainReady=true;
  raw.secondaryMainEndpoint="CABLE Input";raw.physicalBusSecondaryArmed=true;
  const read=()=>protocol.physicalOutputOverview(protocol.normalizeSnapshot(raw));
  assert.deepEqual(read().map(x=>[x.pair,x.role,x.status]),
    [["OUT 5/6","MONITOR","MUTED"],["STREAM","MAIN","ENABLED"]]);
  assert.equal(read()[1].pins,"CABLE Input");
  raw.secondaryMainReady=false;
  assert.equal(read()[1].status,"NEEDS_REMAP");
  assert.equal(read()[1].pins,"WASAPI endpoint disconnected");
  raw.secondaryMainOnly=false;
  assert.equal(read().some(x=>x.pair==="STREAM"),false);
});

test("DMX split topology creates native ChannelId and preserves authored MONITOR on return",()=>{
  const source=read("src/LiveDryRoutingLab.h");
  const start=source.indexOf("juce::String patchDmxSplitOutput(");
  const end=source.indexOf("// CONTROL only, explicit operator MUTE",start);
  assert.ok(start>=0&&end>start);
  const body=source.slice(start,end);
  assert.match(body,/armix::mixer::ChannelEdit edit;edit.kind=armix::mixer::ChannelEdit::Kind::add;/);
  assert.doesNotMatch(body,/ChannelEdit::Kind::remove/);
  assert.equal((body.match(/std::span<const armix::mixer::ChannelEdit>/g)||[]).length,1);
  assert.doesNotMatch(body,/(?<!::)\bChannelEdit\b/);
  assert.match(body,/next=\{2,old.monitorL,old.monitorR,-1,-1,false\}/);
  assert.match(body,/nativeAccepted_\.graph\.ids\.monitor=monitor/);
  assert.match(body,/DynamicChannelFxLeaseBank::reuseRevision/);
  assert.match(body,/nativePublisher_\.commit\(roster,request/);
  const publisher=read("engine/core/DynamicNativeSessionPublisher.h");
  const graph=read("engine/core/DynamicPreparedMixerBlock.h");
  assert.match(publisher,/bool allowUnmappedMonitor=false/);
  assert.match(publisher,/snap->prepared->monitor && !monitorL && !allowUnmappedMonitor/);
  assert.match(graph,/monitor_!=notScheduled && !monitorL && !allowUnmappedMonitor/);
});


test("U1 output rejects preserve actionable native reasons without weakening generic QA filtering",()=>{
 const reasons=["EXTERNAL OUT: stale ASIO device, epoch or PCM revision",
  "EXTERNAL OUT: native graph rejected; previous sound retained",
  "EXTERNAL OUT: physical pair reserved by logical output",
  "EXTERNAL OUT: wait for native bus mapping callback readback",
  "BUS OUTPUT: explicitly MUTE MAIN and STREAM first"];
 for(const reason of reasons)assert.equal(protocol.operatorMessage(reason),reason);
 assert.equal(protocol.operatorMessage("INVALID_NATIVE_SNAPSHOT"),"Change not applied · Previous mixer settings kept");
 const b=read("web/approved-native-bridge.js"),host=read("src/Main.cpp");
 assert.match(b,/ack\?\.requestId!==extraPending\.requestId\|\|ack\.action!==extraPending\.action/);
 assert.match(host,/EXTERNAL_OUTPUT_REJECTED/);
});


test("U1 reserved logical pairs are not offered as direct outputs but installed readback stays visible",()=>{
 const id="123",device={epoch:"31",backend:"ASIO",playbackDevice:"Maono",outputArmed:true,
  enabledOutputs:[0,1,2,3,4,5,6,7],
  outputEndpoints:Array.from({length:8},(_,port)=>({port,active:true,label:"Maono "+port}))};
 const raw={externalRevision:"9",physicalMainOutputs:[0,1],physicalMonitorOutputs:[],
  logicalOutputRegistry:{ports:[{bound:true,playback:"Maono",left:4,right:5}],references:[]}};
 const hardware={device,raw};
 assert.deepEqual(deviceVM.outputChoices(hardware).map(p=>p.left),[-1,2,6]);
 const installed=deviceVM.externalPresentation(hardware,{physicalLeft:4,physicalRight:5,status:"LIVE"});
 assert.equal(installed.state,"LIVE");
 const choice=deviceVM.outputChoices(hardware,true).find(p=>p.left===4);
 const native={admitted:true,device:{epoch:"31"},graph:{revision:"9",channels:[{id,role:"INPUT"}]}};
 const input={native,hardware,channelId:id,choices:[choice],selectedLabel:choice.label,currentLabel:"None"};
 assert.equal(stripVM.externalOutputPlan(input).kind,"UNAVAILABLE");
 assert.equal(stripVM.externalOutputPlan({...input,hardware:{device,raw:{...raw,logicalOutputRegistry:{ports:[],references:[]}}}}).kind,"READY");
 assert.equal(stripVM.externalOutputPlan({...input,hardware:{device,raw:{...raw,logicalOutputRegistry:{ports:[],references:[]}}},native:{...native,graph:{...native.graph,revision:"10"}}}).kind,"UNAVAILABLE");
});


test("FAST workflow directly opens active ASIO vendor panel and one native secondary enable",()=>{
 const b=read("web/approved-native-bridge.js"),n=read("src/Main.cpp"),
  e=read("src/LiveAudioEngine.h"),s=read("engine/device/LiveSecondaryOutputDevice.h");
 const panel=b.slice(b.indexOf('querySelector("[data-audio-driver-panel]").onclick'),b.indexOf('const readHealth=devicePanel'));
 assert.match(panel,/action:"audioDriverPanel"/);
 assert.doesNotMatch(panel,/action:"audioSetup"/);
 const vendor=n.slice(n.indexOf("void showAsioControlPanel("),n.indexOf("void chooseStudioBackup()",n.indexOf("void showAsioControlPanel(")));
 assert.match(vendor,/deviceApply_\.tryBegin/);assert.match(vendor,/device->showControlPanel\(\)/);
 assert.match(vendor,/finishAudioSetupAndRecoverMixer\(\)/);
 assert.doesNotMatch(vendor,/AudioSetupWindow|setMonitorEnabled\(true\)/);
 const enable=e.slice(e.indexOf("juce::String enableSecondaryOutput("),e.indexOf("juce::String armSecondaryOutput("));
 assert.match(enable,/connectSecondaryOutput\(selected,expectedEndpointId\)/);assert.match(enable,/armSecondaryOutput\(true\)/);
 assert.doesNotMatch(enable,/setMonitorEnabled|setOutputMuted/);
 assert.match(s,/createAudioIODeviceType_WASAPI\(juce::WASAPIDeviceMode::shared\)/);
 assert.match(s,/createDevice\(target,\{\}\)/);assert.doesNotMatch(s,/manager_\.initialise/);
});


test("FAST-U1 vendor restart restores only captured full endpoint identity once",()=>{
 const n=read("src/Main.cpp"),e=read("src/LiveAudioEngine.h"),s=read("engine/device/LiveSecondaryOutputDevice.h");
 assert.match(n,/captureSecondaryResumeIntent\(\)/);
 assert.match(n,/serviceAutomaticMain\(\);\s*serviceVendorStreamRecovery\(\)/);
 const r=n.slice(n.indexOf("void serviceVendorStreamRecovery()"),n.indexOf("void showAsioControlPanel("));
 assert.match(r,/dryRoutingLabEpoch\(\)!=p.epoch/);
 assert.match(r,/outputArmRevision\(\)!=p.revision/);
 assert.match(r,/p.waitForMain&&!engine_.isMonitorEnabled\(\)/);
 assert.match(r,/pendingVendorStream_\.reset\(\);\s*const auto error=engine_.restoreSecondaryOutput/);
 assert.match(e,/connectSecondaryOutput\(intent.name,intent.endpointId\)/);
 assert.match(s,/selected.selection.id!=expectedEndpointId/);
 assert.match(s,/endpointId_=selected.selection.id/);
 assert.doesNotMatch(r,/setMonitorEnabled|setOutputMuted/);
});


test("U2 physical bus candidates and overview respect existing logical reservations",()=>{
 const device={backend:"ASIO",playbackDevice:"Maono",epoch:"31",outputArmed:false,
  enabledOutputs:[0,1,2,3,4,5,6,7],outputEndpoints:Array.from({length:8},(_,port)=>({port,active:true,label:"Maono "+port}))};
 const raw={physicalBusPatchSupported:true,physicalBusOutputObserved:true,operatorOutputMuted:true,
  physicalBusSecondaryArmed:false,physicalMainOutputs:[0,1],physicalMonitorOutputs:[],
  externalOutputs:[],logicalOutputRegistry:{ports:[{id:"8",name:"Broadcast",bound:true,backend:"ASIO",playback:"Maono",left:4,right:5}],references:[]},
  mixerSession:{authority:"CHANNEL_ID",deviceEpoch:"31"}};
 const snapshot={device,raw,admitted:true,hardwareConfiguration:"verified",graph:{revision:"9",channels:[{id:"23",name:"Vocal",role:"INPUT"}]}};
 assert.deepEqual(deviceVM.busOutputChoices(snapshot,"physicalMainOutputs").map(x=>x.left),[0,2,6]);
 assert.deepEqual(deviceVM.outputChoices(snapshot).map(x=>x.left),[-1,2,6]);
 assert.equal(protocol.physicalOutputOverview(snapshot).find(x=>x.pair==="OUT 5/6").status,"RESERVED");
 const monitor=deviceVM.patchMatrix(snapshot,"output").rows.find(r=>r.name==="MONITOR");
 assert.equal(monitor.editable,false);assert.deepEqual(monitor.pair,[]);
 const installed={...snapshot,raw:{...raw,externalOutputs:[{channelId:"23",physicalLeft:4,physicalRight:5,status:"LIVE"}],
  logicalOutputRegistry:{...raw.logicalOutputRegistry,references:[{channelId:"23",portId:"8",role:"EXTERNAL",state:"RESOLVED"}]}}};
 const row=deviceVM.patchMatrix(installed,"output").rows.find(r=>r.id==="23");
 assert.equal(row.editable,false);assert.deepEqual(row.cells,["","","","","L","R","",""]);
 assert.match(row.state,/Broadcast/);
 assert.equal(protocol.physicalOutputOverview(installed).find(x=>x.pair==="OUT 5/6").status,"MUTED");
 const unrelated={...snapshot,raw:{...raw,logicalOutputRegistry:{ports:[{...raw.logicalOutputRegistry.ports[0],playback:"Other driver"}],references:[]}}};
 assert.deepEqual(deviceVM.busOutputChoices(unrelated,"physicalMainOutputs").map(x=>x.left),[0,2,4,6]);
});


test("U2-B one secondary readback represents mirror, split, fault and disconnected without inventing PCM",()=>{
 const snapshot={raw:{secondaryMainOnly:false,secondaryMainReady:false,physicalBusSecondaryArmed:false}};
 const live={secondaryOutputReady:true,secondaryOutputArmed:true,secondaryOutputName:"CABLE Input"};
 assert.equal(protocol.secondaryOutputView(snapshot,live).state,"ENABLED");
 assert.deepEqual(protocol.secondaryOutputRow(snapshot,live),{pair:"STREAM",pins:"CABLE Input",role:"MAIN MIRROR",owners:"MAIN",status:"ENABLED",shared:0});
 assert.equal(protocol.secondaryOutputRow(snapshot,{...live,secondaryOutputArmed:false}).status,"MUTED");
 assert.equal(protocol.secondaryOutputRow(snapshot,{...live,secondaryOutputFault:true}).status,"FAULT");
 assert.equal(protocol.secondaryOutputRow(snapshot,{secondaryOutputReady:false,secondaryOutputArmed:false}),null);
 const split={...live,dryRoutingLab:{secondaryMainOnly:true}};
 assert.equal(protocol.secondaryOutputRow(snapshot,split).role,"MAIN");
 assert.equal(protocol.secondaryOutputRow(snapshot,{...split,secondaryOutputReady:false,secondaryOutputName:""}).status,"NEEDS_REMAP");
 assert.equal(protocol.secondaryOutputView(snapshot,{secondaryOutputReady:true,secondaryOutputArmed:true,secondaryOutputName:""}).armed,false);
 assert.equal(protocol.secondaryOutputView({...snapshot,device:{epoch:"19"}},{...live,dryRoutingLab:{epoch:18}}).state,"DISCONNECTED");
 assert.equal(protocol.secondaryOutputView({raw:{secondaryOutputFault:true,secondaryOutputName:"CABLE Input"}},null).state,"FAULT");
 const bridge=read("web/approved-native-bridge.js");
 assert.match(bridge,/host\.append\(extraControls\)/);
 assert.match(bridge,/extraPanel\.append\(extraControls\)/);
 assert.match(bridge,/protocol\.secondaryOutputView\(hardware,live\)/);
});


test("U2-C/U3 secondary continuity is identity-pinned and preference is not ARM permission",()=>{
 const n=read("src/Main.cpp"), bridge=read("web/approved-native-bridge.js");
 const r=n.slice(n.indexOf("void serviceSecondaryContinuity()"),n.indexOf("void serviceVendorStreamRecovery()"));
 assert.match(r,/secondaryContinuity_->epoch!=epoch/);
 assert.match(r,/secondaryContinuity_->revision!=revision/);
 assert.match(r,/current.selection.id!=intent.endpointId/);
 assert.match(r,/secondaryRetry_\.failed\(now\)/);
 assert.doesNotMatch(r,/SECONDARY_HOTPLUG_PAUSED/);
 assert.match(r,/generation!=secondaryRetry_\.generation\(\)/);
 assert.match(r,/engine_\.disconnectSecondaryOutput\(\)/);
 assert.match(r,/deviceApply_\.tryBegin/);
 assert.doesNotMatch(r,/setMonitorEnabled|setOutputMuted/);
 assert.match(n,/secondary-output\.json/);
 assert.match(n,/ep.id==secondaryPreference_->id/);
 assert.match(bridge,/protocol.secondaryOutputSelection\(options/);
 const live={secondaryOutputReady:false,secondaryOutputArmed:false,secondaryOutputFault:true,
   secondaryOutputRecovering:true,secondaryOutputRecoveryName:"CABLE Input"};
 const v=protocol.secondaryOutputView({},live);
 assert.equal(v.state,"RECOVERING");assert.equal(v.name,"CABLE Input");assert.equal(v.armed,false);
 assert.equal(protocol.secondaryOutputRow({},live).status,"RECOVERING");
 assert.equal(protocol.secondaryOutputView({},{...live,secondaryOutputRecovering:false}).state,"FAULT");
});


test("U3 canonical output discovery is single-flight and selection never guesses after saved identity loss",()=>{
 const n=read("src/Main.cpp"),g=read("engine/device/WasapiEndpointIdentityGuard.h");
 const worker=n.slice(n.indexOf("void requestSecondaryInventory("),n.indexOf("void serviceSecondaryInventory("));
 assert.match(worker,/secondaryInventoryFlight_\.valid\(\)/);
 assert.match(worker,/std::async\(std::launch::async,\[\]/);
 assert.match(worker,/renderInventory\(\)/);
 assert.doesNotMatch(worker,/createDevice|scanForDevices|engine_/);
 assert.match(n,/wait_for\(std::chrono::seconds\(0\)\)/);
 assert.match(g,/static RenderInventory renderInventory\(\)/);
 const pick=protocol.secondaryOutputSelection, choices=["CABLE Input","Speakers","Broadcast"];
 assert.equal(pick(choices),"CABLE Input");
 assert.equal(pick(choices,{preferred:"Broadcast",hasSaved:true}),"Broadcast");
 assert.equal(pick(choices,{hasSaved:true}),"");
 assert.equal(pick(choices,{hasSaved:true,selected:"Speakers"}),"Speakers");
 assert.equal(pick(choices,{active:"Broadcast",selected:"Speakers"}),"Broadcast");
 assert.equal(pick(["CABLE Input A","CABLE Input B"]),"");
 assert.equal(pick(["Speakers"]),"");
});


test("U3 Stop supersedes native opens and approved output selection pins inventory identity",()=>{
 const n=read("src/Main.cpp"),b=read("web/approved-native-bridge.js"),e=read("src/LiveAudioEngine.h");
 assert.match(n,/secondaryPublishedIds_\.emplace\(label,ep.id\)/);
 assert.match(n,/token!=secondaryPublishedInventory_/);
 assert.match(n,/engine_\.enableSecondaryOutput\(value.toString\(\),expected\)/);
 assert.match(n,/engine_\.disarmSecondaryOutput\(\);\s*pendingSecondaryStop_=args/);
 assert.match(n,/generation!=secondaryRetry_\.generation\(\)/);
 assert.match(e,/secondaryOutput_\.endpointId\(\)==expectedEndpointId/);
 assert.match(b,/inventoryId:extraInventoryId/);
 assert.match(b,/selectAudioSetupPage\("device"\)/);
});


test("U3 same-device Apply suspends and restores the verified stream without manual Stop",()=>{
 const n=read("src/Main.cpp");
 const a=n.slice(n.indexOf("void applyCustomAudioDevice("),n.indexOf("void chooseImport ("));
 assert.doesNotMatch(a,/Stop STREAM and finish/);
 assert.match(a,/captureSecondaryResumeIntent\(\)/);
 assert.match(a,/requested.playback==prepared.before.outputDeviceName/);
 assert.match(a,/resumeStream\(false\)/);
 assert.match(a,/if\(restored\)resumeStream\(true\)/);
 assert.match(a,/streamGeneration!=secondaryRetry_\.generation\(\)/);
 assert.match(a,/engine_\.beginManualAudioSetup\(\)/);
 assert.match(a,/pendingVendorStream_=PendingVendorStream/);
});


test("U4 independent headphone mix uses admitted two-pin native scope and correlated mode readback",()=>{
 const s={admitted:true,device:{backend:"ASIO",epoch:"17",enabledOutputs:[0,1]},graph:{revision:"19",rosterRevision:"23"},raw:{physicalBusOutputObserved:true,physicalMainOutputs:[0,1],physicalMonitorOutputs:[],externalOutputs:[]}};
 const live={secondaryOutputReady:true,secondaryOutputArmed:true,secondaryOutputName:"CABLE Input"};
 const mode=protocol.secondaryMixMode(s,live);
 assert.equal(mode.available,true);assert.deepEqual(mode.value,{enabled:true,smart:true,deviceEpoch:"17",graphRevision:"19",rosterRevision:"23"});
 assert.equal(protocol.secondaryMixMode({...s,device:{...s.device,enabledOutputs:[0,1,2,3]}},live).available,false);
 assert.equal(protocol.secondaryMixMode({...s,raw:{...s.raw,externalOutputs:[{}]}},live).available,false);
 assert.equal(protocol.secondaryMixMode(s,{...live,secondaryOutputArmed:false}).available,false);
 const split={...s,raw:{...s.raw,secondaryMainOnly:true,physicalMainOutputs:[],physicalMonitorOutputs:[0,1]}};
 assert.equal(protocol.secondaryMixMode(split,live).value.enabled,false);
 const n=read("src/Main.cpp"),b=read("web/approved-native-bridge.js");
 const a=n.slice(n.indexOf("void applyCustomAudioDevice("),n.indexOf("void chooseImport ("));
 assert.doesNotMatch(a,/getCurrentAudioDevice\(/);
 assert.match(n,/engine_.disarmSecondaryOutput\(\);\s*error=engine_.patchDmxSplitOutput/);
 assert.match(b,/protocol.secondaryOutputSettlement\(p,hardware,live,extraStatusSerial\)/);
 assert.match(n,/ack->setProperty\("graphRevision",route.getProperty\("nativeBus"/);
});


test("U4 preserves authored monitor WIREs and reuses healthy prepared render after graph-only mode change",()=>{
 const s={admitted:true,device:{backend:"ASIO",epoch:"17",enabledOutputs:[0,1]},graph:{revision:"19",rosterRevision:"23",channels:[{id:"8",role:"MONITOR",inserts:[]}],edges:[{from:"3",to:"8"}]},raw:{physicalBusOutputObserved:true,secondaryMainOnly:true,physicalMainOutputs:[],physicalMonitorOutputs:[0,1],externalOutputs:[]}};
 const live={secondaryOutputReady:true,secondaryOutputArmed:true,secondaryOutputName:"CABLE Input"};
 assert.equal(protocol.secondaryMixMode(s,live).available,true);
 assert.equal(protocol.secondaryMixMode(s,live).value.enabled,false);
 assert.match(protocol.secondaryMixMode(s,live).reason,/preserved/);
 assert.equal(protocol.secondaryMixMode({...s,graph:{...s.graph,edges:[]}},live).available,true);
 const e=read("src/LiveAudioEngine.h");const f=e.slice(e.indexOf("juce::String restoreSecondaryOutput("),e.indexOf("void disarmSecondaryOutput()"));
 assert.match(f,/secondaryOutput_\.ready\(\)&&!secondaryOutput_\.faulted\(\)/);
 assert.match(f,/current.selection.id!=intent.endpointId/);
 assert.ok(f.indexOf("return armSecondaryOutput(true);")<f.indexOf("connectSecondaryOutput(intent.name,intent.endpointId)"));
});


test("U4 starter mix is an atomic native WIRE transaction, no overwrite of authored sends",()=>{
 const s={admitted:true,device:{epoch:"17"},graph:{revision:"19",rosterRevision:"23",mainChannelId:"5",channels:[{id:"8",role:"MONITOR"}],edges:[{from:"3",to:"5",primaryOutput:true,enabled:true}]},raw:{secondaryMainOnly:true,physicalBusOutputObserved:true}};
 const live={secondaryOutputReady:true,secondaryOutputArmed:true,secondaryOutputName:"CABLE Input"};
 assert.deepEqual(protocol.secondaryMonitorStarter(s,live),{deviceEpoch:"17",graphRevision:"19",rosterRevision:"23"});
 assert.equal(protocol.secondaryMonitorStarter({...s,graph:{...s.graph,edges:[...s.graph.edges,{from:"3",to:"8"}]}},live),null);
 assert.equal(protocol.secondaryMonitorStarter(s,{...live,secondaryOutputArmed:false}),null);
 const l=read("src/LiveDryRoutingLab.h");const f=l.slice(l.indexOf("NativeBusResult seedIndependentMonitor("),l.indexOf("NativeBusResult applyNativeWireTransaction("));
 const core=read("engine/core/DynamicChannelRoster.h");
 const planner=core.slice(core.indexOf("inline std::vector<WireChange> planHeadphoneStarter("));
 assert.match(planner,/send.destination==monitor/);
 assert.match(planner,/send.destination==main&&send.primaryOutput&&send.enabled/);
 assert.match(planner,/std::min\(0.25f,1.f\/static_cast<float>\(changes.size\(\)\)\)/);
 assert.match(planner,/change.preFader=true/);
 assert.match(f,/planHeadphoneStarter\(graph.roster,graph.ids.main,graph.ids.monitor\)/);
 assert.match(f,/return applyNativeWireTransaction\(ep,rev,rosterRev,changes\)/);
 const split=l.slice(l.indexOf("juce::String patchDmxSplitOutput("),l.indexOf("juce::String patchNativeBusOutput("));
 assert.match(split,/roster.wireTransaction\(roster.revision\(\),changes\)/);
 assert.doesNotMatch(split,/ChannelEdit::Kind::remove/);
 assert.match(l,/nativeDormantMonitorBit/);
});


test("U5 first sound uses the ordinary verified ChannelId input selector",()=>{
 const s={admitted:true,hardwareConfiguration:"lease",device:{epoch:"17",enabledInputs:[2],inputEndpoints:[{port:2,label:"Mic",active:true}]},graph:{revision:"19",mainChannelId:"5",channels:[{id:"3",role:"INPUT",name:"Vocal",controls:{mute:false}}],edges:[{from:"3",to:"5",primaryOutput:true,enabled:true}]},raw:{mixerSession:{authority:"CHANNEL_ID",deviceEpoch:"17"}}};
 assert.deepEqual(protocol.firstSoundPlan(s),{channelId:"3",channelName:"Vocal",epoch:"17",revision:"19",config:"lease",pins:[{port:2,label:"Mic"}]});
 assert.equal(protocol.firstSoundPlan({...s,raw:{...s.raw,operatorOutputMuted:true}}),null);
 assert.equal(protocol.firstSoundPlan({...s,raw:{...s.raw,logicalInputRegistry:{references:[{}]}}}),null);
 assert.equal(protocol.firstSoundPlan({...s,graph:{...s.graph,channels:[{...s.graph.channels[0],source:{sourceFormat:"PLAYER"}}]}}),null);
 assert.equal(protocol.firstSoundPlan({...s,device:{...s.device,enabledInputs:[]}}),null);
 const b=read("web/approved-native-bridge.js"),n=read("src/Main.cpp");
 const f=b.slice(b.indexOf('if(field==="input"){\n        // Input selection'),b.indexOf('if(field==="external"){\n        if(event.type!=="change")return true;',b.indexOf('// Input selection is independent')));
 assert.ok(f.length>0);
 assert.match(f,/pendingNativeSources.set/);assert.match(f,/dispatchNativeSource/);assert.doesNotMatch(f,/emitEvent/);
 const h=n.slice(n.indexOf('if(action=="routingNativeSourcePatch"){',n.indexOf('if(action=="routingNativeSourcePatch"){')+1),n.indexOf('if(action=="routingNativeBusCreate"'));
 assert.match(h,/withNativeTopologyContinuity/);assert.match(h,/engine_.patchNativeInputSource/);
 const gate=n.slice(n.indexOf("NativeBusResult withNativeTopologyContinuity("),n.indexOf("void serviceSecondaryContinuity()"));
 assert.match(gate,/engine_.disarmSecondaryOutput/);
 assert.match(gate,/pendingVendorStream_=PendingVendorStream/);assert.match(gate,/requestAutomaticMain/);
});


test("U5 ordinary topology edits share one secondary continuity owner, no duplicated gate",()=>{
 const n=read("src/Main.cpp");const h=n.slice(n.indexOf("NativeBusResult withNativeTopologyContinuity("),n.indexOf("void serviceSecondaryContinuity()"));
 assert.match(h,/deviceApply_\.tryBegin/);
 assert.match(h,/!engine_\.nativeTopologyMatches\(ep,rev\)/);
 assert.match(h,/beforeControlRevision=engine_\.outputArmRevision\(\)/);
 assert.match(h,/generation==secondaryRetry_\.generation\(\)/);
 assert.match(h,/acceptedRevision==beforeControlRevision&&engine_\.nativeTopologyMatches\(ep,rev\)/);
 assert.match(h,/engine_\.secondarySourceMatches/);
 assert.match(h,/pendingSecondaryStop_/);
 assert.equal((n.match(/result=withNativeTopologyContinuity\(ep,rev,/g)||[]).length,5);
 for(const action of ["routingNativeBusCreate","routingNativeTrackDelete","routingNativeBusDelete"]){
   const start=n.lastIndexOf('if(action=="'+action+'"');
   assert.ok(start>=0);assert.match(n.slice(start,start+8500),/withNativeTopologyContinuity/);
 }
});


test("U5 pristine ChannelId topology uses native graph identity instead of M4 control revision",()=>{
 const e=read("src/LiveAudioEngine.h"),l=read("src/LiveDryRoutingLab.h"),n=read("src/Main.cpp");
 assert.match(e,/return dryRoutingLab_\.nativeTopologyMatches\(ep,rev\)/);
 assert.match(l,/bool nativeTopologyMatches[\s\S]*?nativeAccepted_\.matches\(nativePublisher_,ep,rev\)/);
 const r=n.slice(n.indexOf("void serviceVendorStreamRecovery()"),n.indexOf("void showAsioControlPanel("));
 assert.match(r,/monotonicMilliseconds\(\)>=p.deadline[\s\S]*?secondaryRetry_\.fault/);
});


test("U6 output completion needs fresh exact installed graph, physical pair, endpoint and status in either order",()=>{
 const pending={action:"secondaryOutputSplit",acked:true,statusSerial:8,epoch:"17",config:"hw",endpoint:"CABLE Input",pair:[4,5],
   value:{enabled:true,graphRevision:"19"},ackRevision:"20"};
 const s={admitted:true,device:{epoch:"17"},hardwareConfiguration:"hw",graph:{revision:"20"},raw:{secondaryMainOnly:true,physicalMainOutputs:[],physicalMonitorOutputs:[4,5]}};
 const live={secondaryOutputReady:true,secondaryOutputArmed:true,secondaryOutputName:"CABLE Input",
   dryRoutingLab:{dmxDeviceEpoch:"17",nativeBus:{graphRevision:"20"},secondaryMainOnly:true,busOutputObserved:true}};
 const settle=(p=pending,h=s,l=live,serial=9)=>protocol.secondaryOutputSettlement(p,h,l,serial);
 assert.equal(settle(),"SETTLED");
 assert.equal(settle({...pending,acked:false}),"WAIT");
 assert.equal(settle(pending,s,live,8),"WAIT");
 assert.equal(settle(pending,{...s,graph:{revision:"19"}}),"WAIT");
 assert.equal(settle(pending,s,{...live,dryRoutingLab:{...live.dryRoutingLab,nativeBus:{graphRevision:"19"}}}),"WAIT");
 assert.equal(settle(pending,s,{...live,secondaryOutputName:"Other output"}),"WAIT");
 assert.equal(settle(pending,s,{...live,secondaryOutputRecovering:true}),"WAIT");
 assert.equal(settle(pending,{...s,raw:{...s.raw,physicalMonitorOutputs:[5,4]}}),"WAIT");
 assert.equal(settle(pending,{...s,device:{epoch:"18"}}),"STALE");
 assert.equal(settle(pending,{...s,graph:{revision:"21"}}),"STALE");
 assert.equal(settle({...pending,ackRevision:"19"}),"STALE");
 assert.equal(settle({...pending,action:"secondaryOutputStop"},s,{secondaryOutputReady:false,secondaryOutputArmed:false,secondaryOutputRecovering:true}),"WAIT");
 const n=read("src/Main.cpp");
 const retry=n.slice(n.indexOf("void serviceSecondaryContinuity()"),n.indexOf("void showAsioControlPanel("));
 assert.match(retry,/secondaryContinuity_->waitForMain&&!engine_\.isMonitorEnabled\(\)/);
 assert.match(retry,/SecondaryContinuity\{p.intent,p.epoch,p.revision,p.waitForMain\}/);
 assert.match(retry,/SecondaryContinuity\{intent,epoch,revision,waitForMain\}/);
});


test("LIVE-OPS committed workspace supersedes an outstanding fader lease",()=>{
 const base=gmv20AcceptedGraph();
 base.mixerSession={authority:"CHANNEL_ID",deviceEpoch:base.device.epoch,
   routeRevision:base.graph.revision,rosterRevision:base.graph.rosterRevision};
 base.device.outputArmed=false;base.workspaceGeneration="1";
 const seen=[],c=protocol.createController({emit:()=>{},onAccepted:s=>seen.push(s)});
 assert.equal(c.onSnapshot(clone(base)),true);
 const old=c.intent("SET_FADER","7",{valueDb:-5});assert.equal(old.ok,true);
 const restored=clone(base);restored.workspaceGeneration="2";
 assert.equal(c.onSnapshot(restored),true);
 assert.equal(c.state().pending,null);
 assert.equal(c.state().current.raw.workspaceGeneration,"2");
 assert.equal(seen.length,2);
 assert.equal(c.onSnapshot(base),false);
 assert.equal(c.state().current.raw.workspaceGeneration,"2");
 const fresh=c.intent("SET_FADER","7",{valueDb:-17});assert.equal(fresh.ok,true);
 assert.equal(c.onAck({requestId:old.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:base.device.epoch,graphRevision:base.graph.revision,rosterRevision:base.graph.rosterRevision}),false);
 assert.equal(c.state().pending.requestId,fresh.requestId);
 assert.equal(protocol.normalizeSnapshot({...restored,workspaceGeneration:"invalid"}),null);
});


test("LIVE-OPS accepted fader repaints a stale range, cap and readout without emitting an intent",()=>{
 const html=read("site/preview-v2/index.html");
 const start=html.indexOf("function syncFaderVisual("),end=html.indexOf("\nfunction setDensity",start);
 assert.ok(start>=0&&end>start);
 const range={value:"-60"},lcd={textContent:""},db={textContent:""},css=new Map();
 const box={style:{setProperty:(k,v)=>css.set(k,v)}};
 const root={querySelector:s=>({"input.fader":range,".fader-readout":lcd,".fader-box":box,".db-value":db})[s]};
 const vm=require("node:vm");
 const context={document:{querySelector:()=>root},sofEnabled:false,formatDb:v=>String(v),faderPercent:v=>v+60};
 vm.runInNewContext(html.slice(start,end)+"\nsyncFaderVisual({id:1,level:-9});",context);
 assert.equal(range.value,"-9");assert.equal(lcd.textContent,"-9 dB");
 assert.equal(db.textContent,"-9");assert.equal(css.get("--fader-y"),"49%");
});

test("P0-B unsafe live topology requests explain the explicit safe fallback",()=>{
 assert.equal(protocol.operatorMessage("Delete rejected · DELETE_REQUIRES_MUTED_OUTPUTS_PDC_OR_ROUTING"),
   "Delete not applied · Mute outputs before changing this routing");
 assert.match(protocol.operatorMessage("DELETE_FADE_NOT_CONFIRMED_RETRY_MUTED"),/outputs muted/);
 assert.match(protocol.operatorMessage("COMPRESSOR_INSERT_REQUIRES_STABLE_LIVE_GRAPH"),/not added/);
});


test("LIVE-OPS + Smart I/O: restored workspace retires old output transaction even on same hardware",()=>{
 const p={action:"secondaryOutputEnable",value:"CABLE Input",epoch:"41",workspaceGeneration:"1",statusSerial:3,acked:false};
 const same={device:{epoch:"41"},raw:{workspaceGeneration:"1"}};
 const restored={device:{epoch:"41"},raw:{workspaceGeneration:"2"}};
 const live={secondaryOutputReady:true,secondaryOutputArmed:true,secondaryOutputName:"CABLE Input"};
 assert.equal(protocol.secondaryOutputSettlement(p,same,live,4),"WAIT");
 assert.equal(protocol.secondaryOutputSettlement({...p,acked:true},same,live,4),"SETTLED");
 assert.equal(protocol.secondaryOutputSettlement(p,restored,live,4),"STALE");
 assert.equal(protocol.secondaryOutputSettlement({...p,acked:true},restored,live,4),"STALE");
 const b=read("web/approved-native-bridge.js");
 assert.match(b,/workspaceGeneration:hardware\?\.raw\?\.workspaceGeneration/);
 assert.match(b,/scope\?\.cancelNativeSessionGestures\?\.\(\);[\s\S]*?extraPending=null;extraError="";/);
});

 test("unified route diagnostics report accepted topology, not physical audibility",()=>{
 const ch=(id,name,mute=false)=>({id,name,controls:{mute,faderDb:0}});
 const snapshot={admitted:true,graph:{mainChannelId:"3",channels:[ch("1","Vocal"),ch("2","AUX"),ch("3","MAIN")],edges:[{from:"1",to:"2",enabled:true,gainLinear:1},{from:"2",to:"3",enabled:true,gainLinear:0}]}};
 assert.match(wireVM.routeDiagnostic(snapshot,"1"),/no enabled unmuted route/);
 snapshot.graph.edges[1].gainLinear=1;
 assert.match(wireVM.routeDiagnostic(snapshot,"1"),/active route to MAIN/);
 snapshot.graph.channels[1].controls.mute=true;
 assert.match(wireVM.routeDiagnostic(snapshot,"1"),/no enabled unmuted route/);
 assert.match(wireVM.routeDiagnostic({...snapshot,retained:true},"1"),/Audio unavailable/);
 });

 test("legacy recovery routes to approved custom Audio I/O, not native selector",()=>{
 const host=read("src/Main.cpp"),bridge=read("web/approved-native-bridge.js");
 const block=host.slice(host.indexOf('else if (action == "audioSetup")'),host.indexOf('else if (action == "diagnosticRoute")'));
 assert.match(block,/arMixOpenAudioIO/);assert.doesNotMatch(block,/showDeviceSetup\(/);
 assert.match(bridge,/backend.addEventListener\("arMixOpenAudioIO",\(\)=>openNativeAudioIO\(\)\)/);
 });

 test("custom Audio I/O confirmation is asynchronous and fenced against stale native authority",()=>{
 const b=read("web/approved-native-bridge.js");
 assert.doesNotMatch(b,/window\.confirm\s*\(/);
 const f=b.slice(b.indexOf("function ioConfirmationLease()"),b.indexOf("let arsonConfirmation=null;"));
 assert.match(f,/workspaceGeneration/);assert.match(f,/operatorOutputMuted/);
 assert.match(f,/const lease=ioConfirmationLease\(\)/);
 assert.match(f,/await confirmArson/);assert.match(f,/lease!==ioConfirmationLease\(\)/);
 assert.equal((b.match(/await confirmIo\(/g)||[]).length,7);
 });

test("FX Return creation requires prepared wet processor and exact independent readback",()=>{
 const lab=read("src/LiveDryRoutingLab.h"),engine=read("src/LiveAudioEngine.h");
 const f=lab.slice(lab.indexOf("NativeBusResult editNativeBus("),lab.indexOf("NativeBusResult editNativeProcessingFormat("));
 assert.match(f,/!wetReturn->silenceBypass/);assert.match(f,/next\.createReverbReturn/);
 assert.match(f,/DynamicChannelFxLeaseBank::appendOwned/);
 assert.ok(f.indexOf("DynamicChannelFxLeaseBank::appendOwned")<f.indexOf("nativePublisher_.commit"));
 assert.match(engine,/prepareNativeVocalReverbReturn\(rate,frames,nullptr,0\)/);
 const main="8",id="9",epoch="17";
 const p={operation:"CREATE_GROUP",purpose:"FX",epoch,baseGraphRevision:"19",baseRosterRevision:"20",acked:true,ackRevision:"21",createdId:id,beforeIds:[main]};
 const row={id,role:"FX_RETURN",primaryOutId:main,inserts:[{catalogId:"armix.builtin.vocal-reverb-return.v1"}]};
 const snap={admitted:true,device:{epoch},graph:{revision:"21",rosterRevision:"22",mainChannelId:main,channels:[row]},raw:{mixerSession:{authority:"CHANNEL_ID",deviceEpoch:epoch,routeRevision:"21",rosterRevision:"22"}}};
 assert.equal(stripVM.rosterReadbackSettlement(p,snap),"CONFIRMED");
 for(const ch of [{...row,role:"GROUP"},{...row,inserts:[]},{...row,primaryOutId:"7"}])
   assert.equal(stripVM.rosterReadbackSettlement(p,{...snap,graph:{...snap.graph,channels:[ch]}}),"WAIT");
});

test("routing diagnosis uses current callback meter evidence and preserves PRE semantics",()=>{
 const src={id:"1",name:"Vocal",role:"INPUT",source:{portId:"in:2"},controls:{mute:false,faderDb:-60}};
 const bus={id:"2",name:"AUX",role:"GROUP",controls:{mute:false,faderDb:0}};
 const main={id:"3",name:"MAIN",role:"MAIN",controls:{mute:false,faderDb:0}};
 const graph={revision:"7",mainChannelId:"3",channels:[src,bus,main],edges:[
 {from:"1",to:"2",enabled:true,gainLinear:1,primaryOutput:false,tapStage:"PRE_FADER"},
 {from:"2",to:"3",enabled:true,gainLinear:1,primaryOutput:true,tapStage:"POST_FADER"}]};
 const telemetry={channelMeterGraphRevision:"7",channelMeterRmsPeak:{"1":{peakL:0,peakR:0},"3":{peakL:.1,peakR:.1}},
 captureObserved:true,captureCallbackPeaks:[.5]};
 const snapshot={admitted:true,graph,telemetry,device:{outputArmed:false,inputEndpoints:[{id:"in:2",active:true,callback:0}]}};
 const msg=wireVM.routeDiagnostic(snapshot,"1");
 assert.match(msg,/Vocal → AUX → MAIN/);assert.match(msg,/IN -6.0 dBFS/);
 assert.match(msg,/POST silent/);assert.match(msg,/MAIN -20.0 dBFS/);
 assert.match(msg,/outputs disarmed/);assert.match(msg,/physical sound unverified/);
 assert.doesNotMatch(msg,/minimum fader/);
 assert.match(wireVM.routeDiagnostic({...snapshot,telemetry:{...telemetry,channelMeterGraphRevision:"6"}},"1"),/POST N\/A · MAIN N\/A/);
 graph.edges[0].tapStage="POST_FADER";
 assert.match(wireVM.routeDiagnostic(snapshot,"1"),/minimum fader/);
 graph.edges[0].sendEnabled=false;
 assert.match(wireVM.routeDiagnostic(snapshot,"1"),/no enabled unmuted route/);
});

test("no stock JUCE device selector survives in the production host",()=>{
 const host=read("src/Main.cpp");
 assert.doesNotMatch(host,/AudioDeviceSelectorComponent|AudioSetupWindow|showDeviceSetup\(/);
 const a=host.slice(host.indexOf('else if (action == "audioSetup")'),host.indexOf('else if (action == "diagnosticRoute")'));
 assert.match(a,/arMixOpenAudioIO/);
 assert.match(host,/deviceApply_\.busy\(\)/);
});

test("startup recovery and update use branded non-modal native notice, not stock JUCE",()=>{
 const host=read("src/Main.cpp");
 assert.doesNotMatch(host,/juce::NativeMessageBox::showAsync|juce::AlertWindow/);
 const recovery=host.slice(host.indexOf("class ArSonRecoveryNotice"),host.indexOf("class Browser final"));
 assert.match(recovery,/MixerCaptionLookAndFeel/);assert.match(recovery,/ArSonNoticeButton/);
 assert.match(recovery,/clearContentComponent\(\)/);assert.match(recovery,/MessageManager::callAsync/);
 assert.match(recovery,/if\(accepted\)juce::URL/);
 const archive=host.slice(host.indexOf('if(commandLine.trim()=="--archive-mixer-session"'),host.indexOf("// Paint the approved dark native splash"));
 assert.match(archive,/if\(!accepted\)\{systemRequestedQuit\(\);return;\}/);
 assert.match(archive,/quarantineForStartupRecovery/);
 const update=host.slice(host.indexOf("class ArSonMixUpdateNotice"),host.indexOf("class ConsoleApp"));
 assert.match(update,/MixerCaptionLookAndFeel/);assert.match(update,/ArSonNoticeButton/);
 assert.match(update,/onAccept_ && onAccept_\(\)/);
});

test("P0 owned floating FX retries an asset failure, coalesces opens and fences disconnected load",async()=>{
 const vm=require("node:vm"),listeners=new Map(),sent=[],scripts=[];
 const catalog="armix.builtin.auto-ducker.v1",channelId="1",instanceId="fx-one";
 const banner={hidden:true},abort={hidden:true,addEventListener(){}};
 let root=null,opens=0,observerCount=0;
 const makeSnapshot=(instance=instanceId)=>({admitted:true,retained:false,device:{epoch:"1"},
   graph:{channels:[{id:channelId,name:"Voice",inserts:[{id:instance,catalogId:catalog,bypassed:false}]}]},
   raw:{globalFxBypassed:false}});
 const payload={snapshot:makeSnapshot(),channelId,instanceId,catalogId:catalog,slot:0};
 const backend={addEventListener:(name,fn)=>listeners.set(name,fn),emitEvent:(name,payload)=>sent.push({name,payload})};
 const win={__JUCE__:{backend},ArSonApprovedNativeContract:{
   normalizeSnapshot:s=>s,selectNativeReadModel:s=>({kind:"CHANNEL_ID",view:s}),
   nativeHardwareConfiguration:()=>"hardware-A"},addEventListener(){}};
 const doc={getElementById:id=>id==="fxWindowStatus"?banner:abort,addEventListener(){},
   querySelector:()=>root,createElement:()=>({remove(){this.removed=true;}}),
   head:{append:s=>scripts.push(s)}};
 vm.runInNewContext(read("web/approved-floating-fx-bridge.js"),{
   window:win,document:doc,MutationObserver:class{observe(){observerCount++;}},
   requestAnimationFrame:()=>0,console},{timeout:1500});
 const open=()=>listeners.get("arMixFloatingOpen")(payload);
 const flush=()=>new Promise(resolve=>setImmediate(resolve));
 open();open();assert.equal(scripts.length,1);
 scripts[0].onerror();await flush();
 assert.equal(scripts[0].removed,true);
 assert.match(banner.textContent,/unavailable/);
 open();open();assert.equal(scripts.length,2,"explicit retry creates one fresh request");
 win.ArSonMixNativeDucker={open(){opens++;root={hidden:false,addEventListener(){},
   getBoundingClientRect:()=>({width:400,height:300})};}};
 listeners.get("arMixConsoleSnapshot")(makeSnapshot("replaced"));
 scripts[1].onload();await flush();
 assert.equal(opens,0,"must not open disconnected instance");
 open();await flush();
 assert.equal(scripts.length,2,"reuse successfully loaded allowlisted module");
 assert.equal(opens,1);
 assert.equal(observerCount,1);
 assert.equal(sent.filter(x=>x.name==="arMixFloatingMounted").length,1);
 open();await flush();assert.equal(opens,1,"duplicate focus does not recreate the editor");
});

test("Session MON retains verified preference without implying output ARM",()=>{
 const pick=protocol.mainMonitorPreference;
 assert.deepEqual(pick(["Speakers"],"Speakers",true),{value:"__armix_saved__",label:"Speakers · saved / off",endpoint:"Speakers"});
 assert.deepEqual(pick(["Other"],"Speakers",true),{value:"__armix_saved_missing__",label:"Saved device unavailable · off",endpoint:""});
 assert.equal(pick(["CABLE Input"],"",false).value,"");
 const n=read("src/Main.cpp"), b=read("web/approved-native-bridge.js");
 assert.match(n,/preferenceSaved=rememberSecondaryPreference\(name,engine_\.secondarySelectedEndpointId\(\)\)/);
 assert.match(b,/active&&v\.name\?v\.name:saved\.value/);
 assert.match(b,/value==="__armix_saved__"\|\|value==="__armix_saved_missing__"\)return/);
});
test("Channel context menu exposes non-destructive FX save and real native receipts",()=>{
 const menu=read("web/approved-native-context-menu.js"), b=read("web/approved-native-bridge.js"), n=read("src/Main.cpp");
 assert.match(menu,/"rack-save-single":"SAVE_SINGLE"/);
 assert.match(b,/const single=operation==="DEACTIVATE"\|\|operation==="SAVE_SINGLE"/);
 assert.match(b,/receipt\.phase==="COMPLETED"&&!\["SAVE","SAVE_SINGLE"\]\.includes\(pending\.operation\)/);
 assert.match(b,/arMixFxRackFileResult/);
 assert.match(n,/emitFxRackFileResult\(args,"CHOOSER"/);
 assert.match(n,/rackFileTargetAvailable\(ep,rev,roster,channel\)/);
 assert.match(n,/fxRackConsent_=std::make_unique<ArSonRecoveryNotice>/);
 assert.match(n,/if\(accepted\)perform\(true\)/);
 assert.equal(protocol.operatorMessage("FX rack file: Disarm MAIN and STREAM; use a current native rack"),"FX rack file: Disarm MAIN and STREAM; use a current native rack");
 assert.equal(protocol.operatorMessage("FX rack file: Opening file dialog…"),"FX rack file: Opening file dialog…");
 assert.match(n,/if\(archived&&deactivate\)/);
 assert.match(n,/file=file\.withFileExtension\("\.arfx"\)/);
 assert.match(b,/notice\?\.error===true\|\|rackReceipt/);
});

test("Cold restart MON projects actual prepared muted endpoint, not invented ARM",()=>{
 const pick=protocol.mainMonitorPreference;
 assert.equal(pick(["Broadcast"],"Broadcast",true,true).label,"Broadcast · ready / off");
 assert.equal(pick(["Other"],"Broadcast",true,true).endpoint,"");
 assert.equal(pick(["Broadcast"],"",false,true).value,"");
 const n=read("src/Main.cpp"),b=read("web/approved-native-bridge.js");
 assert.match(n,/prepareOnStartup/);
 assert.match(n,/preferenceSaved=forgetSecondaryStartupSelection\(\)/);
 assert.match(b,/ack.action==="secondaryOutputStop"&&ack.preferenceSaved===true/);
 assert.match(b,/v.ready&&!v.fault&&!active&&v.name===extraPreferred/);
});


test("V6 native window resize keeps WebView out of the eight-direction grip",()=>{
 const native=read("src/Main.cpp"),geometry=read("src/ArSonWindowResizeGeometry.h"),chrome=read("src/ArSonWindowChrome.h");
 assert.match(native,/getBorderThickness\(\) const override/);
 assert.match(native,/return juce::BorderSize<int> \(armix::windowchrome::resizeEdge\)/);
 assert.match(native,/isResizable\(\) && !isUsingNativeTitleBar\(\) && !isFullScreen\(\) && !isKioskMode\(\)/);
 assert.match(native,/findControlAtPoint \(juce::Point<float> point\) const override/);
 for(const edge of ["Left","Right","Top","Bottom","TopLeft","TopRight","BottomLeft","BottomRight"])assert.ok(native.includes("WindowControlKind::size"+edge+";"),edge);
 assert.match(native,/return juce::DocumentWindow::findControlAtPoint\(point\)/);
 assert.match(geometry,/resizeEdge = 8/);assert.match(geometry,/resizeCorner = 16/);
 assert.equal((geometry.match(/static_assert\(/g)||[]).length,14);
 assert.match(chrome,/drawResizableWindowBorder[\s\S]*g\.drawRect \(0, 0, w, h, 1\)/);
});


test("V6 Player window owns only desktop presentation, never another player engine",()=>{
 const native=read("src/Main.cpp"),js=read("site/preview-v2/assets/arson-player.js"),bundle=read("scripts/build_approved_native_gui.py");
 const start=native.indexOf("class PlayerDesktopWindow final"),end=native.indexOf("// A post-install GUI probe",start);
 const window=native.slice(start,end);
 assert.match(window,/public juce::TopLevelWindow/);
 assert.match(window,/attachOwnedFloatingWindow\(\*this,mixer_\)/);
 assert.match(window,/placeFloatingEditor\(\*this,mixer_,!everPresented_\)/);
 assert.match(window,/WM_SYSCOMMAND,SC_MOVE\|HTCAPTION/);
 assert.match(window,/arMixPlayerWindowHide/);
 assert.doesNotMatch(window,/activeWindowStatusChanged|focusLost|setAlwaysOnTop|LiveAudioEngine|AudioDeviceManager|playerStop|playerPause/);
 assert.match(native,/std::unique_ptr<PlayerDesktopWindow> playerWindow_/);
 assert.match(native,/if\(playerWindow_\)playerWindow_->emit\(event,payload\)/);
 assert.match(js,/if\(desktopAvailable&&!desktopChild\)\{if\(visible\)windowCommand\("OPEN"\);return;\}/);
 assert.match(bundle,/player-window\.html/);
});
