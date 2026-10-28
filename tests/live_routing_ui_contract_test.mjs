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
