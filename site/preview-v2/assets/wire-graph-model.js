/* ArSonMix WIRE-first reference control-plane reducer.
 * Pure deterministic graph transactions; NEVER a JS PCM/rendering engine.
 * Native authoritative graph will use the same transaction/ACK semantics.
 */
(function(root,factory){
 const api=factory();
 if(typeof module!=='undefined'&&module.exports)module.exports=api;
 else root.ArSonWireGraph=api;
})(typeof globalThis!=='undefined'?globalThis:this,function(){
 'use strict';
 const validId=id=>Number.isSafeInteger(id)&&id>0;
 const edgeKey=e=>e.sourceId+':'+e.destId;
 function validateCandidateGraph(channels,mainId,edges){
  if(!validId(mainId)||!Array.isArray(channels)||!Array.isArray(edges))
   return {ok:false,error:'INVALID_GRAPH'};
  const ids=new Set([mainId]),unique=new Set();
  for(const ch of channels){
   if(!validId(ch.id)||ids.has(ch.id))return {ok:false,error:'DUPLICATE_CHANNEL'};
   ids.add(ch.id);
  }
  const indegree=new Map([...ids].map(id=>[id,0])),adj=new Map([...ids].map(id=>[id,[]]));
  const edgeIds=new Set(),primarySources=new Set(),splitTargets=new Set();
  for(const e of edges){
   if(!validId(e.id)||edgeIds.has(e.id)||!ids.has(e.sourceId)||!ids.has(e.destId)||
      e.sourceId===mainId||e.sourceId===e.destId||
      e.enabled!==true||!Number.isFinite(e.levelDb)||e.levelDb < -60||e.levelDb > 6)
    return {ok:false,error:'INVALID_EDGE'};
   edgeIds.add(e.id);
   const key=edgeKey(e);
   if(unique.has(key))return {ok:false,error:'DUPLICATE_ROUTE'};
   unique.add(key);
   if(e.destId===mainId&&
      (e.mode!=='postFader'||e.kind!=='primary'||e.levelDb!==0))
    return {ok:false,error:'INVALID_MAIN_TAP'};
   if(e.kind==='primary'&&e.destId!==mainId&&e.mode!=='routeOnly')
    return {ok:false,error:'INVALID_PRIMARY_TAP'};
   if(e.kind==='split'&&!['splitPre','splitPost'].includes(e.mode))
    return {ok:false,error:'INVALID_SPLIT_TAP'};
   if(e.kind==='primary'){
    if(primarySources.has(e.sourceId))return {ok:false,error:'MULTIPLE_PRIMARY'};
    primarySources.add(e.sourceId);
   }
   if(e.kind==='split'){
    if(splitTargets.has(e.destId))return {ok:false,error:'MULTIPLE_INPUT_SPLIT'};
    splitTargets.add(e.destId);
   }
   adj.get(e.sourceId).push(e.destId);
   indegree.set(e.destId,indegree.get(e.destId)+1);
  }
  const queue=[...indegree].filter(([,count])=>count===0).map(([id])=>id);
  let visited=0;
  for(let i=0;i<queue.length;i++){
   const id=queue[i];visited++;
   for(const dest of adj.get(id)){
    const degree=indegree.get(dest)-1;
    indegree.set(dest,degree);
    if(degree===0)queue.push(dest);
   }
  }
  return visited===ids.size?{ok:true,order:queue}:{ok:false,error:'FEEDBACK_CYCLE'};
 }
 function applyWireTransaction(state,ops){
  if(!state||!Array.isArray(ops)||!ops.length)return {ok:false,error:'EMPTY_TRANSACTION'};
  const {channels,mainId}=state;
  let edges=state.edges.map(e=>({...e}));
  let nextId=state.nextId;
  if(!validId(nextId))return {ok:false,error:'INVALID_NEXT_ID'};
  for(const op of ops){
   if(!op||!validId(op.sourceId))return {ok:false,error:'INVALID_SOURCE'};
   const sourceId=op.sourceId;
   if(!channels.some(ch=>ch.id===sourceId))return {ok:false,error:'UNKNOWN_SOURCE'};
   let index,prior;
   if(op.type==='CONNECT'||op.type==='ROUTE_ONLY'){
    if(!validId(op.destId)||op.destId===sourceId)return {ok:false,error:'INVALID_DESTINATION'};
    if(op.type==='ROUTE_ONLY'){
     if(op.destId===mainId)return {ok:false,error:'INVALID_ROUTE_ONLY'};
     const existing=edges.find(e=>e.sourceId===sourceId&&e.destId===op.destId);
     edges=edges.filter(e=>!(e.sourceId===sourceId&&
      (e.destId===mainId||e.kind==='primary'||e.destId===op.destId)));
     edges.push({id:existing?.id??nextId++,sourceId,destId:op.destId,enabled:true,
      kind:'primary',mode:'routeOnly',levelDb:0});
    }else{
     if(edges.some(e=>e.sourceId===sourceId&&e.destId===op.destId))
      return {ok:false,error:'DUPLICATE_ROUTE'};
     const isMain=op.destId===mainId;
     const isSplit=op.mode==='splitPre'||op.mode==='splitPost';
     edges.push({id:nextId++,sourceId,destId:op.destId,enabled:true,
      kind:isMain?'primary':isSplit?'split':'send',
      mode:isMain?'postFader':isSplit?op.mode:'send',
      levelDb:0});
    }
   }else if(op.type==='REMOVE'||op.type==='RETARGET'||op.type==='GAIN'){
    index=edges.findIndex(e=>e.id===op.edgeId&&e.sourceId===sourceId);
    if(index<0)return {ok:false,error:'STALE_EDGE'};
    prior=edges[index];
    if(op.type==='REMOVE')edges.splice(index,1);
    if(op.type==='RETARGET'){
     if(!validId(op.destId)||op.destId===sourceId)return {ok:false,error:'INVALID_DESTINATION'};
     if(prior.kind==='split'&&op.destId===mainId)
      return {ok:false,error:'INVALID_SPLIT_DESTINATION'};
     if(edges.some(e=>e.id!==prior.id&&e.sourceId===sourceId&&e.destId===op.destId))
      return {ok:false,error:'DUPLICATE_ROUTE'};
     const main=op.destId===mainId;
     edges[index]={...prior,destId:op.destId,kind:main?'primary':prior.kind==='primary'?'send':prior.kind,
      mode:main?'postFader':prior.kind==='primary'?'send':prior.mode,
      levelDb:main?0:prior.levelDb};
    }
    if(op.type==='GAIN'){
     if(prior.destId===mainId||!Number.isFinite(op.levelDb))return {ok:false,error:'INVALID_GAIN'};
     edges[index]={...prior,levelDb:Math.round(Math.max(-60,Math.min(6,op.levelDb))*10)/10};
    }
   }else return {ok:false,error:'UNKNOWN_OPERATION'};
  }
  const outcome=validateCandidateGraph(channels,mainId,edges);
  return outcome.ok?{ok:true,edges,nextId,order:outcome.order}:outcome;
 }
 function migrateLegacy(channels,mainId,existingEdges=[],nextId=1,{graphVersion=0}={}){
  if(graphVersion===1){
   const status=validateCandidateGraph(channels,mainId,existingEdges);
   return status.ok?{ok:true,edges:existingEdges.map(e=>({...e})),
    nextId:Math.max(nextId,1,...existingEdges.map(e=>e.id+1))}:status;
  }
  const edges=existingEdges.map(e=>({...e})),seen=new Set(edges.map(edgeKey));
  nextId=Math.max(nextId,1,...edges.map(e=>e.id+1));
  const names=new Map(channels.map(c=>[c.name,c.id]));
  const add=(src,dst,kind='send',mode='send',levelDb=0)=>{
   if(!validId(src)||!validId(dst)||src===dst||seen.has(src+':'+dst))return;
   seen.add(src+':'+dst);
   edges.push({id:nextId++,sourceId:src,destId:dst,kind,mode,levelDb,enabled:true});
  };
  for(const t of channels){
   const out=t.output==='Master'?mainId:names.get(t.output);
   if(out)add(t.id,out,'primary',out===mainId?'postFader':'routeOnly');
   if(Array.isArray(t.sendSlots))for(let i=0;i<t.sendSlots.length;i++){
    const slot=i===t.activeSend?t:t.sendSlots[i];
    if(slot.sendOn&&names.has(slot.send))add(t.id,names.get(slot.send),'send','send',
     Math.max(-60,Math.min(6,Number(slot.sendLevel)||0)));
   }
   const split=/^CH(\d+) (PRE|POST)-FX SPLIT$/.exec(String(t.input||''));
   if(split)add(Number(split[1]),t.id,'split',split[2]==='PRE'?'splitPre':'splitPost');
  }
  const valid=validateCandidateGraph(channels,mainId,edges);
  return valid.ok?{ok:true,edges,nextId}:valid;
 }
 return Object.freeze({validateCandidateGraph,applyWireTransaction,migrateLegacy});
});
