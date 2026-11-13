"""V60 approved mixer: tactile unity fader and physical full-span WIRE.
Fast --source contract runs without Playwright; browser mode tests actual DOM,
mouse gestures, responsive rail/Master geometry and SVG path animation.
This is reference GUI evidence, never native PCM or ASIO FAT.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=(ROOT/"site/preview-v2/index.html").read_text("utf-8")
FADER=(ROOT/"site/preview-v2/assets/mixer-fader-assembly.css").read_text("utf-8")
CHROME=(ROOT/"site/preview-v2/assets/mixer-chrome-material.css").read_text("utf-8")
SPLIT=(ROOT/"scripts/compose_operator_ux.py").read_text("utf-8")
WIRE=(ROOT/"site/preview-v2/assets/mixer-wire-dots.css").read_text("utf-8")


def source_contract():
    for text in ("FADER_UNITY_CAPTURE_PX=7,FADER_UNITY_RELEASE_PX=12",
                 "gesture.unityLatched=true", "if(!ev.altKey&&Math.abs",
                 "handleField({target:g.input,type:'change'",
                 "WIRE_IMPACT_TRAVEL_MS=520,WIRE_IMPACT_SETTLE_MS=160",
                 "const WIRE_RETRACT_MS=330",
                 "const tipSwing=1.15*pose.catchUp",
                 "const recoil=amplitude*.13*catchUp",
                 "function wireRetractPose(x0,y0,rx,ry,elapsedMs)",
                 "function wireRetractWave(pose,t)",
                 "function wireRetractPathData(x0,y0,rx,ry,elapsedMs)",
                 "const endWave=wireRetractWave(pose,1)",
                 "plug.setAttribute('cy',shape.pose.tipY.toFixed(1))",
                 "const passed=pose.front-t;",
                 "svg.style.clipPath='none'",
                 "const width=.65-.29*longMix-.07*shortBoost",
                 "const shortBoost=1-short01*short01*(3-2*short01)",
                 "const tinyGuard=Math.pow(Math.min(1,chord/115),1.7)",
                 "const spatialDamping=2.9-1.8*shortBoost",
                 "const length01=Math.max(0,Math.min(1,(chord-95)/300))",
                 "function startWireRetraction(sourceId,releasePoint)",
                 "if(replugRequest)kickWireSpring(replugRequest.sourceId,replugRequest.destId)",
                 "const distance=1-t,passed=front-distance;",
                 "const wave=passed>0?",
                 "const accepted=ops.filter(op=>['CONNECT','ROUTE_ONLY','RETARGET'].includes(op.type)).at(-1)",
                 "wire-drag-line is-wire-selected",
                 "latched?22:14",
                 "line.style.setProperty('--wire-selected-hue'"):
        assert text in HTML,("missing tactile contract",text)
    assert "elapsed<1600" not in HTML
    assert "const boost=Math.min(48,Math.max(0,fxBase-125))" in SPLIT
    assert "const fader=Math.max(130,Math.round(half-pan-switches-footer+boost))" in SPLIT
    assert "const fx=fxBase-boost" in SPLIT
    assert "--fader-fixed-h:clamp(" not in FADER
    assert "--v13-fader-h:clamp(198px" not in CHROME
    assert "stroke:transparent!important;" in WIRE
    assert "stroke-width:3.45px!important;opacity:.98!important;" in WIRE
    assert "78%,#ffffff 22%" in WIRE
    assert "#wireLayer path.wire-drag-line.is-wire-selected{" in WIRE
    BRIDGE=(ROOT/"web/approved-native-bridge.js").read_text("utf-8")
    assert 'event.type==="change"?18:latched?22:14' in BRIDGE
    assert "stroke-dasharray:none!important;" in WIRE
    print("PASS V60 fast source contract: detent / tall rail / full-span impulse / selected jacket")


def point_at_midpoint(path):
    return path.evaluate("""el=>{
        const pt=el.getPointAtLength(el.getTotalLength()*.48);
        const svg=el.ownerSVGElement,r=svg.getBoundingClientRect(),v=svg.viewBox.baseVal;
        return {x:r.left+(pt.x-v.x)*r.width/v.width,
                y:r.top+(pt.y-v.y)*r.height/v.height};
    }""")


def exercise(page,width,height):
    page.goto((ROOT/"dist-pages/preview/index.html").as_uri())
    page.locator('#tracks .strip[data-id="1"] .fader-box').wait_for()
    layout=page.evaluate("""()=> {
      const r=q=>{const n=document.querySelector(q);if(!n)return null;
        const b=n.getBoundingClientRect();return {y:b.y,h:b.height,bottom:b.bottom};};
      return {fader:r('#tracks .strip[data-id="1"] .fader-section'),
              master:r('#master .strip .fader-section'),
              rail:r('.section-rail .rail-cell.fader'),
              travel:r('#tracks .strip[data-id="1"] .fader-travel'),
              fx:r('#tracks .strip[data-id="1"] .fx-section'),
              footer:r('#tracks .strip[data-id="1"] .bottom-section'),
              token:getComputedStyle(document.querySelector('.console-body')).getPropertyValue('--fader-fixed-h').trim(),
              flex:getComputedStyle(document.querySelector('#tracks .strip[data-id="1"] .fader-section')).flexBasis};
    }""")
    a,b,c=layout["fader"],layout["master"],layout["rail"]
    assert all(x and x["h"]>0 for x in (a,b,c)),layout
    assert abs(a["h"]-b["h"])<2 and abs(a["h"]-c["h"])<2,(width,layout)
    min_height=370 if height>=1000 else (320 if height>=850 else 255)
    assert a["h"]>=min_height,(width,"tall hardware fader lower bound",layout)
    print("V60 geometry",width,height,"fader",a["h"],"CSS token",layout["token"],"flex",layout["flex"])
    assert layout["travel"]["h"]>a["h"]-110,(width,layout)
    assert layout["fx"]["h"]>=23,(width,"FX cell disappeared",layout)
    assert a["bottom"]<layout["footer"]["bottom"],(width,"fader overlaps footer",layout)

    strip=page.locator('#tracks .strip[data-id="1"]')
    box=strip.locator(".fader-box").bounding_box()
    travel=strip.locator(".fader-travel").bounding_box()
    fader=strip.locator('input[data-field="level"]')
    x=box["x"]+box["width"]/2
    zero=travel["y"]+travel["height"]*(10/70)
    page.mouse.move(x,zero+5)
    page.mouse.down()
    assert abs(float(fader.input_value()))<.001,(width,"unity detent failed")
    page.mouse.move(x,zero+10)
    assert abs(float(fader.input_value()))<.001,(width,"unity detent hold failed")
    page.mouse.up()
    assert abs(float(fader.input_value()))<.001,(width,"unity release failed")
    page.mouse.move(x,zero+5)
    page.mouse.down()
    page.mouse.move(x,zero+22)
    page.mouse.up()
    assert float(fader.input_value())<-.3,(width,"fader trapped at zero")
    page.keyboard.down("Alt")
    page.mouse.move(x,zero+5)
    page.mouse.down()
    page.mouse.up()
    page.keyboard.up("Alt")
    assert abs(float(fader.input_value()))>.25,(width,"Alt must bypass detent")
    # Restored unity can be stored and re-rendered without display rounding.
    page.mouse.move(x,zero)
    page.mouse.down()
    page.mouse.up()
    assert abs(float(fader.input_value()))<.001
    assert "0.0" in strip.locator(".fader-readout").inner_text()
    return layout


def test_pan(page):
    # Real DOM input events, native-style hysteresis semantics, and bypass.
    pan=page.locator('#tracks .strip[data-id="1"] .center-pan')
    pan.wait_for()
    def value(v):
        pan.evaluate("""(el,val)=>{
            el.value=String(val);
            el.dispatchEvent(new Event('input',{bubbles:true}));
        }""",v)
        return float(pan.input_value())
    assert value(12)==0,("PAN failed to capture center",pan.input_value())
    assert value(20)==0,("PAN lost center too early",pan.input_value())
    assert value(29)==29,("PAN cannot escape sticky detent",pan.input_value())
    assert value(16)==16,("PAN outside capture should remain adjustable",pan.input_value())
    pan.evaluate("(el)=>el.dispatchEvent(new Event('change',{bubbles:true}))")
    assert float(pan.input_value())==0,"PAN should settle to center on release"
    # Keyboard arrow remains fine-step, independent of drag snapping.
    pan.focus()
    page.keyboard.press("ArrowRight")
    assert 0<float(pan.input_value())<=2,"Keyboard fine PAN was trapped"
    pan.evaluate("(el)=>{el.dataset.panAltPointer='true';}")
    assert value(8)==8,"Alt/pointer fine PAN bypass was lost"
    pan.evaluate("(el)=>delete el.dataset.panAltPointer")
    assert value(-8)==0,"PAN reverse side must also snap"
    assert pan.get_attribute("aria-valuetext")=="Center"


def test_wire(page):
    # The SEND bay can be folded by default on the assembled preview.
    # Use the actual operator rail to reveal the physical sockets.
    body=page.locator(".console-body")
    if "collapsed-send" in (body.get_attribute("class") or ""):
        page.locator('.section-rail [data-section="send"]').click()
    source=page.locator('#tracks .strip[data-id="1"] .wire-node-out')
    target=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
    assert source.count()==target.count()==1
    # Listen before a user-originated WIRE acceptance. The observer records
    # only real outgoing jacket path mutations, no synthetic second renderer.
    page.evaluate("""()=>{
      window.__v60WireSamples=[];
      const svg=document.querySelector('#wireLayer');
      window.__v60Observer=new MutationObserver(records=>{
        for(const record of records){
          const el=record.target;
          if(!el.matches?.('path.wire-outgoing-line[data-from="1"][data-to="3"],path.wire-incoming-line[data-from="1"][data-to="3"]'))continue;
          const d=el.getAttribute('d'),len=el.getTotalLength();
          const p=el.getPointAtLength(len*.90),a=el.getPointAtLength(0),
                b=el.getPointAtLength(len);
          window.__v60WireSamples.push({d,near:[p.x,p.y],start:[a.x,a.y],end:[b.x,b.y]});
        }
      });
      window.__v60Observer.observe(svg,{subtree:true,attributes:true,attributeFilter:['d']});
    }""")
    # CH01 -> CH03: simple explicit parallel graph patch, not PCM.
    source.click()
    target.click()
    cable=page.locator('#wireLayer path.wire-outgoing-line[data-from="1"][data-to="3"],#wireLayer path.wire-incoming-line[data-from="1"][data-to="3"]').first
    print("V60 wire paths",page.locator("#wireLayer path[data-from][data-to]").evaluate_all(
        "nodes=>nodes.map(n=>[n.getAttribute('class'),n.dataset.from,n.dataset.to])"))
    cable.wait_for(state="attached",timeout=8000)
    page.wait_for_timeout(850)
    samples=page.evaluate("window.__v60WireSamples")
    # Deterministic physical direction: first visible UP kick near IN,
    # OUT remains quiet initially; then the decayed crest reaches near OUT.
    # Canonical Pages GUI intentionally encapsulates its lexical graph.
    # Probe the exact checked-in motion function without exposing a runtime
    # debug global or duplicating the production renderer.
    motion=HTML[HTML.index("const WIRE_IMPACT_TRAVEL_MS="):
                HTML.index("function kickWireSpring(")]
    physics=page.evaluate("()=>{\n"+motion+"""\n
      const g=[0,0,500,0,72]; // approved long wire with anchored plugs
      const [x0,y0,x1,y1,limit]=g;
      const span=Math.abs(x1-x0),ceiling=Math.max(12,Math.min(100,Number(limit)||72));
      const sag=Math.max(10,Math.min(ceiling,Math.min(ceiling-5,28+Math.sqrt(span)*1.55)));
      const c=sag*4/3;
      const baseline=t=>{
        const v=1-t;
        return v*v*v*y0+3*v*v*t*(y0+c)+3*v*t*t*(y1+c)+t*t*t*y1;
      };
      // Same production physics for short and long spans; measure the
      // maximum deformation against the natural slack cable at each length.
      const peak=span=>{
        const geom=[0,0,span,0,72];
        const sag=Math.max(10,Math.min(72,Math.min(67,28+Math.sqrt(span)*1.55)));
        const ctrl=sag*4/3;
        let max=0;
        for(const ms of [45,85,150,230,300,390,460,520]){
         const pts=wireImpactPathData(...geom,ms).match(/-?\\d+(?:\\.\\d+)?/g).map(Number);
         for(let i=1;i<64;i++){
          const t=i/64,v=1-t;
          const y=3*v*v*t*ctrl+3*v*t*t*ctrl;
          max=Math.max(max,Math.abs(pts[2*i+1]-y));
         }
        }
        return max;
      };
      const displaced=(ms,i)=>{
        const points=wireImpactPathData(...g,ms).match(/-?\\d+(?:\\.\\d+)?/g).map(Number);
        return points[2*i+1]-baseline(i/64);
      };
      return {
        nearInEarly:displaced(45,60),nearOutEarly:displaced(45,10),
        nearOutLate:displaced(420,10),
        pluggedOut:displaced(0,0),pluggedIn:displaced(45,64),
        tinyPeak:peak(70),shortPeak:peak(170),longPeak:peak(500)
      };
    }""")
    assert physics and physics["nearInEarly"]< -2.5,("no upward jerk at receiving IN",physics)
    assert abs(physics["nearOutEarly"])<.5,("wrong direction: OUT moves first",physics)
    assert physics["nearOutLate"]< -1.1,("wave stops before OUT",physics)
    assert abs(physics["nearOutLate"])<abs(physics["nearInEarly"])*.25,\
        ("outgoing wave did not decay strongly toward OUT",physics)
    assert abs(physics["pluggedOut"])<.25 and abs(physics["pluggedIn"])<.25,physics
    assert 0.8<physics["tinyPeak"]<physics["shortPeak"]*.65,\
        ("one-channel wave is too violent or imperceptible",physics)
    assert physics["shortPeak"]<physics["longPeak"]*.65,\
        ("short cable retains long cable amplitude",physics)
    assert len(samples)>=5,("no finite spring movement",len(samples))
    assert any(" L " in x["d"] for x in samples),("no cable impact",len(samples))
    settled=cable.get_attribute("d")
    assert " C " in settled,("must return to exact canonical cubic",settled[:90])
    canonical=cable.evaluate("""el=>{const p=el.getPointAtLength(el.getTotalLength()*.90);
                                  return [p.x,p.y];}""")
    far=max(((x["near"][0]-canonical[0])**2+
             (x["near"][1]-canonical[1])**2)**.5 for x in samples)
    assert far>1.0,("wave never reached receiving end",far)
    for axis in ("start","end"):
        coords=[x[axis] for x in samples]
        assert max(abs(p[j]-coords[0][j]) for p in coords for j in (0,1))<.3,\
            ("motion detached endpoint",axis,coords[:3])

    hit=page.locator('#wireLayer path.wire-interaction-hit[data-cable-key="1-3"]')
    assert hit.count()==1
    # The newly plugged edge is selected immediately, before the user clicks it.
    assert page.locator('#wireLayer path.wire-outgoing-line[data-from="1"][data-to="3"].is-wire-selected,#wireLayer path.wire-incoming-line[data-from="1"][data-to="3"].is-wire-selected').count()==1
    initial_edge=hit.get_attribute("data-edge-id")
    mid=point_at_midpoint(hit)
    page.mouse.click(mid["x"],mid["y"])
    assert hit.get_attribute("data-edge-id")==initial_edge
    selected=page.locator('#wireLayer path.wire-outgoing-line[data-from="1"][data-to="3"].is-wire-selected,#wireLayer path.wire-incoming-line[data-from="1"][data-to="3"].is-wire-selected')
    assert selected.count()==1,"Selected edge must brighten its real visible jacket"
    values=selected.evaluate("""el=>{
      const c=getComputedStyle(el);
      const hit=document.querySelector('#wireLayer .wire-interaction-hit.is-wire-selected');
      return {width:parseFloat(c.strokeWidth),stroke:c.stroke,
              original:el.getAttribute('stroke'),dash:c.strokeDasharray,
              filter:c.filter,hit:getComputedStyle(hit).stroke};
    }""")
    assert 3.35<=values["width"]<=3.55 and values["dash"]=="none",values
    assert values["stroke"]!=values["original"],values
    assert values["filter"]=="none",values
    assert values["hit"] in ("transparent","rgba(0, 0, 0, 0)"),values
    # Re-selecting does not alter source/destination graph identity.
    assert hit.get_attribute("data-edge-id")==initial_edge
    # Start a NEW cable with a real pointer drag: preview is selected even
    # before graph acceptance. Once plugged it retains its exact EdgeId.
    out=page.locator('#tracks .strip[data-id="1"] .wire-node-out')
    dest=page.locator('#tracks .strip[data-id="4"] .wire-node-in')
    a=out.bounding_box()
    b=dest.bounding_box()
    assert a and b,("need two visible physical sockets",a,b)
    page.mouse.move(a["x"]+a["width"]/2,a["y"]+a["height"]/2)
    page.mouse.down()
    page.mouse.move(b["x"]+b["width"]/2,b["y"]+b["height"]/2,steps=8)
    drag=page.locator('#wireLayer path.wire-drag-line.is-wire-selected')
    assert drag.count()==1,"New wire is not highlighted during drag"
    drag_style=drag.evaluate("(el)=>({width:parseFloat(getComputedStyle(el).strokeWidth),opacity:getComputedStyle(el).opacity,filter:getComputedStyle(el).filter})")
    assert 3.35<=drag_style["width"]<=3.55 and drag_style["filter"]=="none",drag_style
    page.mouse.up()
    plugged=page.locator('#wireLayer path.wire-outgoing-line[data-from="1"][data-to="4"].is-wire-selected')
    assert plugged.count()==1,"Newly plugged route must become selected"
    new_hit=page.locator('#wireLayer path.wire-interaction-hit[data-cable-key="1-4"].is-wire-selected')
    assert new_hit.count()==1 and new_hit.get_attribute("data-edge-id")!=initial_edge
    assert page.locator('#wireLayer path.wire-outgoing-line[data-from="1"][data-to="3"].is-wire-selected').count()==0
    page.evaluate("window.__v60Observer.disconnect()")



def test_replug_and_retract(page):
    # All actions are real pointer gestures on the preview mixer. Neither the
    # retract overlay nor a wave can create a second graph EdgeId.
    src=page.locator('#tracks .strip[data-id="1"] .wire-node-out')
    old=page.locator('#tracks .strip[data-id="4"] .wire-node-in')
    dst=page.locator('#tracks .strip[data-id="5"] .wire-node-in')
    assert src.count()==old.count()==dst.count()==1,"need visible channels in wide bank"
    old_edge=page.locator('#wireLayer path.wire-interaction-hit[data-cable-key="1-4"]')
    original_id=old_edge.get_attribute("data-edge-id")
    assert original_id
    page.evaluate("""()=>{
       window.__v62Relink=[];
       const wire=document.querySelector('#wireLayer');
       window.__v62Obs=new MutationObserver(records=>{
         for(const rec of records){
           const el=rec.target;
           if(el.matches?.('path.wire-outgoing-line[data-cable-key="1-5"],path.wire-incoming-line[data-cable-key="1-5"]'))
             window.__v62Relink.push(el.getAttribute('d'));
         }
       });
       window.__v62Obs.observe(wire,{subtree:true,attributes:true,attributeFilter:['d']});
    }""")
    a=old.bounding_box()
    b=dst.bounding_box()
    cx=lambda q:q["x"]+q["width"]/2
    cy=lambda q:q["y"]+q["height"]/2
    page.mouse.move(cx(a),cy(a));page.mouse.down()
    page.mouse.move(cx(b),cy(b),steps=12);page.mouse.up()
    new_edge=page.locator('#wireLayer path.wire-interaction-hit[data-cable-key="1-5"]')
    assert new_edge.count()==1 and new_edge.get_attribute("data-edge-id")==original_id,\
        "Retarget must preserve the exact selected committed EdgeId"
    assert page.locator('#wireLayer path.wire-interaction-hit[data-cable-key="1-4"]').count()==0
    page.wait_for_timeout(850)
    waves=page.evaluate("window.__v62Relink")
    assert len(waves)>=5 and any(" L " in d for d in waves),\
        ("replug did not trigger newly rendered WIRE wavy",waves[:2])
    new_jacket=page.locator('#wireLayer path.wire-outgoing-line[data-cable-key="1-5"],#wireLayer path.wire-incoming-line[data-cable-key="1-5"]').first
    assert " C " in new_jacket.get_attribute("d"),"replug must settle to canonical cubic"

    # Observe the actual detached SVG during an accepted pointer unplug.
    # A 330ms reel MUST be measured with RAF/DOM evidence, not a sequence
    # of slow locator reads that might miss a fast short-lived animation.
    page.evaluate("""()=>{
      window.__v66ReelFrames=[];
      window.__v66ReelObserver=new MutationObserver(()=>{
        const layer=document.querySelector('#wireRetractLayer');
        const plug=layer?.querySelector('[data-wire-retract-plug]');
        const jacket=layer?.querySelector('[data-wire-retract-jacket]');
        if(!plug||!jacket)return;
        const x=Number(plug.getAttribute('cx')),y=Number(plug.getAttribute('cy'));
        if(Number.isFinite(x)&&Number.isFinite(y))
          window.__v66ReelFrames.push({x,y,r:Number(plug.getAttribute('r')),
                                      phase:layer.dataset.wirePhase,
                                      d:jacket.getAttribute('d')});
      });
      window.__v66ReelObserver.observe(document.getElementById('viewport'),
        {childList:true,subtree:true,attributes:true,attributeFilter:['cx','cy','d']});
    }""")
    other=page.locator('#tracks .strip[data-id="4"] .wire-node-in')
    t=dst.bounding_box();q=other.bounding_box()
    blank_x=(cx(t)+cx(q))/2
    blank_y=cy(t)
    page.mouse.move(cx(t),cy(t));page.mouse.down()
    page.mouse.move(blank_x,blank_y,steps=12);page.mouse.up()
    assert page.locator('#wireLayer path.wire-interaction-hit[data-cable-key="1-5"]').count()==0, \
        "unplug must REMOVE the graph edge immediately"
    page.wait_for_timeout(450)
    frames=page.evaluate("window.__v66ReelFrames")
    source_x=cx(src.bounding_box())
    assert len(frames)>=6,("fast plug-out never animated in real browser",frames[:2])
    distances=[abs(item["x"]-source_x) for item in frames]
    assert distances[0]>max(25,distances[-1]*2.5), \
        ("free plug did not reel briskly toward OUT",distances)
    delta=max(item["y"] for item in frames)-min(item["y"] for item in frames)
    assert delta>12,("plug dot did not visibly swing with the WIRE",delta)
    assert frames[-1]["r"]<frames[0]["r"],"free plug does not shrink into OUT"
    assert all(item["phase"]=="reel" for item in frames),"unexpected fall/hang phase"
    assert not page.locator('#wireRetractLayer').count(),"reel left ghost visual"
    assert page.locator('#wireLayer path.wire-interaction-hit[data-cable-key="1-5"]').count()==0
    page.evaluate("window.__v66ReelObserver.disconnect()")

    # Exact production code evaluated in Chromium: a strong pulse starts at
    # OUT, travels through the middle and far cable, then throws the loose
    # plug from side to side. It fades to zero as remaining length runs out.
    model=HTML[HTML.index("const WIRE_RETRACT_MS=330"):
               HTML.index("function startWireRetraction(")]
    physics=page.evaluate("()=>{\n"+model+"""\n
      const chord=400;
      const deviation=(ms,i)=>{
        const {d,pose}=wireRetractPathData(0,0,chord,0,ms);
        const ys=d.match(/-?\d+(?:\.\d+)?/g).map(Number);
        const t=i/64;
        return ys[2*i+1]-pose.sag*Math.sin(Math.PI*t);
      };
      const early=wireRetractPathData(0,0,chord,0,50);
      const middle=wireRetractPathData(0,0,chord,0,120);
      const crest=wireRetractPathData(0,0,chord,0,135);
      const finished=wireRetractPathData(0,0,chord,0,WIRE_RETRACT_MS);
      const shortPose=wireRetractPose(0,0,80,0,95);
      const longPose=wireRetractPose(0,0,chord,0,95);
      const tip=ms=>wireRetractPathData(0,0,chord,0,ms).pose.tipY;
      const svgTip=crest.d.match(/-?\d+(?:\.\d+)?/g).map(Number).slice(-2);
      return {
        nearOut:deviation(20,8),farEarly:deviation(20,51),
        middleSwing:deviation(60,32),farSwing:deviation(80,48),
        tipFirst:tip(100),tipRebound:tip(135),
        sourceAnchor:deviation(135,0),earlyX:early.pose.x,
        middleX:middle.pose.x,tipX:crest.pose.tipX,
        tipY:crest.pose.tipY,svgTip,
        completeX:finished.pose.tipX,completeY:finished.pose.tipY,
        shortAmplitude:shortPose.amplitude,longAmplitude:longPose.amplitude,
        startPhase:early.pose.phase,finishAmplitude:finished.pose.amplitude
      };
    }""")
    assert abs(physics["nearOut"])>3 and abs(physics["farEarly"])<.4, \
        ("impulse must start at OUT, not at the free plug",physics)
    assert abs(physics["middleSwing"])>7 and abs(physics["farSwing"])>9, \
        ("wave fails to excite the entire shortening cable",physics)
    assert abs(physics["tipFirst"])>10 and abs(physics["tipRebound"])>17, \
        ("dot/loose plug does not swing enough",physics)
    assert physics["tipFirst"]*physics["tipRebound"]<0, \
        ("free dot must whip back in the opposite direction",physics)
    assert abs(physics["sourceAnchor"])<.25,"source OUT socket cannot move"
    assert 0<physics["middleX"]<physics["earlyX"]<300, \
        ("retraction speed is not appreciably faster than V65",physics)
    assert abs(physics["tipY"]-physics["svgTip"][1])<.1 and \
           abs(physics["tipX"]-physics["svgTip"][0])<.1, \
        ("free plug circle detached from last path vertex",physics)
    assert abs(physics["completeX"])<.01 and abs(physics["completeY"])<.01,physics
    assert physics["shortAmplitude"]<physics["longAmplitude"]*.7, \
        ("short cable should remain controlled",physics)
    assert physics["startPhase"]=="reel" and physics["finishAmplitude"]==0,physics
    page.evaluate("window.__v62Obs.disconnect()")


def run():
    from playwright.sync_api import sync_playwright
    subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),
                    "--out",str(ROOT/"dist-pages"),"--sha","v60-tactile-test",
                    "--branch","gui-test"],cwd=ROOT,check=True,capture_output=True,text=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        try:
            for width,height in ((640,1005),(1680,900),(1366,768)):
                page=browser.new_page(viewport={"width":width,"height":height})
                errs=[]
                page.on("pageerror",lambda error:errs.append(str(error)))
                exercise(page,width,height)
                test_pan(page)
                if width==1680:
                    test_wire(page)
                    test_replug_and_retract(page)
                if width in (640,1680):
                    page.screenshot(path=f"arsonmix-v60-fader-wire-{width}.png",full_page=False)
                assert not errs,(width,errs)
                page.close()
        finally:
            browser.close()
    print("PASS V66 Chromium: 330ms fast reel, whole cable ripple and swing-back free dot with no ghost graph")


if __name__=="__main__":
    source_contract()
    if "--source" not in sys.argv:run()
