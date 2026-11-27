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
   if(!visible)closeSongMenu(false);
   root.hidden=!visible;updateMini();
   toggle.setAttribute("aria-expanded",String(visible));
   if(visible){
     // An initially hidden WebView has no reliable clientHeight. Recompute
     // the visible window after the approved Player panel is shown.
     rendered="";
     renderTracks();
     renderWaveform(model.transport||{});
     (collectionView==="queue"?$("playerQueueView"):$("playerSearch")).focus({preventScroll:true});
   }
 };
 toggle.addEventListener("click",()=>open(root.hidden));
 $("playerClose").addEventListener("click",()=>open(false));
 $("playerMinimize").addEventListener("click",()=>{open(false);if(!$("playerMini").hidden)$("playerMiniExpand").focus();});
 $("playerMiniExpand").addEventListener("click",()=>open(true));
 $("playerMiniPlay").addEventListener("click",()=>send(model.transport?.state==="PLAYING"?"playerPause":"playerPlay"));
 $("playerMiniNext").addEventListener("click",()=>{if(model.pending<0)send("playerNext");});
 $("playerPrevious").addEventListener("click",()=>{if(model.pending<0&&model.current>0&&!model.tracks[model.current-1]?.missing)send("playerPlayNow",model.current-1);});
 function updateMini(){$("playerMini").hidden=!native||!root.hidden||!model.transport?.title||model.current<0;}
 function setDetails(visible){$("playerDetails").hidden=!visible;$("playerDetailsToggle").setAttribute("aria-expanded",String(visible));if(visible)$("playerDetails").scrollIntoView?.({block:"nearest"});}
 $("playerDetailsToggle").addEventListener("click",()=>setDetails($("playerDetails").hidden));
 $("playerNextCard").addEventListener("click",()=>switchCollection(model.queue?.length?"queue":"setlist"));
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
   paintRange(seek,fraction*100);
   if(length>0 && Number.isFinite(fraction))
     send("playerSeek",Math.max(0,Math.min(length,
       Math.round(fraction*length*100)/100)));
 });
 const paintRange=(input,percent)=>input.style.setProperty?.("--ap-fill",Math.max(0,Math.min(100,Number(percent)||0))+"%");
 seek.addEventListener("input",()=>paintRange(seek,Number(seek.value)/10));
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
 const paintVolume=percent=>{paintRange(volume,percent);volumeDb.textContent=percent===0?"−∞ dB":(20*Math.log10(percent/100)).toFixed(1)+" dB";};
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
 const list=$("playerList"),search=$("playerSearch"),songMenu=$("playerSongMenu");
 let collectionView="setlist",menuOwner=null;
 function closeSongMenu(restoreFocus=false){
   const owner=menuOwner;menuOwner=null;songMenu.hidden=true;
   if(owner){owner.setAttribute("aria-expanded","false");if(restoreFocus&&owner.isConnected!==false)owner.focus({preventScroll:true});}
 }
 function switchCollection(view){
   closeSongMenu(false);collectionView=view;
   const queue=view==="queue";
   $("playerSetlistView").setAttribute("aria-pressed",String(view==="setlist"));
   $("playerLibraryView").setAttribute("aria-pressed",String(view==="library"));$("playerLibraryNote").hidden=view!=="library";
   $("playerQueueView").setAttribute("aria-pressed",String(queue));
   $("playerSetlistTools").hidden=queue;list.hidden=queue;$("playerQueuePanel").hidden=!queue;
   if(!queue){rendered="";renderTracks();}
 }
 $("playerSetlistView").addEventListener("click",()=>switchCollection("setlist"));
 $("playerQueueView").addEventListener("click",()=>switchCollection("queue"));
 $("playerLibraryView").addEventListener("click",()=>switchCollection("library"));
 // Shared context menu. Opening it is presentation-only; no implicit select or playback.
 function showContext(owner,title,commands,valid,event){
   if(menuOwner===owner&&!event){closeSongMenu(true);return;}
   closeSongMenu(false);if(!valid())return;
   menuOwner=owner;owner.setAttribute("aria-expanded","true");
   const heading=document.createElement("div");heading.className="ap-song-menu-title";heading.textContent=title;heading.setAttribute("role","presentation");songMenu.replaceChildren(heading);
   for(const [label,execute,danger,disabled] of commands){
     const item=document.createElement("button");item.type="button";item.textContent=label;item.disabled=Boolean(disabled);item.dataset.danger=String(Boolean(danger));item.setAttribute("role","menuitem");item.tabIndex=-1;
     item.addEventListener("click",()=>{if(menuOwner!==owner||!valid()||item.disabled)return;closeSongMenu(true);execute();});songMenu.append(item);
   }
   songMenu.hidden=false;
   const anchor=owner.getBoundingClientRect?.(),panel=root.getBoundingClientRect?.(),m=songMenu.getBoundingClientRect?.();
   if(anchor&&panel&&m){
     const vw=Number(window.innerWidth)||panel.right,vh=Number(window.innerHeight)||panel.bottom;
     const pointer=event&&Number.isFinite(event.clientX)&&Number.isFinite(event.clientY);
     const x=pointer?event.clientX:anchor.right-m.width,y=pointer?event.clientY:anchor.bottom+4;
     songMenu.style.left=Math.max(4,Math.min(x,vw-m.width-4))+"px";songMenu.style.top=Math.max(4,Math.min(y,vh-m.height-4))+"px";
   }
   Array.from(songMenu.children).slice(1).find(item=>!item.disabled)?.focus({preventScroll:true});
 }
 function contextBind(surface,openMenu){
   surface.addEventListener("contextmenu",event=>{event.preventDefault();event.stopPropagation();openMenu(event);});
   surface.addEventListener("keydown",event=>{if(event.key==="ContextMenu"||(event.shiftKey&&event.key==="F10")){event.preventDefault();event.stopPropagation();openMenu(null);}});
 }
 function showSongMenu(owner,song,revision,setlist,event){
   const valid=()=>native&&revision===libraryRevision&&setlist===model.activeSetlist;
   const live=fn=>()=>{if(model.pending<0)fn();};
   const commands=[
     ["Play next",live(()=>send("playerQueueNext",song.id)),false,song.missing||model.pending>=0],
     ["Add to queue",()=>send("playerQueueAppend",song.id),false,song.missing],
     ["Move earlier",()=>send("playerMoveSong",{from:song.id,to:song.id-1}),false,song.id===0||collectionView==="library"],
     ["Move later",()=>send("playerMoveSong",{from:song.id,to:song.id+1}),false,song.id>=model.tracks.length-1||collectionView==="library"],
     ["Remove from setlist",()=>{if(song.id!==model.pending)send("playerRemoveSong",song.id);},true,song.id===model.pending],
     ["Play now",live(()=>send("playerPlayNow",song.id)),false,song.missing||model.pending>=0],
     ["Prepare / edit key & tempo",()=>send("playerSelect",song.id),false,false]
   ];
   if(song.missing&&song.id===model.selected)commands.unshift(["Locate file",()=>{if(model.selected===song.id)send("playerRelink");}]);
   showContext(owner,song.title||"Untitled",commands,valid,event);
 }
 function showPlayerOptions(owner,event){showContext(owner,"Player",[["Song details",()=>setDetails(true)],["Setlist",()=>switchCollection("setlist")],["Queue",()=>switchCollection("queue")],["Minimize",()=>open(false)]],()=>true,event);}
 function showSetOptions(owner,event){
   const set=model.activeSetlist,valid=()=>native&&model.activeSetlist===set;
   showContext(owner,"Setlist",[
     ["Add music",()=>send("playerAdd"),false,model.libraryBusy],["Import folder",()=>send("playerAddFolder"),false,model.libraryBusy],
     ["New setlist",()=>send("playerNewSetlist")],["Rename setlist",()=>{setDetails(true);$("playerSetManage").open=true;$("playerSetlistName").focus();}],
     ["Undo remove",()=>send("playerUndoRemove"),false,!model.canUndoRemove],["Remove setlist",()=>{if(model.setlists.length>1)send("playerDeleteSetlist",set);},true,model.setlists.length<=1]
   ],valid,event);
 }
 $("playerOptions").addEventListener("click",()=>showPlayerOptions($("playerOptions")));
 contextBind($("playerOptions"),event=>showPlayerOptions($("playerOptions"),event));
 $("playerSetOptions").addEventListener("click",()=>showSetOptions($("playerSetOptions")));
 contextBind($("playerSetOptions"),event=>showSetOptions($("playerSetOptions"),event));
 list.addEventListener("contextmenu",event=>{if(!event.target?.closest?.(".ap-songrow")){event.preventDefault();event.stopPropagation();showSetOptions($("playerSetOptions"),event);}});
 contextBind(waveCanvas,event=>{
   const current=model.current,revision=libraryRevision;
   const valid=()=>native&&model.current===current&&libraryRevision===revision&&model.pending<0;
   const editable=()=>valid()&&current>=0&&model.selected===current&&Number(model.transport?.durationSeconds)>0;
   const cue=id=>()=>{if(editable())send(id);};
   showContext(waveCanvas,"Waveform",[
     ["Set IN at playhead",cue("playerCueIn"),false,!editable()],["Set OUT at playhead",cue("playerCueOut"),false,!editable()],["Go to IN",cue("playerCueGoIn"),false,!editable()],["Clear markers",cue("playerCueClear"),true,!editable()],["Song details",()=>setDetails(true)]
   ],valid,event);
 });
 songMenu.addEventListener("keydown",event=>{
   const key=event.key;if(!["ArrowDown","ArrowUp","Home","End","Escape","Tab"].includes(key))return;
   event.stopPropagation();if(key==="Tab"){closeSongMenu(true);return;}event.preventDefault();
   if(key==="Escape"){closeSongMenu(true);return;}
   const items=Array.from(songMenu.children).slice(1).filter(item=>!item.disabled);if(!items.length)return;
   const pos=items.indexOf(document.activeElement),i=key==="Home"?0:key==="End"?items.length-1:key==="ArrowDown"?(pos+1)%items.length:(pos<=0?items.length-1:pos-1);
   items[i].focus({preventScroll:true});
 });
 root.addEventListener("pointerdown",e=>{if(menuOwner&&!songMenu.contains?.(e.target)&&!menuOwner.contains?.(e.target))closeSongMenu(false);});
 root.addEventListener("focusin",e=>{if(menuOwner&&!songMenu.contains?.(e.target)&&!menuOwner.contains?.(e.target))closeSongMenu(false);});
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
   if(menuOwner)closeSongMenu(false);
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
   panel.hidden=collectionView!=="queue";
   $("playerQueueEmpty").hidden=Boolean(upcoming.length);
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
     const epoch=model.queueRevision;
     const guard=fn=>()=>{if(epoch===model.queueRevision)fn();};
     const up=actionButton("↑","Move "+item.title+" earlier",
       guard(()=>queueGesture("playerQueueMove",item.id,item.id-1)));
     const down=actionButton("↓","Move "+item.title+" later",
       guard(()=>queueGesture("playerQueueMove",item.id,item.id+1)));
     up.disabled=item.id===0 || model.pending>=0;
     down.disabled=item.id===upcoming.length-1 || model.pending>=0;
     const remove=actionButton("×",
       "Remove "+item.title+" from queue",
       guard(()=>queueGesture("playerQueueRemove",item.id)));
     remove.disabled=model.pending>=0;
     controls.append(up,down,remove);
     const queueSet=model.activeSetlist;
     contextBind(row,event=>showContext(up,item.title||"Queued song",[
       ["Move to front",()=>queueGesture("playerQueueMove",item.id,0),false,item.id===0||model.pending>=0],
       ["Move earlier",()=>queueGesture("playerQueueMove",item.id,item.id-1),false,up.disabled],
       ["Move later",()=>queueGesture("playerQueueMove",item.id,item.id+1),false,down.disabled],
       ["Remove from queue",()=>queueGesture("playerQueueRemove",item.id),true,remove.disabled]
     ],()=>native&&epoch===model.queueRevision&&queueSet===model.activeSetlist&&model.pending<0,event));
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
 const ROW_HEIGHT=34,OVERSCAN=8;
 let filteredKey="",previousQuery="",previousSetlist=-1,filtered=[];
 function refreshVisibleTrackStates(){
   // Do not destroy the existing row on CURRENT/NEXT readback; a native
   // status ACK between click and dblclick must not lose the gesture or focus.
   for(const row of list.children){
     if(row.className!=="ap-songrow")continue;
     const id=Number(row.dataset.songId),song=model.tracks[id];
     const choose=row.children[0];
     row.dataset.selected=String(id===model.selected);row.dataset.current=String(id===model.current);row.dataset.missing=String(Boolean(song?.missing));
     choose.dataset.selected=String(id===model.selected);
     choose.dataset.current=String(id===model.current);
     choose.setAttribute("aria-selected",String(id===model.selected));
     choose.children[2].textContent=song?.missing?"Missing":
       id===model.current?"NOW":id===model.selected?"NEXT":"";
     const seconds=id===model.current?Number(model.transport?.durationSeconds):Number(song?.durationSeconds);
     choose.children[4].textContent=Number.isFinite(seconds)&&seconds>0?duration(seconds):"—";
   }
 }
 function renderTracks(){
   const query=search.value.trim().toLocaleLowerCase();
   const key=[libraryRevision,model.activeSetlist,collectionView,query].join("|");
   if(key!==filteredKey){
     if(query!==previousQuery || model.activeSetlist!==previousSetlist)
       list.scrollTop=0;
     previousQuery=query;previousSetlist=model.activeSetlist;
     filteredKey=key;
     filtered=model.tracks.filter(song=>
       String(song.title||"").toLocaleLowerCase().includes(query));
     if(collectionView==="library")filtered.sort((a,b)=>String(a.title||"").localeCompare(String(b.title||"")));
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
   closeSongMenu(false);
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
     row.draggable=native&&collectionView==="setlist";
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
     const time=document.createElement("span");time.className="ap-songtime";time.textContent="—";choose.append(index,title,state,key,time);
     choose.addEventListener("click",()=>send("playerSelect",song.id));
     choose.addEventListener("dblclick",()=>send("playerPlayNow",song.id));
     choose.addEventListener("keydown",event=>{
       if(event.key==="Enter"){
         event.preventDefault();
         send("playerPlayNow",song.id);
       }
     });
     const actions=document.createElement("button");actions.type="button";actions.className="ap-row-menu";
     actions.setAttribute("aria-label","Actions for "+song.title);actions.setAttribute("aria-expanded","false");actions.setAttribute("aria-controls","playerSongMenu");
     actions.title="Actions for "+song.title;actions.disabled=!native;actions.setAttribute("aria-haspopup","menu");
     actions.innerHTML='<svg width="17" height="17" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><circle cx="5" cy="12" r="1.6"/><circle cx="12" cy="12" r="1.6"/><circle cx="19" cy="12" r="1.6"/></svg>';
     const revision=libraryRevision,setlist=model.activeSetlist;
     actions.addEventListener("click",()=>showSongMenu(actions,song,revision,setlist));
     contextBind(row,event=>showSongMenu(actions,song,revision,setlist,event));
     row.addEventListener("dragstart",event=>{
       if(!row.draggable){event.preventDefault();return;}closeSongMenu(false);queueDrag=null; // catalogue and queue are distinct drag domains
       event.dataTransfer?.setData("text/plain",String(song.id));
       if(event.dataTransfer)event.dataTransfer.effectAllowed="move";
     });
     row.addEventListener("dragover",event=>event.preventDefault());
     row.addEventListener("drop",event=>{
       event.preventDefault();
       const data=event.dataTransfer?.getData("text/plain");
       if(!data||collectionView!=="setlist"||revision!==libraryRevision||setlist!==model.activeSetlist)return;
       const source=Number(data);
       if(Number.isInteger(source)&&source>=0&&source<model.tracks.length&&source!==song.id)
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
   $("playerNowSubtitle").textContent=t.artist||(t.title?"Local music file":"Music player");
   $("playerNowMeta").textContent=[t.format,t.sampleRate?Math.round(t.sampleRate/100)/10+" kHz":"",t.channels===2?"Stereo":t.channels===1?"Mono":""].filter(Boolean).join(" · ");
   $("playerMiniTitle").textContent=t.title||"No music loaded";$("playerMiniTime").textContent=duration(t.currentSeconds)+" / "+duration(t.durationSeconds)+" · Key "+(model.tracks?.[model.current]?.keySemitones??0);updateMini();
   $("playerElapsed").textContent=duration(t.currentSeconds);
   $("playerDuration").textContent=duration(t.durationSeconds);
   $("playerRemaining").textContent="−"+duration(Math.max(0,(Number(t.durationSeconds)||0)-(Number(t.currentSeconds)||0)));
   $("playerState").textContent=({EMPTY:"NO TRACK",PLAYING:"NOW PLAYING",PAUSED:"PAUSED",PREPARING:"PREPARING",ENDED:"FINISHED",ERROR:"PLAYBACK ERROR"})[state]||"CURRENT · "+state;
   // Mirror native precedence for presentation only; a queue entry title is
   // projected without file identity, so label it queued rather than guaranteed.
   const selected=model.selected>=0&&model.selected<model.tracks.length?model.selected:-1;
   const sequential=model.current<0?0:model.current+1;
   const nextIndex=selected>=0&&selected!==model.current?selected:sequential;
   const pendingSong=model.pending>=0?model.tracks[model.pending]:null;
   const queueTitle=model.queue?.[0]?.title;
   const nextTitle=pendingSong?.title||queueTitle||model.tracks[nextIndex]?.title;
   $("playerNextLabel").textContent=pendingSong?"PREPARING":queueTitle?"QUEUED NEXT":"UP NEXT";
   $("playerUpNext").textContent=nextTitle||"End of setlist";
   const upcoming=pendingSong||(!queueTitle?model.tracks[nextIndex]:null);$("playerNextMeta").textContent=upcoming?((upcoming.keySemitones>0?"+":"")+(upcoming.keySemitones??0)+" st"+(Number(upcoming.durationSeconds)>0?" · "+duration(upcoming.durationSeconds):"")):"";
   if(!draggingSeek && document.activeElement!==seek){
     seek.value=String(+t.durationSeconds>0?
       Math.round(1000*Math.min(1,Math.max(0,
         +t.currentSeconds/(+t.durationSeconds)))):0);
   }
   paintRange(seek,Number(seek.value)/10);
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
   loopBtn.disabled=!native||(!model.loopAB&&(!editable||!loopValid||model.pending>=0));
   loopBtn.title=model.loopAB?"Disable Loop A–B":"Loop A–B requires saved IN/OUT markers";
   loopBtn.setAttribute("aria-pressed",String(Boolean(model.loopAB)));
   const playing=state==="PLAYING";
   for(const id of ["playerPlay","playerMiniPlay"]){const button=$(id);if(button.dataset.playing!==String(playing)){button.dataset.playing=String(playing);button.innerHTML=playing?"<svg width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.55\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"9\"/><path d=\"M10 9v6m4-6v6\"/></svg>":"<svg width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.55\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"m9 5 11 7-11 7z\" fill=\"currentColor\" stroke=\"none\"/></svg>";button.setAttribute("aria-label",playing?"Pause":"Play");button.title=playing?"Pause":"Play";}}
   $("playerPrevious").disabled=!native||model.pending>=0||model.current<=0||Boolean(model.tracks[model.current-1]?.missing);
   $("playerMiniNext").disabled=model.pending>=0;
   const editingIndex=model.selected>=0?model.selected:model.current;
   const chosen=model.tracks[editingIndex];
   // These controls are song presets: NEXT can be prepared during a live
   // CURRENT performance. The JUCE engine only applies when appropriate.
   const editingNext=editingIndex>=0 && editingIndex!==model.current;
   const targetDescription=editingNext?"queued next song":"current song";
   const pitchTarget=$("playerPitchTarget");
   pitchTarget.dataset.next=String(editingNext);
   pitchTarget.textContent="KEY · "+(editingNext?"NEXT":editingIndex>=0?"CURRENT":"NO TRACK")+(chosen?.title?" · "+chosen.title:"");
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
   $("playerAutoText").textContent=model.autoNext?"Auto next on":"Stop after song";
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
      PLAYING:t.routeAudible?"MUSIC CH → MAIN":
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
   $("playerRouteLight").dataset.state=(model.error||t.error||(state==="PLAYING"&&!t.routeAudible))?"warning":state==="PLAYING"&&t.routeAudible?"audible":"idle";
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
 list.addEventListener("scroll",()=>{closeSongMenu(false);renderTracks();},{passive:true});
 root.addEventListener("scroll",()=>closeSongMenu(false),{passive:true});$("playerQueuePanel").addEventListener("scroll",()=>closeSongMenu(false),{passive:true});
 window.addEventListener?.("resize",()=>{closeSongMenu(false);rendered="";renderTracks();renderWaveform(model.transport||{});});
 root.addEventListener("keydown",event=>{
   if(event.key==="Escape"){event.stopPropagation();if(menuOwner){event.preventDefault();closeSongMenu(true);return;}open(false);toggle.focus();return;}
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
       closeSongMenu(false);model.tracks=payload.tracks;
       ++libraryRevision;
     }
     if(Array.isArray(payload.queue)||Array.isArray(payload.setlists)||("pending" in payload&&payload.pending!==model.pending)||("activeSetlist" in payload&&payload.activeSetlist!==model.activeSetlist))
       ++showRevision;
     model={...model,...payload,tracks:model.tracks};
     render();
   });
   // Dynamic loader may join after the initial WebView readiness event.
   // Request genuine native library/readback, not a browser-local fallback.
   send("playerSubscribe");
 }
 switchCollection("setlist");
 render();
})();
