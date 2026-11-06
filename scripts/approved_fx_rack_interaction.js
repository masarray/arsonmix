// Shared by the one assembled /preview/ and its native bundle.
// Presentation follows the pointer; only accepted native readback moves DSP.
let fxPointerDrag=null,fxPointerFrame=0,fxPointerSuppressUntil=0,fxTransferUndo=null;
function clearFxPointer(){
 if(fxPointerFrame)cancelAnimationFrame(fxPointerFrame);fxPointerFrame=0;
 fxPointerDrag?.ghost?.remove();fxPointerDrag?.marker?.remove();
 document.querySelectorAll('.fx-moving').forEach(e=>e.classList.remove('fx-moving'));
 fxPointerDrag=null;
}
function removeFxReference(t,slot){
 ensureFxInsertIds(t);
 if(!Number.isInteger(slot)||slot<0||t.fx[slot]==='None')return false;
 for(const key of ['fx','slotBypass','fxInsertIds']){
  t[key].splice(slot,1);t[key].push(key==='fx'?'None':key==='slotBypass'?false:null);
 }
 fxReorderUndo=null;fxTransferUndo=null;refreshStrip(t);return true;
}
function undoFxTransfer(t){
 const u=fxTransferUndo;if(!u||window.__JUCE__?.backend||![u.from.id,u.to.id].includes(t.id))return false;
 if(fxOrderSignature(u.from)!==u.afterFrom||fxOrderSignature(u.to)!==u.afterTo)return false;
 for(const [track,before] of [[u.from,u.beforeFrom],[u.to,u.beforeTo]])
  for(const key of ['fx','slotBypass','fxInsertIds'])track[key]=before[key];
 fxTransferUndo=null;refreshStrip(u.from);refreshStrip(u.to);return true;
}
function moveFxBetween(from,slot,to,position,token){
 ensureFxInsertIds(from);ensureFxInsertIds(to);
 if(from.fxInsertIds[slot]?.id!==token||from.fx[slot]==='None')return false;
 if(window.__JUCE__?.backend)
  return !!window.ArSonFxRackAuthority?.move?.(from.id,slot,to.id,position,token);
 if(from===to)return moveReferenceFx(from,slot,position);
 const count=to.fx.filter(x=>x!=='None').length;
 if(count>=8||position<0||position>count)return false;
 const save=t=>({fx:t.fx.slice(),slotBypass:t.slotBypass.slice(),fxInsertIds:clone(t.fxInsertIds)});
 const beforeFrom=save(from),beforeTo=save(to);
 for(const key of ['fx','slotBypass','fxInsertIds']){
  const value=from[key].splice(slot,1)[0];from[key].push(key==='fx'?'None':key==='slotBypass'?false:null);
  to[key].splice(position,0,value);to[key].pop();
 }
 fxTransferUndo={from,to,beforeFrom,beforeTo,afterFrom:fxOrderSignature(from),afterTo:fxOrderSignature(to)};
 refreshStrip(from);refreshStrip(to);say(from.name+' → '+to.name+' · FX moved');return true;
}
function paintFxPointer(){
 fxPointerFrame=0;const g=fxPointerDrag;if(!g?.active)return;
 const hit=document.elementFromPoint(g.x,g.y),rack=hit?.closest?.('.fx-section'),track=rack&&findTrack(rack);
 let drop=null,mark=null;
 if(track){
  const rows=[...rack.querySelectorAll('.fxslot-row:not(.is-empty)')];
  const rect=rack.getBoundingClientRect();let position=rows.length,y=rows.at(-1)?.getBoundingClientRect().bottom||rect.top+25;
  for(let i=0;i<rows.length;i++){
   const box=rows[i].getBoundingClientRect();
   if(g.y<box.top+box.height/2){position=i;y=box.top;break;}
  }
  const final=track.id===g.sourceId&&position>g.slot?position-1:position;
  if(rows.length<8||track.id===g.sourceId){drop={id:track.id,position:final};mark={left:rect.left+3,top:y,width:rect.width-6};}
  const list=rack.querySelector('.fxslot-list'),box=list?.getBoundingClientRect();
  if(list&&box&&list.scrollHeight>list.clientHeight)
   list.scrollTop+=g.y<box.top+18?-4:g.y>box.bottom-18?4:0;
 }
 g.drop=drop;g.ghost.style.transform='translate3d('+(g.x-g.offsetX)+'px,'+(g.y-g.offsetY)+'px,0)';
 g.ghost.classList.toggle('fx-drop-invalid',!drop);
 g.marker.hidden=!mark;
 if(mark){g.marker.style.left=mark.left+'px';g.marker.style.top=mark.top+'px';g.marker.style.width=mark.width+'px';}
 fxPointerFrame=requestAnimationFrame(paintFxPointer);
}
document.addEventListener('pointerdown',e=>{
 if(e.button!==0)return;
 const handle=e.target.closest?.('.fx-grip,.fx-pick:not(.empty)'),row=handle?.closest('.fxslot-row:not(.is-empty)'),t=row&&findTrack(row);
 if(!row||!t)return;ensureFxInsertIds(t);
 const slot=Number(row.dataset.slot),token=t.fxInsertIds[slot]?.id;if(!token)return;
 clearFxPointer();const r=row.getBoundingClientRect();
 fxPointerDrag={pointer:e.pointerId,sourceId:t.id,slot,token,row,x:e.clientX,y:e.clientY,startX:e.clientX,startY:e.clientY,
  offsetX:e.clientX-r.left,offsetY:e.clientY-r.top,width:r.width,active:false};
 if(handle.classList.contains('fx-grip'))e.preventDefault();
},true);
window.addEventListener('pointermove',e=>{
 const g=fxPointerDrag;if(!g||e.pointerId!==g.pointer)return;
 g.x=e.clientX;g.y=e.clientY;
 if(!g.active&&Math.hypot(g.x-g.startX,g.y-g.startY)>=5){
  g.active=true;g.row.classList.add('fx-moving');
  g.ghost=g.row.cloneNode(true);g.ghost.classList.remove('fx-moving');g.ghost.classList.add('fx-drag-ghost');g.ghost.style.width=g.width+'px';
  const originals=[g.row,...g.row.querySelectorAll('*')],copies=[g.ghost,...g.ghost.querySelectorAll('*')];
  for(let i=0;i<copies.length;i++){
   const css=getComputedStyle(originals[i]);
   for(const prop of ['font','color','background-color','height','min-height','border','padding','width','min-width','flex','box-sizing','display','align-items','justify-content','gap'])
    copies[i].style.setProperty(prop,css.getPropertyValue(prop));
  }
  g.ghost.style.backgroundColor=getComputedStyle(g.row.closest('.fx-section')).backgroundColor;
  g.ghost.setAttribute('aria-hidden','true');
  g.ghost.querySelectorAll('button,input').forEach(n=>{n.tabIndex=-1;n.removeAttribute('draggable');});
  g.marker=document.createElement('div');g.marker.className='fx-drop-marker';g.marker.hidden=true;
  document.body.append(g.ghost,g.marker);fxPointerFrame=requestAnimationFrame(paintFxPointer);
 }
 if(g.active)e.preventDefault();
},true);
window.addEventListener('pointerup',e=>{
 const g=fxPointerDrag;if(!g||e.pointerId!==g.pointer)return;
 if(g.active){
  // Resolve the release point even when pointerup preceded the next RAF.
  g.x=e.clientX;g.y=e.clientY;
  if(fxPointerFrame)cancelAnimationFrame(fxPointerFrame);fxPointerFrame=0;paintFxPointer();
  fxPointerSuppressUntil=performance.now()+350;
  const from=tracks.concat(master).find(t=>t.id===g.sourceId),to=tracks.concat(master).find(t=>t.id===g.drop?.id);
  if(from&&to)moveFxBetween(from,g.slot,to,g.drop.position,g.token);
  e.preventDefault();
 }
 clearFxPointer();
},true);
window.addEventListener('pointercancel',clearFxPointer,true);
window.addEventListener('blur',clearFxPointer);
document.addEventListener('dragstart',e=>{
 if(e.target.closest?.('.fx-grip,.fx-pick')){e.preventDefault();e.stopImmediatePropagation();}
},true);
document.addEventListener('click',e=>{
 if(performance.now()<fxPointerSuppressUntil&&e.target.closest?.('.fx-section')){
  e.preventDefault();e.stopImmediatePropagation();
 }
},true);
document.addEventListener('keydown',e=>{
 if(e.key==='Escape'&&fxPointerDrag){clearFxPointer();e.preventDefault();e.stopPropagation();}
},true);
