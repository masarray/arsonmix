// SPDX-License-Identifier: AGPL-3.0-only
// UI-only controller; native DSP remains authoritative. No DOM or timer
// ownership: host calls tick on its control cadence and after final gesture.
export const defaults=Object.freeze({timeMs:320,feedback:.30,echo:.22,highPassHz:150,lowPassHz:5500,duck:.20,duckAttackMs:10,duckReleaseMs:180,mode:1,enabled:true});
const ranges={timeMs:[20,4000],feedback:[0,.9],echo:[0,1],highPassHz:[40,1000],lowPassHz:[1000,20000],duck:[0,1],duckAttackMs:[2,100],duckReleaseMs:[40,1000]};
const keys=Object.keys(defaults);
const validValue=(k,v)=>k==="enabled"?typeof v==="boolean":k==="mode"?Number.isInteger(v)&&v>=0&&v<=2:
  Object.hasOwn(ranges,k)&&Number.isFinite(v)&&((v===0&&(k==="highPassHz"||k==="lowPassHz"))||(v>=ranges[k][0]&&v<=ranges[k][1]));
export const valid=s=>!!s&&keys.every(k=>validValue(k,s[k]));
const clean=s=>Object.fromEntries(keys.map(k=>[k,s[k]]));
const same=(a,b)=>a===b||(typeof a==="number"&&typeof b==="number"&&Math.abs(a-b)<=1e-7*Math.max(1,Math.abs(a),Math.abs(b)));
const leaseKeys=["channel","instance","epoch","graph"];
const leaseValid=l=>!!l&&leaseKeys.every(k=>typeof l[k]==="string"&&/^[1-9][0-9]*$/.test(l[k])&&l[k].length<=20&&BigInt(l[k])<=18446744073709551615n);
const sameLease=(a,b)=>leaseValid(a)&&leaseValid(b)&&leaseKeys.every(k=>a[k]===b[k]);
const revisionValid=r=>Number.isInteger(r)&&r>=0&&r<=0xffffffff;
export class ParameterSync {
  constructor(send,{interval=34,timeout=3500}={}){
    this.send=send;this.interval=interval;this.timeout=timeout;this.close();
  }
  close(){this.lease=null;this.accepted=clean(defaults);this.desired=clean(defaults);this.dirty=new Map();this.gestures=new Set();this.clock=0;this.rev=0;this.next=0;this.flight=null;this.blocked="";this.lastSent=-Infinity;}
  open(snapshot){
    if(!leaseValid(snapshot?.lease)||!revisionValid(snapshot.revision)||!valid(snapshot.accepted))return false;
    // Reopening same instance cannot silently forget an unresolved edit.
    if(this.lease&&sameLease(this.lease,snapshot.lease))return this.snapshot(snapshot);
    this.close();this.lease={...snapshot.lease};this.rev=this.next=snapshot.revision;
    this.accepted=this.desired=clean(snapshot.accepted);this.desired={...this.accepted};return true;
  }
  begin(key){if(!this.lease||!keys.includes(key))return false;this.gestures.add(key);return true;}
  edit(key,value){
    if(!this.lease||!validValue(key,value))return false;
    if(!same(this.desired[key],value)){this.desired[key]=value;this.dirty.set(key,++this.clock);}
    return true;
  }
  preset(values){if(!this.lease||!valid(values))return false;for(const k of keys)this.edit(k,values[k]);return true;}
  end(key,now){this.gestures.delete(key);return this.tick(now,true);}
  // One in flight, latest desired is never discarded while waiting.
  tick(now,force=false){
    if(!this.lease||!Number.isFinite(now))return false;
    if(this.flight&&now-this.flight.sent>=this.timeout){this.blocked="ACK_TIMEOUT";return false;}
    if(this.flight||this.blocked||(!force&&now-this.lastSent<this.interval))return false;
    for(const [k] of this.dirty)if(same(this.desired[k],this.accepted[k]))this.dirty.delete(k);
    if(!this.dirty.size)return false;
    if(this.next===0xffffffff){this.blocked="REVISION_EXHAUSTED";return false;}
    const revision=++this.next,settings=clean(this.desired);
    const flight={revision,sent:now,versions:new Map(this.dirty)};
    this.flight=flight;this.lastSent=now;
    try{
      if(this.send({lease:{...this.lease},revision,settings})===false&&this.flight===flight){
        this.flight=null;this.blocked="QUEUE_REJECTED";return false;
      }
    }catch{if(this.flight===flight){this.flight=null;this.blocked="TRANSPORT_ERROR";}return false;}
    return true;
  }
  ack(reply,now){
    if(!sameLease(this.lease,reply?.lease)||!this.flight||reply.revision!==this.flight.revision)return false;
    if(reply.applied!==true||!valid(reply.accepted)){
      this.flight=null;this.blocked="DSP_REJECTED";return false;
    }
    const flight=this.flight;this.flight=null;this.blocked="";this.rev=reply.revision;
    this.accepted=clean(reply.accepted);
    for(const k of keys){
      const generation=this.dirty.get(k);
      if(generation!==undefined&&generation===flight.versions.get(k)){
        this.dirty.delete(k);
        if(!this.gestures.has(k))this.desired[k]=this.accepted[k];
      }else if(generation===undefined&&!this.gestures.has(k))this.desired[k]=this.accepted[k];
    }
    // Never wait until the next pointer-up to deliver the final value.
    this.tick(now,true);return true;
  }
  snapshot(s){
    if(!sameLease(this.lease,s?.lease)||!revisionValid(s.revision)||s.revision<this.rev||!valid(s.accepted))return false;
    // In-flight state may be newer than snapshot; only exact receipts resolve it.
    if(this.flight)return false;
    this.rev=s.revision;this.next=Math.max(this.next,this.rev);this.accepted=clean(s.accepted);
    for(const k of keys)if(!this.dirty.has(k)&&!this.gestures.has(k))this.desired[k]=this.accepted[k];
    return true;
  }
  // Called only after host proves queue drained or lease changed; never
  // pretend timeout means the audio thread did not apply the command.
  recover(snapshot,queueQuiescent){
    if(queueQuiescent!==true||!sameLease(this.lease,snapshot?.lease)||!revisionValid(snapshot.revision)||snapshot.revision<this.rev||!valid(snapshot.accepted))return false;
    this.flight=null;this.blocked="";return this.snapshot(snapshot);
  }
  stable(){return !!this.lease&&!this.flight&&!this.blocked&&!this.dirty.size&&!this.gestures.size;}
}
