// SPDX-License-Identifier: AGPL-3.0-only
// Native-only, on-demand legal center. Never affects the approved mixer
// renderer, user session, PCM, or Pages /preview/ reference.
(() => {
  "use strict";
  const backend = window.__JUCE__?.backend;
  if (!backend?.emitEvent) return;
  const notices = Object.freeze([
    {id:"overview", label:"ArSonMix", caption:"License scope & source"},
    {id:"agpl", label:"GNU AGPLv3", caption:"Complete license · AGPL-3.0-only"},
    {id:"hrir", label:"Measured HRIR", caption:"MIT KEMAR attribution"},
    {id:"signalsmith-stretch", label:"Signalsmith Stretch", caption:"Third-party license"},
    {id:"signalsmith-linear", label:"Signalsmith Linear", caption:"Third-party license"},
    {id:"inter", label:"Inter Typeface", caption:"SIL Open Font License"},
    {id:"vb-cable", label:"VB-CABLE", caption:"Optional third-party driver"}
  ]);
  const cache = new Map();
  const css = `
#armix-legal-backdrop{position:fixed;inset:0;z-index:2147483000;display:flex;align-items:center;justify-content:center;background:rgba(3,9,15,.72);padding:20px;box-sizing:border-box;font-family:Inter,system-ui,sans-serif;color:#e8f0f5}
#armix-legal-backdrop[hidden]{display:none!important}
#armix-legal-panel{width:min(790px,96vw);height:min(600px,88vh);min-height:330px;display:flex;flex-direction:column;background:#151f29;border:1px solid #31414e;border-radius:11px;box-shadow:0 22px 80px rgba(0,0,0,.48);overflow:hidden;font-size:13px;font-weight:400}
#armix-legal-header{display:flex;align-items:center;gap:14px;padding:17px 21px;border-bottom:1px solid #2c3945}
#armix-legal-header strong{font-weight:550;font-size:17px;letter-spacing:.1px}
#armix-legal-header small{display:block;color:#9eafbb;font-size:11px;margin-top:3px}
#armix-legal-close{margin-left:auto;border:0;border-radius:6px;background:transparent;color:#b9c9d3;font-size:20px;width:30px;height:30px;cursor:pointer}
#armix-legal-close:hover{background:#283846;color:#fff}
#armix-legal-layout{display:flex;min-height:0;flex:1}
#armix-legal-nav{width:176px;flex:none;overflow-y:auto;background:#111b24;border-right:1px solid #2c3945;padding:12px 8px;box-sizing:border-box}
.armix-legal-tab{display:block;width:100%;background:none;border:0;border-radius:6px;color:#a7b8c5;text-align:left;padding:10px 11px;font:inherit;cursor:pointer}
.armix-legal-tab:hover{background:#21303c;color:#e9f4fa}
.armix-legal-tab[aria-selected=true]{background:#273b49;color:#d6f4ff;box-shadow:inset 2px 0 #63bfd3}
#armix-legal-body{display:flex;flex-direction:column;flex:1;min-width:0}
#armix-legal-subtitle{padding:15px 20px 12px;color:#9db3c1;border-bottom:1px solid #263540;font-size:12px}
#armix-legal-text{margin:0;flex:1;overflow:auto;padding:19px 21px 22px;white-space:pre-wrap;overflow-wrap:anywhere;color:#cfdbe3;font:12px/1.7 ui-monospace,SFMono-Regular,Consolas,monospace;tab-size:4;user-select:text}
#armix-legal-footer{display:flex;align-items:center;gap:10px;padding:12px 18px;border-top:1px solid #2c3945}
#armix-legal-footer span{font-size:11px;color:#91a5b3;flex:1}
.armix-legal-action{background:transparent;border:1px solid #3d5464;color:#d7e8f2;border-radius:6px;padding:7px 12px;font:inherit;cursor:pointer}
.armix-legal-action:hover{border-color:#78bdcf;background:#253845}
.armix-legal-action:focus-visible,.armix-legal-tab:focus-visible,#armix-legal-close:focus-visible{outline:2px solid #77cadb;outline-offset:2px}
@media(max-width:560px){#armix-legal-backdrop{padding:8px}#armix-legal-panel{height:min(600px,95vh)}#armix-legal-nav{width:128px}#armix-legal-text{padding:12px;font-size:11px}#armix-legal-header{padding:12px}}
`;
  function element(tag, id, className, text) {
    const node = document.createElement(tag);
    if (id) node.id = id;
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  }
  function install() {
    const menu = document.querySelector("#moreTools .more-tools-panel");
    if (!menu || document.getElementById("armix-open-legal")) return;
    const caption = element("span", null, "more-tools-caption", "APPLICATION");
    const entry = element("button", "armix-open-legal", "tool", "About & Licenses");
    entry.type = "button";
    entry.title = "Read ArSonMix license and third-party notices offline";
    menu.append(caption, entry);

    const style = element("style", "armix-legal-style");
    style.textContent = css; document.head.append(style);
    const backdrop = element("div", "armix-legal-backdrop");
    backdrop.hidden = true;
    const panel = element("section", "armix-legal-panel");
    panel.setAttribute("role", "dialog");
    panel.setAttribute("aria-modal", "true");
    panel.setAttribute("aria-labelledby", "armix-legal-title");
    const header = element("header", "armix-legal-header");
    const identity = element("div");
    identity.append(element("strong", "armix-legal-title", null, "ArSonMix"));
    identity.append(element("small", null, null, "About & Licenses  ·  Offline legal documents"));
    const close = element("button", "armix-legal-close", null, "×");
    close.type="button"; close.setAttribute("aria-label", "Close legal information");
    header.append(identity, close);
    const layout = element("div", "armix-legal-layout");
    const nav = element("nav", "armix-legal-nav");
    nav.setAttribute("aria-label", "Legal documents");
    const body = element("main", "armix-legal-body");
    const subtitle = element("div", "armix-legal-subtitle");
    const content = element("pre", "armix-legal-text");
    content.setAttribute("tabindex", "0");
    body.append(subtitle,content);layout.append(nav,body);
    const footer=element("footer","armix-legal-footer");
    const status=element("span", null, null, "AGPL-3.0-only · Third-party terms retained");
    const source=element("button",null,"armix-legal-action","View source");
    source.type="button";source.title="Open the source code for this build";
    source.addEventListener("click",()=>backend.emitEvent("arMixOpenLegalSource",{}));
    const copy=element("button",null,"armix-legal-action","Copy text");
    copy.type="button";
    copy.addEventListener("click", async()=>{
      try {
        await navigator.clipboard.writeText(content.textContent||"");
        copy.textContent="Copied";
        copy.blur();
      } catch (_) {
        content.focus();
        const selection=window.getSelection();
        const range=document.createRange();
        range.selectNodeContents(content);
        selection?.removeAllRanges();
        selection?.addRange(range);
        copy.textContent="Select to copy";
      }
    });
    footer.append(status,source,copy);
    panel.append(header,layout,footer);
    backdrop.append(panel);
    document.body.append(backdrop);
    const buttons=[];
    let origin=null;
    let version=0;
    async function select(item) {
      const current=++version;
      buttons.forEach(button=>button.setAttribute("aria-selected",String(button.dataset.doc===item.id)));
      subtitle.textContent=item.caption;
      content.textContent="Loading embedded notice…";
      content.scrollTop=0;
      copy.textContent="Copy text";
      if (cache.has(item.id)) {content.textContent=cache.get(item.id);return;}
      try {
        const response=await fetch("./legal/"+item.id,{cache:"no-store"});
        if(!response.ok) throw Error("Notice unavailable");
        const text=await response.text();
        if (!text.trim()) throw Error("Empty notice");
        cache.set(item.id,text);
        if(current===version) content.textContent=text;
      } catch (_) {
        if(current===version) content.textContent="Unable to load the embedded legal notice. Please use the source repository.";
      }
    }
    for (const item of notices) {
      const tab=element("button",null,"armix-legal-tab",item.label);
      tab.type="button";tab.dataset.doc=item.id;
      tab.setAttribute("aria-selected","false");
      tab.addEventListener("click",()=>select(item));
      buttons.push(tab);nav.append(tab);
    }
    const dismiss=()=>{
      if(backdrop.hidden)return;
      backdrop.hidden=true;
      version++;
      origin?.focus?.();
    };
    close.addEventListener("click",dismiss);
    backdrop.addEventListener("click",event=>{if(event.target===backdrop)dismiss();});
    document.addEventListener("keydown",event=>{
      if(backdrop.hidden)return;
      if(event.key==="Escape"){event.preventDefault();event.stopImmediatePropagation();dismiss();}
      if(event.key==="Tab"){
        const focusables=[close,...buttons,content,source,copy];
        const index=focusables.indexOf(document.activeElement);
        if(event.shiftKey&&index<=0){event.preventDefault();copy.focus();}
        else if(!event.shiftKey&&index===focusables.length-1){event.preventDefault();close.focus();}
      }
    },true);
    entry.addEventListener("click",()=>{
      origin=entry;
      document.getElementById("moreTools").open=false;
      backdrop.hidden=false;
      select(notices[0]);
      close.focus();
    });
  }
  if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",install,{once:true});
  else install();
})();
