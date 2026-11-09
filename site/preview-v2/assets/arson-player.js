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
 $("playerNext").addEventListener("click",()=>send("playerNext"));
 $("playerAutoNext").addEventListener("click",()=>
   send("playerAutoNext",!model.autoNext));
 const list=$("playerList"),search=$("playerSearch");
 let rendered="";
 function renderTracks(){
   const q=search.value.trim().toLocaleLowerCase();
   const songs=model.tracks.filter(x=>String(x.title||"").toLocaleLowerCase().includes(q));
   const signature=JSON.stringify([songs,model.selected,model.current]);
   if(rendered===signature)return;
   rendered=signature;
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
     list.append(row);
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
   showFeedback(detail,Boolean(model.error||t.error));
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
     if(Array.isArray(payload.tracks))model.tracks=payload.tracks;
     model={...model,...payload,tracks:model.tracks};
     render();
   });
 }
 render();
})();
