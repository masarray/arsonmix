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
   if(visible){
     // An initially hidden WebView has no reliable clientHeight. Recompute
     // the visible window after the approved Player panel is shown.
     rendered="";
     renderTracks();
     renderWaveform(model.transport||{});
     $("playerSearch").focus({preventScroll:true});
   }
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
 $("playerLoopAB").addEventListener("click",()=>
   send("playerLoopToggle",!Boolean(model.loopAB)));
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
   const peaks=transport.waveform, length=Number(transport.durationSeconds)||0;
   const fraction=length>0?Math.max(0,Math.min(1,(Number(transport.currentSeconds)||0)/length)):0;
   const song=model.tracks?.[model.current], inPoint=Number(song?.cueIn)||0,outPoint=Number(song?.cueOut)||0;
   const rect=waveCanvas.getBoundingClientRect?.();
   const dpr=Math.min(3,Math.max(1,Number(window.devicePixelRatio)||1));
   const w=rect?.width>0?Math.max(1,Math.round(rect.width*dpr)):waveCanvas.width;
   const h=rect?.height>0?Math.max(1,Math.round(rect.height*dpr)):waveCanvas.height;
   const valid=Array.isArray(peaks)&&peaks.length===256&&length>0;
   const signature=[w,h,model.current,inPoint,outPoint,Math.round(fraction*w),valid?peaks.join(","):"empty"].join("|");
   if(signature===lastWaveSignature)return;
   lastWaveSignature=signature;
   if(waveCanvas.width!==w)waveCanvas.width=w;
   if(waveCanvas.height!==h)waveCanvas.height=h;
   const ctx=waveCanvas.getContext("2d");if(!ctx)return;
   ctx.clearRect(0,0,w,h);ctx.fillStyle="#10161b";ctx.fillRect(0,0,w,h);
   if(!valid)return;
   const half=h/2, step=w/256;
   for(let i=0;i<256;i++){
     const normalized=Math.max(0,Math.min(1,(Number(peaks[i])||0)/65535));
     const size=Math.sqrt(normalized)*Math.max(0,half-3*dpr);
     if(size<0.5)continue;
     ctx.fillStyle=i/256<=fraction?"#67dac0":"#465e6c";
     ctx.fillRect(i*step,half-size,Math.max(1,step*.74),size*2);
   }
   for(const point of [inPoint,outPoint]){
     if(point>0&&point<=length){ctx.fillStyle="#a6c4e0";ctx.fillRect(Math.min(w-dpr,point/length*w),0,dpr,h);}
   }
   ctx.fillStyle="#e5ebf0";ctx.fillRect(Math.min(w-dpr,fraction*w),0,dpr,h);
 };
 waveCanvas.addEventListener("click",event=>{
   const rect=waveCanvas.getBoundingClientRect?.(),length=Number(model.transport?.durationSeconds)||0;
   if(!native||!rect?.width||length<=0||!Number.isFinite(event.clientX))return;
   const fraction=Math.max(0,Math.min(1,(event.clientX-rect.left)/rect.width));
   send("playerSeek",Math.round(fraction*length*100)/100);
 });

 $("playerKeyMinus").addEventListener("click",()=>send("playerKeyStep",-1));
 $("playerKeyPlus").addEventListener("click",()=>send("playerKeyStep",1));
 $("playerTempoMinus").addEventListener("click",()=>send("playerTempoStep",-1));
 $("playerTempoPlus").addEventListener("click",()=>send("playerTempoStep",1));
 $("playerPitchReset").addEventListener("click",()=>send("playerPitchTempoReset"));
 $("playerChooseOutput").addEventListener("click",()=>send("playerChooseOutput"));
 $("playerNext").addEventListener("click",()=>send("playerNext"));
 $("playerAutoNext").addEventListener("click",()=>
   send("playerAutoNext",!model.autoNext));
 const volume=$("playerVolume"),volumeDb=$("playerVolumeDb");
 const paintVolume=percent=>{volumeDb.textContent=percent===0?"−∞ dB":(20*Math.log10(percent/100)).toFixed(1)+" dB";};
 volume.disabled=!native;
 volume.addEventListener("input",()=>{paintVolume(Number(volume.value));send("playerVolume",Number(volume.value));});
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
 let renderedSets=-1,showRevision=0;
 let queueDrag=null;
 const queueGesture=(action,from,to=-1)=>{
   if(!native || model.pending>=0 ||
      !Number.isSafeInteger(model.queueRevision) ||
      model.queueRevision<1)return;
   const payload={from,revision:model.queueRevision};
   if(action==="playerQueueMove")payload.to=to;
   send(action,payload);
 };
 function renderSetlists(){
   if(renderedSets===showRevision)return;
   renderedSets=showRevision;
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
   // One scrollable compact queue, bounded at native 128 entries.
   // Repeat song titles are intentionally distinct position IDs.
   for(const item of upcoming.slice(0,128)){
     const row=document.createElement("div");
     row.className="ap-queue-row";row.setAttribute("role","listitem");
     row.draggable=Boolean(native && model.pending<0);
     row.dataset.queueIndex=String(item.id);
     const title=document.createElement("span");
     title.textContent=String(item.title||"Untitled");
     const controls=document.createElement("span");
     controls.className="ap-row-actions";
     const up=actionButton("↑","Move "+item.title+" earlier",
       ()=>queueGesture("playerQueueMove",item.id,item.id-1));
     const down=actionButton("↓","Move "+item.title+" later",
       ()=>queueGesture("playerQueueMove",item.id,item.id+1));
     up.disabled=item.id===0 || model.pending>=0;
     down.disabled=item.id===upcoming.length-1 || model.pending>=0;
     const remove=actionButton("×",
       "Remove "+item.title+" from queue",
       ()=>queueGesture("playerQueueRemove",item.id));
     remove.disabled=model.pending>=0;
     controls.append(up,down,remove);
     row.addEventListener("dragstart",event=>{
       if(!row.draggable || model.pending>=0){
         event.preventDefault();return;
       }
       queueDrag={from:item.id,revision:model.queueRevision,
                  setlist:model.activeSetlist};
       event.dataTransfer?.setData(
         "application/x-arsonmix-queue","reorder");
       if(event.dataTransfer)event.dataTransfer.effectAllowed="move";
     });
     row.addEventListener("dragover",event=>{
       if(!queueDrag || model.pending>=0 ||
          queueDrag.revision!==model.queueRevision ||
          queueDrag.setlist!==model.activeSetlist)return;
       event.preventDefault();
       if(event.dataTransfer)event.dataTransfer.dropEffect="move";
     });
     row.addEventListener("drop",event=>{
       if(!queueDrag || queueDrag.revision!==model.queueRevision ||
          queueDrag.setlist!==model.activeSetlist ||
          model.pending>=0)return;
       event.preventDefault();
       const from=queueDrag.from;
       queueDrag=null;
       if(from!==item.id)queueGesture("playerQueueMove",from,item.id);
     });
     row.addEventListener("dragend",()=>{queueDrag=null;});
     row.append(title,controls);
     queue.append(row);
   }
 }

 let libraryRevision=0,rendered="";
 // Keep the approved scroll list lightweight at all 2,000-song capacities.
 const ROW_HEIGHT=38,OVERSCAN=8;
 let filteredKey="",previousQuery="",previousSetlist=-1,filtered=[];
 function refreshVisibleTrackStates(){
   // Do not destroy the existing row on CURRENT/NEXT readback; a native
   // status ACK between click and dblclick must not lose the gesture or focus.
   for(const row of list.children){
     if(row.className!=="ap-songrow")continue;
     const id=Number(row.dataset.songId),song=model.tracks[id];
     const choose=row.children[0];
     choose.dataset.selected=String(id===model.selected);
     choose.dataset.current=String(id===model.current);
     choose.setAttribute("aria-selected",String(id===model.selected));
     choose.children[2].textContent=song?.missing?"Missing":
       id===model.current?"NOW":id===model.selected?"NEXT":"";
   }
 }
 function renderTracks(){
   const query=search.value.trim().toLocaleLowerCase();
   const key=[libraryRevision,model.activeSetlist,query].join("|");
   if(key!==filteredKey){
     if(query!==previousQuery || model.activeSetlist!==previousSetlist)
       list.scrollTop=0;
     previousQuery=query;previousSetlist=model.activeSetlist;
     filteredKey=key;
     filtered=model.tracks.filter(song=>
       String(song.title||"").toLocaleLowerCase().includes(query));
     rendered="";
   }
   const visible=Math.max(24,Math.min(80,
     Math.ceil((list.clientHeight||380)/ROW_HEIGHT)+2*OVERSCAN));
   const start=Math.max(0,Math.min(filtered.length,
     Math.floor(list.scrollTop/ROW_HEIGHT)-OVERSCAN));
   const end=Math.min(filtered.length,start+visible);
   const signature=[key,start,end].join("|");
   if(rendered===signature){
     refreshVisibleTrackStates();
     return;
   }
   rendered=signature;
   const songs=filtered.slice(start,end);
   list.replaceChildren();
   if(!filtered.length){
     const message=document.createElement("div");
     message.className="ap-empty";
     message.textContent=model.tracks.length?
       "No songs match your search":"Add music files to create your setlist";
     list.append(message);return;
   }
   // Scroll spacers preserve the native scrollbar's real 2k-song range.
   const top=document.createElement("div");
   top.className="ap-list-spacer";
   top.style.height=(start*ROW_HEIGHT)+"px";
   top.setAttribute("aria-hidden","true");
   list.append(top);
   for(const song of songs){
     const row=document.createElement("div");
     row.className="ap-songrow";row.setAttribute("role","listitem");
     row.dataset.songId=String(song.id);
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
     const key=document.createElement("span");key.className="ap-songkey";
     const semitones=Number(song.keySemitones)||0;key.textContent=(semitones>0?"+":"")+semitones+" st";
     choose.append(index,title,state,key);
     choose.addEventListener("click",()=>send("playerSelect",song.id));
     choose.addEventListener("dblclick",()=>send("playerPlayNow",song.id));
     choose.addEventListener("keydown",event=>{
       if(event.key==="Enter"){
         event.preventDefault();
         send("playerPlayNow",song.id);
       }
     });
     // One keyboard-accessible contextual selector per song. No overlay
     // escapes the virtual scroller, and every command retains native ownership.
     const actions=document.createElement("select");actions.className="ap-row-menu";
     actions.setAttribute("aria-label","Actions for "+song.title);
     actions.title="Actions for "+song.title;actions.disabled=!native;
     const revision=libraryRevision,setlist=model.activeSetlist;
     const options=[["","⋯"],["up","Move earlier"],["down","Move later"],["next","Play next"],["append","Add to queue"],["remove","Remove from setlist"]];
     for(const [value,label] of options){
       const option=document.createElement("option");option.value=value;option.textContent=label;
       option.disabled=(value==="up"&&song.id===0)||(value==="down"&&song.id===model.tracks.length-1);
       actions.append(option);
     }
     actions.value="";
     actions.addEventListener("change",()=>{
       const choice=actions.value;actions.value="";
       if(!native||revision!==libraryRevision||setlist!==model.activeSetlist)return;
       if(choice==="up"&&song.id>0)send("playerMoveSong",{from:song.id,to:song.id-1});
       else if(choice==="down"&&song.id<model.tracks.length-1)send("playerMoveSong",{from:song.id,to:song.id+1});
       else if(choice==="next")send("playerQueueNext",song.id);
       else if(choice==="append")send("playerQueueAppend",song.id);
       else if(choice==="remove")send("playerRemoveSong",song.id);
     });

     row.addEventListener("dragstart",event=>{
       queueDrag=null; // catalogue and queue are distinct drag domains
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
   const bottom=document.createElement("div");
   bottom.className="ap-list-spacer";
   bottom.style.height=((filtered.length-end)*ROW_HEIGHT)+"px";
   bottom.setAttribute("aria-hidden","true");
   list.append(bottom);
   refreshVisibleTrackStates();
 }
 function render(){
   const t=model.transport||{};
   const state=String(t.state||"EMPTY");
   $("playerNow").textContent=t.title||"No music loaded";
   $("playerElapsed").textContent=duration(t.currentSeconds);
   $("playerDuration").textContent=duration(t.durationSeconds);
   $("playerRemaining").textContent="−"+duration(Math.max(0,(Number(t.durationSeconds)||0)-(Number(t.currentSeconds)||0)));
   $("playerState").textContent=({EMPTY:"NO TRACK",PLAYING:"NOW PLAYING",PAUSED:"PAUSED",PREPARING:"PREPARING",ENDED:"FINISHED",ERROR:"PLAYBACK ERROR"})[state]||"CURRENT · "+state;
   const nextTitle=model.queue?.[0]?.title||(model.selected!==model.current?model.tracks?.[model.selected]?.title:"");
   $("playerUpNext").textContent=nextTitle||"No next song selected";
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
   const loopBtn=$("playerLoopAB");
   const loopValid=outPoint>inPoint &&
     outPoint-inPoint>=Math.max(1.5,
       Math.max(40,Number(model.crossfadeMs)||0)/1000+.35);
   loopBtn.disabled=!editable || (!model.loopAB && !loopValid);
   loopBtn.textContent=model.loopAB?"Loop A–B · On":"Loop A–B";
   loopBtn.setAttribute("aria-pressed",String(Boolean(model.loopAB)));
   const playButton=$("playerPlay"),playing=state==="PLAYING";
   if(playButton.dataset.playing!==String(playing)){
     playButton.dataset.playing=String(playing);
     playButton.innerHTML=playing?"<svg width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M9 5v14M15 5v14\" stroke-width=\"3\"/></svg><span>Pause</span>":"<svg width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"m9 5 11 7-11 7z\" fill=\"currentColor\" stroke=\"none\"/></svg><span>Play</span>";
     playButton.setAttribute("aria-label",playing?"Pause":"Play");
     playButton.title=playing?"Pause":"Play";
   }
   const editingIndex=model.selected>=0?model.selected:model.current;
   const chosen=model.tracks[editingIndex];
   // These controls are song presets: NEXT can be prepared during a live
   // CURRENT performance. The JUCE engine only applies when appropriate.
   const editingNext=editingIndex>=0 && editingIndex!==model.current;
   const targetDescription=editingNext?"queued next song":"current song";
   const pitchTarget=$("playerPitchTarget");
   pitchTarget.dataset.next=String(editingNext);
   pitchTarget.textContent="KEY / TEMPO · "+(editingNext?"NEXT":editingIndex>=0?"CURRENT":"NO TRACK")+(chosen?.title?" · "+chosen.title:"");
   pitchTarget.title=pitchTarget.textContent;
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
   $("playerAutoNext").textContent=model.autoNext?"Auto next · On":"Stop after song";
   $("playerAutoNext").setAttribute("aria-pressed",String(model.autoNext));
   $("playerFadeMs").value=String(model.crossfadeMs??180);
   const acceptedVolume=Number(t.volumePercent??50);
   if(document.activeElement!==volume){volume.value=String(acceptedVolume);paintVolume(acceptedVolume);}
   volume.title="Native source level before channel FX · "+acceptedVolume+"%";
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
 list.addEventListener("scroll",renderTracks,{passive:true});
 window.addEventListener?.("resize",()=>{rendered="";renderTracks();renderWaveform(model.transport||{});});
 root.addEventListener("keydown",event=>{
   if(event.key==="Escape"){event.stopPropagation();open(false);toggle.focus();}
   const target=event.target;
   const interactive=target?.isContentEditable||["INPUT","TEXTAREA","SELECT","BUTTON","SUMMARY","A"].includes(target?.tagName)||target?.closest?.("button,input,select,textarea,summary,a,[contenteditable=true]");
   if(event.key===" "&&!event.repeat&&!interactive&&!event.altKey&&!event.ctrlKey&&!event.metaKey&&!event.shiftKey){
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
     if(Array.isArray(payload.queue)||Array.isArray(payload.setlists))
       ++showRevision;
     model={...model,...payload,tracks:model.tracks};
     render();
   });
   // Dynamic loader may join after the initial WebView readiness event.
   // Request genuine native library/readback, not a browser-local fallback.
   send("playerSubscribe");
 }
 render();
})();
