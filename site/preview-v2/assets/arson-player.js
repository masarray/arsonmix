// SPDX-License-Identifier: AGPL-3.0-only
// ArSon Player: ONE canonical approved /preview/ GUI; zero browser audio.
(() => {
 "use strict";
 const $=id=>document.getElementById(id);
 const root=$("arsonPlayer"), toggle=$("playerToggle");
 if(!root||!toggle)return;
 const backend=window.__JUCE__?.backend;
 const native=Boolean(backend?.emitEvent&&backend?.addEventListener);
 let model={tracks:[],catalogue:null,setlists:[],queue:[],activeSetlist:0,
   selected:-1,current:-1,pending:-1,autoNext:false,
   transport:{state:"EMPTY",title:"",currentSeconds:0,durationSeconds:0},
   error:""};
 const send=(action,value)=>{
   if(native)backend.emitEvent("arMixAction",{action,value});
   else showFeedback("Playback is available in the ArSonMix desktop app",true);
 };
 const media=new Map();let mediaRevision=0,coverId=-1,coverUri="";
 // Fixed local asset allowlist: no file path or URL from tags.
 const fallbackArts=["blue-hour","prism","tide","dune","bloom"];
 function fallbackArt(song,raw,state){
   if(!song)return "still";
   if(song.missing||state==="ERROR")return "unavailable";
   const key=song.album&&song.artist?song.artist+"\u001f"+song.album:(raw?.title||song.title||"");
   let hash=2166136261;
   for(let i=0;i<key.length;++i)hash=Math.imul(hash^key.charCodeAt(i),16777619);
   return fallbackArts[(hash>>>0)%fallbackArts.length];
 }
 let failedCover=null;
 function paintArtwork(song,raw,state){
   const art=fallbackArt(song,raw,state);
   const uri=song&&!song.missing&&state!=="ERROR"&&song.catalogId===coverId?coverUri:"";
   const accepted=uri&&failedCover!==uri?uri:"";
   const label=art==="still"?"No song loaded":art==="unavailable"?"Music unavailable":"Internal artwork · original cover unavailable";
   for(const [imageId,fallbackId] of [["playerArtwork","playerArtworkFallback"],["playerMiniArtwork","playerMiniArtworkFallback"]]){
     const image=$(imageId),fallback=$(fallbackId);
     if(fallback.dataset.art!==art){fallback.dataset.art=art;fallback.title=label;fallback.setAttribute("aria-label",label);}
     if(image.dataset.uri!==accepted){
       image.dataset.uri=accepted;image.hidden=!accepted;fallback.hidden=Boolean(accepted);
       if(accepted)image.src=accepted;else image.removeAttribute?.("src");
     }
   }
 }
 function coverError(event){
   const image=event.currentTarget||this;
   const uri=image?.dataset?.uri;
   if(!uri||uri!==coverUri)return;
   failedCover=uri;paintArtwork(enriched(model.tracks[model.current]),model.tracks[model.current],model.transport?.state);
 }
 const enriched=song=>song?{...song,...(media.get(song.catalogId)||{}),missing:typeof song.missing==="boolean"?song.missing:media.get(song.catalogId)?.missing}:null;
 const activeCatalogueIndex=new Map();
 let activityRevision=0,repeatMode=0,favorites=new Set(),completed=new Set(),recent=[],libraryFilter="all";
 const catalogView=()=>collectionView==="library"&&Array.isArray(model.catalogue);
 const setText=(el,value)=>{const next=String(value??"");if(el.textContent!==next)el.textContent=next;};
 const showFeedback=(msg,error=false)=>{
   const output=$("playerFeedback");
   setText(output,msg);
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
     render();
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
 $("playerNextCard").addEventListener("click",()=>{
   if(model.pending<0&&model.queue?.length){switchCollection("queue");return;}
   revealTrack(nextTrackIndex());
 });
 function revealTrack(id){
   if(id<0||!model.tracks[id])return;
   switchCollection("setlist");
   if(!filtered.some(song=>song.id===id)){search.value="";renderTracks();}
   focusTrack(filtered.findIndex(song=>song.id===id));
 }
 function focusTrack(pos){
   if(pos<0||pos>=filtered.length)return;
   const item=filtered[pos],top=pos*ROW_HEIGHT,height=list.clientHeight||ROW_HEIGHT;
   if(top<list.scrollTop)list.scrollTop=top;
   else if(top+ROW_HEIGHT>list.scrollTop+height)list.scrollTop=Math.max(0,top+ROW_HEIGHT-height);
   renderTracks();
   Array.from(list.children).find(row=>Number(row.dataset.songId)===(catalogView()?item.catalogId:item.id))?.children[0]?.focus({preventScroll:true});
 }
 function focusSearch(){if(collectionView==="queue")switchCollection("setlist");search.focus({preventScroll:true});search.select?.();}
 $("playerAdd").addEventListener("click",()=>send("playerAdd"));
 $("playerAddFolder").addEventListener("click",()=>send("playerAddFolder"));
 $("playerRelink").addEventListener("click",()=>send("playerRelink"));
 $("playerPlay").addEventListener("click",()=>{
   send(model.transport?.state==="PLAYING"?"playerPause":"playerPlay");
 });
 $("playerStop").addEventListener("click",()=>send("playerStop"));
 for(const id of ["playerCueIn","playerCueOut","playerCueGoIn","playerCueClear"])
   $(id).addEventListener("click",()=>{if(canEditCue())send(id);});
 $("playerLoopAB").addEventListener("click",()=>
   send("playerLoopToggle",!Boolean(model.loopAB)));
 const seek=$("playerSeek"),waveCanvas=$("playerWaveform");
 const canSeek=()=>native&&model.pending<0&&!model.transport?.crossfading&&
   Number(model.transport?.durationSeconds)>0;
 const canEditCue=()=>canSeek()&&model.current>=0&&model.selected===model.current;
 let draggingSeek=false,seekOrigin=null,seekCancelled=false;
 const seekIdentity=()=>[model.activeSetlist,model.current,libraryRevision,Number(model.transport?.durationSeconds)||0].join("|");
 seek.addEventListener("pointerdown",()=>{draggingSeek=true;seekCancelled=false;seekOrigin=seekIdentity();});
 seek.addEventListener("pointerup",()=>{draggingSeek=false;});
 seek.addEventListener("pointercancel",()=>{draggingSeek=false;seekOrigin=null;seekCancelled=true;});
 seek.addEventListener("keydown",()=>{seekCancelled=false;seekOrigin=null;});
 // 'change' emits one intentional seek on release or keyboard commit.
 // Never send a native seek for each pointermove while the show is live.
 seek.addEventListener("change",()=>{
   draggingSeek=false;
   const stale=seekCancelled||(seekOrigin!==null&&seekOrigin!==seekIdentity());
   seekOrigin=null;seekCancelled=false;
   if(stale){render();return;}
   const length=Number(model.transport?.durationSeconds)||0;
   const fraction=Number(seek.value)/1000;
   paintRange(seek,fraction*100);
   if(canSeek()&&Number.isFinite(fraction))
     send("playerSeek",Math.max(0,Math.min(length,
       Math.round(fraction*length*100)/100)));
 });
 const paintRange=(input,percent)=>input.style.setProperty?.("--ap-fill",Math.max(0,Math.min(100,Number(percent)||0))+"%");
 seek.addEventListener("input",()=>paintRange(seek,Number(seek.value)/10));
 let lastWaveSignature="",lastPeaks=null,peakRevision=0;
 const renderWaveform=transport=>{
   if(root.hidden)return;
   const peaks=transport.waveform, length=Number(transport.durationSeconds)||0;
   const fraction=length>0?Math.max(0,Math.min(1,(Number(transport.currentSeconds)||0)/length)):0;
   const song=model.tracks?.[model.current], inPoint=Number(song?.cueIn)||0,outPoint=Number(song?.cueOut)||0;
   const rect=waveCanvas.getBoundingClientRect?.();
   const dpr=Math.min(3,Math.max(1,Number(window.devicePixelRatio)||1));
   const w=rect?.width>0?Math.max(1,Math.round(rect.width*dpr)):waveCanvas.width;
   const h=rect?.height>0?Math.max(1,Math.round(rect.height*dpr)):waveCanvas.height;
   const valid=Array.isArray(peaks)&&peaks.length===256&&length>0;
   if(valid&&(!lastPeaks||peaks.some((value,i)=>value!==lastPeaks[i]))){lastPeaks=peaks.slice();++peakRevision;}
   else if(!valid&&lastPeaks){lastPeaks=null;++peakRevision;}
   const signature=[w,h,model.current,inPoint,outPoint,length,Math.round(fraction*w),peakRevision].join("|");
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
   if(!canSeek()||event.button>0||!rect?.width||!Number.isFinite(event.clientX))return;
   const fraction=Math.max(0,Math.min(1,(event.clientX-rect.left)/rect.width));
   send("playerSeek",Math.round(fraction*length*100)/100);
 });

 $("playerKeyMinus").addEventListener("click",()=>send("playerKeyStep",-1));
 $("playerKeyPlus").addEventListener("click",()=>send("playerKeyStep",1));
 $("playerTempoMinus").addEventListener("click",()=>send("playerTempoStep",-1));
 $("playerTempoPlus").addEventListener("click",()=>send("playerTempoStep",1));
 $("playerPitchReset").addEventListener("click",()=>send("playerPitchTempoReset"));
 $("playerChooseOutput").addEventListener("click",()=>{
   // Native dispatcher opens the canonical Smart I/O panel by event.
   // Do not depend on toolbar markup or a particular toolbar generation.
   send("playerChooseOutput");
 });
 $("playerNext").addEventListener("click",()=>{if(model.pending<0)send("playerNext");});
 $("playerAutoNext").addEventListener("click",event=>{
   const owner=$("playerAutoNext");
   showContext(owner,"Playback mode",[
     [(model.autoNext&&repeatMode===0?"✓ ":"")+"Auto next",()=>send("playerPlaybackMode",true),false,model.pending>=0],
     [(!model.autoNext&&repeatMode===0?"✓ ":"")+"Stop after song",()=>send("playerPlaybackMode",false),false,model.pending>=0],
     [(repeatMode===1?"✓ ":"")+"Repeat one",()=>send("playerRepeat",1),false,model.pending>=0],
     [(repeatMode===2?"✓ ":"")+"Repeat all",()=>send("playerRepeat",2),false,model.pending>=0]
   ],()=>native&&model.pending<0);
 });
 $("playerAfterSong").addEventListener("change",event=>{
   const index=model.selected,track=model.tracks[index],mode=Number(event.target.value);
   event.target.value=String(track?.autoAfter??-1);
   if(native&&model.pending<0&&Number.isInteger(track?.catalogId)&&[-1,0,1].includes(mode))
     send("playerAutoAfter",{id:track.catalogId,index,set:model.activeSetlist,mode});
 });
 const volume=$("playerVolume"),volumeDb=$("playerVolumeDb");
 const paintVolume=percent=>{paintRange(volume,percent);volumeDb.textContent=percent===0?"−∞ dB":(20*Math.log10(percent/100)).toFixed(1)+" dB";};
 volume.disabled=!native;
 let volumeDragging=false,volumeFrame=null,pendingVolume=null;
 function flushVolume(){volumeFrame=null;if(pendingVolume!==null){const v=pendingVolume;pendingVolume=null;send("playerVolume",v);}}
 volume.addEventListener("pointerdown",()=>{volumeDragging=true;});
 volume.addEventListener("pointerup",()=>{volumeDragging=false;});
 volume.addEventListener("pointercancel",()=>{volumeDragging=false;});
 volume.addEventListener("input",()=>{
   const v=Number(volume.value);if(!Number.isFinite(v))return;
   pendingVolume=Math.max(0,Math.min(100,v));paintVolume(pendingVolume);
   if(volumeFrame===null){if(window.requestAnimationFrame)volumeFrame=window.requestAnimationFrame(flushVolume);else flushVolume();}
 });
 volume.addEventListener("change",()=>{if(volumeFrame!==null)window.cancelAnimationFrame?.(volumeFrame);flushVolume();volumeDragging=false;});
 $("playerFadeMs").addEventListener("change",event=>
   send("playerFadeMs",Number(event.target.value)));
 function canSwitchSet(){return native&&!model.libraryBusy&&model.pending<0&&!["PLAYING","PAUSED","PREPARING"].includes(model.transport?.state);}
 $("playerNewSetlist").addEventListener("click",()=>{if(canSwitchSet())send("playerNewSetlist");});
 $("playerDeleteSetlist").addEventListener("click",()=>{if(canSwitchSet()&&model.setlists.length>1)send("playerDeleteSetlist",model.activeSetlist);});
 $("playerSetlistSelect").addEventListener("change",event=>{
   const id=Number(event.target.value);event.target.value=String(model.activeSetlist);
   if(canSwitchSet()&&model.setlists.some(set=>set.id===id)&&id!==model.activeSetlist)send("playerSelectSetlist",id);
 });
 $("playerSetlistName").addEventListener("change",event=>
   send("playerRenameSetlist",event.target.value.trim()));
 $("playerSetlistName").addEventListener("keydown",event=>{
   if(event.key==="Enter"){event.preventDefault();event.target.blur();}
 });
 $("playerUndoRemove").addEventListener("click",()=>send("playerUndoRemove"));
 const list=$("playerList"),search=$("playerSearch"),songMenu=$("playerSongMenu");
 let collectionView="setlist",menuOwner=null,menuEpoch=0;
 function closeSongMenu(restoreFocus=false){
   const owner=menuOwner;menuOwner=null;++menuEpoch;songMenu.hidden=true;
   if(owner){owner.setAttribute("aria-expanded","false");if(restoreFocus&&owner.isConnected!==false)owner.focus({preventScroll:true});}
 }
 function switchCollection(view){
   closeSongMenu(false);collectionView=view;
   if(view==="library"&&native&&!model.catalogue)send("playerCatalogue");
   const queue=view==="queue";
   $("playerSetlistView").setAttribute("aria-pressed",String(view==="setlist"));
   $("playerLibraryView").setAttribute("aria-pressed",String(view==="library"));$("playerLibraryNote").hidden=view!=="library";
   $("playerLibraryFilter").hidden=view!=="library";
   $("playerQueueView").setAttribute("aria-pressed",String(queue));
   $("playerSetlistTools").hidden=queue;list.hidden=queue;$("playerQueuePanel").hidden=!queue;
   if(!queue){rendered="";renderTracks();}
 }
 $("playerSetlistView").addEventListener("click",()=>switchCollection("setlist"));
 $("playerQueueView").addEventListener("click",()=>switchCollection("queue"));
 $("playerLibraryView").addEventListener("click",()=>switchCollection("library"));
 $("playerLibraryFilter").addEventListener("change",event=>{const value=event.target.value;if(!["all","favorites","recent"].includes(value))return;libraryFilter=value;list.scrollTop=0;renderTracks();});
 // Shared context menu. Opening it is presentation-only; no implicit select or playback.
 function showContext(owner,title,commands,valid,event){
   if(menuOwner===owner&&!event){closeSongMenu(true);return;}
   closeSongMenu(false);if(!valid())return;
   menuOwner=owner;const epoch=menuEpoch;owner.setAttribute("aria-expanded","true");
   const heading=document.createElement("div");heading.className="ap-song-menu-title";heading.textContent=title;heading.setAttribute("role","presentation");songMenu.replaceChildren(heading);
   for(const [label,execute,danger,disabled] of commands){
     const item=document.createElement("button");item.type="button";item.textContent=label;item.disabled=Boolean(disabled);item.dataset.danger=String(Boolean(danger));item.setAttribute("role","menuitem");item.tabIndex=-1;
     item.addEventListener("click",()=>{if(menuOwner!==owner||epoch!==menuEpoch||!valid()||item.disabled)return;closeSongMenu(true);execute();});songMenu.append(item);
   }
   songMenu.hidden=false;
   positionContext(event);
   Array.from(songMenu.children).slice(1).find(item=>!item.disabled)?.focus({preventScroll:true});
 }
 function positionContext(event){
   if(!menuOwner)return;
   if(menuOwner.isConnected===false){closeSongMenu(false);return;}
   const anchor=menuOwner.getBoundingClientRect?.(),panel=root.getBoundingClientRect?.(),m=songMenu.getBoundingClientRect?.();
   if(anchor&&panel&&m){
     const vw=Number(window.innerWidth)||panel.right,vh=Number(window.innerHeight)||panel.bottom;
     const pointer=event&&Number.isFinite(event.clientX)&&Number.isFinite(event.clientY);
     const x=pointer?event.clientX:anchor.right-m.width,y=pointer?event.clientY:anchor.bottom+4;
     songMenu.style.left=Math.max(4,Math.min(x,vw-m.width-4))+"px";songMenu.style.top=Math.max(4,Math.min(y,vh-m.height-4))+"px";
   }
 }
 const listOwnsMenu=()=>Boolean(menuOwner&&list.contains?.(menuOwner));
 function contextBind(surface,openMenu){
   surface.addEventListener("contextmenu",event=>{event.preventDefault();event.stopPropagation();openMenu(event);});
   surface.addEventListener("keydown",event=>{if(event.key==="ContextMenu"||(event.shiftKey&&event.key==="F10")){event.preventDefault();event.stopPropagation();openMenu(null);}});
 }
 function canRemoveSong(id){return id!==model.pending&&(id!==model.current||["STOPPED","ENDED"].includes(model.transport?.state));}
 const afterLabel=mode=>mode===0?"Stop":mode===1?"Continue":"Use global";
 const favoriteCommand=song=>[favorites.has(song.catalogId)?"Remove from favorites":"Add to favorites",()=>send("playerFavorite",{id:song.catalogId,enabled:!favorites.has(song.catalogId)}),false,!Number.isInteger(song.catalogId)];
 function showSongMenu(owner,song,revision,setlist,event){
   const valid=()=>native&&revision===libraryRevision&&setlist===model.activeSetlist;
   const live=fn=>()=>{if(model.pending<0)fn();};
   const commands=[
     ["Play next",live(()=>send("playerQueueNext",song.id)),false,song.missing||model.pending>=0],
     ["Add to queue",()=>send("playerQueueAppend",song.id),false,song.missing],
     ["Prepare / edit key & tempo",()=>send("playerSelect",song.id),false,false],
     ["Play now",live(()=>send("playerPlayNow",song.id)),false,song.missing||model.pending>=0],
     ["Move earlier",()=>send("playerMoveSong",{from:song.id,to:song.id-1}),false,song.id===0||collectionView==="library"],
     ["Move later",()=>send("playerMoveSong",{from:song.id,to:song.id+1}),false,song.id>=model.tracks.length-1||collectionView==="library"],
     ["Remove from setlist",()=>{if(canRemoveSong(song.id))send("playerRemoveSong",song.id);},true,!canRemoveSong(song.id)]
   ];
   if(Number.isInteger(song.catalogId))commands.splice(commands.length-1,0,favoriteCommand(song),["After song: "+afterLabel(song.autoAfter),()=>{if(valid()){send("playerSelect",song.id);setDetails(true);$("playerAfterSong").focus();}},false,model.pending>=0]);
   if(song.missing&&song.id===model.selected)commands.unshift(["Locate file",()=>{if(model.selected===song.id)send("playerRelink");}]);
   showContext(owner,song.title||"Untitled",commands,valid,event);
 }
 function showPlayerOptions(owner,event){showContext(owner,"Player",[
   ["Find songs · Ctrl+F",focusSearch],["Show current song",()=>revealTrack(model.current),false,model.current<0],
   ["Show upcoming songs",()=>$("playerNextCard").click()],
   ["Song details",()=>setDetails(true)],["Setlist",()=>switchCollection("setlist")],["Queue",()=>switchCollection("queue")],
   ["Favorites",()=>{libraryFilter="favorites";$("playerLibraryFilter").value=libraryFilter;switchCollection("library");}],
   ["Recently played (this session)",()=>{libraryFilter="recent";$("playerLibraryFilter").value=libraryFilter;switchCollection("library");}],
   ["Clear session history",()=>send("playerClearHistory"),false,!native||(!recent.length&&!completed.size)],
   ["Refresh music metadata",()=>send("playerMediaRefresh"),false,!native],
   ["Keyboard shortcuts",()=>{setDetails(true);$("playerKeyboardHelp").open=true;$("playerKeyboardHelp").scrollIntoView?.({block:"nearest"});}],
   ["Minimize",()=>open(false)]
 ],()=>true,event);}
 contextBind($("playerNow"),event=>showPlayerOptions($("playerNow"),event));
 function showSetOptions(owner,event){
   const set=model.activeSetlist,epoch=showRevision,valid=()=>native&&model.activeSetlist===set&&epoch===showRevision;
   showContext(owner,"Setlist",[
     ["Add music",()=>send("playerAdd"),false,model.libraryBusy],["Import folder",()=>send("playerAddFolder"),false,model.libraryBusy],
     ["New setlist",()=>{if(canSwitchSet())send("playerNewSetlist");},false,!canSwitchSet()],["Rename setlist",()=>{setDetails(true);$("playerSetManage").open=true;$("playerSetlistName").focus();}],
     ["Undo remove",()=>send("playerUndoRemove"),false,!model.canUndoRemove],["Remove setlist",()=>{if(canSwitchSet()&&model.setlists.length>1)send("playerDeleteSetlist",set);},true,!canSwitchSet()||model.setlists.length<=1]
   ],valid,event);
 }
 $("playerOptions").addEventListener("click",()=>showPlayerOptions($("playerOptions")));
 contextBind($("playerOptions"),event=>showPlayerOptions($("playerOptions"),event));
 $("playerSetOptions").addEventListener("click",()=>showSetOptions($("playerSetOptions")));
 contextBind($("playerSetOptions"),event=>showSetOptions($("playerSetOptions"),event));
 list.addEventListener("contextmenu",event=>{if(!event.target?.closest?.(".ap-songrow")){event.preventDefault();event.stopPropagation();showSetOptions($("playerSetOptions"),event);}});
 contextBind(waveCanvas,event=>{
   const current=model.current,revision=libraryRevision;
   const valid=()=>canSeek()&&model.current===current&&libraryRevision===revision;
   const editable=()=>valid()&&canEditCue();
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
 let queueDrag=null,trackDrag=null;
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
   $("playerDeleteSetlist").disabled=!canSwitchSet()||(model.setlists?.length||0)<=1;
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
     const queueSet=model.activeSetlist;
     const guard=fn=>()=>{if(epoch===model.queueRevision&&queueSet===model.activeSetlist)fn();};
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
       trackDrag=null;queueDrag={from:item.id,revision:model.queueRevision,
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
 const normalizeSearch=value=>String(value??"").normalize("NFD").replace(/[\u0300-\u036f]/g,"").toLocaleLowerCase().trim();
 const searchCache=new WeakMap();
 function searchText(raw){const tags=media.get(raw.catalogId);const cached=searchCache.get(raw);if(cached&&cached.tags===tags)return cached.text;const value=normalizeSearch([raw.title,tags?.title,tags?.artist||raw.artist,tags?.album||raw.album,tags?.format||raw.format].filter(Boolean).join(" "));searchCache.set(raw,{tags,text:value});return value;}
 function refreshVisibleTrackStates(){
   // Do not destroy the existing row on CURRENT/NEXT readback; a native
   // status ACK between click and dblclick must not lose the gesture or focus.
   for(const row of list.children){
     if(row.className!=="ap-songrow")continue;
     const id=Number(row.dataset.songId),song=enriched(catalogView()?model.catalogue[id]:model.tracks[id]);
     const activeId=catalogView()?(activeCatalogueIndex.get(id)??-2):id;
     const choose=row.children[0];
     const endedCurrent=activeId>=0&&activeId===model.current&&model.transport?.state==="ENDED";
     row.dataset.selected=String(activeId>=0&&activeId===model.selected&&!endedCurrent);row.dataset.current=String(activeId>=0&&activeId===model.current&&!endedCurrent);row.dataset.missing=String(Boolean(song?.missing));
     choose.dataset.selected=row.dataset.selected;choose.dataset.current=row.dataset.current;
     choose.setAttribute("aria-selected",row.dataset.selected);
     row.dataset.favorite=String(favorites.has(song?.catalogId));row.dataset.done=String(completed.has(song?.catalogId));
     choose.children[2].textContent=song?.missing?"Missing":
       activeId>=0&&activeId===model.current&&model.transport?.state!=="ENDED"?"NOW":activeId>=0&&activeId===model.selected&&activeId!==model.current?"NEXT":completed.has(song?.catalogId)?"DONE":favorites.has(song?.catalogId)?"★":"";
     const seconds=activeId>=0&&activeId===model.current?Number(model.transport?.durationSeconds)||Number(song?.durationSeconds):Number(song?.durationSeconds);
     choose.children[4].textContent=Number.isFinite(seconds)&&seconds>0?duration(seconds):"—";
   }
 }
 function renderTracks(){
   const query=normalizeSearch(search.value);
   const key=[libraryRevision,mediaRevision,model.activeSetlist,collectionView,query,catalogView()?libraryFilter:"",catalogView()&&libraryFilter!=="all"?activityRevision:0].join("|");
   if(key!==filteredKey){
     if(query!==previousQuery || model.activeSetlist!==previousSetlist)
       list.scrollTop=0;
     previousQuery=query;previousSetlist=model.activeSetlist;
     filteredKey=key;
     const global=catalogView(),source=global?model.catalogue:model.tracks;
     const pool=global&&libraryFilter==="recent"?recent.map(id=>model.catalogue[id]).filter(Boolean):
       global&&libraryFilter==="favorites"?source.filter(song=>favorites.has(song.catalogId)):source;
     const tokens=query.split(/\s+/).filter(Boolean);
     // Cache normalized text per row + metadata object, enrich only visible
     // rows. A 12-file metadata update must not normalize/copy 64k songs.
     filtered=tokens.length?pool.filter(song=>tokens.every(token=>searchText(song).includes(token))):pool;
     let known=0,seconds=0;
     for(const song of filtered){const value=Number(media.get(song.catalogId)?.durationSeconds??song.durationSeconds);if(value>0&&Number.isFinite(value)){++known;seconds+=value;}}
     const total=known?(duration(seconds)+(known<filtered.length?"+":"")):"";
     setText($("playerCount"),(query?filtered.length+" / "+pool.length:pool.length)+" songs"+(total?" · "+total:""));
     if(collectionView==="library"&&!global)filtered=[...filtered].sort((a,b)=>String(a.title||"").localeCompare(String(b.title||"")));
     if(list.scrollTop>=filtered.length*ROW_HEIGHT)list.scrollTop=0;
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
   if(listOwnsMenu())closeSongMenu(false);
   list.replaceChildren();
   if(!filtered.length){
     const message=document.createElement("div");
     message.className="ap-empty";
     message.textContent=catalogView()&&libraryFilter==="favorites"?"No favorites yet":catalogView()&&libraryFilter==="recent"?"No songs played this session":model.tracks.length?"No songs match your search":"Add music files to create your setlist";
     list.append(message);return;
   }
   // Scroll spacers preserve the native scrollbar's real 2k-song range.
   const top=document.createElement("div");
   top.className="ap-list-spacer";
   top.style.height=(start*ROW_HEIGHT)+"px";
   top.setAttribute("aria-hidden","true");
   list.append(top);
   for(const item of songs){
     const raw=enriched(item),global=catalogView(),catalogId=raw.catalogId;
     const activeId=global?(activeCatalogueIndex.get(catalogId)??-1):raw.id;
     const song=global?{...raw,id:catalogId,keySemitones:activeId>=0?model.tracks[activeId]?.keySemitones:undefined}:raw;
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
     const semitones=Number(song.keySemitones)||0;key.textContent=global&&activeId<0?"—":(semitones>0?"+":"")+semitones+" st";
     const time=document.createElement("span");time.className="ap-songtime";time.textContent="—";choose.append(index,title,state,key,time);
     const revision=libraryRevision,setlist=model.activeSetlist;
     const valid=()=>revision===libraryRevision&&setlist===model.activeSetlist;
     const add=()=>{if(valid()&&global&&activeId<0&&!song.missing&&model.tracks.length<2000)send("playerCatalogueAdd",{id:catalogId,set:setlist});};
     const select=()=>{if(valid()&&activeId>=0)send("playerSelect",activeId);};
     const playNow=()=>{if(valid()&&!song.missing){if(activeId>=0&&model.pending<0)send("playerPlayNow",activeId);else if(activeId<0)add();}};
     choose.addEventListener("click",select);
     choose.addEventListener("dblclick",playNow);
     choose.addEventListener("keydown",event=>{
       if(event.altKey||event.metaKey||event.shiftKey||!valid())return;
       if(event.key==="Enter"){
         event.preventDefault();event.stopPropagation?.();if(event.repeat)return;
         if(event.ctrlKey){if(activeId>=0&&!song.missing)send("playerQueueAppend",activeId);}
         else playNow();
       }else if(!event.ctrlKey&&["ArrowUp","ArrowDown","Home","End"].includes(event.key)){
         event.preventDefault();event.stopPropagation?.();
         const i=filtered.findIndex(item=>(global?item.catalogId:item.id)===song.id);
         focusTrack(event.key==="Home"?0:event.key==="End"?filtered.length-1:Math.max(0,Math.min(filtered.length-1,i+(event.key==="ArrowDown"?1:-1))));
       }
     });
     const actions=document.createElement("button");actions.type="button";actions.className="ap-row-menu";
     actions.setAttribute("aria-label","Actions for "+song.title);actions.setAttribute("aria-expanded","false");actions.setAttribute("aria-controls","playerSongMenu");
     actions.title="Actions for "+song.title;actions.disabled=!native;actions.setAttribute("aria-haspopup","menu");
     actions.innerHTML='<svg width="17" height="17" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><circle cx="5" cy="12" r="1.6"/><circle cx="12" cy="12" r="1.6"/><circle cx="19" cy="12" r="1.6"/></svg>';
     if(global&&activeId<0)choose.title="Double-click to add to active setlist; no playback";
     const openActions=event=>{if(global&&activeId<0)showContext(actions,song.title,[["Add to active setlist",add,false,song.missing||model.tracks.length>=2000],favoriteCommand(song)],valid,event);else showSongMenu(actions,{...song,id:activeId,autoAfter:model.tracks[activeId]?.autoAfter},revision,setlist,event);};
     actions.addEventListener("click",()=>openActions());
     contextBind(row,openActions);
     row.addEventListener("dragstart",event=>{
       if(!row.draggable){event.preventDefault();return;}closeSongMenu(false);queueDrag=null; // catalogue and queue are distinct drag domains
       trackDrag={from:song.id,revision,setlist};
       event.dataTransfer?.setData("application/x-arsonmix-track","reorder");
       if(event.dataTransfer)event.dataTransfer.effectAllowed="move";
     });
     const validDrag=()=>trackDrag&&collectionView==="setlist"&&valid()&&trackDrag.revision===libraryRevision&&trackDrag.setlist===model.activeSetlist;
     row.addEventListener("dragover",event=>{if(validDrag())event.preventDefault();});
     row.addEventListener("drop",event=>{
       event.preventDefault();if(!validDrag())return;
       const source=trackDrag.from;trackDrag=null;
       if(Number.isInteger(source)&&source>=0&&source<model.tracks.length&&source!==song.id)
         send("playerMoveSong",{from:source,to:song.id});
     });
     row.addEventListener("dragend",()=>{trackDrag=null;});
     row.append(choose,actions);list.append(row);
   }
   const bottom=document.createElement("div");
   bottom.className="ap-list-spacer";
   bottom.style.height=((filtered.length-end)*ROW_HEIGHT)+"px";
   bottom.setAttribute("aria-hidden","true");
   list.append(bottom);
   refreshVisibleTrackStates();
 }
 function nextTrackIndex(){if(model.pending>=0)return model.pending;const selected=model.selected>=0&&model.selected<model.tracks.length?model.selected:-1;const override=model.tracks[model.current]?.autoAfter;const repeat=override===0?0:repeatMode;if(selected>=0&&selected!==model.current)return selected;if(repeat===1&&model.current>=0)return model.current;const next=model.current<0?0:model.current+1;return next<model.tracks.length?next:repeat===2&&model.tracks.length?0:-1;}
 function render(){
   const t=model.transport||{};
   const state=String(t.state||"EMPTY");
   const currentMedia=enriched(model.tracks[model.current]);
   setText($("playerNow"),currentMedia?.title||t.title||"No music loaded");
   setText($("playerNowSubtitle"),[currentMedia?.artist||t.artist,currentMedia?.album].filter(Boolean).join(" · ")||(t.title?"Local music file":"Music player"));
   const format=currentMedia?.format||t.format,rate=currentMedia?.sampleRate||t.sampleRate,channels=currentMedia?.channels||t.channels;
   setText($("playerNowMeta"),[format,rate?Math.round(rate/100)/10+" kHz":"",channels===2?"Stereo":channels===1?"Mono":""].filter(Boolean).join(" · "));
   paintArtwork(currentMedia,model.tracks[model.current],state);
   setText($("playerMiniTitle"),currentMedia?.title||t.title||"No music loaded");setText($("playerMiniTime"),duration(t.currentSeconds)+" / "+duration(t.durationSeconds)+" · Key "+(model.tracks?.[model.current]?.keySemitones??0));updateMini();
   setText($("playerElapsed"),duration(t.currentSeconds));
   setText($("playerDuration"),duration(t.durationSeconds));
   setText($("playerRemaining"),"−"+duration(Math.max(0,(Number(t.durationSeconds)||0)-(Number(t.currentSeconds)||0))));
   setText($("playerState"),({EMPTY:"NO TRACK",PLAYING:"NOW PLAYING",PAUSED:"PAUSED",PREPARING:"PREPARING",ENDED:"FINISHED",ERROR:"PLAYBACK ERROR"})[state]||"CURRENT · "+state);
   // Mirror native precedence for presentation only; a queue entry title is
   // projected without file identity, so label it queued rather than guaranteed.
   const nextIndex=nextTrackIndex();
   const pendingSong=model.pending>=0?enriched(model.tracks[model.pending]):null;
   const queueTitle=model.queue?.[0]?.title;
   const nextTitle=pendingSong?.title||queueTitle||enriched(model.tracks[nextIndex])?.title;
   $("playerNextLabel").textContent=pendingSong?"PREPARING":queueTitle?"QUEUED NEXT":"UP NEXT";
   setText($("playerUpNext"),nextTitle||"End of setlist");
   const upcoming=pendingSong||(!queueTitle?enriched(model.tracks[nextIndex]):null);$("playerNextMeta").textContent=upcoming?((upcoming.keySemitones>0?"+":"")+(upcoming.keySemitones??0)+" st"+(Number(upcoming.durationSeconds)>0?" · "+duration(upcoming.durationSeconds):"")):"";
   if(!draggingSeek){
     seek.value=String(+t.durationSeconds>0?
       Math.round(1000*Math.min(1,Math.max(0,
         +t.currentSeconds/(+t.durationSeconds)))):0);
   }
   paintRange(seek,Number(seek.value)/10);
   seek.disabled=!canSeek();
   renderWaveform(t);
   const currentSong=model.tracks?.[model.current];
   const inPoint=Number(currentSong?.cueIn)||0;
   const outPoint=Number(currentSong?.cueOut)||0;
   $("playerCueSummary").textContent="IN "+duration(inPoint)+
     " · OUT "+(outPoint>0?duration(outPoint):"end");
   const editable=canEditCue();
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
   $("playerMiniNext").disabled=model.pending>=0;$("playerNext").disabled=model.pending>=0;
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
   const override=currentSong?.autoAfter??-1;
   const modeLabel=override===0?"Stop · song":override===1&&repeatMode===0?"Continue · song":repeatMode===1?"Repeat one":repeatMode===2?"Repeat all":model.autoNext?"Auto next on":"Stop after song";
   setText($("playerAutoText"),modeLabel);$("playerAutoNext").title="Playback mode · "+modeLabel;
   $("playerAutoNext").setAttribute("aria-pressed",String(override===0?false:override===1||model.autoNext||repeatMode!==0));
   $("playerAfterSong").value=String(chosen?.autoAfter??-1);$("playerAfterSong").disabled=!native||!Number.isInteger(chosen?.catalogId)||model.pending>=0;
   setText($("playerAfterSongTarget"),chosen?"After "+chosen.title:"After selected song");
   $("playerFadeMs").value=String(model.crossfadeMs??180);
   const acceptedVolume=Number(t.volumePercent??50);
   if(!volumeDragging&&pendingVolume===null){volume.value=String(acceptedVolume);paintVolume(acceptedVolume);}
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
   const indicator=model.mediaRefreshQueued && !model.error && !t.error?
     "Metadata refresh queued…":
     model.libraryBusy && !model.error?
     "Adding songs from folder…":
     (t.processingChangePending && !model.error && !t.error &&
       state!=="EMPTY" && state!=="ERROR")
       ?"Preparing music KEY/TEMPO in decoder…":detail;
   showFeedback(indicator,Boolean(model.error||t.error));
   $("playerRouteLight").dataset.state=(model.error||t.error||(state==="PLAYING"&&!t.routeAudible))?"warning":state==="PLAYING"&&t.routeAudible?"audible":"idle";
   $("playerChooseOutput").hidden=!native || !Boolean(model.chooseOutput);
   const missing=enriched(model.tracks[model.selected])?.missing;
   $("playerRelink").hidden=!native || !missing;
   $("playerAddFolder").disabled=Boolean(model.libraryBusy);
   $("playerAdd").disabled=Boolean(model.libraryBusy);
   const canSwitch=canSwitchSet();
   $("playerSetlistSelect").disabled=!canSwitch;
   $("playerSetlistSelect").title=canSwitch?"Select setlist":"Stop the current song before switching setlists";
   $("playerNewSetlist").disabled=!canSwitch;
   renderSetlists();
   $("playerDeleteSetlist").disabled=!canSwitch||model.setlists.length<=1;
   if(!root.hidden&&collectionView!=="queue"&&!menuOwner&&!trackDrag)renderTracks();
 }
 search.addEventListener("input",renderTracks);
 search.addEventListener("keydown",event=>{
   if(event.key==="Escape"&&search.value){event.preventDefault();event.stopPropagation?.();search.value="";renderTracks();return;}
   if(event.key==="ArrowDown"&&filtered.length){event.preventDefault();event.stopPropagation?.();focusTrack(0);}
   if(event.key==="Enter"&&!event.repeat&&!event.altKey&&!event.metaKey&&!event.shiftKey&&filtered.length){event.preventDefault();event.stopPropagation?.();const song=filtered[0],id=catalogView()?(activeCatalogueIndex.get(song.catalogId)??-1):song.id;if(native&&id>=0){if(event.ctrlKey){if(!song.missing)send("playerQueueAppend",id);}else send("playerSelect",id);}focusTrack(0);}
 });
 // Focus-induced/delayed scroll must not dismiss a header menu.
 list.addEventListener("scroll",()=>{if(listOwnsMenu())closeSongMenu(false);renderTracks();positionContext();},{passive:true});
 root.addEventListener("scroll",()=>positionContext(),{passive:true});$("playerQueuePanel").addEventListener("scroll",()=>{if(menuOwner&&$("playerQueuePanel").contains?.(menuOwner))closeSongMenu(false);else positionContext();},{passive:true});
 window.addEventListener?.("resize",()=>{closeSongMenu(false);rendered="";renderTracks();renderWaveform(model.transport||{});});
 root.addEventListener("keydown",event=>{
   if(event.ctrlKey&&!event.altKey&&!event.shiftKey&&!event.metaKey&&event.key.toLowerCase()==="f"){event.preventDefault();event.stopPropagation?.();focusSearch();return;}
   if(event.key==="Escape"){event.stopPropagation();if(menuOwner){event.preventDefault();closeSongMenu(true);return;}open(false);toggle.focus();return;}
   const target=event.target;
   const interactive=target?.isContentEditable||["INPUT","TEXTAREA","SELECT","BUTTON","SUMMARY","A"].includes(target?.tagName)||target?.closest?.("button,input,select,textarea,summary,a,[contenteditable=true]");
   if(event.key===" "&&!event.repeat&&!interactive&&!event.altKey&&!event.ctrlKey&&!event.metaKey&&!event.shiftKey){
     event.preventDefault();$("playerPlay").click();
   }
 });
 for(const id of ["playerArtwork","playerMiniArtwork"])$(id).addEventListener("error",coverError);
 if(native){
   backend.addEventListener("arMixPlayerActivity",payload=>{if(payload?.schema!=="arsonmix.activity.v1")return;const ids=values=>Array.isArray(values)?values.filter(id=>Number.isInteger(id)&&id>=0&&id<64000):[];favorites=new Set(ids(payload.favorites));completed=new Set(ids(payload.done));recent=ids(payload.recent).slice(0,256);repeatMode=[0,1,2].includes(payload.repeat)?payload.repeat:0;++activityRevision;render();});
   backend.addEventListener("arMixPlayerCatalogue",payload=>{if(payload?.schema!=="arsonmix.catalogue.v1"||!Array.isArray(payload.rows))return;model.catalogue=payload.rows.slice(0,64000);++libraryRevision;if(collectionView==="library")renderTracks();});
   backend.addEventListener("arMixPlayerMedia",payload=>{if(payload?.schema!=="arsonmix.media.v1"||!Array.isArray(payload.rows))return;for(const row of payload.rows)if(Number.isInteger(row.catalogId)&&row.catalogId>=0&&row.catalogId<64000){media.set(row.catalogId,row);const activeId=activeCatalogueIndex.get(row.catalogId);if(Number.isInteger(activeId)&&model.tracks[activeId]&&typeof row.missing==="boolean")model.tracks[activeId].missing=row.missing;}++mediaRevision;render();});
   backend.addEventListener("arMixPlayerCover",payload=>{const incoming=Number(payload?.catalogId);if(incoming!==coverId||payload?.uri!==coverUri)failedCover=null;coverId=incoming;coverUri=typeof payload?.uri==="string"&&payload.uri.length<=350000&&/^data:image\/(png|jpeg);base64,[A-Za-z0-9+/=]+$/.test(payload.uri)?payload.uri:"";render();});
   backend.addEventListener("arMixPlayerMediaReset",()=>{media.clear();++mediaRevision;coverId=-1;coverUri="";failedCover=null;render();});
   backend.addEventListener("arMixPlayerState",payload=>{
     if(!payload || payload.schema!=="arsonmix.player.v1")return;
     if(Array.isArray(payload.tracks)){
       closeSongMenu(false);trackDrag=null;model.tracks=payload.tracks;
       ++libraryRevision;activeCatalogueIndex.clear();for(const song of model.tracks)if(Number.isInteger(song.catalogId))activeCatalogueIndex.set(song.catalogId,song.id);
     }
     if(Array.isArray(payload.queue)||Array.isArray(payload.setlists)||("pending" in payload&&payload.pending!==model.pending)||("activeSetlist" in payload&&payload.activeSetlist!==model.activeSetlist))
       ++showRevision;
     model={...model,...payload,tracks:model.tracks};
     if(seekOrigin!==null&&seekOrigin!==seekIdentity()){seekCancelled=true;draggingSeek=false;}
     render();
   });
   // Dynamic loader may join after the initial WebView readiness event.
   // Request genuine native library/readback, not a browser-local fallback.
   send("playerSubscribe");
 }
 switchCollection("setlist");
 render();
})();
