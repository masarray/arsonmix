// SPDX-License-Identifier: AGPL-3.0-only
// ArSon Player: ONE canonical approved /preview/ GUI; zero browser audio.
(() => {
 "use strict";
 const $=id=>document.getElementById(id);
 const root=$("arsonPlayer"), toggle=$("playerToggle");
 if(!root||!toggle)return;
 const backend=window.__JUCE__?.backend;
 const native=Boolean(backend?.emitEvent&&backend?.addEventListener);
 let model={tracks:[],setlists:[],queue:[],activeSetlist:0,
   selected:-1,current:-1,autoNext:false,
   transport:{state:"EMPTY",title:"",currentSeconds:0,durationSeconds:0},
   error:""};
 const send=(action,value)=>{
   if(native)backend.emitEvent("arMixAction",{action,value});
   else showFeedback("Playback is available in the ArSonMix desktop app",true);
 };
 const showFeedback=(msg,error=false)=>{
   const output=$("playerFeedback");
   output.textContent=msg||"";
   output.dataset.error=error?"true":"false";
 };
 const duration=(seconds)=>{
   const v=Number.isFinite(+seconds)?Math.max(0,Math.floor(+seconds)):0;
   return String(Math.floor(v/60)).padStart(2,"0")+":"+
     String(v%60).padStart(2,"0");
 };
 const open=visible=>{
   root.hidden=!visible;
   toggle.setAttribute("aria-expanded",String(visible));
   if(visible) $("playerSearch").focus({preventScroll:true});
 };
 toggle.addEventListener("click",()=>open(root.hidden));
 $("playerClose").addEventListener("click",()=>open(false));
 $("playerAdd").addEventListener("click",()=>send("playerAdd"));
 $("playerAddFolder").addEventListener("click",()=>send("playerAddFolder"));
 $("playerRelink").addEventListener("click",()=>send("playerRelink"));
 $("playerPlay").addEventListener("click",()=>{
   send(model.transport?.state==="PLAYING"?"playerPause":"playerPlay");
 });
 $("playerStop").addEventListener("click",()=>send("playerStop"));
 for(const id of ["playerCueIn","playerCueOut","playerCueGoIn","playerCueClear"])
   $(id).addEventListener("click",()=>send(id));
 const seek=$("playerSeek"),waveCanvas=$("playerWaveform");
 let draggingSeek=false;
 seek.addEventListener("pointerdown",()=>{draggingSeek=true;});
 seek.addEventListener("pointerup",()=>{draggingSeek=false;});
 seek.addEventListener("pointercancel",()=>{draggingSeek=false;});
 // 'change' emits one intentional seek on release or keyboard commit.
 // Never send a native seek for each pointermove while the show is live.
 seek.addEventListener("change",()=>{
   draggingSeek=false;
   const length=Number(model.transport?.durationSeconds)||0;
   const fraction=Number(seek.value)/1000;
   if(length>0 && Number.isFinite(fraction))
     send("playerSeek",Math.max(0,Math.min(length,
       Math.round(fraction*length*100)/100)));
 });
 let lastWaveSignature="";
 const renderWaveform=transport=>{
   const peaks=transport.waveform;
   if(!Array.isArray(peaks)||peaks.length!==256)return;
   const fraction=Number(transport.durationSeconds)>0?
     Math.max(0,Math.min(1,
       Number(transport.currentSeconds)/Number(transport.durationSeconds))):0;
   // Incremental worker histogram. Redraw only when bins or playhead change.
   const signature=peaks.join(",")+"|"+Math.round(fraction*256);
   if(signature===lastWaveSignature)return;
   lastWaveSignature=signature;
   const ctx=waveCanvas.getContext("2d");
   if(!ctx)return;
   const w=waveCanvas.width,h=waveCanvas.height,half=h/2;
   ctx.clearRect(0,0,w,h);
   ctx.fillStyle="#1e363e";ctx.fillRect(0,0,w,h);
   for(let i=0;i<256;++i){
     // Display only already decoded regions, no fake waveform data.
     const normalized=Math.max(0,Math.min(1,(Number(peaks[i])||0)/65535));
     const size=Math.max(0,Math.sqrt(normalized)*(half-2));
     if(size<0.5)continue;
     ctx.fillStyle=i<=fraction*256?"#62d1b4":"#567e87";
     ctx.fillRect(i,half-size,1,size*2);
   }
   ctx.fillStyle="#def1e9";
   ctx.fillRect(Math.round(fraction*255),0,1,h);
 };

 $("playerKeyMinus").addEventListener("click",()=>send("playerKeyStep",-1));
 $("playerKeyPlus").addEventListener("click",()=>send("playerKeyStep",1));
 $("playerTempoMinus").addEventListener("click",()=>send("playerTempoStep",-1));
 $("playerTempoPlus").addEventListener("click",()=>send("playerTempoStep",1));
 $("playerPitchReset").addEventListener("click",()=>send("playerPitchTempoReset"));
 $("playerChooseOutput").addEventListener("click",()=>send("playerChooseOutput"));
 $("playerNext").addEventListener("click",()=>send("playerNext"));
 $("playerAutoNext").addEventListener("click",()=>
   send("playerAutoNext",!model.autoNext));
 $("playerFadeMs").addEventListener("change",event=>
   send("playerFadeMs",Number(event.target.value)));
 $("playerNewSetlist").addEventListener("click",()=>send("playerNewSetlist"));
 $("playerDeleteSetlist").addEventListener("click",()=>send("playerDeleteSetlist",model.activeSetlist));
 $("playerSetlistSelect").addEventListener("change",event=>
   send("playerSelectSetlist",Number(event.target.value)));
 $("playerSetlistName").addEventListener("change",event=>
   send("playerRenameSetlist",event.target.value.trim()));
 $("playerSetlistName").addEventListener("keydown",event=>{
   if(event.key==="Enter"){event.preventDefault();event.target.blur();}
 });
 $("playerUndoRemove").addEventListener("click",()=>send("playerUndoRemove"));
 const list=$("playerList"),search=$("playerSearch");
 const actionButton=(label,title,onClick)=>{
   const button=document.createElement("button");
   button.type="button";button.className="ap-row-action";
   button.textContent=label;button.title=title;
   button.setAttribute("aria-label",title);
   button.addEventListener("click",onClick);
   return button;
 };
 let renderedSets=-1;
 function renderSetlists(){
   if(renderedSets===libraryRevision)return;
   renderedSets=libraryRevision;
   const selector=$("playerSetlistSelect"),name=$("playerSetlistName");
   const oldFocus=document.activeElement===name;
   selector.replaceChildren();
   for(const set of model.setlists||[]){
     const option=document.createElement("option");
     option.value=String(set.id);
     option.textContent=String(set.name||"Setlist")+" · "+set.count;
     selector.append(option);
   }
   selector.value=String(model.activeSetlist||0);
   if(!oldFocus)name.value=model.setlists?.[model.activeSetlist]?.name||"";
   $("playerDeleteSetlist").disabled=(model.setlists?.length||0)<=1;
   $("playerUndoRemove").hidden=!model.canUndoRemove;
   const panel=$("playerQueuePanel"),queue=$("playerQueueList");
   const upcoming=model.queue||[];
   panel.hidden=!upcoming.length;
   $("playerQueueCount").textContent=String(upcoming.length);
   queue.replaceChildren();
   for(const item of upcoming.slice(0,12)){
     const row=document.createElement("div");
     row.className="ap-queue-row";row.setAttribute("role","listitem");
     const title=document.createElement("span");
     title.textContent=String(item.title||"Untitled");
     const controls=document.createElement("span");
     controls.className="ap-row-actions";
     const up=actionButton("↑","Move "+item.title+" earlier",
       ()=>send("playerQueueMove",{from:item.id,to:item.id-1}));
     const down=actionButton("↓","Move "+item.title+" later",
       ()=>send("playerQueueMove",{from:item.id,to:item.id+1}));
     up.disabled=item.id===0;
     down.disabled=item.id===upcoming.length-1;
     controls.append(up,down,actionButton("×",
       "Remove "+item.title+" from queue",
       ()=>send("playerQueueRemove",item.id)));
     row.append(title,controls);
     queue.append(row);
   }
   if(upcoming.length>12){
     const more=document.createElement("small");
     more.textContent="+"+(upcoming.length-12)+" more queued";queue.append(more);
   }
 }

 let libraryRevision=0,rendered="";
 function renderTracks(){
   // Avoid serializing and rebuilding a 2,000-song list on every realtime
   // status tick. The song catalogue changes only on a native full snapshot.
   const signature=[libraryRevision,model.selected,model.current,
     search.value].join("|");
   if(rendered===signature)return;
   rendered=signature;
   const q=search.value.trim().toLocaleLowerCase();
   const all=model.tracks.filter(x=>String(x.title||"").toLocaleLowerCase().includes(q));
   const songs=all.slice(0,128);
   list.replaceChildren();
   if(!songs.length){
     const message=document.createElement("div");
     message.className="ap-empty";
     message.textContent=model.tracks.length?
       "No songs match your search":"Add music files to create your setlist";
     list.append(message);return;
   }
   for(const song of songs){
     const row=document.createElement("div");
     row.className="ap-songrow";row.setAttribute("role","listitem");
     row.draggable=native;
     const choose=document.createElement("button");
     choose.type="button";choose.className="ap-song";
     choose.dataset.selected=String(song.id===model.selected);
     choose.dataset.current=String(song.id===model.current);
     choose.setAttribute("aria-selected",String(song.id===model.selected));
     choose.title="Click to prepare NEXT · Double-click to play now";
     const index=document.createElement("span");
     index.className="ap-index";
     index.textContent=String(song.id+1).padStart(2,"0");
     const title=document.createElement("span");
     title.className="ap-songname";
     title.textContent=String(song.title||"Untitled");
     const state=document.createElement("small");
     state.textContent=song.missing?"Missing":
       song.id===model.current?"NOW":
       song.id===model.selected?"NEXT":"";
     choose.append(index,title,state);
     choose.addEventListener("click",()=>send("playerSelect",song.id));
     choose.addEventListener("dblclick",()=>send("playerPlayNow",song.id));
     choose.addEventListener("keydown",event=>{
       if(event.key==="Enter"){
         event.preventDefault();
         send("playerPlayNow",song.id);
       }
     });
     const actions=document.createElement("span");
     actions.className="ap-row-actions";
     const move=delta=>send("playerMoveSong",
       {from:song.id,to:song.id+delta});
     actions.append(
       actionButton("↑","Move "+song.title+" up",()=>move(-1)),
       actionButton("↓","Move "+song.title+" down",()=>move(1)),
       actionButton("↥","Play "+song.title+" next",
         ()=>send("playerQueueNext",song.id)),
       actionButton("+","Add "+song.title+" to queue",
         ()=>send("playerQueueAppend",song.id)),
       actionButton("×","Remove "+song.title+" from this setlist",
         ()=>send("playerRemoveSong",song.id)));
     row.addEventListener("dragstart",event=>{
       event.dataTransfer?.setData("text/plain",String(song.id));
       if(event.dataTransfer)event.dataTransfer.effectAllowed="move";
     });
     row.addEventListener("dragover",event=>event.preventDefault());
     row.addEventListener("drop",event=>{
       event.preventDefault();
       const source=Number(event.dataTransfer?.getData("text/plain"));
       if(Number.isInteger(source)&&source>=0 && source!==song.id)
         send("playerMoveSong",{from:source,to:song.id});
     });
     row.append(choose,actions);list.append(row);
   }
   if(all.length>songs.length){
     const hint=document.createElement("div");
     hint.className="ap-empty";
     hint.textContent="Showing first "+songs.length+" of "+
       all.length+" songs · Search to find another";
     list.append(hint);
   }
 }
 function render(){
   const t=model.transport||{};
   const state=String(t.state||"EMPTY");
   $("playerNow").textContent=t.title||"No music loaded";
   $("playerElapsed").textContent=duration(t.currentSeconds);
   $("playerDuration").textContent=duration(t.durationSeconds);
   if(!draggingSeek && document.activeElement!==seek){
     seek.value=String(+t.durationSeconds>0?
       Math.round(1000*Math.min(1,Math.max(0,
         +t.currentSeconds/(+t.durationSeconds)))):0);
   }
   seek.disabled=!(+t.durationSeconds>0) || !native;
   renderWaveform(t);
   const currentSong=model.tracks?.[model.current];
   const inPoint=Number(currentSong?.cueIn)||0;
   const outPoint=Number(currentSong?.cueOut)||0;
   $("playerCueSummary").textContent="IN "+duration(inPoint)+
     " · OUT "+(outPoint>0?duration(outPoint):"end");
   const editable=native && model.current>=0 &&
     model.selected===model.current && +t.durationSeconds>0;
   $("playerCueIn").disabled=!editable;
   $("playerCueOut").disabled=!editable;
   $("playerCueGoIn").disabled=!editable;
   $("playerCueClear").disabled=!editable;
   $("playerPlay").textContent=state==="PLAYING"?"Ⅱ Pause":"▶ Play";
   const editingIndex=model.selected>=0?model.selected:model.current;
   const chosen=model.tracks[editingIndex];
   // These controls are song presets: NEXT can be prepared during a live
   // CURRENT performance. The JUCE engine only applies when appropriate.
   const editingNext=editingIndex>=0 && editingIndex!==model.current;
   const targetDescription=editingNext?"queued next song":"current song";
   const pitchRow=root.querySelector(".ap-pitchtempo");
   if(pitchRow)pitchRow.setAttribute("aria-label",
      "Music pitch and playback speed for "+targetDescription);
   $("playerKeyValue").title="Key for "+targetDescription;
   $("playerTempoValue").title="Tempo for "+targetDescription;
   const key=Number(chosen?.keySemitones??t.requestedKey??0);
   const tempo=Number(chosen?.tempoPercent??t.requestedTempo??100);
   $("playerKeyValue").textContent=(key>0?"+":"")+key+" st";
   $("playerTempoValue").textContent=tempo+"%";
   $("playerKeyMinus").disabled=key<=-6;
   $("playerKeyPlus").disabled=key>=6;
   $("playerTempoMinus").disabled=tempo<=80;
   $("playerTempoPlus").disabled=tempo>=120;
   $("playerAutoNext").textContent=model.autoNext?"Auto Next · On":"Auto Next · Off";
   $("playerAutoNext").setAttribute("aria-pressed",String(model.autoNext));
   $("playerFadeMs").value=String(model.crossfadeMs??180);
   $("playerTransitionStatus").textContent=t.crossfading?"Crossfading…":
     t.transitionPending?"Preparing NEXT…":"";
   const detail=model.error||t.error||({
      EMPTY:"Add music to begin",
      PREPARING:"Preparing audio…",
      READY:"Ready to play",
      PAUSED:"Paused",
      STOPPED:"Stopped · ready",
      ENDED:"Song finished · next song is ready",
      PLAYING:t.routeAudible?"Playing through MAIN":
        t.outputReady?"Music playing · MAIN disconnected":
        "Music playing · output muted",
      ERROR:"Unable to decode this file"
   }[state]||"");
   const indicator=model.libraryBusy && !model.error?
     "Adding songs from folder…":
     (t.processingChangePending && !model.error && !t.error &&
       state!=="EMPTY" && state!=="ERROR")
       ?"Preparing music KEY/TEMPO in decoder…":detail;
   showFeedback(indicator,Boolean(model.error||t.error));
   $("playerChooseOutput").hidden=!native || !Boolean(model.chooseOutput);
   const missing=model.tracks.find(x=>x.id===model.selected)?.missing;
   $("playerRelink").hidden=!native || !missing;
   $("playerAddFolder").disabled=Boolean(model.libraryBusy);
   $("playerAdd").disabled=Boolean(model.libraryBusy);
   $("playerCount").textContent=model.tracks.length+" songs";
   renderSetlists();
   renderTracks();
 }
 search.addEventListener("input",renderTracks);
 root.addEventListener("keydown",event=>{
   if(event.key==="Escape"){event.stopPropagation();open(false);toggle.focus();}
   if(event.key===" " && event.target!==search &&
      event.target.tagName!=="BUTTON"){
     event.preventDefault();$("playerPlay").click();
   }
 });
 if(native){
   backend.addEventListener("arMixPlayerState",payload=>{
     if(!payload || payload.schema!=="arsonmix.player.v1")return;
     if(Array.isArray(payload.tracks)){
       model.tracks=payload.tracks;
       ++libraryRevision;
     }
     model={...model,...payload,tracks:model.tracks};
     render();
   });
   // Dynamic loader may join after the initial WebView readiness event.
   // Request genuine native library/readback, not a browser-local fallback.
   send("playerSubscribe");
 }
 render();
})();
