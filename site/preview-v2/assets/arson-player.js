// SPDX-License-Identifier: AGPL-3.0-only
// ArSon Player: ONE canonical approved /preview/ GUI; zero browser audio.
(() => {
 "use strict";
 const $=id=>document.getElementById(id);
 const root=$("arsonPlayer"), toggle=$("playerToggle");
 if(!root||!toggle)return;
 const backend=window.__JUCE__?.backend;
 const native=Boolean(backend?.emitEvent&&backend?.addEventListener);
 let model={tracks:[],selected:-1,current:-1,autoNext:false,
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
 $("playerPlay").addEventListener("click",()=>{
   send(model.transport?.state==="PLAYING"?"playerPause":"playerPlay");
 });
 $("playerStop").addEventListener("click",()=>send("playerStop"));
 $("playerKeyMinus").addEventListener("click",()=>send("playerKeyStep",-1));
 $("playerKeyPlus").addEventListener("click",()=>send("playerKeyStep",1));
 $("playerTempoMinus").addEventListener("click",()=>send("playerTempoStep",-1));
 $("playerTempoPlus").addEventListener("click",()=>send("playerTempoStep",1));
 $("playerPitchReset").addEventListener("click",()=>send("playerPitchTempoReset"));
 $("playerChooseOutput").addEventListener("click",()=>send("playerChooseOutput"));
 $("playerNext").addEventListener("click",()=>send("playerNext"));
 $("playerAutoNext").addEventListener("click",()=>
   send("playerAutoNext",!model.autoNext));
 const list=$("playerList"),search=$("playerSearch");
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
     const row=document.createElement("button");
     row.type="button";row.className="ap-song";
     row.dataset.selected=String(song.id===model.selected);
     row.dataset.current=String(song.id===model.current);
     row.setAttribute("role","option");
     row.setAttribute("aria-selected",String(song.id===model.selected));
     row.title="Select next · double-click to play now";
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
     row.append(index,title,state);
     row.addEventListener("click",()=>send("playerSelect",song.id));
     row.addEventListener("dblclick",()=>send("playerPlayNow",song.id));
     row.addEventListener("keydown",event=>{
       if(event.key==="Enter"){
         event.preventDefault();
         send("playerPlayNow",song.id);
       }
     });
     list.append(row);
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
   $("playerProgress").value=+t.durationSeconds>0?
     Math.min(1,Math.max(0,+t.currentSeconds/(+t.durationSeconds))):0;
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
   const indicator=(t.processingChangePending && !model.error && !t.error &&
       state!=="EMPTY" && state!=="ERROR")
     ?"Preparing music KEY/TEMPO in decoder…":detail;
   showFeedback(indicator,Boolean(model.error||t.error));
   $("playerChooseOutput").hidden=!native || !Boolean(model.chooseOutput);
   $("playerCount").textContent=model.tracks.length+" songs";
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
