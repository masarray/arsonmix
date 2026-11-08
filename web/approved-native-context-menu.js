// SPDX-License-Identifier: AGPL-3.0-only
// Native-only context surfaces; never mutate the Pages reference mixer or PCM.
(() => {
  "use strict";
  const backend=window.__JUCE__?.backend;
  const hooks=window.__ARMIX_NATIVE_M2__;
  const scope=window.__ARMIX_NATIVE_SCOPE__;
  if(!backend?.emitEvent||!backend?.addEventListener||!hooks?.enabled||!scope)return;
  function install(){
    const css=document.createElement("style");
    css.textContent=[
      "#armixContext{position:fixed;z-index:10020;min-width:188px;max-width:260px;padding:4px;background:#152732;border:1px solid #4a7181;border-radius:6px;box-shadow:0 12px 28px #000b;color:#dcebf1;font:12px Inter,system-ui,sans-serif}",
      "#armixContext[hidden],#armixOrbit[hidden]{display:none!important}",
      "#armixContext button{display:block;width:100%;padding:8px 10px;border:0;border-radius:4px;background:transparent;color:inherit;text-align:left;font:inherit;cursor:pointer}",
      "#armixContext button:hover,#armixContext button:focus-visible{background:#2a4c5b;color:#fff;outline:none}",
      "#armixContext .danger{color:#ffc3bc}#armixContext .detail{font-size:10px;color:#a8bdc6;padding:5px 10px;line-height:1.5}",
      "#armixOrbit{position:fixed;z-index:10018;width:324px;max-width:calc(100vw - 16px);max-height:calc(100vh - 16px);overflow-y:auto;padding:13px;background:#142832;color:#e1eef3;border:1px solid #4b7081;border-radius:8px;box-shadow:0 16px 38px #000b;font:12px Inter,system-ui,sans-serif}",
      "#armixOrbit .head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:8px;font-size:14px;font-weight:550;cursor:grab;touch-action:none}",
      "#armixOrbit .head:active{cursor:grabbing}",
      "#armixContext button:disabled{opacity:.35;cursor:default}",
      "body,body *{-webkit-user-select:none!important;user-select:none!important}",
      "input,textarea,[contenteditable],[contenteditable] *{-webkit-user-select:text!important;user-select:text!important}",
      "#armixOrbit .sub{color:#9cb8c3;font-size:11px;margin-bottom:12px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}",
      "#armixOrbit label{display:block;font-size:10px;color:#9ebac6;margin-top:9px}",
      "#armixOrbit select,#armixOrbit input{box-sizing:border-box;width:100%;height:29px;margin-top:3px;border:1px solid #486575;border-radius:4px;background:#0d1c25;color:#e2f3f7;padding:3px 7px;font:12px Inter,system-ui,sans-serif}",
      "#armixOrbit .grid{display:grid;grid-template-columns:1fr 1fr;gap:0 9px}",
      "#armixOrbit .foot{display:flex;gap:7px;margin-top:13px}#armixOrbit button{border:1px solid #52788a;border-radius:4px;background:#224858;color:#e1f5f9;padding:7px 9px;font:12px Inter,system-ui,sans-serif;cursor:pointer}",
      "#armixOrbit .foot button{flex:1}#armixOrbit button:disabled{opacity:.35;cursor:default}",
      "#armixOrbit .info{color:#9ebcc8;font-size:10px;line-height:1.5;margin-top:9px}",
      "#armixOrbit .state{color:#92e3d3}#armixOrbit button.close{background:transparent;border:0;font-size:16px;padding:1px 6px}"
    ].join("\n");
    document.head.appendChild(css);
    // Chromium title tooltips look and behave like browser chrome, not a DAW.
    // Keep aria-labels and actual editor hints; strip only native title bubbles.
    function clearTitles(root){
      if(root.nodeType!==1)return;
      if(root.hasAttribute("title"))root.removeAttribute("title");
      root.querySelectorAll?.("[title]").forEach(el=>el.removeAttribute("title"));
    }
    clearTitles(document.documentElement);
    new MutationObserver(records=>{
      for(const entry of records){
        if(entry.type==="attributes")clearTitles(entry.target);
        else for(const node of entry.addedNodes)clearTitles(node);
      }
    }).observe(document.documentElement,{
      subtree:true,childList:true,attributes:true,attributeFilter:["title"]
    });
    const menu=document.createElement("div");
    menu.id="armixContext";menu.hidden=true;menu.setAttribute("role","menu");
    document.body.appendChild(menu);
    const orbit=document.createElement("section");
    orbit.id="armixOrbit";orbit.hidden=true;orbit.setAttribute("role","dialog");
    orbit.setAttribute("aria-label","dearVR PRO 2 Orbit");
    orbit.innerHTML=
      '<div class="head"><span>dearVR · Orbit</span><button type="button" class="close" data-orbit-close aria-label="Close Orbit">×</button></div>'+
      '<div class="sub" id="armixOrbitTarget"></div>'+
      '<label for="armixOrbitShape">Motion path</label><select id="armixOrbitShape"><option value="0">Circle · 360°</option><option value="1">Front ↔ Back</option><option value="2">Left ↔ Right</option><option value="3">Circle · Rise/Fall</option><option value="4">Front ↔ Back · Rise/Fall</option><option value="5">Left ↔ Right · Rise/Fall</option></select>'+
      '<label for="armixOrbitAz">Azimuth parameter</label><select id="armixOrbitAz"></select>'+
      '<div class="grid"><div><label for="armixOrbitDistance">Distance parameter</label><select id="armixOrbitDistance"></select></div>'+
      '<div><label for="armixOrbitElevation">Elevation parameter</label><select id="armixOrbitElevation"></select></div></div>'+
      '<div class="grid"><div><label for="armixOrbitNear">Near</label><input id="armixOrbitNear" type="number" step=".1"></div>'+
      '<div><label for="armixOrbitFar">Far</label><input id="armixOrbitFar" type="number" step=".1"></div></div>'+
      '<div class="grid"><div><label for="armixOrbitSeconds">Seconds / turn</label><input id="armixOrbitSeconds" type="number" min="2" max="180" value="20" step="1"></div>'+
      '<div><label for="armixOrbitReverse">Direction</label><select id="armixOrbitReverse"><option value="false">Clockwise</option><option value="true">Counter-clockwise</option></select></div></div>'+
      '<div class="foot"><button type="button" data-orbit-start disabled>Start Orbit</button><button type="button" data-orbit-stop>Stop</button></div>'+
      '<div class="info" id="armixOrbitInfo">Reading verified VST3 parameters…</div>';
    document.body.appendChild(orbit);
    let menuTrack=null,menuSlot=-1,menuTarget=null,confirmation=false;
    let orbitTrack=null,orbitSlot=-1,orbitTarget=null,orbitState=null;
    let orbitDrag=null;
    const $=id=>document.getElementById(id);
    const dismissMenu=()=>{menu.hidden=true;menuTrack=null;menuSlot=-1;menuTarget=null;confirmation=false;};
    function place(el,x,y){
      el.style.left=Math.max(6,Math.min(x,window.innerWidth-(el.offsetWidth||222)-8))+"px";
      el.style.top=Math.max(6,Math.min(y,window.innerHeight-(el.offsetHeight||130)-8))+"px";
    }
    function showMenu(x,y,kind){
      menu.hidden=false;
      if(kind==="strip"){
        const kindName=menuTrack?.kind==="audio"?"Track":
          menuTrack?.kind==="fx"?"AUX":"BUS";
        if(confirmation){
          const confirmLabel=kindName==="Track"?"Confirm Delete Track":
            kindName==="AUX"?"Confirm Delete AUX":"Confirm Delete BUS";
          menu.innerHTML='<div class="detail">Delete this '+kindName+
            ' and disconnect its FX/WIRE? Other channels are preserved.</div>'+
            '<button role="menuitem" class="danger" data-menu="confirm">'+confirmLabel+'</button>'+
            '<button role="menuitem" data-menu="cancel">Cancel</button>';
        }else{
          const canDelete=menuTrack?.kind!=="master" && hooks.canDeleteStrip?.(menuTrack);
          menu.innerHTML='<button role="menuitem" data-menu="rename">Rename…</button>'+
            '<button role="menuitem" class="danger" data-menu="delete" '+
            (canDelete?'':'disabled')+'>Delete…</button>'+
            (canDelete?'':'<div class="detail">Delete unavailable for this native strip</div>');
        }
      }else menu.innerHTML='<button role="menuitem" data-menu="orbit">Orbit…</button>';
      place(menu,x,y);
      menu.querySelector("[role=menuitem]:not(:disabled)")?.focus({preventScroll:true});
    }
    function closeOrbit(){
      orbitDrag=null;
      orbit.hidden=true;orbitTrack=null;orbitSlot=-1;orbitTarget=null;orbitState=null;
    }
    function placeOrbitNear(x,y){
      // Open immediately BELOW the selected Orbit command; if there is
      // insufficient vertical room, open above it. Never teleport to (0,0)
      // after hiding the menu (hidden elements have offsetLeft == 0).
      const height=orbit.offsetHeight||440;
      const targetY=y+8+height<=window.innerHeight-8?y+8:y-height-8;
      place(orbit,x,targetY);
    }
    const dragBar=orbit.querySelector(".head");
    dragBar.addEventListener("pointerdown",e=>{
      if(orbit.hidden||e.button!==0||e.target.closest("button"))return;
      orbitDrag={id:e.pointerId,startX:e.clientX,startY:e.clientY,
        left:orbit.offsetLeft,top:orbit.offsetTop};
      dragBar.setPointerCapture(e.pointerId);
      e.preventDefault();
    });
    dragBar.addEventListener("pointermove",e=>{
      if(!orbitDrag||e.pointerId!==orbitDrag.id)return;
      place(orbit,orbitDrag.left+e.clientX-orbitDrag.startX,
        orbitDrag.top+e.clientY-orbitDrag.startY);
    });
    const releaseDrag=e=>{
      if(orbitDrag?.id!==e.pointerId)return;
      orbitDrag=null;
      if(dragBar.hasPointerCapture(e.pointerId))dragBar.releasePointerCapture(e.pointerId);
    };
    dragBar.addEventListener("pointerup",releaseDrag);
    dragBar.addEventListener("pointercancel",releaseDrag);
    dragBar.addEventListener("lostpointercapture",()=>{orbitDrag=null;});
    function orbitStillCurrent(){
      if(!orbitTrack||!orbitTarget)return false;
      const actual=hooks.orbitTarget(orbitTrack,orbitSlot);
      return actual?.insertId===orbitTarget.insertId &&
        actual?.channelId===orbitTarget.channelId &&
        actual?.authority===orbitTarget.authority;
    }
    function dispatchOrbit(kind,values={}){
      if(!orbitStillCurrent()){closeOrbit();return false;}
      return hooks.orbitAction(orbitTrack,orbitSlot,kind,values);
    }
    function populate(select,values,chosen,emptyLabel){
      select.replaceChildren();
      if(!values.length){
        const option=new Option(emptyLabel,"-1");select.add(option);return;
      }
      values.forEach(p=>{select.add(new Option(p.name+" · "+p.minText+" / "+p.maxText,String(p.index)));});
      if(values.some(p=>p.index===chosen))select.value=String(chosen);
    }
    function validOrbitValues(){
      const shape=Number($("armixOrbitShape").value),az=Number($("armixOrbitAz").value),
        distance=Number($("armixOrbitDistance").value),elevation=Number($("armixOrbitElevation").value),
        near=Number($("armixOrbitNear").value),far=Number($("armixOrbitFar").value),
        seconds=Number($("armixOrbitSeconds").value);
      const needsDistance=[1,2,4,5].includes(shape),needsElevation=shape>=3;
      const minimum=orbitState?.distanceUnit==="m"?orbitState.distanceMin:0;
      const maximum=orbitState?.distanceUnit==="m"?orbitState.distanceMax:100;
      return az>=0&&(!needsDistance||distance>=0)&&(!needsElevation||elevation>=0)&&
        Number.isFinite(near)&&Number.isFinite(far)&&near>=minimum&&far<=maximum&&
        near<=far&&seconds>=2&&seconds<=180&&Number.isFinite(seconds);
    }
    function paintOrbit(state){
      if(!orbitTarget||state?.channelId!==orbitTarget.channelId||
         state?.instanceId!==orbitTarget.insertId)return;
      if(!orbitStillCurrent()){closeOrbit();return;}
      const first=!orbitState;
      orbitState=state;
      const params=Array.isArray(state.parameters)?state.parameters:[];
      if(first){
        populate($("armixOrbitAz"),params.filter(p=>/azimuth/i.test(p.name)&&!/scale/i.test(p.name)),
          state.preferredParameter,"Azimuth not exposed");
        populate($("armixOrbitDistance"),params.filter(p=>/distance/i.test(p.name)),
          state.preferredDistance,"Distance not exposed");
        populate($("armixOrbitElevation"),params.filter(p=>/elevation/i.test(p.name)),
          state.preferredElevation,"Elevation not exposed");
        $("armixOrbitNear").value=String(state.nearSetpoint??1);
        $("armixOrbitFar").value=String(state.farSetpoint??3);
        $("armixOrbitSeconds").value=String(state.periodSeconds??20);
        $("armixOrbitReverse").value=String(Boolean(state.reverse));
        $("armixOrbitShape").value=String(state.shape??0);
      }
      const unit=state.distanceUnit==="m"?"m":"%";
      $("armixOrbitNear").previousElementSibling.textContent="Near ("+unit+")";
      $("armixOrbitFar").previousElementSibling.textContent="Far ("+unit+")";
      const active=state.active===true;
      $("armixOrbitInfo").textContent=active?
        "Orbit running · "+(state.polarMode==="MANUAL_REQUIRED"?
          "Select POLAR in the dearVR editor if needed":state.polarMode||"verified"):
        "Only this verified dearVR instance is controlled. The audio callback remains native.";
      $("armixOrbitInfo").classList.toggle("state",active);
      $("armixOrbitStop").disabled=!active;
      $("armixOrbitStart").disabled=!validOrbitValues();
    }
    // Give buttons identifiers without introducing a second UI renderer.
    orbit.querySelector("[data-orbit-start]").id="armixOrbitStart";
    orbit.querySelector("[data-orbit-stop]").id="armixOrbitStop";
    document.addEventListener("contextmenu",e=>{
      // Never expose Chromium/WebView2 browser menu inside the native app.
      e.preventDefault();
      dismissMenu();
      const header=e.target.closest?.(".strip-identity-top");
      const row=e.target.closest?.(".fxslot-row:not(.is-empty)");
      if(header){
        const track=scope.findTrack(header);
        if(!track||!["audio","fx","bus","master"].includes(track.kind))return;
        menuTrack=track;menuSlot=-1;
        showMenu(e.clientX,e.clientY,"strip");return;
      }
      if(row){
        const track=scope.findTrack(row),slot=Number(row.dataset.slot);
        const target=hooks.orbitTarget(track,slot);
        if(!target)return;
        menuTrack=track;menuSlot=slot;menuTarget={...target};
        showMenu(e.clientX,e.clientY,"orbit");
      }
    },true);
    menu.addEventListener("click",e=>{
      const btn=e.target.closest("[data-menu]");if(!btn)return;
      const action=btn.dataset.menu,track=menuTrack,slot=menuSlot;
      const selected=menuTarget;
      // Read coordinates before dismissMenu(): hidden menu offset is zero.
      const originX=menu.offsetLeft,originY=menu.offsetTop+menu.offsetHeight;
      if(action==="delete"){
        if(!hooks.canDeleteStrip?.(track))return;
        confirmation=true;showMenu(menu.offsetLeft,menu.offsetTop,"strip");return;
      }
      dismissMenu();
      if(action==="rename"){
        const anchor=document.querySelector('.strip[data-id="'+track?.id+'"] .strip-identity-top');
        hooks.renameStrip?.(track,anchor);return;
      }
      if(action==="confirm"){
        if(track?.kind==="audio")hooks.requestDeleteTrack(track);
        else hooks.requestDeleteStrip?.(track);
        return;
      }
      if(action!=="orbit")return;
      const actual=hooks.orbitTarget(track,slot);
      if(!selected||selected.insertId!==actual?.insertId||
         selected.channelId!==actual?.channelId||
         selected.authority!==actual?.authority)return;
      orbitTrack=track;orbitSlot=slot;orbitTarget=selected;
      orbitState=null;orbit.hidden=false;
      $("armixOrbitTarget").textContent=track.name+" · FX "+(slot+1);
      $("armixOrbitInfo").textContent="Reading verified VST3 parameters…";
      $("armixOrbitStart").disabled=true;$("armixOrbitStop").disabled=true;
      placeOrbitNear(originX,originY);
      dispatchOrbit("inspect",{shape:Number($("armixOrbitShape").value)});
    });
    backend.addEventListener("arMixNativeOrbitState",paintOrbit);
    orbit.addEventListener("click",e=>{
      if(e.target.closest("[data-orbit-close]")){closeOrbit();return;}
      if(!orbitTrack)return;
      if(e.target.closest("[data-orbit-stop]")){dispatchOrbit("stop");return;}
      if(!e.target.closest("[data-orbit-start]")||!validOrbitValues())return;
      dispatchOrbit("start",{
        shape:Number($("armixOrbitShape").value),
        parameterIndex:Number($("armixOrbitAz").value),
        distanceIndex:Number($("armixOrbitDistance").value),
        elevationIndex:Number($("armixOrbitElevation").value),
        nearDistance:Number($("armixOrbitNear").value),
        farDistance:Number($("armixOrbitFar").value),
        distanceUnit:orbitState?.distanceUnit||"%",
        seconds:Number($("armixOrbitSeconds").value),
        reverse:$("armixOrbitReverse").value==="true"
      });
    });
    orbit.addEventListener("input",()=>{if(orbitState)$("armixOrbitStart").disabled=!validOrbitValues();});
    $("armixOrbitShape").addEventListener("change",()=>{
      if(orbitTrack)dispatchOrbit("inspect",{shape:Number($("armixOrbitShape").value)});
    });
    document.addEventListener("pointerdown",e=>{
      if(!menu.hidden&&!menu.contains(e.target))dismissMenu();
    },true);
    document.addEventListener("keydown",e=>{
      if(e.key!=="Escape")return;
      if(!menu.hidden){e.preventDefault();e.stopImmediatePropagation();dismissMenu();}
      else if(!orbit.hidden){e.preventDefault();e.stopImmediatePropagation();closeOrbit();}
    },true);
    window.addEventListener("blur",dismissMenu);
    window.addEventListener("resize",dismissMenu);
    document.addEventListener("scroll",dismissMenu,true);
  }
  if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",install,{once:true});
  else install();
})();