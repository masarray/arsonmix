// SPDX-License-Identifier: AGPL-3.0-only
// Fast G0 architecture gate. Tests the INSTALLED owner (M4 compatibility)
// without pretending that the prepared ChannelId graph is live.
// When ChannelId cutover is actually approved, this gate MUST change in
// the same exact-SHA integration commit as the JUCE callback and native ACK.
import test from 'node:test';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const read=p=>readFileSync(p,'utf8');
const engine=read('src/LiveAudioEngine.h');
const host=read('src/Main.cpp');
const cmake=read('CMakeLists.txt');
const authority=read('engine/core/NativeRenderAuthority.h');
const bridge=read('src/ConsoleLiveBridgeV6.h');
const wire=read('engine/core/ConsolePuzzleV8Wire.h');
const schema=JSON.parse(read('contracts/console-puzzle-v8.json'));
const worker=read('src/NativeShowSaveWorker.h');
const showCapture=read('src/NativeLiveShowCapture.h');
const showRecall=read('src/NativeDryShowRecall.h');
const physical=read('src/LiveDryRoutingLab.h');
const dynamic=read('engine/core/DynamicNativeSessionPublisher.h');
const realtimePermit=read('engine/core/RealtimeRenderPermit.h');
const start=engine.indexOf('void audioDeviceIOCallbackWithContext (');
assert.ok(start>0,'missing ONE JUCE production callback');
const callback=engine.slice(start);
const count=(s,pattern)=>[...s.matchAll(pattern)].length;

test('exactly one production audio callback and one native PCM owner',()=>{
  assert.equal(count(engine,/void audioDeviceIOCallbackWithContext\s*\(/g),1);
  assert.equal(count(host,/LiveAudioEngine engine_;/g),1);
  assert.match(engine,/#include "\.\.\/engine\/core\/NativeRenderAuthority\.h"/);
  assert.match(callback,/NativeRenderAuthority::select\(/);
  assert.match(callback,/NativeRenderOwner::failSilentUnpatched/);
  assert.match(callback,/NativeRenderOwner::acceptedM4Session/);
  assert.equal(count(callback,/dryRoutingLab_\.process\(/g),1,
    'two native M4 PCM renders would double-sum or double-process vendor FX');
  assert.doesNotMatch(callback,/DynamicNativeSessionPublisher|DynamicAcceptedSession|\.publisher_\.render\(/,
    'unadmitted universal graph must never run alongside real M4 callback');
  assert.match(authority,/acceptedM4Session/);
  assert.match(authority,/isolatedDiagnostic/);
});

test('unpatched is silent and diagnostic path cannot host a legacy second VST engine',()=>{
  const unpatched=callback.indexOf('NativeRenderOwner::failSilentUnpatched');
  const routed=callback.indexOf('NativeRenderOwner::acceptedM4Session');
  const isolated=callback.indexOf('// The only remaining stereo renderer is DIAGNOSTIC');
  const finish=callback.indexOf('const auto elapsedUs = clockMicros() - beginUs',isolated);
  assert.ok(unpatched>=0&&routed>unpatched&&isolated>routed&&finish>isolated);
  const first=callback.slice(unpatched,routed);
  assert.match(first,/FloatVectorOperations::clear\(outputs\[c\], samples\)/);
  assert.match(first,/handoff_\.endCycle\(cycle\)/,
    'plugin owner must still retire on the CONTROL thread when unpatched');
  const tail=callback.slice(isolated,finish);
  assert.doesNotMatch(tail,/processor->processBlock|MidiBuffer midi|createPluginInstance|prepareToPlay/,
    'do not revive a hidden stereo plugin engine in diagnostic fallback');
  assert.match(tail,/diagnosticTone_\.next/);
});

test('original CH1 vendor FX/Orbit is leased exactly once in accepted M4 renderer',()=>{
  const old=engine.indexOf('static void renderOriginalChannelOneGraph(');
  const end=engine.indexOf('void audioDeviceIOCallbackWithContext',old);
  assert.ok(old>=0&&end>old);
  const fx=engine.slice(old,end);
  assert.match(fx,/slot\.processor->processBlock\(buffer,midi\)/);
  assert.match(fx,/bypass\.request\(graph\.bypassFlags/);
  assert.match(fx,/catch\(\.\.\.\)/);
  assert.match(callback,/LegacyChannelOneLease ch1Lease\{cycle\.graph\}/);
  assert.match(callback,/&processLegacyChannelOne/);
  assert.doesNotMatch(callback,/std::make_unique<|std::make_shared<|createPluginInstance\(/,
    'no FX instantiation or graph allocation in device callback');
});

test('occupied CH1 Orbit uses one strong Graph lease through native renderer and Show handoff',()=>{
  assert.match(engine,/std::shared_ptr<Graph> graphOwned_\s*\{std::make_shared<Graph>\(\)\}/);
  assert.match(engine,/std::shared_ptr<Graph> stagedOwner_/);
  assert.match(engine,/fx\.owner=std::static_pointer_cast<void>\(graphOwned_\)/);
  assert.match(engine,/fx\.context=graphOwned_\.get\(\)/);
  assert.match(engine,/fx\.render=&processOwnedChannelOne/);
  assert.match(engine,/renderOriginalChannelOneGraph\(\s*\*static_cast<Graph\*>\(token\)/);
  assert.match(engine,/Graph\(\) \{ dryScratch\.setSize\(2,maxFrames,false,true,false\); \}/);
  assert.match(engine,/graph\.dryScratch\.getWritePointer\(0\)/);
  assert.match(engine,/graph\.orderPacked\.load\(std::memory_order_acquire\)/);
  assert.match(engine,/dryRoutingLab_\.revokeChannelIdPcm\(\)/);
  assert.equal(count(engine,/dryRoutingLab_\.revokeChannelIdPcm\(\)/g),2,
    'both Show recall and original CH1 ADD/REMOVE must revoke native renderer');
  assert.match(physical,/occupiedExternalCh1Fx>0,hw/);
  assert.match(physical,/nativePublisher_,nativeAccepted_,std::move\(externalCh1\)/);
  assert.match(physical,/channelIdPcmEpoch_\.store\(0,std::memory_order_release\)/);
  assert.equal(count(engine,/void audioDeviceIOCallbackWithContext\s*\(/g),1);
});

test('E3 stable ChannelId GROUP/FX RETURN is a real guarded physical PCM transaction',()=>{
  assert.match(physical,/NativeBusResult editNativeBus\(/);
  assert.match(physical,/next\.createBusForSelected\(/);
  assert.match(physical,/next\.removeBusAndRestoreSources\(/);
  assert.match(physical,/DynamicChannelFxLeaseBank::reuseRevision\(/);
  assert.match(physical,/nativePublisher_\.commit\(/);
  assert.match(physical,/nativeAccepted_\.graph\.roster=std::move\(next\)/);
  assert.match(physical,/if\(!published\.ok\(\)\)/);
  assert.match(physical,/nativeStructureLive_=nativeAccepted_\.graph\.roster\.size\(\)/);
  assert.match(physical,/nativeAccepted_\.hardware\.monitorEnabled/);
  assert.match(physical,/topologyFault_\.store\(true,std::memory_order_release\)/);
  assert.match(engine,/LiveDryRoutingLab::NativeBusResult editNativeBus\(/);
  assert.match(engine,/if\(dryRoutingLab_\.nativeBusTopologyLive\(\)\)return false/);
  assert.match(bridge,/juce::var processNativeBusIntent\(/);
  assert.match(bridge,/nativeGraphRevision/);
  assert.match(bridge,/NATIVE_CREATE_GROUP_SELECTED/);
  assert.match(bridge,/NATIVE_CREATE_FX_RETURN_SELECTED/);
  assert.match(bridge,/NATIVE_REMOVE_BUS/);
  assert.match(host,/consoleBridge_\.processNativeBusIntent\(/);
  assert.match(bridge,/w\.productionCallbackLive=false/);
  assert.match(wire,/callbackOwnsChannelIds/);
  assert.match(physical,/nativeBusStructural/);
  assert.match(showCapture,/nativeBusStructural/);
  assert.match(showRecall,/nativeBusStructural/);
  assert.equal(count(callback,/dryRoutingLab_\.process\(/g),1);
});


test('E4 installed PATCH operator uses native ChannelId BUS ACK and real PCM snapshot, never ten-strip draft',()=>{
  const ui=read('web/monitor.js');
  const binding=ui.slice(ui.indexOf('// E4: The EXISTING routing workspace controls'),
                         ui.indexOf('// Processing channel width is independent'));
  assert.ok(binding.length>3000,'real native Group/FX operator binding absent');
  assert.match(binding,/const nativeBusAccepted=\(\)=>/);
  assert.match(binding,/legacy\?\.controlPlane!=='LEGACY_M4_ORDINAL'/);
  assert.match(binding,/legacy\?\.pcmRenderer!=='CHANNEL_ID_10CH4AUX'/);
  assert.match(binding,/busWireDecimal\(c\.id\)===null/);
  assert.match(binding,/bus\.supportedOps\.includes\(op\)/);
  assert.match(binding,/state\.secondaryOutputArmed/);
  assert.match(binding,/state\.monitor/);
  assert.match(binding,/!mixerDraftActive/);
  assert.match(binding,/targetChannelId:targetChannelId\|\|null,payload/);
  assert.match(binding,/selectedChannelIds:ids,sendGain:gain/);
  assert.match(binding,/api\.emitEvent\('arMixConsoleIntent',command\)/);
  assert.match(binding,/api\.addEventListener\('arMixConsoleAck'/);
  assert.match(binding,/api\.addEventListener\('arMixConsoleSnapshot'/);
  assert.match(binding,/busSnapshotSerial>busPending\.ackSnapshotSerial/);
  assert.match(binding,/BigInt\(info\.bus\.graphRevision\)>=BigInt\(ack\.nativeGraphRevision\)/);
  assert.match(binding,/BigInt\(info\.bus\.rosterRevision\)>=BigInt\(ack\.nativeRosterRevision\)/);
  assert.match(binding,/NATIVE_CREATE_GROUP_SELECTED/);
  assert.match(binding,/NATIVE_CREATE_FX_RETURN_SELECTED/);
  assert.match(binding,/NATIVE_REMOVE_BUS/);
  assert.match(binding,/NATIVE_SET_BUS_MIX/);
  assert.doesNotMatch(binding,/productionCallbackLive\s*=\s*true|createAudioContext|AudioContext\(/,
    'operator MUST NOT fabricate universal capabilities or DSP');
  assert.match(ui,/busRequest\(\);renderNativeBus\(true\)/);
  assert.match(bridge,/processNativeBusIntent/);
  assert.match(physical,/nativeBusStatus\(\)/);
});


test('E4 operator native Group+FX Return controls run through real ACK then newer accepted PCM snapshot',()=>{
  const ui=read('web/monitor.js');
  const start=ui.indexOf('// E4: The EXISTING routing workspace controls');
  const end=ui.indexOf('// Processing channel width is independent',start);
  assert.ok(start>0&&end>start,'E4 actual route controller must exist');
  class Node {
    constructor(tag){this.tag=tag;this.children=[];this.listeners={};
      this.value='';this.disabled=false;this.checked=false;this.textContent='';
      this.className='';this.id='';this.attrs={};}
    setAttribute(name,value){this.attrs[name]=value;}
    append(...children){this.children.push(...children);}
    replaceChildren(...children){this.children=[...children];}
    addEventListener(name,fn){this.listeners[name]=fn;}
    insertAdjacentElement(_,child){this.children.push(child);}
  }
  const root=new Node('section'),sent=[],events={};
  const doc={createElement:tag=>new Node(tag),
    createTextNode:text=>({textContent:text}),
    getElementById:id=>id==='arBusCapability'?root:null};
  const api={emitEvent:(name,payload)=>sent.push({name,payload}),
    addEventListener:(name,fn)=>events[name]=fn};
  const ctx=vm.createContext({
    document:doc,api,window:{crypto:{getRandomValues:n=>n.fill(7)}},
    routeOverlay:{classList:{contains:()=>true}},
    state:{monitor:false,secondaryOutputArmed:false},
    patchPending:false,mixerDraftActive:false
  });
  vm.runInContext(ui.slice(start,end),ctx,{timeout:1000});
  const snap=(revision='10',roster='3',epoch='12',armed=false)=>({
    schemaVersion:6,productionCallbackLive:false,
    device:{epoch,outputArmed:armed},
    legacyM4:{controlPlane:'LEGACY_M4_ORDINAL',
      pcmRenderer:'CHANNEL_ID_10CH4AUX',routingRevision:'7',
      nativeBus:{graphRevision:revision,rosterRevision:roster,
        supportedOps:['NATIVE_CREATE_GROUP_SELECTED',
          'NATIVE_CREATE_FX_RETURN_SELECTED','NATIVE_SET_BUS_MIX',
          'NATIVE_REMOVE_BUS'],
        channels:[{id:'1',role:'INPUT',name:'Microphone'},
          {id:'2',role:'INPUT',name:'Music'},
          {id:'3',role:'MAIN',name:'MAIN'}]}}
  });
  events.arMixConsoleSnapshot(snap());
  const probe=code=>vm.runInContext(code,ctx,{timeout:1000});
  assert.equal(probe('groupCreate.disabled'),false);
  assert.equal(probe('nativeBusSources.children.length'),2);
  probe("nativeBusSources.children[0].children[0].checked=true");
  probe("nativeBusSources.children[0].children[0].listeners.change()");
  probe("nativeBusName.value='Vocal Group';nativeBusGain.value='.6';groupCreate.listeners.click()");
  const commands=()=>sent.filter(x=>x.name==='arMixConsoleIntent');
  assert.equal(commands().length,1);
  const one=commands()[0].payload;
  assert.equal(one.operation,'NATIVE_CREATE_GROUP_SELECTED');
  assert.deepEqual(Array.from(one.payload.selectedChannelIds),['1']);
  assert.equal(one.deviceEpoch,'12');
  assert.equal(one.legacyRoutingRevision,'7');
  assert.equal(one.nativeGraphRevision,'10');
  assert.equal(one.nativeRosterRevision,'3');
  assert.equal(one.payload.sendGain,.6);
  assert.equal(probe('groupCreate.disabled'),true);
  probe('groupCreate.listeners.click()');
  assert.equal(commands().length,1,'pending request must block duplicate AUDIO graph commit');
  // Engine ACK only proves acceptance. The UI stays locked until a
  // strictly newer accepted PCM publisher snapshot supplies real revisions.
  events.arMixConsoleAck({
    requestId:one.requestId,status:'ACCEPTED',deviceEpoch:'12',
    nativeGraphRevision:'11',nativeRosterRevision:'4',errorCode:''
  });
  assert.equal(probe('groupCreate.disabled'),true);
  events.arMixConsoleSnapshot(snap('10','3'));
  assert.equal(probe('groupCreate.disabled'),true);
  const updated=snap('11','4');
  updated.legacyM4.nativeBus.channels.splice(2,0,{
    id:'20',role:'GROUP',name:'Vocal Group',fader:1,pan:0});
  events.arMixConsoleSnapshot(updated);
  assert.equal(probe('groupCreate.disabled'),false);
  assert.equal(probe('nativeBusRows.children.length'),1);
  // Browser cannot forge a BUS by sending the universal graph capability:
  // authoritative namespace must be ChannelId PCM actually installed.
  const bogus=snap('12','5');bogus.legacyM4.pcmRenderer='M4_COMPAT';
  events.arMixConsoleSnapshot(bogus);
  assert.equal(probe('groupCreate.disabled'),true);
  probe("fxReturnCreate.listeners.click()");
  assert.equal(commands().length,1,'no synthetic native BUS without production PCM');
  events.arMixConsoleSnapshot(updated);
  probe("nativeBusSources.children[0].children[0].checked=true");
  probe("nativeBusSources.children[0].children[0].listeners.change()");
  probe("nativeBusName.value='Vocal Wet';fxReturnCreate.listeners.click()");
  assert.equal(commands().length,2);
  assert.equal(commands()[1].payload.operation,'NATIVE_CREATE_FX_RETURN_SELECTED');
  events.arMixConsoleAck({
    requestId:commands()[1].payload.requestId,status:'REJECTED',
    deviceEpoch:'12',errorCode:'NATIVE_REJECTED'
  });
  assert.equal(probe('groupCreate.disabled'),false,
    'rejected native intent should unlock controls while keeping old PCM unchanged');
  const armed=snap('11','4','12',true);
  events.arMixConsoleSnapshot(armed);
  assert.equal(probe('groupCreate.disabled'),true,'output armed structural edits fail closed');
  probe("groupCreate.listeners.click()");
  assert.equal(commands().length,2);
});

test('shipping application does not compile a second test-only native mixer',()=>{
  const app=cmake.split(/\r?\n/).find(l=>l.startsWith('target_sources(ARMixConsole PRIVATE'));
  assert.ok(app,'native JUCE application source list missing');
  assert.match(app,/src\/LiveAudioEngine\.h/);
  assert.match(app,/engine\/core\/NativeRenderAuthority\.h/);
  assert.doesNotMatch(app,/PreparedNativeMixerSession|DynamicNativeSessionPublisher|DynamicPreparedMixerBlock|UnifiedLegacyProductionAdmission|LegacyLiveGraphMigration/,
    'prepared graph may be compiled as tests/includes, not advertised as a second application entry point');
  assert.match(cmake,/target_sources\(ARMixNativeMixerSessionTests PRIVATE tests\/prepared_native_mixer_session_test\.cpp engine\/core\/PreparedNativeMixerSession\.h\)/);
});

test('GUI projection cannot falsely claim the prepared ChannelId engine is live',()=>{
  assert.equal(schema.schemaVersion,6);
  assert.deepEqual(schema.wire.events,
    ['arMixConsoleSubscribe','arMixConsoleSnapshot','arMixConsoleIntent','arMixConsoleAck']);
  for(const item of ['deviceEpoch','baseGraphRevision','baseRosterRevision','requestId','targetChannelId'])
    assert.ok(schema.intentRequired.includes(item));
  assert.ok(bridge.includes('w.productionCallbackLive=false'));
  assert.match(bridge,/legacyM4/);
  assert.match(wire,/callbackOwnsChannelIds\\":false/);
  assert.match(wire,/fullVendorFxMigration\\":false/);
  assert.ok(wire.includes('supportedOps'));
  assert.match(bridge,/parseNativeChannelId/);
  assert.match(bridge,/validConsoleRequestId/);
});

test('workers handle Show IO outside the audio callback, not as a second DSP engine',()=>{
  assert.match(worker,/class NativeShowSaveWorker final : private juce::Thread/);
  assert.match(worker,/store\.commit\(show,\{snapshot_\},blobs_\)/);
  assert.match(worker,/void releaseJob\(\)/);
  assert.doesNotMatch(callback,/NativeShowStore|\.loadWithStates\(|\.commit\(show/);
});

test('E2 uses exactly one installed JUCE callback with admitted native ChannelId PCM and M4 fallback',()=>{
  assert.match(engine,/dryRoutingLab_\.admitChannelIdFromAcceptedM4\(/);
  assert.match(physical,/UnifiedLegacyProductionAdmission::compileAndCommit\(/);
  assert.match(physical,/channelIdPcmEpoch_\.store\(generation,std::memory_order_release\)/);
  assert.match(physical,/const bool channelIdRender=/);
  assert.match(physical,/if\(channelIdRender\)\s*\{/);
  assert.match(physical,/nativePublisher_\.render\(/);
  assert.match(physical,/else \{\s*processed=snapshot->prepared->process\(/);
  assert.equal(count(callback,/dryRoutingLab_\.process\(/g),1);
  assert.equal(count(callback,/DynamicNativeSessionPublisher::render\(/g),0,
    'a second JUCE native renderer may not be installed');
  assert.match(physical,/channelIdPcmEpoch_\.store\(0,std::memory_order_release\)/,
    'stale epoch and structural edits must turn off the native selector');
  assert.match(physical,/channelIdPhysicalPcmActive\(/,
    'live GUI scalar control must follow the admitted native renderer');
  assert.match(engine,/dryRoutingLab_\.revokeChannelIdPcm\(\)/,
    'original CH1 FX publication must first retire native PCM authority');
  assert.match(physical,/nativePublisher_\.pollRetirement\(\)/,
    'native VST owner generations must retire off audio callback');
});

test('installed M4 and future ChannelId share one nonblocking render permit',()=>{
  assert.ok(physical.includes('RealtimeRenderPermit exclusive{rendering_}'));
  assert.ok(dynamic.includes('RealtimeRenderPermit exclusive{rendering_}'));
  assert.ok(realtimePermit.includes('test_and_set(std::memory_order_acquire)'));
  assert.ok(realtimePermit.includes('clear(std::memory_order_release)'));
  assert.ok(!realtimePermit.includes('std::mutex') && !realtimePermit.includes('condition_variable'));
  assert.ok(physical.includes('if(!exclusive)'));
  assert.ok(dynamic.includes('if(!exclusive)'));
});

test('legacy M4 operations are advertised separately, not as fake universal ChannelIds',()=>{
  assert.ok(bridge.includes('LEGACY_M4_ORDINAL'));
  assert.ok(bridge.includes('acceptedChannels'));
  assert.ok(bridge.includes('route.getProperty("topologyFault",false)'));
  assert.ok(bridge.includes('object->setProperty("supportedOps",juce::var(allowed))'));
  assert.ok(bridge.includes('w.productionCallbackLive=false'));
  assert.ok(wire.includes('supportedOps'));
});

test('legacy M4 GUI commands require accepted physical authority, not just guessed ordinal',()=>{
  assert.ok(bridge.includes('.getProperty("controlPlane","").toString()!="LEGACY_M4_ORDINAL"'));
  assert.ok(bridge.includes('PRODUCTION_GRAPH_NOT_ADMITTED'));
  assert.ok(bridge.includes('legacyRoutingRevision'));
});
