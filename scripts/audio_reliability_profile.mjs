// SPDX-License-Identifier: AGPL-3.0-only
// INT-1B: offline-only analyzer for the 1 Hz AUDIO_HEALTH native diagnostic log.
// NEVER runs in the JUCE audio callback, opens devices, or uploads evidence.
import {readFileSync, writeFileSync} from "node:fs";
import {resolve} from "node:path";
import {pathToFileURL} from "node:url";

export const SCHEMA="arsonmix.audio-health.int1b.v1";
export const BUCKET_UPPER=[1,2,4,8,12,20,30,40,55,70,85,100,null];
const COUNTERS=["callbackCount","overBudgetCount","slowCallbackCount",
  "lateCallbackIntervals","callbackGapCount","callbackCadenceClockRegressions",
  "deviceStartCount","deviceStopCount"];
const isCount=x=>Number.isSafeInteger(x)&&x>=0;
const finitePositive=x=>typeof x==="number"&&Number.isFinite(x)&&x>0;
const percent=(a,b)=>b>0?Math.round(10000*a/b)/100:null;

export function parseHealthLog(log) {
  const samples=[], warnings=[], rejectedLines=[];
  let sessionId=0;
  for(const [idx,line] of String(log).split(/\r?\n/).entries()){
    if(/ \| SESSION \| started\s*$/.test(line)){sessionId++;continue;}
    if(!line.includes(" | AUDIO_HEALTH | "))continue;
    const m=/^(\S+) \| AUDIO_HEALTH \| (\{.*\})$/.exec(line);
    if(!m){warnings.push("line "+(idx+1)+": malformed/truncated AUDIO_HEALTH");rejectedLines.push(idx+1);continue;}
    let sample;
    try {sample=JSON.parse(m[2]);}
    catch {warnings.push("line "+(idx+1)+": invalid AUDIO_HEALTH JSON");rejectedLines.push(idx+1);continue;}
    if(!sample || typeof sample!=="object" || Array.isArray(sample) ||
       sample.schema!==SCHEMA){
      warnings.push("line "+(idx+1)+": legacy/unknown schema (excluded)");
      rejectedLines.push(idx+1);
      continue;
    }
    const time=Date.parse(m[1]);
    if(!Number.isFinite(time)||!finitePositive(sample.rate)||
       !isCount(sample.buffer)||sample.buffer===0||
       !isCount(sample.deviceGeneration)||!isCount(sample.callbackCount)||
       typeof sample.liveBackendClass!=="string" ||
       typeof sample.dryTest!=="boolean" || typeof sample.connected!=="boolean" ||
       typeof sample.nativeGlobalFxBypassed!=="boolean" ||
       !isCount(sample.nativeFxInstalledCount) ||
       !isCount(sample.nativeFxProcessingCount) ||
       sample.nativeFxProcessingCount>sample.nativeFxInstalledCount ||
       sample.nativeFxActivityComplete!==true ||
       typeof sample.nativeExternalCh1Leased!=="boolean" ||
       !isCount(sample.nativeExternalCh1InstalledCount) ||
       !isCount(sample.nativeExternalCh1ProcessingCount) ||
       sample.nativeExternalCh1ProcessingCount>sample.nativeExternalCh1InstalledCount ||
       sample.nativeExternalCh1InstalledCount>sample.nativeFxInstalledCount ||
       sample.nativeExternalCh1ProcessingCount>sample.nativeFxProcessingCount ||
       (!sample.nativeExternalCh1Leased && sample.nativeExternalCh1InstalledCount!==0) ||
       (sample.nativeGlobalFxBypassed && sample.nativeFxProcessingCount!==0) ||
       typeof sample.nativeFxGraphRevision!=="string" ||
       !/^\d+$/.test(sample.nativeFxGraphRevision) ||
       typeof sample.activeInputMaskHex!=="string" ||
       typeof sample.activeOutputMaskHex!=="string" ||
       !/^[0-9a-f]*$/i.test(sample.activeInputMaskHex) ||
       !/^[0-9a-f]*$/i.test(sample.activeOutputMaskHex)){
      warnings.push("line "+(idx+1)+": invalid source metadata/counters");
      rejectedLines.push(idx+1);
      continue;
    }
    const buckets=sample.callbackBudgetBuckets;
    if(!Array.isArray(buckets)||buckets.length!==13||!buckets.every(isCount)){
      warnings.push("line "+(idx+1)+": missing/invalid 13 histogram buckets");
      rejectedLines.push(idx+1);
      continue;
    }
    if(!COUNTERS.every(key=>isCount(sample[key]))){
      warnings.push("line "+(idx+1)+": missing/invalid cumulative counter");
      rejectedLines.push(idx+1);
      continue;
    }
    samples.push({...sample,atMs:time,line:idx+1,sessionId});
  }
  return {samples,warnings,rejectedLines};
}

export function p95p99FromDelta(counts) {
  if(!Array.isArray(counts)||counts.length!==13||!counts.every(isCount))
    return {p95:null,p99:null,samples:0};
  const total=counts.reduce((n,x)=>n+x,0);
  if(!Number.isSafeInteger(total)||total===0)
    return {p95:null,p99:null,samples:0};
  const bound=percentile=>{
    const target=Math.ceil(total*percentile);
    let seen=0;
    for(let i=0;i<13;i++){
      seen+=counts[i];
      if(seen>=target)return BUCKET_UPPER[i];
    }
    return null;
  };
  return {p95:bound(0.95),p99:bound(0.99),samples:total};
}

// Immutable hardware/graph provenance must survive the DRY -> FX switch.
// Mode keys are separate so only the actual accepted global power transition
// can bridge adjacent windows; all other boundary reasons invalidate pairing.
const STABLE_SCOPE_KEYS=[
  "sessionId","deviceGeneration","device","liveBackendClass","driver","rate","buffer",
  "inputDevice","outputDevice","deviceInputChannels","deviceOutputChannels",
  "activeInputMaskHex","activeOutputMaskHex","nativeFxGraphRevision",
  "nativeFxInstalledCount","nativeExternalCh1Leased","nativeExternalCh1InstalledCount",
  "connected","diagnosticRoute","productionRoutingState","directRouteAuthorized",
  "backendXrunMeaning","callbackTimingScope"
];
const MODE_KEYS=[
  "nativeFxProcessingCount","nativeGlobalFxBypassed",
  "nativeExternalCh1ProcessingCount","dryTest"
];
const sameValues=(a,b,keys)=>keys.every(k=>a[k]===b[k]);
const nondecreasing=(a,b,key)=>isCount(a[key])&&isCount(b[key])&&b[key]>=a[key];
const continuousEvidence=(a,b)=>b.atMs>a.atMs && b.atMs-a.atMs<=5000 &&
  COUNTERS.every(k=>nondecreasing(a,b,k))&&
  a.callbackBudgetBuckets.every((x,i)=>b.callbackBudgetBuckets[i]>=x);
const validInterval=(a,b)=>continuousEvidence(a,b) &&
  sameValues(a,b,STABLE_SCOPE_KEYS)&&sameValues(a,b,MODE_KEYS);
const acceptedGlobalPowerTransition=(a,b)=>continuousEvidence(a,b) &&
  sameValues(a,b,STABLE_SCOPE_KEYS) && a.connected===true && b.connected===true &&
  a.nativeGlobalFxBypassed===true && b.nativeGlobalFxBypassed===false &&
  a.nativeFxProcessingCount===0 && b.nativeFxProcessingCount>0;

const CPU_SCOPE="WINDOWS_PROCESS_CPU_ALL_LOGICAL_CORES_1HZ_NOT_AUDIO_CALLBACK_OR_SYSTEM_TOTAL";
const resourceValue=(s,key)=>typeof s[key]==="number"&&Number.isFinite(s[key])&&s[key]>=0?s[key]:null;
const cpuValue=s=>{
  const value=resourceValue(s,"footerProcessCpuPercent");
  return s.footerCpuProvenance===CPU_SCOPE&&value!==null&&value<=100?value:null;
};
function recordResources(segment,s){
  for(const [name,value] of [["cpu",cpuValue(s)],["workingSet",resourceValue(s,"processWorkingSetMiB")],
    ["privateCommit",resourceValue(s,"processPrivateCommitMiB")]]){
    if(value===null)continue;
    const r=segment.resources[name];
    if(r.count===0)r.first=value;
    r.last=value;r.peak=r.count?Math.max(r.peak,value):value;r.sum+=value;r.count++;
  }
}
const resourceSummary=r=>({samples:r.count,mean:r.count?Math.round(r.sum/r.count*100)/100:null,
  peak:r.peak,delta:r.count>=2?Math.round((r.last-r.first)*100)/100:null});
function describeSegment(segment){
  const s=segment.start, first=segment.start, last=segment.last;
  const p=p95p99FromDelta(segment.histogram);
  const callbacks=segment.delta.callbackCount, period=1000000*s.buffer/s.rate;
  const durationSeconds=Math.round(segment.durationMs/100)/10;
  const scope=s.callbackTimingScope??"LEGACY_RENDER_TIMING";
  // For V2, require nearly complete sampled callback coverage. Independent
  // atomics can skew by a callback at the endpoints, so allow a small margin.
  const completeCoverage=scope!=="RENDER_THROUGH_FINAL_OUTPUT_PUBLICATION_V2" ||
    (callbacks>0&&Math.abs(p.samples-callbacks)<=Math.max(2,callbacks*0.01));
  const missing=segment.intervals===0||p.samples===0||!completeCoverage;
  // A driver restart or regressed monotonic callback clock invalidates
  // comparison even when a driver reuses its nominal device generation.
  const stableLifecycle=segment.delta.deviceStartCount===0&&
    segment.delta.deviceStopCount===0&&segment.delta.callbackCadenceClockRegressions===0;
  const over=segment.delta.overBudgetCount,late=segment.delta.lateCallbackIntervals;
  const gap=segment.delta.callbackGapCount;
  let diagnosis="INSUFFICIENT_EVIDENCE";
  if(!missing){
    if(segment.delta.deviceStopCount>0||segment.delta.deviceStartCount>0)
      diagnosis="DEVICE_LIFECYCLE_CHANGE";
    else if(over>0&&late>0)diagnosis="BOTH_DSP_BUDGET_AND_SCHEDULING_INDICATORS";
    else if(over>0)diagnosis="DSP_BUDGET_PRESSURE_INDICATOR";
    else if(late>0||gap>0)diagnosis="CALLBACK_SCHEDULING_INDICATOR";
    else diagnosis="NO_HOST_TIMING_ANOMALY_OBSERVED";
  }
  return {
    // The legacy dryTest control is only a UI intent; actual DRY/FX is
    // derived from accepted native INSERT+rack+GLOBAL bypass readback.
    lineStart:first.line,lineEnd:last.line,
    invalidBefore:segment.invalidBefore,
    sessionId:s.sessionId,startedAt:new Date(first.atMs).toISOString(),
    endedAt:new Date(last.atMs).toISOString(),
    startTimeMs:first.atMs,endTimeMs:last.atMs,
    mode:s.nativeFxProcessingCount===0?"DRY":"FX",
    nativeFxInstalledCount:s.nativeFxInstalledCount,
    nativeFxProcessingCount:s.nativeFxProcessingCount,
    nativeGlobalFxBypassed:s.nativeGlobalFxBypassed,
    nativeExternalCh1Leased:s.nativeExternalCh1Leased,
    nativeExternalCh1InstalledCount:s.nativeExternalCh1InstalledCount,
    nativeExternalCh1ProcessingCount:s.nativeExternalCh1ProcessingCount,
    device:String(s.device??"UNKNOWN"),backend:s.liveBackendClass,
    inputDevice:s.inputDevice??"UNKNOWN",outputDevice:s.outputDevice??"UNKNOWN",
    inputMask:s.activeInputMaskHex,outputMask:s.activeOutputMaskHex,
    nativeFxGraphRevision:s.nativeFxGraphRevision,
    inputChannels:s.deviceInputChannels??null,
    outputChannels:s.deviceOutputChannels??null,
    sampleRate:s.rate,bufferFrames:s.buffer,periodUs:Math.round(period*10)/10,
    deviceGeneration:s.deviceGeneration,
    diagnosticRoute:s.diagnosticRoute??"UNKNOWN",
    productionRoutingState:s.productionRoutingState??"UNKNOWN",
    directRouteAuthorized:s.directRouteAuthorized===true,
    durationSeconds,intervals:segment.intervals,callbackCount:callbacks,
    windowHistogramSamples:p.samples,
    callbackTimingScope:scope,
    completeHistogramCoverage:completeCoverage,
    // Cumulative readback atomics are not a transactional snapshot; a
    // callback at either endpoint can contribute one sample of skew.
    histogramCoveragePct:percent(p.samples,callbacks),
    p95BudgetUpperPct:p.p95,p99BudgetUpperPct:p.p99,
    overBudgetCallbacks:over,overBudgetPer1000:callbacks?Math.round(100000*over/callbacks)/100:null,
    slowCallbacks:segment.delta.slowCallbackCount,
    lateCallbackIntervals:late,callbackGaps:gap,
    clockRegressions:segment.delta.callbackCadenceClockRegressions,
    deviceStarts:segment.delta.deviceStartCount,deviceStops:segment.delta.deviceStopCount,
    pluginWorstUsCumulative:last.worstPluginUs??null,
    processWorkingSetMiB:last.processWorkingSetMiB??null,
    processPrivateCommitMiB:last.processPrivateCommitMiB??null,
    processCpuAllCorePct:resourceSummary(segment.resources.cpu),
    workingSetMiB:resourceSummary(segment.resources.workingSet),
    privateCommitMiB:resourceSummary(segment.resources.privateCommit),
    backendXrunMeaning:s.backendXrunMeaning??"UNAVAILABLE",
    backendXrunDelta:nondecreasing(first,last,"backendXrunCount")?
      last.backendXrunCount-first.backendXrunCount:null,
    renderHardwareXrun:"UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE",
    stableLifecycle,
    diagnosis,comparable:!missing&&stableLifecycle&&durationSeconds>=30&&callbacks>0&&
      s.connected===true&&
      s.productionRoutingState==="NATIVE_ROUTE_ACCEPTED"&&
      s.directRouteAuthorized===true,
    notes:["Actual DRY/FX includes admitted external CH1 vendor leases and the accepted ChannelId insert-bypass readback.",
      "Actual DRY/FX is accepted native insert-bypass readback, NOT the dryTest UI flag.",
      "Host callback p95/p99 are bucket CEILINGS, not exact CPU utilization.",
      "Callback cadence/over-budget are indicators, not driver render XRUN.",
      "Device/driver capture discontinuities are backend-specific.",
      "Reported plugin worst time can predate this window; it is not an interval peak."]
  };
}

export function analyzeHealthLog(log) {
  const {samples,warnings,rejectedLines}=parseHealthLog(log);
  const groups=[];
  let segment=null,rejectedIndex=0;
  const start=(s,invalidBefore=false)=>{
    const segment={start:s,last:s,intervals:0,durationMs:0,
      delta:Object.fromEntries(COUNTERS.map(k=>[k,0])),
      histogram:Array(13).fill(0),invalidBefore,
      resources:Object.fromEntries(["cpu","workingSet","privateCommit"].map(k=>[k,{count:0,first:null,last:null,peak:null,sum:0}]))};
    recordResources(segment,s);return segment;
  };
  for(const current of samples){
    if(!segment){segment=start(current);continue;}
    // Rejected telemetry is an absolute comparison barrier, including a
    // failed opaque vendor lease. Only an uninterrupted *accepted* global
    // bypass ON -> OFF transition may connect a DRY/FX pair.
    // Rejections and samples are in source order. Visit each rejected
    // line once instead of rescanning the whole log on every interval.
    while(rejectedIndex<rejectedLines.length&&rejectedLines[rejectedIndex]<=segment.last.line)
      rejectedIndex++;
    const missingBetween=rejectedIndex<rejectedLines.length&&rejectedLines[rejectedIndex]<current.line;
    if(missingBetween || !validInterval(segment.last,current)){
      const acceptedPower=!missingBetween &&
        acceptedGlobalPowerTransition(segment.last,current);
      groups.push(describeSegment(segment));
      warnings.push("line "+current.line+": "+
        (missingBetween?"rejected AUDIO_HEALTH boundary; ":
         acceptedPower?"accepted global FX bypass transition; ":
         "device/graph/epoch/counter/timing discontinuity; ")+
        "comparison window split");
      segment=start(current,!acceptedPower);
      continue;
    }
    const previous=segment.last;
    for(const counter of COUNTERS)segment.delta[counter]+=current[counter]-previous[counter];
    for(let i=0;i<13;i++)segment.histogram[i]+=
      current.callbackBudgetBuckets[i]-previous.callbackBudgetBuckets[i];
    segment.durationMs+=current.atMs-previous.atMs;
    segment.intervals++;
    segment.last=current;
    recordResources(segment,current);
  }
  if(segment)groups.push(describeSegment(segment));
  return {schema:"arsonmix.int1b.profile-report.v1",
    source:"1Hz native AUDIO_HEALTH (local logs; NOT hardware FAT)",
    totalSamples:samples.length,segments:groups,warnings,
    renderHardwareXrun:"UNAVAILABLE",roundTripLatency:"NOT_MEASURED"};
}

export function compareDryFx(analysis){
  const ready=s=>s.comparable&&
    s.diagnosticRoute==="NORMAL"&&s.renderHardwareXrun==="UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE";
  // Pair only an immediately preceding accepted DRY window with the next
  // accepted FX window. Never compare an earlier app session, an unrelated
  // third run, or a hours-old source recording with today's loaded inserts.
  const fxIndex=analysis.segments.findLastIndex(s=>ready(s) &&
    s.mode==="FX" && s.nativeFxInstalledCount>0 && s.nativeFxProcessingCount>0);
  const fx=fxIndex>=0?analysis.segments[fxIndex]:null;
  const dry=fxIndex>0?analysis.segments[fxIndex-1]:null;
  if(!fx||!dry||!dry.comparable||fx.invalidBefore||dry.mode!=="DRY"||
     dry.diagnosticRoute!=="NORMAL")
    return {comparable:false,reason:"Need contiguous real NORMAL/DRY then NORMAL/FX windows, each >=30s."};
  if(dry.sessionId!==fx.sessionId ||
     fx.startTimeMs<dry.endTimeMs ||
     fx.startTimeMs-dry.endTimeMs>60000)
    return {comparable:false,reason:"DRY/FX windows belong to different app sessions or are more than 60s apart."};
  for(const key of ["backend","device","inputDevice","outputDevice",
                     "sessionId","deviceGeneration","nativeFxGraphRevision",
                     "inputMask","outputMask","inputChannels","outputChannels",
                     "sampleRate","bufferFrames","callbackTimingScope"]){
    if(dry[key]!==fx[key])return {comparable:false,reason:"Mismatched "+key+": do not compare unlike hardware profiles."};
  }
  return {comparable:true,baseline:"DRY",candidate:"FX",
    device:dry.device,sampleRate:dry.sampleRate,bufferFrames:dry.bufferFrames,
    dry:{p95:dry.p95BudgetUpperPct,p99:dry.p99BudgetUpperPct,
      overBudgetPer1000:dry.overBudgetPer1000,late:dry.lateCallbackIntervals},
    fx:{p95:fx.p95BudgetUpperPct,p99:fx.p99BudgetUpperPct,
      overBudgetPer1000:fx.overBudgetPer1000,late:fx.lateCallbackIntervals},
    deltaOverBudgetPer1000:Math.round(100*(fx.overBudgetPer1000-dry.overBudgetPer1000))/100,
    qualification:"HOST_TIMING_ONLY_NOT_PHYSICAL_XRUN_OR_ROUND_TRIP_FAT"};
}

export function markdownReport(analysis){
  const cmp=compareDryFx(analysis);
  const lines=["# ArSonMix INT-1B — Audio Reliability Profile","",
    "This is native host timing and optional process resource sampling, not physical render-XRUN, GPU or ASIO round-trip.",
    "Real hardware FAT and operator listening remain separately required.","",
    "| Mode | Driver | Rate/Frames | Seconds | p95/p99 budget ceiling | Over-budget/1000 | Late callback | RAM working set / private MiB | Observation |",
    "|---|---|---|---:|---:|---:|---:|---|"];
  for(const s of analysis.segments){
    const pct=x=>x===null?">100% / N/A":x+"%";
    lines.push("| "+s.mode+" | "+s.backend+" | "+s.sampleRate+"/"+s.bufferFrames+
      " | "+s.durationSeconds+" | "+pct(s.p95BudgetUpperPct)+"/"+pct(s.p99BudgetUpperPct)+
      " | "+(s.overBudgetPer1000??"N/A")+" | "+s.lateCallbackIntervals+
      " | "+(s.processWorkingSetMiB??"N/A")+" / "+(s.processPrivateCommitMiB??"N/A")+
      " | "+s.diagnosis+" |");
  }
  lines.push("","## Process resources (sampled, not realtime callback CPU)","",
    "| Mode | CPU mean / peak (all cores %) | CPU samples | RAM peak MiB | Private commit peak / change MiB |",
    "|---|---:|---:|---:|---:|");
  const fmt=x=>x===null||x===undefined?"N/A":String(x);
  for(const s of analysis.segments)
    lines.push("| "+s.mode+" | "+fmt(s.processCpuAllCorePct.mean)+" / "+fmt(s.processCpuAllCorePct.peak)+
      " | "+s.processCpuAllCorePct.samples+" | "+fmt(s.workingSetMiB.peak)+" | "+
      fmt(s.privateCommitMiB.peak)+" / "+fmt(s.privateCommitMiB.delta)+" |");
  lines.push("","Memory change is an observation, not proof of a leak.","",cmp.comparable?
    "**Matched actual native dry→FX comparison:** Δ over-budget per 1000 callbacks = "+cmp.deltaOverBudgetPer1000+
      "; **NOT** a hardware XRUN or causal DSP proof.":
    "**No valid matched dry/FX comparison:** "+cmp.reason);
  if(analysis.warnings.length)lines.push("","Data warnings: "+analysis.warnings.slice(0,12).join("; "));
  lines.push("","Only optimize the measured problematic path; keep primary ASIO direct and one PCM/FX graph.");
  return lines.join("\n")+"\n";
}

function main(args){
  if(!args.length||args.includes("--help")){
    process.stdout.write("Usage: node scripts/audio_reliability_profile.mjs audio-diagnostics.log [--json report.json] [--markdown report.md]\n");
    return args.includes("--help")?0:2;
  }
  const input=args[0];
  const opt=name=>{const i=args.indexOf(name);return i>=0?args[i+1]:null;};
  if(args.some((x,i)=>x.startsWith("--")&&
    (!["--json","--markdown"].includes(x)||!args[i+1])))
    throw Error("Invalid output option; use --json FILE or --markdown FILE");
  const result=analyzeHealthLog(readFileSync(input,"utf8"));
  if(result.totalSamples===0)throw Error("No valid INT-1B AUDIO_HEALTH snapshots; check schema/log rotation");
  const json=opt("--json"), md=opt("--markdown");
  if(json)writeFileSync(json,JSON.stringify({...result,dryFxComparison:compareDryFx(result)},null,2)+"\n");
  if(md)writeFileSync(md,markdownReport(result));
  if(!json&&!md)process.stdout.write(markdownReport(result));
  return 0;
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href){
  try {process.exitCode=main(process.argv.slice(2));}
  catch(err){console.error("INT1B_PROFILE_ERROR: "+err.message);process.exitCode=1;}
}
