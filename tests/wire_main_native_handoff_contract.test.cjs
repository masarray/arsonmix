"use strict";
/* Zero-browser/zero-audio handoff guard; the native-produced fixture and
   Windows joined FAT remain separately mandatory. This test verifies only
   approved GUI display invariants + additive v8 contract, never PCM. */
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const root=path.resolve(__dirname,"..");
const read=p=>fs.readFileSync(path.join(root,p),"utf8");
const contract=JSON.parse(read("contracts/console-puzzle-v8.json"));
const ui=read("site/preview-v2/index.html");
const css=read("site/preview-v2/assets/mixer-wire-dots.css");
const handoff=read("docs/workstreams/WIRE_V59_MAIN_NATIVE_ENGINE_RECONCILIATION_20261104.md");

assert.equal(contract.schemaVersion,6,"The installed Console v8 envelope must not be replaced");
assert.equal(contract.wireAcceptedProjection.operation,"APPLY_WIRE_TRANSACTION");
assert.equal(contract.wireAcceptedProjection.requiredNativeReadback.ids,
  "canonical decimal uint64 strings, never Number");
for(const f of ["mainChannelId","edges","revision","rosterRevision"])
  assert(contract.wireAcceptedProjection.requiredNativeReadback.graph.includes(f),f);
for(const f of ["edgeId","from","to","enabled","signalKind","tap","gainLinear"])
  assert(contract.wireAcceptedProjection.requiredNativeReadback.edge.includes(f),f);
for(const f of ["requestId","deviceEpoch","baseGraphRevision","baseRosterRevision",
                "operation","targetChannelId","payload"])
  assert(contract.intentRequired.includes(f),f+" v8 envelope");
for(const f of ["requestId","status","deviceEpoch","graphRevision","rosterRevision","errorCode"])
  assert(contract.ackRequired.includes(f),f+" v8 ACK");
assert(contract.wireAcceptedProjection.consistency.includes("later native snapshot"));
assert(contract.wireAcceptedProjection.stagePolicy.includes("productionCallbackLive false"));

assert(ui.includes("function syncMainWireFragments()"));
assert(ui.includes("const focus=selectedChannels.length===1?selectedChannels[0]:null;"),
  "MAIN display must require exactly one selected channel");
assert(ui.includes("paintMainWireFragment(lane,!folded&&focus?.id===t.id?edge:null,false);"),
  "Unselected channels may not paint physical MAIN cables");
assert(ui.includes("paintMainWireFragment(masterEl,!folded?chosenEdge:null,true);"),
  "Folded MAIN must not paint a hanging receiver cable");
assert(ui.includes("const custom=committed.filter(e=>e.destId!==master.id);"),
  "Only custom non-MAIN edges may illuminate OUT");
assert(ui.includes("function wireOutgoingCount(id){return outgoingWires(id).filter(e=>e.destId!==master.id).length;}"),
  "OUT count must not include default MAIN");
assert(ui.includes("const source=dragging?wireSourceTrack(wireGesture.sourceId):"),
  "Physical cable display must not depend on the pinned control source");
assert(!ui.includes("wireView==='all'"),"ALL cables display restores unrelated spaghetti");
assert(!css.includes(".wire-fold-summary[data-wire-main-edge]::after"),
  "Folded MAIN pseudo-tails must never reappear");
assert(css.includes("collapsed-send .strip .wire-main-fragment"),
  "CSS must hide physical fragments in collapsed WIRE");
assert(handoff.includes("#384")&&handoff.includes("#271")&&handoff.includes("#266"));
console.log("PASS V59 main GUI / v8 native WIRE handoff contract (NOT live PCM FAT)");
