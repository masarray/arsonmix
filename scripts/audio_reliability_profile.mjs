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
  const samples=[], warnings=[];
  for(const [idx,line] of String(log).split(/\r?\n/).entries()){
    if(!line.includes(" | AUDIO_HEALTH | "))continue;
    const m=/^(\S+) \| AUDIO_HEALTH \| (\{.*\})$/.exec(line);
    if(!m){warnings.push("line "+(idx+1)+": malformed/truncated AUDIO_HEALTH");continue;}
    let sample;
    try {sample=JSON.parse(m[2]);}
    catch {warnings.push("line "+(idx+1)+": invalid AUDIO_HEALTH JSON");continue;}
    if(sample.schema!==SCHEMA){
      warnings.push("line "+(idx+1)+": legacy/unknown schema (excluded)");
      continue;
    }
    const time=Date.parse(m[1]);
    if(!Number.isFinite(time)||!finitePositive(sample.rate)||
       !isCount(sample.buffer)||sample.buffer===0||
       !isCount(sample.deviceGeneration)||!isCount(sample.callbackCount)||
       typeof sample.liveBackendClass!=="string" ||
       typeof sample.dryTest!=="boolean" || typeof sample.connected!=="boolean"){
      warnings.push("line "+(idx+1)+": invalid source metadata/counters");
      continue;
    }
    const buckets=sample.callbackBudgetBuckets;
    if(!Array.isArray(buckets)||buckets.length!==13||!buckets.every(isCount)){
      warnings.push("line "+(idx+1)+": missing/invalid 13 histogram buckets");
      continue;
    }
    if(!COUNTERS.every(key=>isCount(sample[key]))){
      warnings.push("line "+(idx+1)+": missing/invalid cumulative counter");
      continue;
    }
    samples.push({...sample,atMs:time,line:idx+1});
  }
  return {samples,warnings};
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

const sameScope=(a,b)=>[
  "deviceGeneration","device","liveBackendClass","driver","rate","buffer",
  "dryTest","diagnosticRoute","productionRoutingState","directRouteAuthorized",
  "backendXrunMeaning"
].every(k=>a[k]===b[k]);
const nondecreasing=(a,b,key)=>isCount(a[key])&&isCount(b[key])&&b[key]>=a[key];
const validInterval=(a,b)=>b.atMs>a.atMs && b.atMs-a.atMs<=5000 &&
  sameScope(a,b)&&COUNTERS.every(k=>nondecreasing(a,b,k))&&
  a.callbackBudgetBuckets.every((x,i)=>b.callbackBudgetBuckets[i]>=x);

function describeSegment(segment){
  const s=segment.start, first=segment.start, last=segment.last;
  const p=p95p99FromDelta(segment.histogram);
  const callbacks=segment.delta.callbackCount, period=1000000*s.buffer/s.rate;
  const durationSeconds=Math.round(segment.durationMs/100)/10;
  const missing=segment.intervals===0||p.samples===0;
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
    lineStart:first.line,lineEnd:last.line,mode:s.dryTest?"DRY":"FX",
    device:String(s.device??"UNKNOWN"),backend:s.liveBackendClass,
    sampleRate:s.rate,bufferFrames:s.buffer,periodUs:Math.round(period*10)/10,
    deviceGeneration:s.deviceGeneration,
    diagnosticRoute:s.diagnosticRoute??"UNKNOWN",
    productionRoutingState:s.productionRoutingState??"UNKNOWN",
    directRouteAuthorized:s.directRouteAuthorized===true,
    durationSeconds,intervals:segment.intervals,callbackCount:callbacks,
    windowHistogramSamples:p.samples,
    p95BudgetUpperPct:p.p95,p99BudgetUpperPct:p.p99,
    overBudgetCallbacks:over,overBudgetPer1000:callbacks?Math.round(100000*over/callbacks)/100:null,
    slowCallbacks:segment.delta.slowCallbackCount,
    lateCallbackIntervals:late,callbackGaps:gap,
    clockRegressions:segment.delta.callbackCadenceClockRegressions,
    deviceStarts:segment.delta.deviceStartCount,deviceStops:segment.delta.deviceStopCount,
    pluginWorstUsCumulative:last.worstPluginUs??null,
    backendXrunMeaning:s.backendXrunMeaning??"UNAVAILABLE",
    backendXrunDelta:nondecreasing(first,last,"backendXrunCount")?
      last.backendXrunCount-first.backendXrunCount:null,
    renderHardwareXrun:"UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE",
    diagnosis,comparable:!missing&&durationSeconds>=30&&callbacks>0&&
      segment.invalidTransitions===0&&s.connected===true&&
      s.productionRoutingState==="NATIVE_ROUTE_ACCEPTED"&&
      s.directRouteAuthorized===true,
    notes:["Host callback p95/p99 are bucket CEILINGS, not exact CPU utilization.",
      "Callback cadence/over-budget are indicators, not driver render XRUN.",
      "Device/driver capture discontinuities are backend-specific.",
      "Reported plugin worst time can predate this window; it is not an interval peak."]
  };
}

export function analyzeHealthLog(log) {
  const {samples,warnings}=parseHealthLog(log);
  const groups=[];
  let segment=null;
  const start=s=>({start:s,last:s,intervals:0,durationMs:0,
    delta:Object.fromEntries(COUNTERS.map(k=>[k,0])),
    histogram:Array(13).fill(0),invalidTransitions:0});
  for(const current of samples){
    if(!segment){segment=start(current);continue;}
    if(!validInterval(segment.last,current)){
      groups.push(describeSegment(segment));
      warnings.push("line "+current.line+": scope/device epoch, counter reset or >5s log gap; comparison window split");
      segment=start(current);
      continue;
    }
    const previous=segment.last;
    for(const counter of COUNTERS)segment.delta[counter]+=current[counter]-previous[counter];
    for(let i=0;i<13;i++)segment.histogram[i]+=
      current.callbackBudgetBuckets[i]-previous.callbackBudgetBuckets[i];
    segment.durationMs+=current.atMs-previous.atMs;
    segment.intervals++;
    segment.last=current;
  }
  if(segment)groups.push(describeSegment(segment));
  return {schema:"arsonmix.int1b.profile-report.v1",
    source:"1Hz native AUDIO_HEALTH (local logs; NOT hardware FAT)",
    totalSamples:samples.length,segments:groups,warnings,
    renderHardwareXrun:"UNAVAILABLE",roundTripLatency:"NOT_MEASURED"};
}

export function compareDryFx(analysis){
  const ready=analysis.segments.filter(s=>s.comparable&&
    s.diagnosticRoute==="NORMAL"&&s.renderHardwareXrun==="UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE");
  const dry=[...ready].reverse().find(s=>s.mode==="DRY");
  const fx=[...ready].reverse().find(s=>s.mode==="FX");
  if(!dry||!fx)return {comparable:false,reason:"Need >=30s each of real accepted NORMAL/DRY and NORMAL/FX under the same device."};
  for(const key of ["backend","device","sampleRate","bufferFrames"]){
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
    "This is native host timing, not physical render-XRUN, GPU, ASIO round-trip or CPU profiling.",
    "Real hardware FAT and operator listening remain separately required.","",
    "| Mode | Driver | Rate/Frames | Seconds | p95/p99 budget ceiling | Over-budget/1000 | Late callback | Observation |",
    "|---|---|---|---:|---:|---:|---:|---|"];
  for(const s of analysis.segments){
    const pct=x=>x===null?">100% / N/A":x+"%";
    lines.push("| "+s.mode+" | "+s.backend+" | "+s.sampleRate+"/"+s.bufferFrames+
      " | "+s.durationSeconds+" | "+pct(s.p95BudgetUpperPct)+"/"+pct(s.p99BudgetUpperPct)+
      " | "+(s.overBudgetPer1000??"N/A")+" | "+s.lateCallbackIntervals+
      " | "+s.diagnosis+" |");
  }
  lines.push("",cmp.comparable?
    "**Matched dry→FX comparison:** Δ over-budget per 1000 callbacks = "+cmp.deltaOverBudgetPer1000+
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
