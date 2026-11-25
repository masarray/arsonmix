// SPDX-License-Identifier: AGPL-3.0-only
import {valid} from './editor.mjs';
const fields=['channelId','deviceEpoch','graphRevision','instanceId','slot'];
export const sameIdentity=(a,b)=>!!a&&!!b&&fields.every(k=>a[k]===b[k])&&
 (a.workspaceToken||'')===(b.workspaceToken||'');
let globalSequence=0;
const nextRequest=()=>String(globalSequence=Math.max(globalSequence+1,Date.now()*1000));
// One transport per open panel. The caller supplies the CURRENT accepted
// graph identity; responses must match both it and the request's identity.
export function nativeTransport(backend,context,clock=()=>Date.now()){
 let listener=()=>{},reading=null,writing=null,closed=false,lastId='',lastSerial='',seenAt=0,receipt=null,lastGoodAt=null,lastContext=null;
 const off=[];
 function attach(event,fn){const token=backend.addEventListener(event,fn);off.push(()=>backend.removeEventListener?.(token));}
 function unavailable(message){reading=null;writing=null;receipt=null;listener({kind:'unavailable',error:message});}
 function identity(){return context();}
 function matches(m,job){return !!job&&m?.requestId===job.requestId&&sameIdentity(m,job.context)&&sameIdentity(identity(),job.context);}
 function finish(m,applied){
  const job=writing;if(!job)return;writing=null;
  if(valid(m.settings)){
   listener({kind:'ack',id:job.id,requestId:job.requestId,applied,settings:m.settings,error:m.errorCode});
  }else listener({kind:'failure',id:job.id,error:m.errorCode||'Native state changed · reopen FX'});
 }
 attach('arMixBuiltinAntiNoizSnapshot',m=>{
  if(closed||!matches(m,reading))return;
  reading=null;
  if(m.status!=='ACCEPTED'||!valid(m.settings)){unavailable(m.errorCode);return;}
  lastGoodAt=clock();lastContext=reading?.context||identity();
  const o=m.observation;
  let observation=null;
  if(o&&typeof o.serial==='string'&&/^\d+$/.test(o.serial)&&
    typeof o.profile==='boolean'&&typeof o.learning==='boolean'&&typeof o.rackBypassed==='boolean'&&
    ['Idle','Learning','Accepted','Rejected','Cancelled'].includes(o.learnStatus)&&
    ['Auto','Voice','Singing','Music'].includes(o.effectiveMode)&&
    [256,512].includes(o.latency)&&[o.spectralGain,o.residualGain].every(x=>Number.isFinite(x)&&x>=0&&x<=1)){
   if(o.serial!=='0'&&o.serial!==lastSerial){lastSerial=o.serial;seenAt=clock();}
   observation={...o,live:o.serial!=='0'&&clock()-seenAt<650};
  }
  if(receipt&&writing){const accepted=receipt;receipt=null;finish({...m,errorCode:accepted.errorCode},accepted.status==='APPLIED');}
  listener({kind:'snapshot',settings:m.settings,observation});
 });
 attach('arMixBuiltinAntiNoizQueue',m=>{
  if(closed||!matches(m,writing)||m.status!=='REJECTED')return;
  finish(m,false);read();
 });
 attach('arMixBuiltinAntiNoizAck',m=>{
  if(closed||!matches(m,writing))return;
  // Live ACK has accepted settings; logical save ACK must be followed
  // by a persisted readback, never repaint from a submitted JS draft.
  if(m.status==='APPLIED'&&!valid(m.settings)){receipt=m;reading=null;read();return;}
  finish(m,m.status==='APPLIED');reading=null;read();
 });
 function read(){
  if(closed)return;
  const c=identity();
  if(!c){unavailable('Native FX identity changed · reopen FX Rack');return;}
  if(lastContext&&!sameIdentity(lastContext,c)&&!writing){
   lastSerial='';lastGoodAt=null;lastContext=null;
   unavailable('Native identity updated · reading DSP');
  }
  if(writing&&!sameIdentity(c,writing.context)){unavailable('Native FX identity changed · reopen FX Rack');return;}
  const now=clock();
  if(lastGoodAt!==null&&now-lastGoodAt>1500&&!writing){
   lastGoodAt=null;unavailable('Native DSP readback unavailable');
  }
  if(writing&&now-writing.started>4500){const job=writing;writing=null;receipt=null;listener({kind:'failure',id:job.id,error:'DSP ACK timeout · outcome unknown'});}
  if(reading&&sameIdentity(c,reading.context)&&now-reading.started<750)return;
  reading={requestId:'ansnap-'+nextRequest(),context:c,started:now};
  backend.emitEvent('arMixAction',{action:'nativeAntiNoizSnapshot',...c,requestId:reading.requestId});
 }
 return {read,subscribe(fn){listener=fn;return()=>{closed=true;for(const fn of off)fn();};},
  lastRequestId:()=>lastId,
  send(c){
   const target=identity();if(closed||!target||writing)throw Error('Native lease unavailable');
   if(c.kind==='learn'&&target.deviceEpoch==='0')throw Error('Learn requires live audio');
   const requestId='an-'+nextRequest();lastId=requestId;
   writing={id:c.id,context:target,requestId,started:clock()};
   try{backend.emitEvent('arMixAction',{action:c.kind==='learn'?'nativeAntiNoizLearn':'nativeAntiNoizSet',
    ...target,requestId,settings:c.settings});}catch(e){writing=null;throw e;}
  }};
}
