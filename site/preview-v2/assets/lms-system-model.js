/* ArSonMix LMS logical DESIGN model. No PCM, no device writes, no fake ACK.
 * Portable CommonJS/browser API. Hardware mapping belongs to native control.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.ArSonMixLmsModel = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";
  const MIN_HZ = 20, MAX_HZ = 20000, SPEED_M_S = 343;
  const SIDES = Object.freeze(["L", "R"]);
  const MAX_WAYS = 16;
  const MAX_DEMO_PORTS = 32;
  const requireNumber = (v, lo, hi, label) => {
    if (typeof v !== "number" || !Number.isFinite(v) || v < lo || v > hi)
      throw new RangeError(label);
    return v;
  };
  const requireInteger = (v, lo, hi, label) => {
    requireNumber(v, lo, hi, label);
    if (!Number.isInteger(v)) throw new RangeError(label);
    return v;
  };
  const suffixes = n => {
    if (n === 1) return ["FULL"];
    if (n === 2) return ["LOW", "HIGH"];
    if (n === 3) return ["LOW", "MID", "HIGH"];
    if (n === 4) return ["SUB", "LOW", "MID", "HIGH"];
    return Array.from({length:n}, (_,i) => "WAY " + String(i+1).padStart(2,"0"));
  };
  function sectionsFor(ways, stereo) {
    requireInteger(ways, 1, MAX_WAYS, "ways");
    const names = suffixes(ways);
    return (stereo ? SIDES : ["M"]).flatMap(side =>
      names.map((band,i) => Object.freeze({
        id: side + ":" + (i+1),
        name: band + (side === "M" ? "" : " " + side),
        band, side, index:i
      }))
    );
  }
  function defaultCutoffs(ways) {
    if (ways === 1) return [];
    if (ways === 2) return [1600];
    if (ways === 3) return [120,2400];
    if (ways === 4) return [80,250,2400];
    return Array.from({length:ways-1}, (_,i) =>
      Math.round(40 * Math.pow(12000/40,(i+1)/ways)));
  }
  function validateCutoffs(cutoffs, ways) {
    if (!Array.isArray(cutoffs) || cutoffs.length !== ways-1)
      throw new RangeError("crossover boundary count");
    let previous = MIN_HZ;
    for (const value of cutoffs) {
      requireNumber(value, MIN_HZ, MAX_HZ, "crossover frequency");
      if (value <= previous*1.01 || value >= MAX_HZ)
        throw new RangeError("crossover bands must remain strictly ordered");
      previous = value;
    }
    return true;
  }
  function demoPorts(n) {
    requireInteger(n, 1, MAX_DEMO_PORTS, "demo port count");
    return Array.from({length:n}, (_,i) => "DEMO-" + String(i+1).padStart(2,"0"));
  }
  const defaultDsp = () => ({
    gainDb:0, delayMs:0, polarity:"normal", peq:[],
    muted:true, limiterEnabled:false, limiterThresholdDb:null
  });
  const defaultPosition = i => ({x:(i%2===0?-4.5:4.5), y:3+2*Math.floor(i/2)});
  function createSystem({ways=3,stereo=true,ports=[]}={}) {
    if (typeof stereo!=="boolean") throw new TypeError("stereo");
    requireInteger(ways,1,MAX_WAYS,"ways");
    if (!Array.isArray(ports) || ports.length>64 || ports.some(p=>typeof p!=="string" || !p.trim()))
      throw new TypeError("ports");
    if (new Set(ports).size !== ports.length) throw new Error("duplicate port identity");
    return {
      schema:"arsonmix.lms.draft.v1", revision:0, name:"Main PA",
      ways, stereo, cutoffsHz:defaultCutoffs(ways),
      sections:sectionsFor(ways,stereo), ports:ports.slice(),
      // Empty crosspoints are intentional: NEVER auto-connect to an unknown speaker.
      routes:{}, dsp:Object.fromEntries(ports.map(p=>[p,defaultDsp()])),
      positions:Object.fromEntries(ports.map((p,i)=>[p,defaultPosition(i)])),
      listener:{x:0,y:15}, applied:false
    };
  }
  function validateSystem(s) {
    if (!s || s.schema!=="arsonmix.lms.draft.v1" || s.applied!==false)
      throw new Error("not an offline LMS draft");
    requireInteger(s.ways,1,MAX_WAYS,"ways");
    validateCutoffs(s.cutoffsHz,s.ways);
    if (!Array.isArray(s.ports) || new Set(s.ports).size!==s.ports.length)
      throw new Error("invalid port list");
    const expected = sectionsFor(s.ways,s.stereo).map(x=>x.id);
    if (JSON.stringify(expected)!==JSON.stringify(s.sections.map(x=>x.id)))
      throw new Error("section identities inconsistent");
    for (const [port,section] of Object.entries(s.routes)) {
      if (!s.ports.includes(port)||!expected.includes(section))
        throw new Error("orphan output mapping");
    }
    return true;
  }
  function withWays(s,ways) {
    validateSystem(s);
    requireInteger(ways,1,MAX_WAYS,"ways");
    if (ways===s.ways) return s;
    // Section ordinal meaning changes: clearing ALL crosspoints is intentional.
    // Old acoustic protection settings must never silently migrate to a new driver.
    return {...s, revision:s.revision+1, ways,
      cutoffsHz:defaultCutoffs(ways), sections:sectionsFor(ways,s.stereo),
      routes:{}, dsp:Object.fromEntries(s.ports.map(p=>[p,defaultDsp()]))
    };
  }
  function withStereo(s,stereo) {
    validateSystem(s);
    if (typeof stereo!=="boolean") throw new TypeError("stereo");
    if (s.stereo===stereo) return s;
    return {...s, revision:s.revision+1, stereo,
      sections:sectionsFor(s.ways,stereo), routes:{},
      dsp:Object.fromEntries(s.ports.map(p=>[p,defaultDsp()]))};
  }
  function withCutoff(s,index,hz) {
    validateSystem(s);
    requireInteger(index,0,s.ways-2,"cutoff index");
    const next=s.cutoffsHz.slice();
    next[index]=requireNumber(hz,MIN_HZ,MAX_HZ,"crossover frequency");
    validateCutoffs(next,s.ways);
    return {...s, revision:s.revision+1, cutoffsHz:next};
  }
  function withPorts(s,ports) {
    validateSystem(s);
    if (!Array.isArray(ports) || ports.some(p=>typeof p!=="string" || !p.trim())
        || new Set(ports).size!==ports.length || ports.length>64)
      throw new TypeError("ports must have distinct stable identities");
    if (JSON.stringify(ports)===JSON.stringify(s.ports)) return s;
    // Device identity changed: any previous patch is now unsafe, even if
    // a previous ordinal happens to have the same visible number.
    return {...s, revision:s.revision+1, ports:ports.slice(), routes:{},
      dsp:Object.fromEntries(ports.map(p=>[p,defaultDsp()])),
      positions:Object.fromEntries(ports.map((p,i)=>[p,s.positions[p]||defaultPosition(i)]))
    };
  }
  function withRoute(s,port,section) {
    validateSystem(s);
    if (!s.ports.includes(port)) throw new RangeError("unknown output port");
    if (section!==null && !s.sections.some(x=>x.id===section))
      throw new RangeError("unknown crossover section");
    const routes={...s.routes};
    if (section===null || routes[port]===section) delete routes[port];
    else routes[port]=section; // one feed per physical output; fanout is allowed
    return {...s, revision:s.revision+1, routes};
  }
  function withDsp(s,port,patch) {
    validateSystem(s);
    if (!s.ports.includes(port) || !patch || typeof patch!=="object")
      throw new RangeError("unknown output DSP target");
    const allowed=new Set(["gainDb","delayMs","polarity","muted",
                          "limiterEnabled","limiterThresholdDb"]);
    for (const key of Object.keys(patch)) if (!allowed.has(key))
      throw new Error("unknown DSP property: "+key);
    const p={...s.dsp[port],...patch};
    requireNumber(p.gainDb,-60,6,"gainDb");
    requireNumber(p.delayMs,0,500,"delayMs");
    if (!["normal","invert"].includes(p.polarity)) throw new RangeError("polarity");
    if (typeof p.muted!=="boolean" || typeof p.limiterEnabled!=="boolean")
      throw new TypeError("DSP switches");
    if (p.limiterThresholdDb!==null)
      requireNumber(p.limiterThresholdDb,-60,0,"limiterThresholdDb");
    // No hardware safety claims: this offline draft NEVER authorizes output.
    return {...s, revision:s.revision+1, dsp:{...s.dsp,[port]:p}};
  }
  // Output DSP band editor. The browser never runs audio; parameters stay
  // in a typed disconnected draft pending native rate/port verification.
  function validateBand(b) {
    if (!b || typeof b !== "object" || typeof b.enabled !== "boolean")
      throw new TypeError("PEQ band identity");
    requireNumber(b.frequencyHz,20,20000,"PEQ frequency");
    requireNumber(b.q,0.1,18,"PEQ Q");
    requireNumber(b.gainDb,-18,18,"PEQ gain");
    return true;
  }
  function withPeqBand(s,port,index,patch) {
    validateSystem(s);
    if (!s.ports.includes(port)) throw new RangeError("unknown output DSP target");
    if (!Number.isInteger(index)||index<0||index>=8)throw new RangeError("PEQ band index");
    const before=s.dsp[port].peq;
    if (!Array.isArray(before)||index>before.length)throw new RangeError("PEQ gap");
    const allowed=["frequencyHz","q","gainDb","enabled"];
    if (!patch || Object.keys(patch).some(key=>!allowed.includes(key)))
      throw new Error("unknown PEQ property");
    const next=before.map(p=>({...p}));
    next[index]={frequencyHz:1000,q:0.70710678118,gainDb:0,enabled:true,
                 ...next[index],...patch};
    validateBand(next[index]);
    return {...s,revision:s.revision+1,dsp:{
      ...s.dsp,[port]:{...s.dsp[port],peq:next}
    }};
  }
  function withoutPeqBand(s,port,index) {
    validateSystem(s);
    if(!s.ports.includes(port))throw new RangeError("unknown output DSP target");
    const before=s.dsp[port].peq;
    requireInteger(index,0,before.length-1,"PEQ band index");
    return {...s,revision:s.revision+1,dsp:{
      ...s.dsp,[port]:{...s.dsp[port],peq:before.filter((_,i)=>i!==index)}
    }};
  }
  // C++ PreparedSpeakerSystem::Biquad::make is the authority. Mirror its
  // 2 x Butterworth LR4 magnitude here, not made-up rectangular "curves".
  // Preview always labels its ASSUMED 48kHz sample rate; not a measurement.
  function biquadMagnitude(hz,cutoff,high,sampleRate) {
    requireNumber(hz,20,sampleRate*.5-.01,"preview Hz");
    const omega=2*Math.PI*cutoff/sampleRate,c=Math.cos(omega);
    const alpha=Math.sin(omega)/(2*Math.SQRT1_2);
    const scale=1/(1+alpha);
    const b0=((high?1+c:1-c)*.5)*scale;
    const b1=(high?-(1+c):1-c)*scale,b2=b0;
    const a1=-2*c*scale,a2=(1-alpha)*scale;
    const theta=2*Math.PI*hz/sampleRate;
    const cos=Math.cos(theta),sin=Math.sin(theta);
    const real=b0+b1*cos+b2*Math.cos(2*theta);
    const imag=-b1*sin-b2*Math.sin(2*theta);
    const denR=1+a1*cos+a2*Math.cos(2*theta);
    const denI=-a1*sin-a2*Math.sin(2*theta);
    return Math.sqrt((real*real+imag*imag)/
      Math.max(1e-28,denR*denR+denI*denI));
  }
  function crossoverMagnitude(cutoffs,way,hz,sampleRate=48000) {
    if(!Array.isArray(cutoffs)||!Number.isInteger(way)||
       way<0||way>cutoffs.length||sampleRate<44100||sampleRate>192000||
       !Number.isFinite(hz)||hz<20||hz>=sampleRate*.5)
      throw new RangeError("crossover plot request");
    let magnitude=1;
    if(way>0){
      const one=biquadMagnitude(hz,cutoffs[way-1],true,sampleRate);
      magnitude*=one*one;
    }
    if(way<cutoffs.length){
      const one=biquadMagnitude(hz,cutoffs[way],false,sampleRate);
      magnitude*=one*one;
    }
    return Math.max(0,Math.min(1,magnitude));
  }
  function peqMagnitude(bands,hz,sampleRate=48000) {
    if(!Array.isArray(bands)||sampleRate<44100||sampleRate>192000||
       !Number.isFinite(hz)||hz<20||hz>=sampleRate*.5)
      throw new RangeError("PEQ plot request");
    let mag=1;
    for(const b of bands) {
      validateBand(b);
      if(!b.enabled)continue;
      if(b.frequencyHz>=sampleRate*.45)continue; // visual marker; native rejects
      const w=2*Math.PI*b.frequencyHz/sampleRate,c=Math.cos(w);
      const alpha=Math.sin(w)/(2*b.q),A=Math.pow(10,b.gainDb/40);
      const scale=1/(1+alpha/A),th=2*Math.PI*hz/sampleRate;
      const b0=(1+alpha*A)*scale,b1=-2*c*scale,b2=(1-alpha*A)*scale;
      const a1=b1,a2=(1-alpha/A)*scale;
      const cos=Math.cos(th),sin=Math.sin(th);
      const nr=b0+b1*cos+b2*Math.cos(2*th),ni=-b1*sin-b2*Math.sin(2*th);
      const dr=1+a1*cos+a2*Math.cos(2*th),di=-a1*sin-a2*Math.sin(2*th);
      mag*=Math.sqrt((nr*nr+ni*ni)/Math.max(1e-28,dr*dr+di*di));
    }
    return 20*Math.log10(Math.max(1e-10,mag));
  }
  function withSuggestedDelays(s) {
    validateSystem(s);
    const estimates=alignment(s);
    if(estimates.some(x=>x.additionalMs>500))throw new RangeError("alignment delay budget");
    const dsp={...s.dsp};
    for(const x of estimates)
      dsp[x.port]={...dsp[x.port],delayMs:Math.round(x.additionalMs*100)/100};
    return {...s,revision:s.revision+1,dsp};
  }
  function withPosition(s,port,x,y) {
    validateSystem(s);
    if (!s.ports.includes(port)) throw new RangeError("unknown speaker");
    requireNumber(x,-1000,1000,"speaker x");
    requireNumber(y,-1000,1000,"speaker y");
    return {...s, revision:s.revision+1,
      positions:{...s.positions,[port]:{x,y}}};
  }
  function withListener(s,x,y) {
    validateSystem(s);
    requireNumber(x,-1000,1000,"listener x");
    requireNumber(y,-1000,1000,"listener y");
    return {...s, revision:s.revision+1,listener:{x,y}};
  }
  function alignment(s,{speed=SPEED_M_S}={}) {
    validateSystem(s);
    requireNumber(speed,300,360,"sound speed");
    const distances=s.ports.map(p=>{
      const v=s.positions[p], dx=v.x-s.listener.x,dy=v.y-s.listener.y;
      return {port:p,distanceM:Math.hypot(dx,dy)};
    });
    const farthest=Math.max(0,...distances.map(d=>d.distanceM));
    return distances.map(d=>({...d,additionalMs:(farthest-d.distanceM)/speed*1000}));
  }
  return Object.freeze({
    MAX_WAYS,MAX_DEMO_PORTS,sectionsFor,defaultCutoffs,validateCutoffs,
    demoPorts,createSystem,validateSystem,withWays,withStereo,withCutoff,
    withPorts,withRoute,withDsp,withPeqBand,withoutPeqBand,
    crossoverMagnitude,peqMagnitude,withSuggestedDelays,
    withPosition,withListener,alignment
  });
});
