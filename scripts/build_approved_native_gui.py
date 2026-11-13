#!/usr/bin/env python3
"""Stage the EXACT ONE approved /preview/ Pages GUI into native JUCE WebView2.

Calls scripts/build_pages.py to generate the *same* published index.html and
assets. There is no second HTML/CSS assembler, cloned GUI, or alternate skin.
A nonvisual bridge JS is the only native-specific addition.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CANONICAL = ROOT / "site" / "preview-v2"
NATIVE_ADAPTER = ROOT / "web" / "approved-native-bridge.js"
ADAPTER_MARKER = ('<script defer src="assets/approved-native-contract.js"></script>\n'
                  '<script defer src="assets/approved-native-bridge.js"></script>\n'
                  '<script defer src="assets/approved-native-context-menu.js"></script>\n'
                  '<style data-armix-o1="physical-output">\n'
                  '.armix-native-desktop .console-body{--route-h:94px}\n'
                  '.armix-native-desktop .console-body[data-density="standard"]{--route-h:91px}\n'
                  '.armix-native-desktop .console-body[data-density="compact"]{--route-h:81px}\n'
                  '.armix-native-desktop .external-out-field .route-caption{color:#a8d0dd}\n'
                  '.armix-native-desktop .external-out-field .route-select{border-color:#547685;background:#192a32}\n'
                  '.armix-native-desktop .external-out-field .route-select[aria-busy="true"]{border-color:#ddb576}\n'
                  '.armix-native-desktop .external-out-field .route-select[data-status="NEEDS_REMAP"]{color:#d5aa8b}\n'
                  '.armix-native-desktop .external-out-state{font:500 9px/1.2 system-ui,sans-serif;letter-spacing:.025em;color:#aabdc3}\n'
                  '.armix-native-desktop .external-out-state[data-status="LIVE"]{color:#9bd3b8}\n'
                  '.armix-native-desktop .external-out-state[data-status="PENDING"]{color:#e7c081}\n'
                  '.armix-native-desktop .external-out-state[data-status="MUTED"]{color:#abbcc2}\n'
                  '.armix-native-desktop .external-out-state[data-status="NEEDS_REMAP"]{color:#e0a18b}\n'
                  '</style>')
SCHEMA = "arsonmix.approved-native-pages-bundle.v2"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_public_pages(destination: Path) -> Path:
    """Call the ONE authoritative Pages assembler; never duplicate its CSS/JS rules."""
    result = subprocess.run(
        [sys.executable, "-X", "utf8", str(ROOT / "scripts" / "build_pages.py"),
         "--out", str(destination), "--sha", "native-source-check",
         "--branch", "native-approved-gui"],
        cwd=ROOT, capture_output=True, encoding="utf-8", errors="replace",
    )
    if result.returncode:
        detail = (result.stderr or result.stdout).strip()
        raise RuntimeError(f"Pages assembler exited with status {result.returncode}: {detail}")
    public = destination / "preview"
    data = json.loads((public / "build.json").read_text("utf-8"))
    if data.get("kind") != "APPROVED_SINGLE_GUI" or data.get("canonical_url") != "/preview/":
        raise ValueError("Pages build is not the user-approved single /preview/ GUI")
    if not (public / "index.html").is_file():
        raise ValueError("Pages assembler did not generate the approved HTML")
    return public


def accepted_pages_html() -> str:
    with tempfile.TemporaryDirectory(prefix="armix-pages-source-") as tmp:
        return (build_public_pages(Path(tmp) / "public") / "index.html").read_text("utf-8")


# Native-only event seam: the original PUBLIC Pages HTML is byte-for-byte
# unchanged. Insert five small control hooks ONLY into the generated WebView2
# HTML at native build time. Every anchor must occur exactly once: no silent
# breakage if Pages GUI source evolves. No extra layout or simulated PCM.
NATIVE_EVENT_HOOKS = (
    # The mixer shell must remain visible even with zero audio endpoints or
    # before the first accepted roster. Native control intents are still
    # fenced by the bridge's authoritative ACK/readback; no mock PCM runs.
    ("Mixer · Reference Session", "Mixer · Native Session"),
    ("Interactive visual reference. Changes are local to this mockup.", "Preparing native mixer…"),
    ("SIMULATED METERS · NO REAL AUDIO", "NATIVE AUDIO"),
    # The published /preview/ HTML and its V28 checkpoint are immutable.
    # Change ONLY the compiled native WebView's operator fader travel law.
    # The DSP gain remains exact dB -> 10^(dB/20), and the mapping is
    # invertible at every tick; no new JS audio engine or controller.
    ("function faderPercent(value,isSend){return rangePct(value,-60,isSend?6:10);}\n",
     "const FADER_TAPER=Object.freeze([[-60,0],[-48,15],[-36,27],[-24,39],[-12,54],[-6,65],[0,77],[6,92],[10,100]]);\n"
     "function faderPercent(value,isSend){\n"
     " if(isSend)return rangePct(value,-60,6);\n"
     " const db=Math.max(-60,Math.min(10,Number(value)));\n"
     " for(let i=1;i<FADER_TAPER.length;i++){\n"
     "  const [hiDb,hiPct]=FADER_TAPER[i],[loDb,loPct]=FADER_TAPER[i-1];\n"
     "  if(db<=hiDb)return loPct+(db-loDb)*(hiPct-loPct)/(hiDb-loDb);\n"
     " }return 100;\n"
     "}\n"
     "function faderDbFromTravelPercent(percent,isSend){\n"
     " const p=Math.max(0,Math.min(100,Number(percent)));\n"
     " if(isSend)return -60+66*p/100;\n"
     " for(let i=1;i<FADER_TAPER.length;i++){\n"
     "  const [hiDb,hiPct]=FADER_TAPER[i],[loDb,loPct]=FADER_TAPER[i-1];\n"
     "  if(p<=hiPct)return loDb+(p-loPct)*(hiDb-loDb)/(hiPct-loPct);\n"
     " }return 10;\n"
     "}\n"),
    ("scaleTopPct(v,-60,gainMax)",
     "(100.0-faderPercent(v,isSend))"),
    (" let gain=Math.round((max-(max-min)*frac)*10)/10;\n",
     " let gain=Math.round(faderDbFromTravelPercent(100*(1-frac),max===6)*10)/10;\n"),
    # The native fader has a calibrated logarithmic travel (unity is 77%),
    # while the public reference uses a linear dB scale. The detent MUST
    # snap at the visual/PCM unity position of the native tapered curve.
    (" const unityY=travel.top+travel.height*(max/(max-min));\n",
     " const unityY=travel.top+travel.height*(1-faderPercent(0,max===6)/100);\n"),
    # Only the compiled Windows WebView gets verified native driver inputs.
    # Public /preview/ continues to show its labeled simulated reference.
    ("const channelOptions=(t,type)=>{\n",
     "const channelOptions=(t,type)=>{\n"
     " if(type==='external'&&window.__JUCE__?.backend)\n"
     "  return window.__ARMIX_NATIVE_M2__?.externalChoices?.(t)||['None'];\n"
     " if(type==='input'&&window.__JUCE__?.backend)\n"
     "  return window.__ARMIX_NATIVE_M2__?.inputChoices?.(t)||['None'];\n"
     " if(type==='output'&&window.__JUCE__?.backend)\n"
     "  return window.__ARMIX_NATIVE_M2__?.outputChoices?.(t)||[t.output];\n"),
    (" code+=sectionFx(t)+sectionSend(t);\n",
     " if(t.kind!=='master'){\n"
     "  if(!code.endsWith('</div></div>'))throw Error('Native O1 route anchor drift');\n"
     "  const extRow='<div class=\"route-field external-out-field\"><span class=\"route-caption\">EXT</span>'+\n"
     "   '<select class=\"route-select\" data-field=\"external\" title=\"Separate post-FX physical ASIO output, internal WIRE unchanged\" aria-label=\"'+escapeHtml(t.name)+' external output\">'+\n"
     "   option(channelOptions(t,'external'),t.externalOutput||'None')+'</select><span class=\"external-out-state\" aria-label=\"External output status\"></span></div>';\n"
     "  code=code.slice(0,-6)+extRow+'</div>';\n"
     " }\n"
     " code+=sectionFx(t)+sectionSend(t);\n"),
    ("function wireGraphCommit(ops,{record=true}={}){\n",
     "function wireGraphCommit(ops,{record=true}={}){\n"
     " if(window.__JUCE__?.backend){window.__ARMIX_NATIVE_M2__?.consumeWire?.(ops);return false;}\n"),
    ("function wireWsUndo(){\n",
     "function wireWsUndo(){\n"
     " if(window.__JUCE__?.backend){window.__ARMIX_NATIVE_M2__?.consumeUndo?.();return false;}\n"),
    ("function handleField(event){\n",
     "function handleField(event){\n"
     " if(window.__JUCE__?.backend){window.__ARMIX_NATIVE_M2__?.consumeField?.(event);return;}\n"),
    # Native-only: the SAME original quick picker/manager owns the UI.
    # Only insertion leaves the demo reducer and goes to JUCE's actual VST3
    # graph; never mark a browser insert as loaded.
    ("function applyChannelIdentity(){\n",
     "function applyChannelIdentity(){\n"
     " if(window.__JUCE__?.backend&&!window.__ARMIX_NATIVE_M2__?.applyIdentity?.())return;\n"),
    ("function insertCatalogPlugin(id){\n",
     "function insertCatalogPlugin(id){\n"
     " if(window.__JUCE__?.backend){window.__ARMIX_NATIVE_M2__?.insertPlugin?.(id,activeFxTarget);return;}\n"),
    (" if(a==='wireExpand'){document.querySelector('.section-rail",
     " if(window.__JUCE__?.backend&&!window.__ARMIX_NATIVE_M2__?.enabled)return;\n"
     " if(window.__ARMIX_NATIVE_M2__?.consumeAction?.(t,a,btn))return;\n"
     " if(a==='wireExpand'){document.querySelector('.section-rail"),
    ("document.querySelector('.console-body').addEventListener('dblclick',e=>{\n"
     " const t=findTrack(e.target);if(!t)return;\n",
     "document.querySelector('.console-body').addEventListener('dblclick',e=>{\n"
     " const t=findTrack(e.target);if(!t)return;\n"
     " if(window.__JUCE__?.backend&&!window.__ARMIX_NATIVE_M2__?.enabled)return;\n"
     " if(window.__ARMIX_NATIVE_M2__?.consumeDoubleClick?.(e,t))return;\n"),
    # Native-only: prevent keyboard FX moves from mutating demo inserts in JUCE.
    ("function fxNativeTransactionRequired(){\n",
     "function fxNativeTransactionRequired(){\n"
     " if(window.__JUCE__?.backend)return true;\n"),
    # V60 source sends the final change through handleField() exactly once
    # on pointer release. The existing native-only handleField interception
    # below/above takes that committed target to the ACK/readback dispatcher.
    # Do NOT install a second synthetic pointerup native event.
    # Stage B: retain the approved exact V51 portrait meter gradient/peak hold
    # and dBFS calibration. Only elide redundant WebView2 style mutations:
    # these are authoritative native telemetry writes, NOT simulated signal.
    # The ORIGINAL Pages model and CSS are untouched; native_html is exactly
    # reversible by public_from_native, and no new stylesheet is loaded.
    ("const meterPeakHolds=new Map();\n",
     "const meterPeakHolds=new Map();\n"
     "const nativeMeterPainted=new WeakMap(); // native only, freed with detached fills\n"),
    (" const litPx=Math.min(geometry.height,steps*METER_STEP_PX);\n"
     " fill.style.setProperty('--lit',(100*litPx/geometry.height)+'%');\n",
     " const litPx=Math.min(geometry.height,steps*METER_STEP_PX);\n"
     " let painted=nativeMeterPainted.get(fill);\n"
     " if(!painted){painted={marker:lane.querySelector('.meter-peak-hold')};nativeMeterPainted.set(fill,painted);}\n"
     " const lit=(100*litPx/geometry.height)+'%';\n"
     " if(painted.lit!==lit){fill.style.setProperty('--lit',lit);painted.lit=lit;}\n"),
    (" const marker=lane.querySelector('.meter-peak-hold');\n"
     " if(marker){\n"
     "  marker.style.opacity=peak.steps>0?'1':'0';\n",
     " const marker=painted.marker;\n"
     " if(marker){\n"
     "  const opacity=peak.steps>0?'1':'0';\n"
     "  if(painted.opacity!==opacity){marker.style.opacity=opacity;painted.opacity=opacity;}\n"),
    ("  marker.style.setProperty('--peak-extent',extent+'px');\n",
     "  const peakExtent=extent+'px';\n"
     "  if(painted.peakExtent!==peakExtent){marker.style.setProperty('--peak-extent',peakExtent);painted.peakExtent=peakExtent;}\n"),
    # Stage B: WIRE idle pointer proximity still works, but avoid forcing a
    # synchronous layout read for *every mousemove* while a 12 Hz snapshot
    # updates native meters. Bound geometry age and invalidate on scroll/resize.
    ("let nearWireOut=null;\n",
     "let nearWireOut=null;\n"
     "let nativeWireBounds=new WeakMap();\n"
     "window.addEventListener('resize',()=>{nativeWireBounds=new WeakMap();},{passive:true});\n"
     "window.addEventListener('scroll',()=>{nativeWireBounds=new WeakMap();},true);\n"),
    (" let out=tray?.querySelector('.wire-node-out:not(:disabled)')||null;\n"
     " if(out){\n"
     "  const r=out.getBoundingClientRect();\n",
     " let out=tray?.querySelector('.wire-node-out:not(:disabled)')||null;\n"
     " if(out){\n"
     "  const now=performance.now();\n"
     "  let box=nativeWireBounds.get(out);\n"
     "  if(!box||now-box.at>=120){box={at:now,rect:out.getBoundingClientRect()};nativeWireBounds.set(out,box);}\n"
     "  const r=box.rect;\n"),
    # Native WebView2 only: expose the original Pages IIFE model to the thin
    # bridge. No second renderer/state reducer, no change to public /preview/.
    ("restorePluginPreferences();init();\n})();",
     "let verifiedM4WireSignature='',verifiedM4WireDirty=false;\n"
     "window.__ARMIX_NATIVE_SCOPE__=Object.freeze({\n"
     " get tracks(){return tracks;},get master(){return master;},\n"
     " get wireEdges(){return wireEdges;}, // native read-only UI edge identity\n"
     " get sofEnabled(){return sofEnabled;},get sofTarget(){return sofTarget;},\n"
     " get sofTargetId(){return sofTargetId;},toggleSof,\n"
     " get selectedWireEdgeId(){return selectedWireEdgeId;},\n"
     " get wireGainDragging(){return !!sendKnobDrag;},paintWireKnob,\n"
     " syncNativeWireGains(rows){\n"
     "  for(const row of rows){\n"
     "   const edge=wireEdges.find(e=>e.id===row.id&&e.kind!=='primary');\n"
     "   if(!edge||!Number.isFinite(row.levelDb))continue;\n"
     "   edge.levelDb=row.levelDb;\n"
     "   const input=document.querySelector('.wire-gain[data-wire-id=\"'+edge.id+'\"]');\n"
     "   if(input&&input!==sendKnobDrag?.input&&!row.pending){\n"
     "    input.value=String(row.levelDb);paintWireKnob(input,row.levelDb);\n"
     "   }\n"
     "   // Inspector LCD follows independent accepted native PCM readback,\n"
     "   // not a speculative range input. No strip or inspector rerender.\n"
     "   const ws=document.getElementById('wireWsGain');\n"
     "   if(ws?.dataset.wsGain===String(edge.id)&&!row.pending){\n"
     "    if(document.activeElement!==ws)ws.value=String(row.levelDb);\n"
     "    const value=document.getElementById('wireWsGainText');\n"
     "    if(value)value.textContent=row.levelDb+' dB';\n"
     "   }\n"
     "  }\n"
     " },\n"
     " syncVerifiedM4Wires(rows,ids){\n"
     "  if(!Array.isArray(rows)||rows.length!==10||!Array.isArray(ids)||ids.length!==10)\n"
     "   return false;\n"
     "  const tracksById=new Map(tracks.filter(t=>t.kind==='audio').map(t=>[t.id,t]));\n"
     "  if(new Set(ids).size!==10||ids.some(id=>!tracksById.has(id))||\n"
     "     rows.some(r=>!r||typeof r.main!=='boolean'))return false;\n"
     "  const next=[];\n"
     "  for(let i=0;i<10;i++)if(rows[i].main){\n"
     "   const prior=wireEdges.find(e=>e.enabled&&e.kind==='primary'&&\n"
     "     e.sourceId===ids[i]&&e.destId===master.id);\n"
     "   next.push(prior||{id:nextWireId++,sourceId:ids[i],destId:master.id,\n"
     "     kind:'primary',mode:'postFader',enabled:true,levelDb:0});\n"
     "  }\n"
     "  const sig=rows.map((r,i)=>r.main?String(ids[i]):'').join('|');\n"
     "  if(sig!==verifiedM4WireSignature || wireEdges.length!==next.length ||\n"
     "     next.some((e,i)=>wireEdges[i]!==e)){\n"
     "   wireEdges=next;verifiedM4WireSignature=sig;verifiedM4WireDirty=true;\n"
     "   if(selectedWireEdgeId!==null&&!wireEdges.some(e=>e.id===selectedWireEdgeId))\n"
     "    selectedWireEdgeId=null;\n"
     "   syncWireOutputProjection();\n"
     "  }\n"
     "  if(verifiedM4WireDirty&&!wireGesture&&!faderGesture&&\n"
     "     !document.activeElement?.matches?.('.strip select')){\n"
     "   verifiedM4WireDirty=false;refreshWireRoutingNodes();\n"
     "  }\n"
     "  return true;\n"
     " },\n"
     " make,render,findTrack,displayMeterDbfs,revealTrack,\n"
     " openChannelIdentity,closeChannelIdentity,\n"
     " get identityTrackId(){return identityTrackId;},\n"
     " get identityColor(){return identityColor;},get identityIcon(){return identityIcon;},\n"
     " refreshStrip,syncFaderVisual,openFastPicker,openPluginBrowser,\n"
     " get activeFaderId(){return faderGesture?Number(faderGesture.input.closest('.strip')?.dataset.id):-1;},\n"
     " setNativeCatalog(rows){\n"
     "  pluginCatalog=rows.filter(x=>x&&typeof x.id==='string'&&typeof x.name==='string').map(x=>({\n"
     "   id:x.id,name:x.name,vendor:x.vendor||'Unknown',category:x.category||'Utility',\n"
     "   format:x.format==='Native'?'Native':'VST3',status:'ready',latencySamples:null,\n"
     "   favorite:!!x.favorite,hidden:false}));\n"
     "  const label=document.querySelector('.plugin-disclaimer');\n"
     "  if(label)label.textContent='Native EQ/Compressor/Harmony ready · external VST3 require verified scan';\n"
     "  renderPluginCatalog();renderFastPicker();return pluginCatalog.length;\n"
     " },\n"
     " closeNativePicker(){setFastPicker(false);},\n"
     " colors:COLORS,channelIcons:CHANNEL_ICONS,wireGraph:ArSonWireGraph,\n"
     " allocateWireId(){return nextWireId++;},kickWireSpring,\n"
     " installGraph(channels,main,edges){\n"
     "  tracks=channels;master=main;wireEdges=edges;\n"
     "  wireWsHistory=[];selectedWireEdgeId=null;\n"
     "  syncWireOutputProjection();render();\n"
     " },\n"
     " stopDemo(){if(preview)togglePreview();}\n"
     "});\nrestorePluginPreferences();init();\n})();"),
)


# Native-only WIRE Inspector: keep the public /preview/ HTML strictly unchanged.
# These fail-closed exact-source edits are reversed by public_from_native().
# A PRIMARY_OUT is unity, a PARALLEL_SEND can target MAIN, and native edits
# are never accepted by the browser mock before installed PCM readback.
NATIVE_EVENT_HOOKS += (
    (" const tapEditable=!native||(src.nativeTapAvailable===true&&\n  edge?.kind==='send'&&['PRE_FADER','POST_FADER'].includes(edge.nativeTap));\n",
     " const tapEditable=edge&&(!native?edge.kind!=='primary':\n  window.__ARMIX_NATIVE_M2__?.canWireTap?.(edge.id,edge.sourceId)===true);\n"),
    (" const controls=edge?\n  '<div class=\"wire-ws-controls\"><label for=\"wireWsGain\">SEND LEVEL</label>'+\n  '<input type=\"range\" id=\"wireWsGain\" data-ws-gain=\"'+edge.id+'\" min=\"-60\" max=\"6\" step=\"1\" value=\"'+edge.levelDb+'\" '+(dst.kind==='master'?'disabled':'')+'>'+\n  '<span id=\"wireWsGainText\">'+edge.levelDb+' dB</span>'+\n  '<label for=\"wireWsTap\">TAP</label>'+\n  '<select id=\"wireWsTap\" data-ws-tap=\"'+edge.id+'\" '+\n  (edge.kind==='primary'||!tapEditable?'disabled':'')+\n  (native?' title=\"Change this exact SEND tap after native PCM ACK\"':'')+'>'+\n  '<option value=\"send\" '+(tapValue==='send'?'selected':'')+'>'+\n   (native?'POST FADER':'Follow Fader')+'</option>'+\n  '<option value=\"split\" '+(tapValue==='split'?'selected':'')+'>'+\n   (native?'PRE FADER':'Pre FX')+'</option></select>'+\n  '<button type=\"button\" data-ws-remove=\"'+key+'\">'+(wireWsRemoveConfirm===key?'Confirm remove':'Disconnect')+'</button></div>':\n  '<div class=\"wire-ws-controls\"><span>Managed by existing '+escapeHtml(info.kind.toUpperCase())+' routing. No duplicate WIRE edge.</span></div>';\n",
     " const controls=edge?\n  '<div class=\"wire-ws-controls\"><label for=\"wireWsGain\">SEND LEVEL</label>'+\n  '<input type=\"range\" id=\"wireWsGain\" data-ws-gain=\"'+edge.id+'\" min=\"-60\" max=\"6\" step=\"1\" value=\"'+edge.levelDb+'\" '+\n   (gainEditable?'':'disabled')+' title=\"'+(edge.kind==='primary'?'PRIMARY OUT · fixed unity':\n     native?'SEND LEVEL · native PCM ACK required':'SEND LEVEL · GUI reference only')+'\">'+\n  '<span id=\"wireWsGainText\">'+(edge.kind==='primary'?'PRIMARY · UNITY':\n    edge.levelDb+' dB')+'</span>'+\n  '<label for=\"wireWsTap\">TAP</label>'+\n  '<select id=\"wireWsTap\" data-ws-tap=\"'+edge.id+'\" '+\n  (edge.kind==='primary'||!tapEditable?'disabled':'')+\n  (native?' title=\"Change this exact SEND tap after native PCM ACK\"':'')+'>'+\n  '<option value=\"send\" '+(tapValue==='send'?'selected':'')+'>'+\n   (native?'POST FADER':'Follow Fader')+'</option>'+\n  '<option value=\"split\" '+(tapValue==='split'?'selected':'')+'>'+\n   (native?'PRE FADER':'Pre FX')+'</option></select>'+\n  '<button type=\"button\" data-ws-remove=\"'+key+'\">'+(wireWsRemoveConfirm===key?'Confirm remove':'Disconnect')+'</button></div>':\n  '<div class=\"wire-ws-controls\"><span>Managed by existing '+escapeHtml(info.kind.toUpperCase())+' routing. No duplicate WIRE edge.</span></div>';\n"),
    ("document.getElementById('wireWsInspector').addEventListener('input',e=>{\n const gain=e.target.closest?.('[data-ws-gain]');\n if(!gain)return;\n const edge=wireEdges.find(x=>x.id===Number(gain.dataset.wsGain)&&x.enabled);\n if(!edge||edge.destId===master.id)return;\n const amount=Math.max(-60,Math.min(6,Math.round(Number(gain.value)||0)));\n if(!wireGraphCommit([{type:'GAIN',sourceId:edge.sourceId,edgeId:edge.id,\n  levelDb:amount}],{record:false}))return;\n document.getElementById('wireWsGainText').textContent=amount+' dB';\n});\ndocument.getElementById('wireWsInspector').addEventListener('change',e=>{\n const gain=e.target.closest?.('[data-ws-gain]'),tap=e.target.closest?.('[data-ws-tap]');\n if(tap){\n  const edge=wireEdges.find(x=>x.id===Number(tap.dataset.wsTap)&&x.enabled);\n  if(!edge||!wireGraphCommit([{type:'MODE',sourceId:edge.sourceId,edgeId:edge.id,\n   mode:tap.value==='split'?'split':'send'}],{record:false}))return;\n  const owner=wireSourceTrack(edge.sourceId);if(owner)owner.wireTapMode=tap.value==='split'?'split':'send';\n }\n if(gain||tap){\n  wireWsRecord(wireWsEditBefore);wireWsEditBefore=null;render();renderWireWorkspace();\n  say('WIRE inspector accepted · reference graph only');\n }\n});\n\n",
     "document.getElementById('wireWsInspector').addEventListener('input',e=>{\n const gain=e.target.closest?.('[data-ws-gain]');\n if(!gain)return;\n const edge=wireEdges.find(x=>x.id===Number(gain.dataset.wsGain)&&x.enabled);\n if(!edge||edge.kind==='primary')return;\n const amount=Math.max(-60,Math.min(6,Math.round(Number(gain.value)||0)));\n if(window.__JUCE__?.backend){\n  if(window.__ARMIX_NATIVE_M2__?.canWireGain?.(edge.id,edge.sourceId)!==true)return;\n  // Coalesce rapid gestures in the existing native control queue. This LCD\n  // shows a pending operation, NEVER a speculative accepted PCM level.\n  window.__ARMIX_NATIVE_M2__?.consumeWire?.([{type:'GAIN',\n   sourceId:edge.sourceId,edgeId:edge.id,levelDb:amount}]);\n  const readout=document.getElementById('wireWsGainText');\n  if(readout)readout.textContent='PENDING · '+amount+' dB';\n  return;\n }\n if(!wireGraphCommit([{type:'GAIN',sourceId:edge.sourceId,edgeId:edge.id,\n  levelDb:amount}],{record:false}))return;\n document.getElementById('wireWsGainText').textContent=amount+' dB';\n});\ndocument.getElementById('wireWsInspector').addEventListener('change',e=>{\n const gain=e.target.closest?.('[data-ws-gain]'),tap=e.target.closest?.('[data-ws-tap]');\n if(window.__JUCE__?.backend){\n  if(gain){\n   const edge=wireEdges.find(x=>x.id===Number(gain.dataset.wsGain)&&x.enabled);\n   if(edge?.kind!=='primary'&&window.__ARMIX_NATIVE_M2__?.canWireGain?.(edge.id,edge.sourceId)===true){\n    // Keyboard-only range change may not have emitted 'input'.\n    window.__ARMIX_NATIVE_M2__?.consumeWire?.([{type:'GAIN',sourceId:edge.sourceId,\n     edgeId:edge.id,levelDb:Math.max(-60,Math.min(6,Math.round(Number(gain.value)||0)))}]);\n   }\n  }\n  if(tap){\n   const edge=wireEdges.find(x=>x.id===Number(tap.dataset.wsTap)&&x.enabled);\n   const requested=tap.value==='split'?'split':'send';\n   // Restore the accepted selector position immediately. An independent\n   // structural readback will move it after the engine actually publishes.\n   tap.value=edge?.nativeTap==='PRE_FADER'?'split':'send';\n   if(edge&&window.__ARMIX_NATIVE_M2__?.canWireTap?.(edge.id,edge.sourceId)===true){\n    window.__ARMIX_NATIVE_M2__?.consumeWire?.([{type:'MODE',sourceId:edge.sourceId,\n     edgeId:edge.id,mode:requested}]);\n    say('WIRE tap requested · awaiting accepted native PCM readback');\n   }else say('WIRE tap unavailable · native graph or EdgeId changed');\n  }\n  wireWsEditBefore=null;return; // native owns state; no demo undo or full repaint\n }\n if(tap){\n  const edge=wireEdges.find(x=>x.id===Number(tap.dataset.wsTap)&&x.enabled);\n  if(!edge||!wireGraphCommit([{type:'MODE',sourceId:edge.sourceId,edgeId:edge.id,\n   mode:tap.value==='split'?'split':'send'}],{record:false}))return;\n  const owner=wireSourceTrack(edge.sourceId);if(owner)owner.wireTapMode=tap.value==='split'?'split':'send';\n }\n if(gain||tap){\n  wireWsRecord(wireWsEditBefore);wireWsEditBefore=null;render();renderWireWorkspace();\n  say('WIRE inspector accepted · reference graph only');\n }\n});\n\n"),
)



def native_html(approved: str | None = None) -> str:
    original = accepted_pages_html() if approved is None else approved
    newline = "\r\n" if "\r\n" in original else "\n"
    html = original.replace("\r\n", "\n")
    if html.count("</body>") != 1 or ADAPTER_MARKER in html:
        raise ValueError("Approved Pages must have exactly one body and no prior native injection")
    for before, after in NATIVE_EVENT_HOOKS:
        if html.count(before) != 1 or after in html:
            raise ValueError("Native-only M2 control hook anchor drift: " + before[:55])
        html = html.replace(before, after, 1)
    html = html.replace("</body>", ADAPTER_MARKER + "\n</body>")
    return html.replace("\n", newline)


def public_from_native(native: str) -> str:
    """Reconstruct original public Pages HTML byte-for-byte from the native source."""
    newline = "\r\n" if "\r\n" in native else "\n"
    html = native.replace("\r\n", "\n")
    if html.count(ADAPTER_MARKER + "\n") != 1:
        raise ValueError("Native adapter marker missing or duplicated")
    html = html.replace(ADAPTER_MARKER + "\n", "", 1)
    for before, after in reversed(NATIVE_EVENT_HOOKS):
        if html.count(after) != 1:
            raise ValueError("Native-only M2 control hook missing or duplicated")
        html = html.replace(after, before, 1)
    return html.replace("\n", newline)


def safe_symbol(filename: str) -> str:
    # JUCE 8 BinaryData: 'mixer-control-keys.css' -> 'mixercontrolkeys_css'
    stem, ext = filename.rsplit(".", 1)
    return re.sub(r"[^A-Za-z0-9_]", "", stem).replace("_", "") + "_" + re.sub(r"[^A-Za-z0-9]", "", ext)


def resource_header(files: list[Path], root: Path) -> str:
    identifiers = {}
    lines = [
        "// Generated from scripts/build_pages.py /preview/. DO NOT EDIT.",
        "// Only the approved Pages renderer; NO legacy web/index or console-native UI.",
        "#pragma once",
        "inline bool resolveApprovedGuiResource(const juce::String& request,",
        "                                       const char*& data, int& size, juce::String& mime)",
        "{",
        '    const auto path = request.upToFirstOccurrenceOf("?", false, false);',
    ]
    for file in files:
        uri = "/" + file.relative_to(root).as_posix()
        sym = safe_symbol(file.name)
        if sym in identifiers:
            raise ValueError(f"JUCE symbol collision: {sym}: {identifiers[sym]}, {uri}")
        identifiers[sym] = uri
        mime = mimetypes.guess_type(file.name)[0] or "application/octet-stream"
        if file.suffix == ".js":
            mime = "text/javascript"
        elif file.suffix == ".css":
            mime = "text/css"
        condition = '(path == "/" || path == "/index.html")' if uri == "/index.html" else f"(path == {json.dumps(uri)})"
        lines.extend([
            f"    if {condition}",
            "    {",
            f"        data = BinaryData::{sym};",
            f"        size = BinaryData::{sym}Size;",
            f"        mime = {json.dumps(mime)};",
            "        return true;",
            "    }",
        ])
    return "\n".join([*lines, "    return false;", "}", ""])


def stage(out: Path) -> dict:
    out = out.resolve()
    if out in (ROOT, ROOT / "site", ROOT / "web", CANONICAL) or (out / ".git").exists():
        raise ValueError(f"Unsafe bundle destination: {out}")
    with tempfile.TemporaryDirectory(prefix="armix-native-pages-") as tmp:
        pages = build_public_pages(Path(tmp) / "public")
        approved = (pages / "index.html").read_bytes()
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        shutil.copytree(pages / "assets", out / "assets")
        shutil.copy2(NATIVE_ADAPTER, out / "assets" / "approved-native-bridge.js")
        shutil.copy2(ROOT / "web" / "approved-native-context-menu.js",
                     out / "assets" / "approved-native-context-menu.js")
        shutil.copy2(ROOT / "web" / "approved-native-contract.js",
                     out / "assets" / "approved-native-contract.js")
        native = native_html(approved.decode("utf-8")).encode("utf-8")
        (out / "index.html").write_bytes(native)
    files = [out / "index.html", *sorted(p for p in (out / "assets").rglob("*") if p.is_file())]
    if any(p.is_symlink() or p.relative_to(out).parts[0] not in ("index.html", "assets") for p in files):
        raise ValueError("Unsafe GUI resource input")
    if len(files) != len({f.name for f in files}):
        raise ValueError("Duplicate GUI asset basename would collide in JUCE BinaryData")
    (out / "ApprovedGuiResources.h").write_text(resource_header(files, out), encoding="utf-8")
    manifest = {
        "schema": SCHEMA,
        "authoritativePagesRoute": "/preview/",
        "authoritativePagesOutput": "preview/index.html",
        "canonicalSource": "site/preview-v2/index.html",
        "canonicalSourceSha256": sha256((CANONICAL / "index.html").read_bytes()),
        "approvedPagesHtmlSha256": sha256(approved),
        "nativeHtmlSha256": sha256(native),
        "adapter": "web/approved-native-bridge.js",
        "adapterState": "SCAFFOLD_ONLY__NOT_LIVE_PCM",
        "files": [{"path":p.relative_to(out).as_posix(), "sha256":sha256(p.read_bytes())} for p in files],
    }
    (out / "approved-gui-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def verify(out: Path) -> dict:
    if not out.is_dir():
        raise ValueError("Approved native bundle missing")
    before = (out / "approved-gui-manifest.json").read_bytes()
    existing = json.loads(before)
    with tempfile.TemporaryDirectory(prefix="armix-approved-check-") as tmp:
        expected_out = Path(tmp) / "bundle"
        expected = stage(expected_out)
        if existing != expected:
            # Diagnose the exact differing byte source. Never weaken the gate,
            # and never hide a Python/zlib or asset generation mismatch behind
            # an unhelpful generic manifest error.
            mismatches = []
            for key in sorted(set(existing) | set(expected)):
                old, new = existing.get(key), expected.get(key)
                if old == new:
                    continue
                if key == "files" and isinstance(old, list) and isinstance(new, list):
                    old_files = {x["path"]: x["sha256"] for x in old}
                    new_files = {x["path"]: x["sha256"] for x in new}
                    for path in sorted(old_files.keys() | new_files.keys()):
                        if old_files.get(path) != new_files.get(path):
                            mismatches.append(
                                f"resource={path} existing={old_files.get(path)} regenerated={new_files.get(path)}")
                else:
                    mismatches.append(f"field={key} existing={old!r} regenerated={new!r}")
            raise ValueError("STALE OR WRONG APPROVED GUI BUNDLE: " +
                             "; ".join(mismatches[:4]))
        paths = {"index.html", "ApprovedGuiResources.h", "approved-gui-manifest.json"}
        paths.update(p["path"] for p in expected["files"])
        for path in sorted(paths):
            if (out / path).read_bytes() != (expected_out / path).read_bytes():
                raise ValueError(f"STALE OR WRONG APPROVED GUI BUNDLE: {path}")
    return existing


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--verify", action="store_true")
    args = p.parse_args()
    result = verify(args.out) if args.verify else stage(args.out)
    print("APPROVED_NATIVE_GUI_BUNDLE_OK", result["approvedPagesHtmlSha256"],
          len(result["files"]), "resources", result["adapterState"])


if __name__ == "__main__":
    main()
