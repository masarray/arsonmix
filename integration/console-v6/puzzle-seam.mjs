// SPDX-License-Identifier: AGPL-3.0-only
// The single socket between ENGINE native producer, GUI, FAT. Control plane only.
// Not a PCM renderer, not JUCE device telemetry, not hardware qualification.
export const SOCKET_VERSION=6;
const MAX=(1n<<64n)-1n;
const fail=(path,msg)=>{throw new Error(path+': '+msg)};
const yes=(pred,path,msg)=>{if(!pred)fail(path,msg)};
export function uint64(value,path,{positive=false}={}) {
  yes(typeof value==='string'&&/^(0|[1-9][0-9]*)$/.test(value),path,'canonical decimal-string uint64 required');
  const n=BigInt(value);yes(n<=MAX&&(!positive||n!==0n),path,'uint64 overflow/zero');return n;
}
export function requestToken(id,path='requestId') {
  yes(typeof id==='string'&&id.length<=128&&/^[\x21-\x7e]+$/.test(id)&&
      /^[^\s:]+:[1-9][0-9]*$/.test(id),path,'unique-session:positive-sequence token required');
  return id;
}
const arr=(x,path)=>yes(Array.isArray(x),path,'array required');
const flag=(x,path)=>yes(typeof x==='boolean',path,'boolean required');
const uniq=(xs,path)=>yes(new Set(xs).size===xs.length,path,'duplicate value');
const pins=(xs,path)=>{arr(xs,path);uniq(xs,path);yes(xs.every(p=>Number.isSafeInteger(p)&&p>=0&&p<=255),path,'invalid physical pin');};
const roles=new Set(['INPUT','GROUP','FX_RETURN','MONITOR','MAIN','AUX']);
export function snapshot(s) {
  yes(s&&s.schemaVersion===SOCKET_VERSION,'snapshot','schemaVersion 6 required');
  flag(s.productionCallbackLive,'productionCallbackLive');
  yes(s.device&&s.graph&&s.telemetry,'snapshot','device, graph, telemetry required');
  const d=s.device,g=s.graph,c=g.capabilities;
  uint64(d.epoch,'device.epoch',{positive:true});
  uint64(g.revision,'graph.revision');uint64(g.rosterRevision,'graph.rosterRevision');
  flag(d.outputArmed,'device.outputArmed');
  pins(d.enabledInputs,'device.enabledInputs');pins(d.enabledOutputs,'device.enabledOutputs');
  arr(d.inputEndpoints,'device.inputEndpoints');arr(d.outputEndpoints,'device.outputEndpoints');
  for(const [type,eps,enabled] of [['in',d.inputEndpoints,d.enabledInputs],['out',d.outputEndpoints,d.enabledOutputs]]) {
    const ids=[];
    for(const [i,e] of eps.entries()){
      yes(e&&typeof e.id==='string'&&e.id===type+':'+e.port,'device.'+type+i,'stable portId missing');
      yes(Number.isInteger(e.port)&&enabled.includes(e.port),'device.'+type+i,'port not in enabled physical mask');
      yes(Number.isInteger(e.callback)&&e.callback>=0,'device.'+type+i,'invalid callback ordinal');
      yes(typeof e.label==='string'&&e.label.length>0,'device.'+type+i,'missing label');
      flag(e.active,'device.'+type+i+'.active');
      ids.push(e.id);
    }
    uniq(ids,'device.'+type+'.id');uniq(eps.map(e=>e.callback),'device.'+type+'.callback');
  }
  arr(g.channels,'graph.channels');arr(g.destinations,'graph.destinations');
  yes(c&&typeof c==='object','graph.capabilities','capabilities required');
  for(const k of ['callbackOwnsChannelIds','fullVendorFxMigration','showAndOutputParity','physicalCaptureMapped','dynamicChannels','groups','fxReturns','monitor','sendsOnFaders'])
    flag(c[k],'capabilities.'+k);
  arr(c.supportedOps,'capabilities.supportedOps');uniq(c.supportedOps,'capabilities.supportedOps');
  yes(c.supportedOps.every(op=>typeof op==='string'&&/^[A-Z_]+$/.test(op)),'supportedOps','invalid op token');
  if(s.productionCallbackLive)
    yes(c.callbackOwnsChannelIds&&c.fullVendorFxMigration&&c.showAndOutputParity&&c.physicalCaptureMapped,
      'snapshot','cannot mark LIVE before installed full-fidelity callback, plugin state and pins');
  else yes(g.channels.length===0&&c.supportedOps.length===0,'snapshot','G0 is fail closed: no fictitious ChannelId/ops');
  const idset=new Set(g.channels.map(ch=>ch.id));
  for(const [i,ch] of g.channels.entries()){
    const p='graph.channels['+i+']';uint64(ch.id,p+'.id',{positive:true});
    yes(roles.has(ch.role),p+'.role','unrecognized role');
    yes(['MONO','STEREO'].includes(ch.processingFormat),p+'.processingFormat','invalid format');
    yes(typeof ch.name==='string',p+'.name','missing name');
    arr(ch.inserts,p+'.inserts');arr(ch.sends,p+'.sends');
    if(ch.primaryOutId!==undefined&&ch.primaryOutId!==null){
      uint64(ch.primaryOutId,p+'.primaryOutId',{positive:true});
      yes(idset.has(ch.primaryOutId),p+'.primaryOutId','target does not exist');
    }
    if(ch.source){
      yes(ch.role==='INPUT',p+'.source','only INPUT owns physical capture');
      yes(typeof ch.source.portId==='string',p+'.source.portId','endpoint ID required');
      arr(ch.source.physicalPins,p+'.source.physicalPins');
      pins(ch.source.physicalPins,p+'.source.physicalPins');
      yes([1,2].includes(ch.source.physicalPins.length),p+'.source','mono or two DISTINCT stereo pins');
      yes(ch.source.physicalPins.every(x=>d.enabledInputs.includes(x)),p+'.source','pin absent from driver mask');
      yes(d.inputEndpoints.some(e=>e.id===ch.source.portId&&e.active),p+'.source','endpoint unavailable');
      // A physical mono source may intentionally drive stereo *processing*
      // format: do not equate source pin count with DSP channel width.
    }
    for(const send of ch.sends){
      uint64(send.targetId,p+'.send.targetId',{positive:true});
      yes(idset.has(send.targetId),p+'.send','missing target');
      yes(send.targetId!==ch.id,p+'.send','self feedback');
      yes(['PRE','POST'].includes(send.tap),p+'.send.tap','invalid send tap');
    }
  }
  uniq(g.channels.map(ch=>ch.id),'graph.channels.id');
  for(const d of g.destinations){
    uint64(d.id,'graph.destinations.id',{positive:true});
    yes(idset.has(d.id),'graph.destinations','must exist in accepted roster');
  }
  for(const ch of g.channels){
    if(ch.role==='GROUP')yes(c.groups,'group','missing native capability');
    if(ch.role==='FX_RETURN')yes(c.fxReturns,'fxReturn','missing native capability');
    if(ch.role==='MONITOR')yes(c.monitor,'monitor','missing native capability');
  }
  for(const k of ['rawCapturePresent','mainPcmPresent','monitorPcmPresent'])flag(s.telemetry[k],'telemetry.'+k);
  yes(typeof s.telemetry.rawCapturePeak==='number'&&Number.isFinite(s.telemetry.rawCapturePeak)&&s.telemetry.rawCapturePeak>=0,'telemetry.rawCapturePeak','finite nonnegative peak required');
  return s;
}
export function intent(x,accepted) {
  yes(x&&typeof x==='object','intent','object required');
  requestToken(x.requestId);
  for(const k of ['deviceEpoch','baseGraphRevision','baseRosterRevision'])
    uint64(x[k],'intent.'+k,{positive:k==='deviceEpoch'});
  yes(typeof x.operation==='string','intent.operation','operation required');
  if(x.targetChannelId!==undefined&&x.targetChannelId!==null)
    uint64(x.targetChannelId,'intent.targetChannelId',{positive:true});
  yes(x.payload&&typeof x.payload==='object'&&!Array.isArray(x.payload),'intent.payload','object required');
  if(accepted){
    snapshot(accepted);
    yes(x.deviceEpoch===accepted.device.epoch,'intent','stale device');
    yes(x.baseGraphRevision===accepted.graph.revision&&x.baseRosterRevision===accepted.graph.rosterRevision,'intent','stale accepted graph');
    yes(accepted.productionCallbackLive&&accepted.graph.capabilities.supportedOps.includes(x.operation),'intent','unsupported native op');
    if(x.targetChannelId!==undefined&&x.targetChannelId!==null)
      yes(accepted.graph.channels.some(c=>c.id===x.targetChannelId),'intent','channel not in accepted roster');
    if(x.operation==='SET_SOURCE')yes(
      accepted.device.inputEndpoints.some(e=>e.id===x.payload.portId&&e.active),
      'intent','selected input not enabled');
  }
  return x;
}
export function ack(a,pending,prior,updated=null) {
  yes(a&&['ACCEPTED','REJECTED','REQUIRES_DEVICE_CHANGE'].includes(a.status),'ack.status','invalid');
  yes(a.requestId===pending.requestId,'ack.requestId','not same operator action');
  uint64(a.deviceEpoch,'ack.deviceEpoch',{positive:true});
  uint64(a.graphRevision,'ack.graphRevision');uint64(a.rosterRevision,'ack.rosterRevision');
  yes(a.deviceEpoch===pending.deviceEpoch,'ack','unexpected device epoch');
  if(a.status==='ACCEPTED'){
    yes(updated!==null,'ack','accepted ACK MUST be followed by NEW native snapshot');
    snapshot(updated);
    yes(updated.device.epoch===a.deviceEpoch&&updated.graph.revision===a.graphRevision&&
      updated.graph.rosterRevision===a.rosterRevision,'ack','new accepted snapshot does not match ACK');
    yes(BigInt(updated.graph.revision)>=BigInt(prior.graph.revision),'ack','graph revision rolled back');
  } else {
    yes(typeof a.errorCode==='string'&&a.errorCode.length>0,'ack','rejection reason required');
    if(updated){
      snapshot(updated);
      yes(updated.device.epoch===prior.device.epoch&&
        updated.graph.revision===prior.graph.revision,'ack','rejected request replaced accepted audio graph');
    }
  }
  return a;
}
