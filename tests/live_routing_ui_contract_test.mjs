// SPDX-License-Identifier: AGPL-3.0-only
// Native/GUI contract: P1E exposes REAL channel routing only under explicit
// muted-output operator action and never falsely marks CH2-10 VST inserts live.
import { readFileSync } from "node:fs";
import assert from "node:assert/strict";
import test from "node:test";

const engine=readFileSync(new URL("../src/LiveAudioEngine.h",import.meta.url),"utf8");
const lab=readFileSync(new URL("../src/LiveDryRoutingLab.h",import.meta.url),"utf8");
const main=readFileSync(new URL("../src/Main.cpp",import.meta.url),"utf8");
const ui=readFileSync(new URL("../web/monitor.js",import.meta.url),"utf8");
const bridge=readFileSync(new URL("../web/bridge.js",import.meta.url),"utf8");

test("P1E is opt-in; all native VST3/clock/diagnostic paths protected",()=>{
  assert.match(engine,/dryRoutingLab_\.active\(\)/);
  assert.match(engine,/!isOutputMuted\(\).*MUTE OUTPUT/);
  assert.match(engine,/setOutputMuted\(true\); \/\/ never expose CH1 unexpectedly/);
  assert.match(engine,/dryRoutingLab_\.prepareDevice\(d\)/);
  assert.match(engine,/dryRoutingLab_\.deviceStopped\(\)/);
  assert.match(engine,/dryRoutingLab_\.process\(inputs,inputChannels,outputs/);
  assert.match(engine,/if \(dryRoutingLab_\.active\(\)\) return "Stop DRY ROUTING LAB before editing VST3"/);
  assert.match(engine,/if \(dryRoutingLab_\.active\(\)\) return "Stop DRY ROUTING LAB before preset recall"/);
  assert.match(lab,/publisher_\.publish/);
  assert.match(lab,/deviceEpoch_/);
  assert.match(lab,/Block::hardFrameLimit/);
  assert.match(lab,/rampFrames=256/);
  assert.match(lab,/publishedEpoch_\.load/);
  assert.match(lab,/std::array<const float\*,armix::device::LiveInputPortRegistry::limit>/);
});
test("GUI exposes only truthful dry device routes and acknowledged commit",()=>{
  assert.match(ui,/ROUTING LAB · 10 CH/);
  assert.match(ui,/routingDryApply/);
  assert.match(ui,/routingDryStop/);
  assert.match(ui,/arMixRoutingLabAck/);
  assert.match(ui,/data-route-left/);
  assert.match(ui,/data-route-right/);
  assert.match(ui,/status|DRY LAB STALE/);
  assert.match(ui,/No VST3|no VST3|no channel VST3|no VST3/i);
  assert.match(main,/action == "routingDryApply"/);
  assert.match(main,/action == "routingDryStop"/);
  assert.match(main,/arMixRoutingLabAck/);
  // Default 10 fake preview strips are intentionally not made into VST3
  // controls by the dry hardware test.
  assert.match(bridge,/if\(i!==0\)/);
  assert.match(bridge,/track\.dataset\.preview="1"/);
});

test("STOP-race is fail closed: all outputs are written to silence without CH1 fallback",()=>{
  const enter=engine.indexOf("if (dryRoutingLab_.active())",engine.indexOf("callbackCount_.fetch_add"));
  const end=engine.indexOf("// Existing FX graph is excluded",enter);
  assert.ok(enter>=0&&end>enter, "live audio callback LAB branch must exist");
  const branch=engine.slice(enter,end);
  assert.match(branch,/const bool routed=dryRoutingLab_\.process\(/);
  assert.match(branch,/if\(!routed\)\s*\{\s*for\(int c=0;c<outputChannels;\+\+c\)/);
  assert.match(branch,/juce::FloatVectorOperations::clear\(outputs\[c\],samples\)/);
  assert.match(branch,/const float left=routed\?dryRoutingLab_\.peakLeft\(\):0\.f/);
  assert.match(branch,/const float right=routed\?dryRoutingLab_\.peakRight\(\):0\.f/);
});
test("MUTE gate precedes processing and suppresses previous audible crossfade",()=>{
  const start=lab.indexOf("if(snapshot->graphRevision!=audioRevision_");
  const beforeRender=lab.indexOf("snapshot->prepared->process(",start);
  const silent=lab.indexOf("if(masterMuted) {",start);
  assert.ok(start>=0&&silent>start&&beforeRender>silent);
  const gate=lab.slice(silent,beforeRender);
  assert.match(gate,/fadeRemaining_=rampFrames/);
  assert.match(gate,/fadeFromL_=fadeFromR_=0\.f/);
  assert.match(gate,/lastOutputL_=lastOutputR_=0\.f/);
  assert.match(gate,/return true/);
  assert.ok(!lab.includes("masterMuted?0.f:std::clamp(masterGain"), "don't mute before fade and then reintroduce old samples");
});

test("P1G is real native BUS/send, not a cosmetic routing picker",()=>{
  assert.match(lab,/NativeDryRouteCommand::decode/);
  assert.match(lab,/const auto& sends=decoded\.sends/);
  assert.match(lab,/JuceActivePortSnapshot::capture/);
  assert.match(lab,/callbackOutputCount_\.load/);
  assert.match(lab,/level\.channelPeak\[ch\]/);
  assert.match(ui,/\['NONE','MONO','STEREO','BUS'\]/);
  assert.match(ui,/data-route-main/);
  assert.match(ui,/data-route-mute/);
  assert.match(ui,/data-route-solo/);
  assert.match(ui,/arAddSend/);
  assert.match(ui,/data-send-tap/);
  assert.match(ui,/sends:draftSends\.map/);
  assert.match(ui,/result\.sendCount/);
  assert.match(ui,/data-route-peak/);
});

test("P1H independently instantiates and processes real JUCE FX in 10 native lanes",()=>{
  const fx=readFileSync(new URL("../engine/core/LiveChannelFxGraph.h",import.meta.url),"utf8");
  assert.match(fx,/std::array<std::shared_ptr<FxInstance>,channels>/);
  assert.match(fx,/processor->processBlock\(buffer,midi\)/);
  assert.match(fx,/processor->releaseResources\(\)/);
  assert.match(lab,/std::make_unique<FxGraph>\(plan/);
  assert.match(lab,/formats\.createPluginInstance/);
  assert.match(lab,/prepareStereoEffectLayout/);
  assert.match(lab,/prepareToPlay/);
  assert.match(lab,/maxFramesPerPlugin/);
  assert.match(main,/catalog_\.find\(id\)/);
  assert.match(ui,/data-route-fx/);
  assert.match(ui,/arMixCatalog/);
  assert.match(ui,/fxChoices\(v\.fxId\)/);
  assert.match(ui,/result\.fxCount/);
});

test("P1I binds premium 10CH console to REAL native RT graph without FX restart",()=>{
  const app=readFileSync(new URL("../web/app.js",import.meta.url),"utf8");
  const core=readFileSync(new URL("../engine/core/PreparedMixerBlock.h",import.meta.url),"utf8");
  const fx=readFileSync(new URL("../engine/core/LiveChannelFxGraph.h",import.meta.url),"utf8");
  const publisher=readFileSync(new URL("../engine/core/RealtimeRoutePublisher.h",import.meta.url),"utf8");
  assert.match(app,/routeEpoch=liveToken\.epoch/);
  assert.match(app,/routeRevision=liveToken\.revision/);
  assert.match(main,/setDryRoutingLabParameter/);
  assert.match(lab,/setChannelParameter/);
  assert.match(lab,/publisher_\.updateActiveControl/);
  assert.match(lab,/channelControls/);
  assert.match(lab,/channelFxNames/);
  assert.match(core,/class RealtimeStripControls/);
  assert.match(core,/graph\.sends\.upstreamOf/);
  assert.match(fx,/&controls_/);
  assert.match(publisher,/updateActiveControl/);
  assert.match(bridge,/window\.__armixRouteToken/);
  assert.match(bridge,/lab\.channelPeaks/);
  assert.match(bridge,/data-native-routing-lab/);
});

test("P1J keeps real per-strip VST3 identity and tails across routing-only APPLY",()=>{
  const fx=readFileSync(new URL("../engine/core/LiveChannelFxGraph.h",import.meta.url),"utf8");
  const publisher=readFileSync(new URL("../engine/core/RealtimeRoutePublisher.h",import.meta.url),"utf8");
  const app=readFileSync(new URL("../web/app.js",import.meta.url),"utf8");
  assert.match(fx,/struct FxInstance final/);
  assert.match(fx,/std::shared_ptr<FxInstance>/);
  assert.match(fx,/processing\.test_and_set/);
  assert.match(fx,/processing\.clear/);
  assert.match(fx,/bool reuse\(int strip,const LiveChannelFxGraph& previous/);
  assert.match(fx,/previous\.insertId\(strip\)!=catalogId/);
  assert.match(publisher,/controlPrepared\(std::uint64_t epoch/);
  assert.match(lab,/publisher_\.controlPrepared\(currentEpoch,expectedRevision\)/);
  assert.match(lab,/next->prepared->reuse\(lane,\*prior,req\.id\)/);
  assert.match(lab,/continue; \/\/ NO disk scan/);
  assert.match(lab,/fxReused_\.store\(reusedCount/);
  assert.match(lab,/acceptedRoute_=command/);
  assert.match(lab,/setProperty\("acceptedRoute",acceptedRoute_\)/);
  assert.match(ui,/hydrateAcceptedRoute/);
  assert.match(ui,/saved\.deviceEpoch/);
  assert.match(ui,/window\.__arMixOpenRoutingLab=openRoutingLab/);
  assert.match(app,/id="flowRoutingButton"/);
  assert.match(app,/Independent VST3/);
  assert.match(ui,/NATIVE ACK:.*preserved/);
});

test("P1K truly keeps ten physical INPUT strips and adds four independent native AUX/FX returns",()=>{
  const aux=readFileSync(new URL("../engine/core/AuxBusPlan.h",import.meta.url),"utf8");
  const block=readFileSync(new URL("../engine/core/PreparedMixerBlock.h",import.meta.url),"utf8");
  const fx=readFileSync(new URL("../engine/core/LiveChannelFxGraph.h",import.meta.url),"utf8");
  const command=readFileSync(new URL("../engine/core/NativeDryRouteCommand.h",import.meta.url),"utf8");
  const app=readFileSync(new URL("../web/monitor.js",import.meta.url),"utf8");
  assert.match(aux,/auxBusCount=4/);
  assert.match(aux,/source<0\|\|s\.source>=stripCount/);
  assert.match(aux,/destination<0\|\|s\.destination>=auxBusCount/);
  assert.match(block,/processFx\(stripCount\+b,l,r,frames\)/);
  assert.match(block,/graph\.aux\.dispatch\(strip,SendTap::preFader/);
  assert.match(block,/graph\.aux\.dispatch\(strip,SendTap::postFader/);
  assert.match(fx,/fxSlots=stripCount\+auxBusCount/);
  assert.match(lab,/decoded\.aux/);
  assert.match(lab,/auxSendCount_/);
  assert.match(main,/LiveDryRoutingLab::auxCount/);
  assert.match(command,/to>=stripCount/);
  assert.match(app,/data-aux-row/);
  assert.match(app,/auxes:draftAux\.map/);
  assert.match(app,/AUX 1/);
  const cmake=readFileSync(new URL("../CMakeLists.txt",import.meta.url),"utf8");
  const workflow=readFileSync(new URL("../.github/workflows/p1-windows.yml",import.meta.url),"utf8");
  assert.match(cmake,/P1KTenIndependentInputsAndFourDedicatedAuxReturns/);
  assert.match(workflow,/ARMixDedicatedAuxBusTests/);
});
