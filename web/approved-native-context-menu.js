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
      "#armixHotkeyCard[hidden],#armixHotkeyRegister[hidden],#armixFxParamBrowse[hidden]{display:none!important}",
      "#armixFxParamBrowse{position:fixed;z-index:10026;width:355px;max-width:calc(100vw - 16px);max-height:76vh;box-sizing:border-box;display:flex;flex-direction:column;overflow:hidden;padding:11px;border:1px solid #507584;border-radius:7px;background:#142933;box-shadow:0 15px 35px #000b;color:#e0edf2;font:12px Inter,system-ui,sans-serif}",
      "#armixFxParamBrowse input{width:100%;box-sizing:border-box;height:29px;padding:5px 8px;background:#0d202a;color:#e5f4f6;border:1px solid #426a7b;border-radius:4px}",
      "#armixFxParamBrowse .armix-param-list{overflow-y:auto;min-height:64px;margin-top:8px}",
      "#armixFxParamBrowse .armix-param-row{padding:8px 2px;border-bottom:1px solid #304c5b;display:flex;align-items:center;gap:5px;justify-content:space-between}",
      "#armixFxParamBrowse .armix-param-name{min-width:0;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}",
      "#armixFxParamBrowse .armix-param-actions{display:flex;flex-shrink:0;gap:4px}",
      "#armixFxParamBrowse button{cursor:pointer;border:1px solid #426c7d;border-radius:4px;background:#225061;color:#e4f5f7;padding:4px 7px;font:11px Inter,system-ui,sans-serif}",
      "#armixFxParamBrowse .armix-param-row span small{display:block;color:#88aebc;font-size:10px}",

      "#armixHotkeyCard,#armixHotkeyRegister{position:fixed;z-index:10028;box-sizing:border-box;max-width:calc(100vw - 16px);padding:12px;border:1px solid #4b7180;border-radius:7px;background:#132731;color:#dbeef4;box-shadow:0 14px 35px #000c;font:12px Inter,system-ui,sans-serif}",
      "#armixHotkeyCard{width:286px}#armixHotkeyRegister{width:580px;max-height:76vh;display:flex;flex-direction:column}",
      ".armix-hk-head{display:flex;justify-content:space-between;align-items:center;gap:10px;cursor:grab;touch-action:none;font-weight:550;font-size:12px;margin-bottom:10px}",
      ".armix-hk-target,.armix-hk-state{color:#a9c2ca;font-size:11px;margin:6px 0;overflow-wrap:anywhere}",
      ".armix-hk-chord{border:1px solid #385e70;background:#0e2029;border-radius:5px;text-align:center;padding:11px 6px;margin:10px 0;color:#b4e8f4;font-size:14px;font-weight:550}",
      ".armix-hk-operations{display:flex;gap:7px;justify-content:flex-end;margin-top:11px}",
      "#armixHotkeyCard button,#armixHotkeyRegister button{border:1px solid #3f6575;background:#214653;color:#e3f1f5;padding:5px 9px;border-radius:4px;font:inherit;cursor:pointer}",
      "#armixHotkeyCard button:disabled,#armixHotkeyRegister button:disabled{opacity:.45;cursor:default}",
      "#armixHotkeyRegister input{background:#0c2029;color:#e0f0f5;border:1px solid #395f71;border-radius:4px;height:29px;padding:4px 8px;box-sizing:border-box;width:100%}",
      "#armixHotkeyRegister .hk-list{overflow-y:auto;min-height:50px;max-height:58vh;margin-top:8px}",
      "#armixHotkeyRegister .hk-row{display:grid;grid-template-columns:110px 1fr 68px 86px 80px;align-items:center;gap:6px;padding:6px 3px;border-bottom:1px solid #294653;font-size:11px}",
      "#armixHotkeyRegister .hk-row .hk-status{color:#8acbc4}",
      "#armixHotkeyRegister .hk-row .hk-actions{display:flex;gap:3px}",
      "#armixHotkeyRegister .hk-row button{padding:4px 5px}",
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
    const hotkeyCard=document.createElement("section");
    hotkeyCard.id="armixHotkeyCard";hotkeyCard.hidden=true;
    hotkeyCard.setAttribute("role","dialog");
    hotkeyCard.setAttribute("aria-label","Assign Hotkey");
    hotkeyCard.innerHTML=
      '<div class="armix-hk-head"><span data-hk-title>ASSIGN HOTKEY</span><button type="button" data-hk-cancel aria-label="Close">×</button></div>'+
      '<div class="armix-hk-target" id="armixHkTarget"></div>'+
      '<div class="armix-hk-state">Please press Hotkey</div>'+
      '<div class="armix-hk-chord" id="armixHkChord">Press Ctrl / Alt + key</div>'+
      '<div class="armix-hk-state" id="armixHkError" role="status"></div>'+
      '<div class="armix-hk-operations"><button type="button" data-hk-cancel>Cancel</button><button type="button" data-hk-save disabled>Save</button></div>';
    document.body.appendChild(hotkeyCard);
    const hotkeyRegister=document.createElement("section");
    hotkeyRegister.id="armixHotkeyRegister";hotkeyRegister.hidden=true;
    hotkeyRegister.setAttribute("role","dialog");
    hotkeyRegister.setAttribute("aria-label","Hotkey Register");
    hotkeyRegister.innerHTML=
      '<div class="armix-hk-head"><span>HOTKEY REGISTER</span><button type="button" data-hk-register-close>×</button></div>'+
      '<input aria-label="Search hotkeys" placeholder="Find shortcut or target…" data-hk-search>'+
      '<div class="hk-list" data-hk-list></div>'+
      '<div class="armix-hk-state" data-hk-feedback role="status"></div>'+
      '<div class="armix-hk-operations"><button type="button" data-hk-panic>+ Emergency Mute</button><button type="button" data-hk-refresh>Refresh</button></div>';
    document.body.appendChild(hotkeyRegister);
    const fxParamPanel=document.createElement("section");
    fxParamPanel.id="armixFxParamBrowse";fxParamPanel.hidden=true;
    fxParamPanel.setAttribute("role","dialog");
    fxParamPanel.setAttribute("aria-label","Assign Parameter Hotkey");
    fxParamPanel.innerHTML=
      '<div class="armix-hk-head"><span>VST3 PARAMETERS</span><button type="button" data-fxparam-close aria-label="Close">×</button></div>'+
      '<div class="armix-hk-target" data-fxparam-title></div>'+
      '<input type="search" aria-label="Find VST3 parameter" placeholder="Find parameter…" data-fxparam-search>'+
      '<div class="armix-hk-state" role="status" data-fxparam-state>Reading VST3 parameters…</div>'+
      '<div class="armix-param-list" data-fxparam-list></div>';
    document.body.appendChild(fxParamPanel);
    let fxParamSelection=null,fxParamStatus=null,fxParamRequest="";
    const moreTools=document.querySelector("#moreTools .more-tools-panel");
    if(moreTools){
      const openButton=document.createElement("button");
      openButton.type="button";openButton.className="tool";
      openButton.id="armixHotkeyRegisterOpen";openButton.textContent="Hotkeys";
      openButton.setAttribute("aria-label","Open Hotkey Register");
      moreTools.appendChild(openButton);
    }
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
            (canDelete?'':'<div class="detail">Delete unavailable for this native strip</div>')+
            (eligibleHotkey(menuTrack,"SET_MUTE")?
              '<button role="menuitem" data-menu="hotkey-mute">Mute · Assign Hotkey…</button>'+
              '<button role="menuitem" data-menu="hotkey-solo">Solo · Assign Hotkey…</button>':'')+
            '<button role="menuitem" data-menu="hotkey-register">Hotkey Register…</button>';
        }
      }else if(kind==="fx"){
        menu.innerHTML=
          '<button role="menuitem" data-menu="fx-param-last">Last Touched Hotkey…</button>'+
          '<button role="menuitem" data-menu="fx-param-browse">Browse Parameters…</button>'+
          (hooks.orbitTarget(menuTrack,menuSlot)?
            '<button role="menuitem" data-menu="orbit">Orbit…</button>':'')+
          '<button role="menuitem" data-menu="hotkey-register">Hotkey Register…</button>';
      }else if(kind==="hotkey"){
        const existing=hotkeyEntries.find(m=>sameHotkeyTarget(m,menuTarget));
        menu.innerHTML='<button role="menuitem" data-menu="hotkey-add">'+
          (existing?'Change Hotkey…':'Assign Hotkey…')+'</button>'+
          (existing?'<button role="menuitem" data-menu="hotkey-remove">Remove Hotkey</button>':'')+
          '<button role="menuitem" data-menu="hotkey-register">Hotkey Register…</button>';
      }
      else menu.innerHTML='<button role="menuitem" data-menu="orbit">Orbit…</button>'+
          '<button role="menuitem" data-menu="hotkey-register">Hotkey Register…</button>';
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

    let hotkeyEntries=[],hotkeySession="",hotkeyPending=null,hotkeyEdit=null;
    let hotkeyChord=null,hotkeyTarget=null,dragHotkey=null;
    let hotkeySerial=0;
    const nextHotkeyRequest=()=> "hotkey-ui-"+Date.now().toString(36)+"-"+(++hotkeySerial);
    const activeNative=()=>hooks.state?.()?.current;
    function eligibleHotkey(track,operation) {
      const channelId=hooks.nativeIdFor?.(track),state=activeNative();
      const channel=state?.graph?.channels?.find(ch=>ch.id===channelId);
      return state?.admitted===true && channelId && channel &&
        ["INPUT","GROUP","FX_RETURN"].includes(channel.role) &&
        ["SET_MUTE","SET_SOLO"].includes(operation) &&
        state.graph.capabilities.supportedOps.includes(operation)?channelId:null;
    }
    function nativeFxHotkeyTarget(button){
      const track=scope.findTrack(button),state=activeNative();
      const channelId=hooks.nativeIdFor?.(track);
      const channel=state?.graph?.channels?.find(ch=>ch.id===channelId);
      if(!state?.admitted||state?.retained||!channelId||!channel||
         !["INPUT","GROUP","FX_RETURN"].includes(channel.role))return null;
      if(button.dataset.action==="fxOn" &&
         typeof channel.fxRackBypassed==="boolean")
        return {kind:"hotkey",channelId,operation:"SET_FX_RACK_BYPASS",
          scope:"SESSION",label:track.name+" · FX Rack Power"};
      const slot=Number(button.dataset.slot ??
        button.closest(".fxslot-row")?.dataset.slot);
      const insert=channel.inserts?.[slot];
      if(button.dataset.action==="slotPower" && Number.isInteger(slot) &&
         slot>=0&&slot<8&&typeof insert?.id==="string"&&insert.id&&
         typeof insert.bypassed==="boolean")
        return {kind:"hotkey",channelId,operation:"SET_FX_SLOT_BYPASS",
          scope:"SESSION",slot,insertId:insert.id,
          label:track.name+" · FX "+(slot+1)+" · "+(insert.name||"Power")};
      return null;
    }
    function sameHotkeyTarget(m,target){
      return target && m.scope===target.scope &&
        m.operation===target.operation &&
        m.channelId===(target.channelId||"") &&
        (!target.operation.startsWith("SET_FX_PARAM_") ||
          (m.insertId===target.insertId&&m.catalogId===target.catalogId&&
           m.paramId===target.paramId)) &&
        (target.operation!=="SET_FX_SLOT_BYPASS" ||
         (m.slot===target.slot&&m.insertId===target.insertId));
    }
    function fxParameterOwner(track,slot){
      const state=activeNative(),channelId=hooks.nativeIdFor?.(track);
      if(!state?.admitted||state?.retained||!channelId||
         !Number.isInteger(slot)||slot<0||slot>=8)return null;
      const channel=state.graph?.channels?.find(ch=>ch.id===channelId);
      if(!channel||!["INPUT","GROUP","FX_RETURN"].includes(channel.role))return null;
      const insert=channel.inserts?.[slot];
      if(!insert?.id||!insert?.catalogId)return null;
      return {channelId,slot,insertId:insert.id,catalogId:insert.catalogId,
        label:track.name+" · FX "+(slot+1),epoch:state.device?.epoch,
        revision:state.graph?.revision,rosterRevision:state.graph?.rosterRevision};
    }
    function closeFxParamBrowser(){
      fxParamPanel.hidden=true;
      fxParamSelection=null;fxParamStatus=null;fxParamRequest="";
    }
    function openFxParamBrowser(track,slot,mode,x,y){
      const owner=fxParameterOwner(track,slot);
      if(!owner)return;
      closeFxParamBrowser();
      fxParamSelection={...owner,mode};
      fxParamRequest=nextHotkeyRequest();
      fxParamPanel.hidden=false;
      fxParamPanel.querySelector("[data-fxparam-title]").textContent=owner.label;
      fxParamPanel.querySelector("[data-fxparam-search]").value="";
      fxParamPanel.querySelector("[data-fxparam-list]").replaceChildren();
      fxParamPanel.querySelector("[data-fxparam-state]").textContent="Inspecting native VST3…";
      place(fxParamPanel,x,y);
      backend.emitEvent("arMixFxParameterHotkeyInspect",{
        requestId:fxParamRequest,channelId:owner.channelId,
        insertId:owner.insertId,catalogId:owner.catalogId,
        deviceEpoch:owner.epoch,revision:owner.revision,
        rosterRevision:owner.rosterRevision});
    }
    function paintFxParamBrowser(){
      const list=fxParamPanel.querySelector("[data-fxparam-list]");
      list.replaceChildren();
      if(!fxParamSelection||!fxParamStatus?.ok)return;
      const filter=fxParamPanel.querySelector("[data-fxparam-search]").value.toLowerCase();
      const params=fxParamStatus.parameters||[];
      const last=fxParamStatus.lastTouchedId;
      const eligible=fxParamSelection.mode==="last"&&last?
        params.filter(p=>p.id===last):params;
      for(const p of eligible){
        if(!(p.name+" "+p.id).toLowerCase().includes(filter))continue;
        const row=document.createElement("div");
        row.className="armix-param-row";
        const label=document.createElement("span");label.className="armix-param-name";
        label.textContent=p.name||p.id;
        const detail=document.createElement("small");
        detail.textContent="ID "+p.id;
        label.appendChild(detail);row.appendChild(label);
        const commands=document.createElement("span");commands.className="armix-param-actions";
        const choices=p.toggle?[["Toggle","SET_FX_PARAM_TOGGLE"]]:
          [["+", "SET_FX_PARAM_UP"],["−","SET_FX_PARAM_DOWN"]];
        for(const [caption,operation] of choices){
          const button=document.createElement("button");
          button.type="button";button.textContent=caption;
          button.setAttribute("aria-label","Assign "+caption+" hotkey for "+(p.name||p.id));
          button.addEventListener("click",()=>{
            // Latest native accepted identity still must match the inspected
            // instance; a removed/replaced plugin must never capture a key.
            const current=activeNative()?.graph?.channels
              ?.find(ch=>ch.id===fxParamSelection.channelId)
              ?.inserts?.some(ins=>ins.id===fxParamSelection.insertId&&
                                     ins.catalogId===fxParamSelection.catalogId);
            if(!current){closeFxParamBrowser();return;}
            const target={scope:"SESSION",channelId:fxParamSelection.channelId,
              operation,slot:fxParamSelection.slot,
              insertId:fxParamSelection.insertId,catalogId:fxParamSelection.catalogId,
              paramId:p.id,paramName:p.name,
              label:fxParamSelection.label+" · "+p.name+" · "+caption};
            const existing=hotkeyEntries.find(m=>sameHotkeyTarget(m,target));
            closeFxParamBrowser();
            openHotkeyCapture(target,existing||null);
          });
          commands.appendChild(button);
        }
        row.appendChild(commands);list.appendChild(row);
      }
      if(!list.childElementCount) {
        const empty=document.createElement("span");empty.className="armix-hk-state";
        empty.textContent="No matching parameter";list.appendChild(empty);
      }
    }
    backend.addEventListener("arMixFxParameterHotkeyState",state=>{
      if(!fxParamSelection||state?.requestId!==fxParamRequest ||
         state.channelId!==fxParamSelection.channelId ||
         state.insertId!==fxParamSelection.insertId)return;
      fxParamStatus=state;
      const message=fxParamPanel.querySelector("[data-fxparam-state]");
      if(!state.ok){message.textContent=state.error||"VST3 unavailable";return;}
      message.textContent=fxParamSelection.mode==="last"?
        (state.lastTouchedId?"Verified last-touched parameter":
          "No vendor gesture yet · Browse Parameters instead"):
        "Automatable parameters · native ParamID";
      if(fxParamSelection.mode==="last"&&!state.lastTouchedId)
        fxParamSelection.mode="browse";
      paintFxParamBrowser();
    });
    fxParamPanel.querySelector("[data-fxparam-search]")
      .addEventListener("input",paintFxParamBrowser);
    fxParamPanel.addEventListener("click",e=>{
      if(e.target.closest("[data-fxparam-close]"))closeFxParamBrowser();
    });
    function requestHotkeyList(){
      backend.emitEvent("arMixHotkeyIntent",{operation:"LIST"});
    }
    function hotkeyLabel(mapping){
      if(mapping.operation==="PANIC_MUTE")return "Emergency Output Mute";
      const name=activeNative()?.graph?.channels?.find(c=>c.id===mapping.channelId)?.name;
      const suffix=mapping.operation.startsWith("SET_FX_PARAM_")?
          "VST3 · "+(mapping.paramName||mapping.paramId)+" · "+
          (mapping.operation==="SET_FX_PARAM_TOGGLE"?"Toggle":
           mapping.operation==="SET_FX_PARAM_UP"?"Step +":"Step −"):
        mapping.operation==="SET_FX_RACK_BYPASS"?"FX Rack Power":
        mapping.operation==="SET_FX_SLOT_BYPASS"?
          "FX "+(Number(mapping.slot)+1)+" · Power":
        mapping.operation==="SET_MUTE"?"Mute":"Solo";
      return (name||"Channel "+mapping.channelId)+" · "+suffix;
    }
    function shortcutLabel(vk,mods){
      const label=vk>=0x70&&vk<=0x7a?"F"+(vk-0x6f):
        String.fromCharCode(vk);
      return [(mods&2)?"Ctrl":null,(mods&1)?"Alt":null,
        (mods&4)?"Shift":null,label].filter(Boolean).join(" + ");
    }
    function openHotkeyRegister(){
      hotkeyRegister.hidden=false;
      place(hotkeyRegister,Math.round((innerWidth-580)/2),62);
      requestHotkeyList();
      hotkeyRegister.querySelector("[data-hk-search]")?.focus({preventScroll:true});
    }
    function paintHotkeyRegister(){
      const target=hotkeyRegister.querySelector("[data-hk-list]");
      const filter=hotkeyRegister.querySelector("[data-hk-search]").value.toLowerCase();
      target.replaceChildren();
      for(const mapping of hotkeyEntries){
        const title=hotkeyLabel(mapping),chord=shortcutLabel(mapping.vk,mapping.modifiers);
        if(!(title+" "+chord+" "+mapping.scope).toLowerCase().includes(filter))continue;
        const row=document.createElement("div");row.className="hk-row";
        for(const value of [chord,title,mapping.scope,mapping.status||"UNAVAILABLE"]){
          const cell=document.createElement("span");cell.textContent=value;
          if(value===mapping.status)cell.className="hk-status";
          row.appendChild(cell);
        }
        const actions=document.createElement("span");actions.className="hk-actions";
        for(const action of ["Edit","Delete"]){
          const button=document.createElement("button");button.type="button";
          button.textContent=action;
          button.addEventListener("click",()=>{
            if(hotkeyPending)return;
            if(action==="Edit")openHotkeyCapture({
              channelId:mapping.channelId,operation:mapping.operation,
              slot:mapping.slot,insertId:mapping.insertId,
              catalogId:mapping.catalogId,paramId:mapping.paramId,
              paramName:mapping.paramName,
              label:title,scope:mapping.scope},mapping);
            else {
              hotkeyPending={requestId:nextHotkeyRequest(),kind:"DELETE",mapping};
              backend.emitEvent("arMixHotkeyIntent",{
                operation:"DELETE",requestId:hotkeyPending.requestId,mappingId:mapping.id});
            }
          });
          actions.appendChild(button);
        }
        row.appendChild(actions);target.appendChild(row);
      }
      if(!target.childElementCount){
        const empty=document.createElement("div");empty.className="armix-hk-state";
        empty.textContent=hotkeyEntries.length?"No matching hotkeys":"No registered shortcuts";
        target.appendChild(empty);
      }
    }
    function setHotkeyCapture(active){
      backend.emitEvent("arMixHotkeyCapture",{enabled:Boolean(active)});
    }
    function closeHotkeyCapture(){
      hotkeyCard.hidden=true;hotkeyTarget=null;hotkeyEdit=null;hotkeyChord=null;
      if(!hotkeyPending)setHotkeyCapture(false);
    }
    function openHotkeyCapture(target,editing=null){
      if(!target)return;
      closeHotkeyCapture();hotkeyTarget={...target};hotkeyEdit=editing;
      hotkeyCard.querySelector("[data-hk-title]").textContent=
        editing?"CHANGE HOTKEY":"ASSIGN HOTKEY";
      hotkeyChord=editing?{vk:editing.vk,modifiers:editing.modifiers}:null;
      $("armixHkTarget").textContent=target.label;
      $("armixHkChord").textContent=hotkeyChord?
        shortcutLabel(hotkeyChord.vk,hotkeyChord.modifiers):"Press Ctrl / Alt + key";
      $("armixHkError").textContent="Global shortcuts work while ArSonMix is minimized";
      hotkeyCard.querySelector("[data-hk-save]").disabled=!hotkeyChord;
      hotkeyCard.hidden=false;
      place(hotkeyCard,Math.round((innerWidth-286)/2),Math.round(innerHeight/3));
      setHotkeyCapture(true);
      hotkeyCard.tabIndex=-1;hotkeyCard.focus({preventScroll:true});
    }
    function keyToVk(code){
      if(/^Key[A-Z]$/.test(code))return code.charCodeAt(3);
      if(/^Digit[0-9]$/.test(code))return code.charCodeAt(5);
      if(/^F([1-9]|1[01])$/.test(code))return 0x6f+Number(code.slice(1));
      return null;
    }
    document.addEventListener("keydown",e=>{
      if(hotkeyCard.hidden)return;
      e.preventDefault();e.stopImmediatePropagation();
      if(e.key==="Escape"){closeHotkeyCapture();return;}
      if(e.key==="Enter"&&hotkeyChord){
        hotkeyCard.querySelector("[data-hk-save]").click();return;
      }
      if(e.repeat)return;
      const vk=keyToVk(e.code),modifiers=(e.altKey?1:0)|
        (e.ctrlKey?2:0)|(e.shiftKey?4:0);
      if(!vk || e.metaKey || e.getModifierState?.("AltGraph") ||
         !(modifiers&3)){
        hotkeyChord=null;$("armixHkChord").textContent="Invalid shortcut";
        $("armixHkError").textContent="Use Ctrl or Alt + letter, digit or F1–F11; no Win/F12";
      }else{
        hotkeyChord={vk,modifiers};
        $("armixHkChord").textContent=shortcutLabel(vk,modifiers);
        $("armixHkError").textContent="Ready to register with Windows";
      }
      hotkeyCard.querySelector("[data-hk-save]").disabled=!hotkeyChord;
    },true);
    hotkeyCard.addEventListener("click",e=>{
      if(e.target.closest("[data-hk-cancel]"))return closeHotkeyCapture();
      if(!e.target.closest("[data-hk-save]")||!hotkeyTarget||!hotkeyChord||
         hotkeyPending)return;
      const mapping={
        id:hotkeyEdit?.id||"hk-"+Date.now().toString(36)+"-"+(++hotkeySerial),
        scope:hotkeyTarget.scope,sessionId:hotkeyTarget.scope==="APP"?"":hotkeySession,
        channelId:hotkeyTarget.channelId||"",operation:hotkeyTarget.operation,
        vk:hotkeyChord.vk,modifiers:hotkeyChord.modifiers,
        ...((hotkeyTarget.operation==="SET_FX_SLOT_BYPASS"||
             hotkeyTarget.operation.startsWith("SET_FX_PARAM_"))?
          {slot:hotkeyTarget.slot,insertId:hotkeyTarget.insertId}:{}),
        ...(hotkeyTarget.operation.startsWith("SET_FX_PARAM_")?
          {catalogId:hotkeyTarget.catalogId,paramId:hotkeyTarget.paramId,
           paramName:hotkeyTarget.paramName}:{})
      };
      hotkeyPending={kind:"REGISTER",requestId:nextHotkeyRequest(),mapping};
      $("armixHkError").textContent="Registering and saving in native host…";
      hotkeyCard.querySelector("[data-hk-save]").disabled=true;
      backend.emitEvent("arMixHotkeyIntent",{
        operation:"REGISTER",requestId:hotkeyPending.requestId,mapping});
    });
    hotkeyRegister.addEventListener("click",e=>{
      if(e.target.closest("[data-hk-register-close]")){
        hotkeyRegister.hidden=true;return;
      }
      if(e.target.closest("[data-hk-refresh]"))requestHotkeyList();
      if(e.target.closest("[data-hk-panic]"))openHotkeyCapture({
        scope:"APP",operation:"PANIC_MUTE",channelId:"",
        label:"Emergency · Mute Hardware Output"});
    });
    hotkeyRegister.querySelector("[data-hk-search]").addEventListener("input",paintHotkeyRegister);
    $("armixHotkeyRegisterOpen")?.addEventListener("click",openHotkeyRegister);
    backend.addEventListener("arMixHotkeyRegistry",state=>{
      if(state?.schema!=="arsonmix.hotkey-registry.v1"||
         !Array.isArray(state.entries))return;
      hotkeySession=String(state.sessionId||"");
      hotkeyEntries=state.entries;
      if(hotkeyPending&&state.requestId===hotkeyPending.requestId){
        if(state.status!=="ACCEPTED"){
          $("armixHkError").textContent=state.errorCode||"Native registration rejected";
          hotkeyRegister.querySelector("[data-hk-feedback]").textContent=
            state.errorCode||"Native registration rejected";
          hotkeyCard.querySelector("[data-hk-save]").disabled=!hotkeyChord;
          hotkeyPending=null;
          if(!hotkeyCard.hidden)setHotkeyCapture(true);
        }else{
          hotkeyPending.acked=true;
          requestHotkeyList(); // independent native readback, not optimistic DOM
        }
      }else if(hotkeyPending?.acked&&!state.requestId){
        const p=hotkeyPending;
        const found=hotkeyEntries.find(m=>m.id===p.mapping?.id);
        const confirmed=p.kind==="DELETE"?!hotkeyEntries.some(m=>m.id===p.mapping.id):
          found&&found.vk===p.mapping.vk&&
          found.modifiers===p.mapping.modifiers&&found.operation===p.mapping.operation&&
          found.channelId===p.mapping.channelId&&
          (found.operation!=="SET_FX_SLOT_BYPASS"||
           (found.slot===p.mapping.slot&&found.insertId===p.mapping.insertId))&&
          (!found.operation.startsWith("SET_FX_PARAM_")||
           (found.insertId===p.mapping.insertId&&
            found.catalogId===p.mapping.catalogId&&
            found.paramId===p.mapping.paramId));
        if(confirmed){
          hotkeyPending=null;if(p.kind==="REGISTER")closeHotkeyCapture();
        }else{
          hotkeyPending=null;
          $("armixHkError").textContent="Native readback mismatch; refresh before retry";
        }
      }
      paintHotkeyRegister();
    });
    const hotkeyDragBar=hotkeyCard.querySelector(".armix-hk-head");
    hotkeyDragBar.addEventListener("pointerdown",e=>{
      if(e.button!==0||e.target.closest("button"))return;
      dragHotkey={id:e.pointerId,x:e.clientX,y:e.clientY,
        left:hotkeyCard.offsetLeft,top:hotkeyCard.offsetTop};
      hotkeyDragBar.setPointerCapture(e.pointerId);e.preventDefault();
    });
    hotkeyDragBar.addEventListener("pointermove",e=>{
      if(dragHotkey?.id!==e.pointerId)return;
      place(hotkeyCard,dragHotkey.left+e.clientX-dragHotkey.x,
        dragHotkey.top+e.clientY-dragHotkey.y);
    });
    const stopHotkeyDrag=e=>{
      if(dragHotkey?.id!==e.pointerId)return;dragHotkey=null;
      if(hotkeyDragBar.hasPointerCapture(e.pointerId))
        hotkeyDragBar.releasePointerCapture(e.pointerId);
    };
    hotkeyDragBar.addEventListener("pointerup",stopHotkeyDrag);
    hotkeyDragBar.addEventListener("pointercancel",stopHotkeyDrag);
    // Right-click on an actual MUTE/SOLO command, not an entire strip.
    document.addEventListener("contextmenu",e=>{
      // Never expose Chromium/WebView2 browser menu inside the native app.
      e.preventDefault();
      dismissMenu();
      const power=e.target.closest?.('[data-action="slotPower"],[data-action="fxOn"]');
       if(power){
         const selected=nativeFxHotkeyTarget(power);
         if(selected){
           menuTrack=scope.findTrack(power);menuSlot=selected.slot??-1;
           menuTarget=selected;requestHotkeyList();
           showMenu(e.clientX,e.clientY,"hotkey");
         }
         return;
       }
       const button=e.target.closest?.('.mx-hw-key[data-action="mute"],.mx-hw-key[data-action="solo"]');
      if(button){
        const track=scope.findTrack(button);
        const operation=button.dataset.action==="mute"?"SET_MUTE":"SET_SOLO";
        const channelId=eligibleHotkey(track,operation);
        if(channelId){
          menuTrack=track;menuSlot=-1;
          menuTarget={kind:"hotkey",channelId,operation,scope:"SESSION",
            label:track.name+" · "+button.dataset.action.toUpperCase()};
          showMenu(e.clientX,e.clientY,"hotkey");
        }
        return;
      }
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
        const paramTarget=fxParameterOwner(track,slot);
        const orbitTarget=hooks.orbitTarget(track,slot);
        if(!paramTarget&&!orbitTarget)return;
        menuTrack=track;menuSlot=slot;menuTarget=orbitTarget||paramTarget;
        showMenu(e.clientX,e.clientY,paramTarget?"fx":"orbit");
      }
    },true);
    menu.addEventListener("click",e=>{
      const btn=e.target.closest("[data-menu]");if(!btn)return;
      const action=btn.dataset.menu,track=menuTrack,slot=menuSlot;
      const selected=menuTarget;
      // Read coordinates before dismissMenu(): hidden menu offset is zero.
      const originX=menu.offsetLeft,originY=menu.offsetTop+menu.offsetHeight;
      if(action==="hotkey-register"){dismissMenu();openHotkeyRegister();return;}
      if(action==="fx-param-last"||action==="fx-param-browse"){
        const mode=action==="fx-param-last"?"last":"browse";
        dismissMenu();openFxParamBrowser(track,slot,mode,originX,originY);return;
      }
      if(action==="hotkey-add"&&selected?.kind==="hotkey"){
        const existing=hotkeyEntries.find(m=>sameHotkeyTarget(m,selected));
         dismissMenu();openHotkeyCapture(selected,existing||null);return;
      }
      if(action==="hotkey-remove"&&selected?.kind==="hotkey"){
         const existing=hotkeyEntries.find(m=>sameHotkeyTarget(m,selected));
         dismissMenu();
         if(existing&&!hotkeyPending){
           hotkeyPending={requestId:nextHotkeyRequest(),kind:"DELETE",mapping:existing};
           backend.emitEvent("arMixHotkeyIntent",{operation:"DELETE",
             requestId:hotkeyPending.requestId,mappingId:existing.id});
         }
         return;
       }
       if(action==="hotkey-mute"||action==="hotkey-solo"){
        const operation=action==="hotkey-mute"?"SET_MUTE":"SET_SOLO";
        const channelId=eligibleHotkey(track,operation);
        dismissMenu();
        if(channelId){
          const target={channelId,operation,scope:"SESSION",
            label:track.name+" · "+(operation==="SET_MUTE"?"MUTE":"SOLO")};
          openHotkeyCapture(target,hotkeyEntries.find(m=>sameHotkeyTarget(m,target))||null);
        }
        return;
      }
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
      else if(!fxParamPanel.hidden){e.preventDefault();closeFxParamBrowser();}
      else if(!hotkeyRegister.hidden){e.preventDefault();hotkeyRegister.hidden=true;}
      else if(!orbit.hidden){e.preventDefault();e.stopImmediatePropagation();closeOrbit();}
    },true);
    window.addEventListener("blur",()=>{
      dismissMenu();
      if(!hotkeyCard.hidden && !hotkeyPending)closeHotkeyCapture();
    });
    window.addEventListener("resize",dismissMenu);
    document.addEventListener("scroll",dismissMenu,true);
  }
  if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",install,{once:true});
  else install();
})();