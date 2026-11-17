// SPDX-License-Identifier: AGPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import {context,requiredLanes,expectedWindowsTitle,workflowPath,findRuns,evaluate,
  extractPrNumbers,refreshOne} from "../scripts/required_pr_merge_gate.mjs";

const sha="478cf65d18f05d3bdb62285ab32e56e427304930";
const repo="masarray/arsonmix",number=753;
const file=(filename)=>({filename});
const automatic=(id,workflow)=>({
  id,event:"pull_request",head_sha:sha,status:"completed",conclusion:"success",
  path:".github/workflows/"+workflow,
  pull_requests:[{number}]
});
const dispatched=(id,workflow)=>({
  id,event:"workflow_dispatch",head_sha:workflow==="p1-windows.yml"?
    "aec9158a6ab7c625a6c330b178f2f234a9020d2a":sha,
  status:"completed",conclusion:"success",
  path:".github/workflows/"+workflow,
  display_title:workflow==="p1-windows.yml"?expectedWindowsTitle(number,sha):"UI qualification"
});
const job=(name,conclusion="success")=>({name,status:"completed",conclusion});
const names={
  native:"Fast native contracts (no Windows / JUCE checkout)",
  windows:"Windows MSVC / realtime CTest",
  ui:"Fast UI contracts (every relevant PR commit)",
  browser:"Full Chromium /preview/ visual and interaction qualification",
  release:"No native compiler; owner authorization, catalog integrity and YAML shape"
};
test("GitHub REST workflow.path may omit @ref; both path shapes are trusted",()=>{
  assert.equal(workflowPath({path:".github/workflows/p1-windows.yml"},"p1-windows.yml"),true);
  assert.equal(workflowPath({path:repo+"/.github/workflows/p1-windows.yml@refs/heads/main"},
    "p1-windows.yml"),true);
  assert.equal(workflowPath({path:".github/workflows/fake.yml"},"p1-windows.yml"),false);
});
test("docs-only PR succeeds without Windows/Chromium or release services",()=>{
  const lanes=requiredLanes(["docs/readme.md","README.md"]);
  assert.deepEqual(lanes,{nativeFast:false,nativeFull:false,uiFast:false,
    uiFull:false,releaseFast:false});
  assert.equal(evaluate(lanes,{},{}).state,"success");
});
test("native changes fail closed until quick and exact-head Windows qualification",()=>{
  const lanes=requiredLanes(["src/NativeLogicalMixerWorkspace.h","tests/native_mixer_session_store_test.cpp"]);
  assert.equal(lanes.nativeFast,true);
  assert.equal(lanes.nativeFull,true);
  assert.equal(lanes.uiFast,false);
  const fast=automatic(10,"p1-windows.yml");
  const older=dispatched(11,"p1-windows.yml");
  older.display_title=expectedWindowsTitle(number,sha.replace(/^4/,"5"));
  let found=findRuns([fast,older],number,sha);
  let result=evaluate(lanes,found,{10:[job(names.native)]});
  assert.equal(result.state,"pending");
  assert.ok(result.waiting.some(s=>s.startsWith("nativeFull")));
  const current=dispatched(12,"p1-windows.yml");
  found=findRuns([fast,current,older],number,sha);
  result=evaluate(lanes,found,{10:[job(names.native)],12:[job(names.windows)]});
  assert.equal(result.state,"success");
  assert.equal(context,"ArSonMix / Required CI Merge Gate");
  result=evaluate(lanes,found,{10:[job(names.native)],12:[job(names.windows,"failure")]});
  assert.equal(result.state,"failure");
});
test("approved FAT Windows run fulfills the native CTest lane without duplicate NATIVE",()=>{
  const lanes=requiredLanes(["src/LiveAudioEngine.h"]);
  const fast=automatic(10,"p1-windows.yml");
  const fat=dispatched(13,"p1-windows.yml");
  fat.display_title="ArSonMix FAT PR"+number+" "+sha;
  const selected=findRuns([fast,fat],number,sha);
  assert.equal(selected.fullWin.id,13);
  assert.equal(evaluate(lanes,selected,
    {10:[job(names.native)],13:[job(names.windows)]}).state,"success");
});
test("GUI bridge requires both fast native and full Chromium, not Windows CTest",()=>{
  const lanes=requiredLanes(["web/approved-native-bridge.js"]);
  assert.equal(lanes.nativeFast,true);
  assert.equal(lanes.nativeFull,false);
  assert.equal(lanes.uiFast,true);
  assert.equal(lanes.uiFull,true);
  const runs=[automatic(10,"p1-windows.yml"),automatic(20,"ui-smoke.yml"),
    dispatched(30,"ui-smoke.yml")];
  const found=findRuns(runs,number,sha);
  const jobs={10:[job(names.native)],20:[job(names.ui)],30:[job(names.browser)]};
  assert.equal(evaluate(lanes,found,jobs).state,"success");
  jobs[30]=[job(names.browser,"skipped")];
  assert.equal(evaluate(lanes,found,jobs).state,"failure");
});
test("release-only files use release checks, never expensive Windows",()=>{
  const lanes=requiredLanes(["scripts/build_release_catalog.py"]);
  assert.deepEqual(lanes,{nativeFast:false,nativeFull:false,
    uiFast:false,uiFull:false,releaseFast:true});
  assert.equal(evaluate(lanes,{release:automatic(40,"release-contracts.yml")},
    {40:[job(names.release)]}).state,"success");
});
test("missing expected job or duplicate job never counts success",()=>{
  const lanes=requiredLanes(["src/Main.cpp"]);
  const matched={native:automatic(10,"p1-windows.yml"),fullWin:dispatched(12,"p1-windows.yml"),
    release:automatic(40,"release-contracts.yml")};
  const jobs={10:[job("Unrelated test")],12:[job(names.windows)],40:[job(names.release)]};
  assert.equal(evaluate(lanes,matched,jobs).state,"failure");
  jobs[10]=[job(names.native),job(names.native)];
  assert.equal(evaluate(lanes,matched,jobs).state,"failure");
});
test("old head SHA, wrong PR and untrusted workflow source cannot qualify",()=>{
  const bad=automatic(10,"p1-windows.yml");
  bad.head_sha="9".repeat(40);
  const wrong=automatic(11,"ui-smoke.yml");
  wrong.pull_requests=[{number:752}];
  const fake=dispatched(12,"p1-windows.yml");
  fake.display_title="ArSonMix NATIVE PR752 "+sha;
  const found=findRuns([bad,wrong,fake],number,sha);
  assert.equal(found.native,undefined);
  assert.equal(found.ui,undefined);
  assert.equal(found.fullWin,undefined);
});
test("fresh run is pending; cancelled or skipped jobs fail closed",()=>{
  const lanes=requiredLanes(["web/styles.css"]);
  const fast=automatic(20,"ui-smoke.yml");
  const full=dispatched(30,"ui-smoke.yml");
  full.status="in_progress";full.conclusion=null;
  const found=findRuns([fast,full],number,sha);
  const jobs={20:[job(names.ui)],30:[{name:names.browser,status:"in_progress"}]};
  assert.equal(evaluate(lanes,found,jobs).state,"pending");
  full.status="completed";full.conclusion="cancelled";
  jobs[30]=[job(names.browser,"skipped")];
  assert.equal(evaluate(lanes,found,jobs).state,"failure");
});
test("reject empty, incomplete or invalid PR file inventories",()=>{
  assert.throws(()=>requiredLanes([]),/INVALID_PR_FILES/);
  assert.throws(()=>requiredLanes(["../evil"]),/INVALID_PR_FILES/);
  assert.throws(()=>requiredLanes(["docs/x",null]),/INVALID_PR_FILES/);
});
test("only server event identifiers drive PR reconciliation",()=>{
  assert.deepEqual(extractPrNumbers({pull_request:{number:753}}),[753]);
  assert.deepEqual(extractPrNumbers({workflow_run:{display_title:
    expectedWindowsTitle(number,sha),pull_requests:[]}}),[753]);
  assert.deepEqual(extractPrNumbers({workflow_run:{
    display_title:"not a valid marker",pull_requests:[{number:752},{number:752}]}}),[752]);
});
test("status publication is exact-head and uses deterministic scoped checks",async()=>{
  const writes=[];
  const pr={state:"open",base:{ref:"main",repo:{full_name:repo}},
    head:{sha,repo:{full_name:"contributor/fork"}}};
  const runs=[automatic(10,"p1-windows.yml"),dispatched(12,"p1-windows.yml")];
  const api=async(path,method,payload)=>{
    if(path==="/pulls/753")return pr;
    if(path.startsWith("/pulls/753/files?"))return [file("src/NativeLogicalMixerWorkspace.h")];
    if(path.startsWith("/actions/runs?"))return {workflow_runs:[runs[0]]};
    if(path.startsWith("/actions/workflows/p1-windows.yml/runs?"))
      return {workflow_runs:[runs[1]]};
    if(path.startsWith("/actions/runs/10/jobs?"))return {jobs:[job(names.native)]};
    if(path.startsWith("/actions/runs/12/jobs?"))return {jobs:[job(names.windows)]};
    if(path==="/statuses/"+sha && method==="POST") {
      writes.push(payload);return {};
    }
    throw Error("Unexpected API "+method+" "+path);
  };
  const result=await refreshOne(repo,number,api);
  assert.equal(result.state,"success");
  assert.equal(writes.length,1);
  assert.equal(writes[0].state,"success");
  assert.equal(writes[0].context,context);
});
