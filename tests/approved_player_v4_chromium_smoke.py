"""Actual assembled native Player UI. Test-only JUCE events; no audio claim."""
from pathlib import Path
import base64
import tempfile
import sys
from playwright.sync_api import sync_playwright
from native_event_fixture import NATIVE_EVENTS
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
import build_approved_native_gui as bundle

def main():
    evidence=ROOT/"player-visual-evidence"
    evidence.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as temp,sync_playwright() as p:
        out=Path(temp)/"native";bundle.stage(out)
        browser=p.chromium.launch()
        for width,height,scale in [(1366,768,1),(1255,700,1.25),(900,540,1.5),(390,844,2)]:
            page=browser.new_page(viewport={"width":width,"height":height},device_scale_factor=scale)
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.add_init_script(NATIVE_EVENTS+"""window.__playerActions=[];window.__JUCE__={backend:{emitEvent(name,payload){if(name==='arMixAction'&&String(payload.action).startsWith('player'))window.__playerActions.push(payload)},...window.__nativeListenerMethods}};""")
            page.goto((out/"index.html").as_uri())
            page.wait_for_function("()=>window.__nativeListeners.has('arMixPlayerState')")
            tracks=[dict(id=i,catalogId=i,title="Track "+str(i+1),keySemitones=0,tempoPercent=100,cueIn=0,cueOut=0,missing=False) for i in range(30)]
            state=dict(schema="arsonmix.player.v1",tracks=tracks,queue=[dict(id=0,title="Track 2")],setlists=[dict(id=0,name="Acoustic Session",count=30)],activeSetlist=0,current=0,selected=0,pending=-1,queueRevision=1,autoNext=False,transport=dict(state="PLAYING",title="Track 1",durationSeconds=240,currentSeconds=102,routeAudible=True,waveform=[(i*1729)%65536 for i in range(256)]))
            page.evaluate("s=>window.__nativeListeners.get('arMixPlayerState')(s)",state)
            page.locator("#ribbon-tab-player").click()
            assert page.locator("#ribbon-tab-player").get_attribute("aria-selected")=="true"
            assert page.locator(".armix-ribbon-player img").count()==0
            for ident,size in [("ribbonPrevious",28),("ribbonPlay",32),("ribbonNext",28),("ribbonStop",22)]:
                g=page.locator("#"+ident).evaluate("e=>({w:e.offsetWidth,h:e.offsetHeight,r:getComputedStyle(e).borderRadius})")
                assert g==dict(w=size,h=size,r="50%"),(ident,g)
            assert page.locator("#ribbonTrack").inner_text()=="Track 1"
            if width>=1255:
                layout=page.evaluate("""()=>{const ids=['ribbonPrevious','ribbonPlay','ribbonNext','ribbonStop','ribbonTrack','ribbonTime','ribbonSeek','ribbonLoop','ribbonAdd','ribbonVolume','playerToggle'];return ids.map(id=>{const r=document.getElementById(id).getBoundingClientRect();return {id,x:r.x,right:r.right}})}""")
                assert layout[0]["x"]<30,(width,layout)
                assert all(a["right"]<=b["x"]+1 for a,b in zip(layout,layout[1:])),(width,layout)
                assert layout[-1]["right"]<width-150,(width,layout)
                assert page.locator('.armix-ribbon-player .armix-ribbon-separator').count()==4
                assert page.locator('#playerToggle .armix-ribbon-open-icon').count()==1
                page.screenshot(path=str(evidence/f"ribbon-parity-{width}-{height}.png"))
            assert page.locator("#ribbonPlay").get_attribute("aria-pressed")=="true"
            if width>=900:
                page.locator("#ribbonPlay").click()
                assert page.evaluate("()=>window.__playerActions.at(-1).action")=="playerPause"
                page.locator("#ribbonStop").click()
                assert page.evaluate("()=>window.__playerActions.at(-1).action")=="playerStop"
                page.locator("#ribbonSeek").evaluate("e=>{e.value='500';e.dispatchEvent(new Event('change',{bubbles:true}))}")
                assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerSeek","value":120}
                page.locator("#ribbonVolume").evaluate("e=>{e.value='67';e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}))}")
                assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerVolume","value":67}
                page.screenshot(path=str(evidence/f"ribbon-v6-{width}-{height}.png"))
            page.locator("#playerToggle").click()
            panel=page.locator("#arsonPlayer")
            panel.wait_for(state="visible")
            page.evaluate("document.fonts.ready")
            box=panel.bounding_box();assert box and box["x"]>=0 and box["x"]+box["width"]<=width+2,(width,box)
            play=page.locator("#playerPlay")
            geometry=play.evaluate("e=>({w:e.offsetWidth,h:e.offsetHeight,r:getComputedStyle(e).borderRadius})")
            assert geometry==dict(w=32,h=32,r="50%"),geometry
            # The full device, mini and ribbon share identical transport faces.
            # Compare computed CSS, not a second hand-copied colour assertion.
            def face(ident):
                return page.locator("#"+ident).evaluate("""e=>{
                    const s=getComputedStyle(e),g=getComputedStyle(e.querySelector('svg'));
                    return {w:s.width,h:s.height,r:s.borderRadius,bg:s.backgroundImage,
                      shadow:s.boxShadow,color:s.color,svgW:g.width,svgH:g.height,
                      shine:getComputedStyle(e,'::after').backgroundImage,
                      glyph:e.querySelector('svg').innerHTML};
                }""")
            def assert_center(ident):
                g=page.locator("#"+ident).evaluate("""e=>{
                    const a=e.getBoundingClientRect(),svg=e.querySelector('svg'),b=svg.getBoundingClientRect(),p=svg.getBBox();
                    return {dx:(b.left+b.width/2)-(a.left+a.width/2),dy:(b.top+b.height/2)-(a.top+a.height/2),
                      vx:p.x+p.width/2-12,vy:p.y+p.height/2-12};
                }""")
                assert abs(g["dx"])<=0.5 and abs(g["dy"])<=0.5,(ident,g)
                assert abs(g["vx"])<=1 and abs(g["vy"])<=1,(ident,g)
            for full,ribbon in [("playerPlay","ribbonPlay"),("playerStop","ribbonStop"),
                                ("playerPrevious","ribbonPrevious"),("playerNext","ribbonNext")]:
                assert face(full)==face(ribbon),(full,face(full),face(ribbon))
                assert_center(full)
                assert_center(ribbon)
            screen=page.locator("#playerScreen")
            glass=panel.evaluate("""e=>{
                const s=getComputedStyle(e),coat=getComputedStyle(e,'::after'),
                    screen=getComputedStyle(e.querySelector('.ap-screen')),menu=getComputedStyle(e.querySelector('.ap-song-menu'));
                return {gradients:(coat.backgroundImage.match(/(?:linear|radial)-gradient\\(/g)||[]).length,
                    shadows:(coat.boxShadow.match(/inset/g)||[]).length,
                    content:coat.content,position:coat.position,inset:[coat.top,coat.right,coat.bottom,coat.left],
                    pointer:coat.pointerEvents,layer:Number(coat.zIndex),screenLayer:Number(screen.zIndex),
                    menuLayer:Number(menu.zIndex),isolation:s.isolation,filter:coat.filter,
                    backdrop:coat.backdropFilter,animation:coat.animationName,
                    radius:coat.borderRadius,panelRadius:s.borderRadius,screenRadius:screen.borderRadius,
                    rim:s.borderTopWidth, corners:[s.borderTopLeftRadius,s.borderTopRightRadius,s.borderBottomRightRadius,s.borderBottomLeftRadius],
                    overflow:s.overflowY};
            }""")
            assert glass["gradients"]==1 and glass["shadows"]==8,glass
            assert glass["content"] not in ("none","normal") and glass["position"]=="absolute",glass
            assert glass["inset"]==["0px"]*4 and glass["pointer"]=="none",glass
            assert glass["layer"]>glass["menuLayer"]>glass["screenLayer"] and glass["isolation"]=="isolate",glass
            assert glass["filter"]==glass["backdrop"]==glass["animation"]=="none",glass
            outer=11 if width<=640 else 14
            inner=outer-1
            assert glass["panelRadius"]==f"{outer}px" and glass["rim"]=="1px",glass
            assert glass["corners"]==[f"{outer}px"]*4,glass
            assert glass["radius"]==glass["screenRadius"]==f"{inner}px" and glass["overflow"]=="hidden",glass
            assert screen.evaluate("e=>getComputedStyle(e).overflowY==='auto' && getComputedStyle(e).scrollbarWidth==='none'")
            # Reference geometry: measure the actual glass on a uniform dark
            # substrate, not the busy controls. The entire upper-left plane
            # must be brighter, with ONE straight diagonal transition and no
            # additional bright bands. Check multiple scanlines at every DPI.
            probe_style=page.add_style_tag(content="#arsonPlayer{background:#202830!important} #playerScreen{visibility:hidden!important}")
            try:
                plane=panel.screenshot(scale="css",animations="disabled")
                (evidence/f"glass-plane-{width}-{height}.png").write_bytes(plane)
                profile=page.evaluate("""async s=>{
                    const bmp=await createImageBitmap(new Blob([Uint8Array.from(atob(s),c=>c.charCodeAt(0))],{type:'image/png'}));
                    const c=document.createElement('canvas');c.width=bmp.width;c.height=bmp.height;
                    const ctx=c.getContext('2d');ctx.drawImage(bmp,0,0);bmp.close();
                    const {width:w,height:h}=c,d=ctx.getImageData(0,0,w,h).data;
                    const sample=(x,y)=>{const i=(y*w+x)*4;return (d[i]+d[i+1]+d[i+2])/3;};
                    const lines=[];
                    for(const f of [.2,.4,.6,.8]){
                        const x=Math.round(w*f),expected=.4*(w+h)-x;
                        if(expected<26||expected>h-26)continue;
                        const jumps=[];let otherMax=0;
                        for(let y=20;y<h-21;y++){
                            const jump=sample(x,y)-sample(x,y+1);
                            if(jump>5)jumps.push({y,jump});
                            if(Math.abs(y-expected)>4)otherMax=Math.max(otherMax,Math.abs(jump));
                        }
                        const bright=sample(x,Math.round(expected)-8),dark=sample(x,Math.round(expected)+8);
                        const total=jumps.reduce((n,j)=>n+j.jump,0);
                        lines.push({x,expected,jumps,otherMax,bright,dark,total});
                    }
                    return {w,h,lines};
                }""",base64.b64encode(plane).decode("ascii"))
                assert len(profile["lines"])>=2,profile
                for line in profile["lines"]:
                    assert line["bright"]-line["dark"]>18 and line["total"]>18,line
                    assert 1<=len(line["jumps"])<=3 and all(abs(j["y"]-line["expected"])<=3 for j in line["jumps"]),line
                    assert line["otherMax"]<=3,line
                print("PLAYER_EPS_SINGLE_PLANE_PASS",width,profile,flush=True)
            finally:
                probe_style.evaluate("e=>e.remove()")
            # Real composed corner clipping, including header, bezel and coat.
            # Compare with the exact same scene without the panel. Disable only
            # the intentional external shadow during this geometry probe.
            rect=panel.bounding_box()
            corner_style=page.add_style_tag(content="#arsonPlayer{box-shadow:none!important}")
            try:
                shown=page.screenshot(clip=rect,scale="css",animations="disabled")
                hidden_style=page.add_style_tag(content="#arsonPlayer{visibility:hidden!important}")
                try:
                    behind=page.screenshot(clip=rect,scale="css",animations="disabled")
                finally:
                    hidden_style.evaluate("e=>e.remove()")
                corners=page.evaluate("""async ([a,b,r])=>{
                    const pixels=async s=>{const bmp=await createImageBitmap(new Blob([Uint8Array.from(atob(s),c=>c.charCodeAt(0))],{type:'image/png'}));
                        const c=document.createElement('canvas');c.width=bmp.width;c.height=bmp.height;
                        const ctx=c.getContext('2d');ctx.drawImage(bmp,0,0);bmp.close();
                        return {w:c.width,h:c.height,data:ctx.getImageData(0,0,c.width,c.height).data};};
                    const [x,y]=await Promise.all([pixels(a),pixels(b)]);
                    const delta=(px,py)=>{const i=(py*x.w+px)*4;return Math.max(...[0,1,2].map(k=>Math.abs(x.data[i+k]-y.data[i+k])));};
                    return [[false,false],[true,false],[true,true],[false,true]].map(([right,bottom])=>{
                        const dx=n=>right?x.w-1-n:n,dy=n=>bottom?x.h-1-n:n;
                        const outside=[0,1,2].flatMap(i=>[0,1,2].map(j=>delta(dx(i),dy(j))));
                        const inside=[r-2,r-1,r].flatMap(i=>[r-2,r-1,r].map(j=>delta(dx(i),dy(j))));
                        return {outside:Math.max(...outside),inside:Math.max(...inside)};
                    });
                }""",[base64.b64encode(v).decode("ascii") for v in (shown,behind)]+[outer])
                assert all(c["outside"]<=3 and c["inside"]>=8 for c in corners),corners
                print("PLAYER_CONCENTRIC_CORNERS_PASS",width,corners,flush=True)
            finally:
                corner_style.evaluate("e=>e.remove()")
            def hit(control):
                control.scroll_into_view_if_needed()
                assert control.evaluate("""e=>{const r=e.getBoundingClientRect();
                    return e.contains(document.elementFromPoint(r.x+r.width/2,r.y+r.height/2));}"""),control
            # Pixel proof, not just computed z-index: compare actual composited
            # control pixels with the front coat on/off. A rear background
            # cannot tint an opaque dropdown or metal button surface.
            def verify_coat_pixels(control):
                control.scroll_into_view_if_needed()
                hit(control)
                rect=control.bounding_box()
                assert rect and rect["width"]>8 and rect["height"]>8,rect
                clip=dict(x=rect["x"]+3,y=rect["y"]+3,width=rect["width"]-6,height=rect["height"]-6)
                on=page.screenshot(clip=clip,scale="css",animations="disabled")
                off_style=page.add_style_tag(content="#arsonPlayer::after{visibility:hidden!important}")
                try:
                    off=page.screenshot(clip=clip,scale="css",animations="disabled")
                finally:
                    off_style.evaluate("e=>e.remove()")
                stats=page.evaluate("""async ([a,b])=>{
                    const pixels=async s=>{const bmp=await createImageBitmap(new Blob([Uint8Array.from(atob(s),c=>c.charCodeAt(0))],{type:'image/png'}));
                        const c=document.createElement('canvas');c.width=bmp.width;c.height=bmp.height;
                        const ctx=c.getContext('2d');ctx.drawImage(bmp,0,0);bmp.close();
                        return ctx.getImageData(0,0,c.width,c.height).data;};
                    const [x,y]=await Promise.all([pixels(a),pixels(b)]);let changed=0,sum=0;
                    for(let i=0;i<x.length;i+=4){const d=Math.abs(x[i]-y[i])+Math.abs(x[i+1]-y[i+1])+Math.abs(x[i+2]-y[i+2]);sum+=d;if(d>=3)changed++;}
                    return {ratio:changed/(x.length/4),mean:sum/(x.length/4)/3};
                }""",[base64.b64encode(v).decode("ascii") for v in (on,off)])
                assert stats["ratio"]>.70 and stats["mean"]>1,stats
                print("PLAYER_FRONT_COAT_PIXEL_PASS",width,control.get_attribute("id"),stats,flush=True)
            verify_coat_pixels(play)
            key=page.locator("#playerKeyPlus")
            verify_coat_pixels(key)
            page.evaluate("()=>window.__playerActions.length=0")
            key.click()
            assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerKeyStep","value":1}
            tempo=page.locator("#playerTempoPlus")
            hit(tempo)
            tempo.click()
            assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerTempoStep","value":1}
            page.locator("#playerDetailsToggle").click()
            dropdown=page.locator("#playerFadeMs")
            verify_coat_pixels(dropdown)
            dropdown.focus()
            dropdown.press("ArrowDown")
            assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerFadeMs","value":500}
            page.locator("#playerDetailsToggle").click()
            screen.evaluate("e=>e.scrollTop=0")
            listbox=page.locator("#playerList")
            assert listbox.evaluate("e=>getComputedStyle(e).overflowY==='auto' && getComputedStyle(e).scrollbarWidth==='none'")
            # Hidden scrollbar does not change virtual list scroll mechanics.
            listbox.scroll_into_view_if_needed()
            listbox.evaluate("e=>{e.scrollTop=34*12;e.dispatchEvent(new Event('scroll'))}")
            assert listbox.evaluate("e=>e.scrollTop>0")
            assert page.locator(".ap-songrow").count()<=80
            listbox.evaluate("e=>{e.scrollTop=0;e.dispatchEvent(new Event('scroll'))}")
            screen.evaluate("e=>e.scrollTop=0")
            # Same exact skin and glyph in paused/playing native readback.
            page.evaluate("s=>window.__nativeListeners.get('arMixPlayerState')(s)",{**state,"transport":{**state["transport"],"state":"PAUSED"}})
            page.wait_for_function("()=>document.getElementById('ribbonPlay').getAttribute('aria-pressed')==='false'")
            assert face("playerPlay")==face("ribbonPlay")
            assert_center("playerPlay")
            assert_center("ribbonPlay")
            page.evaluate("s=>window.__nativeListeners.get('arMixPlayerState')(s)",state)
            page.wait_for_function("()=>document.getElementById('ribbonPlay').getAttribute('aria-pressed')==='true'")
            assert face("playerPlay")==face("ribbonPlay")
            fallback=page.locator("#playerArtworkFallback")
            assert fallback.is_visible()
            assert fallback.get_attribute("data-art") in ("blue-hour","prism","tide","dune","bloom")
            names=("blue-hour","prism","tide","dune","bloom","still","unavailable")
            total=0
            for name in names:
                asset=out/"assets"/f"player-cover-{name}.jpg"
                assert asset.is_file() and asset.read_bytes().startswith(b"\xff\xd8\xff")
                assert not (out/"assets"/f"player-cover-{name}.svg").exists()
                total+=asset.stat().st_size
            assert total<=160*1024,total
            # Decode every CSS-mapped cover using the actual assembled resource paths.
            # Restore the native-driven state before taking any screenshots.
            assert fallback.evaluate("""async (el,names)=>{
                const initial=el.dataset.art;
                try {
                    for (const name of names) {
                        el.dataset.art=name;
                        const url=getComputedStyle(el).backgroundImage.slice(5,-2);
                        if (!url.endsWith('/player-cover-'+name+'.jpg')) return false;
                        const image=new Image();image.src=url;await image.decode();
                        if(image.naturalWidth!==256||image.naturalHeight!==256)return false;
                    }
                    return true;
                } finally {el.dataset.art=initial;}
            }""",list(names))
            page.screenshot(path=str(evidence/f"player-{width}-{height}.png"))
            assert page.locator("#playerSetlistSelect").is_disabled()
            search=page.locator("#playerSearch")
            search.fill("2 track")
            assert " / 30 songs" in page.locator("#playerCount").inner_text()
            page.evaluate("()=>window.__playerActions.length=0")
            search.press("ArrowDown")
            assert page.evaluate("()=>window.__playerActions.length")==0
            search.fill("Track 2")
            search.press("Enter")
            assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerSelect","value":1}
            # P1/P2: native readback must preserve click target and focus.
            # Queue-full Ctrl+Enter is still sent to the authoritative host;
            # simulate its rejection and confirm actionable feedback.
            publish=lambda delta: page.evaluate("s=>window.__nativeListeners.get('arMixPlayerState')({schema:'arsonmix.player.v1',...s})",delta)
            publish(dict(queue=[dict(id=i,title="Queued "+str(i)) for i in range(128)],queueRevision=2))
            page.evaluate("()=>window.__playerActions.length=0")
            search.press("Control+Enter")
            assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerQueueAppend","value":1}
            publish(dict(error="Queue is full (128 songs); remove an entry first"))
            assert "128 songs" in page.locator("#playerFeedback").inner_text()
            publish(dict(queue=state["queue"],queueRevision=3,error=""))
            search.fill("")
            row=page.locator('.ap-songrow[data-song-id="1"] .ap-song')
            row.scroll_into_view_if_needed()
            row.click()
            row.evaluate("el=>window.__playerRowBefore=el")
            publish(dict(selected=1))
            assert row.evaluate("el=>el===window.__playerRowBefore && document.activeElement===el")
            page.evaluate("()=>window.__playerActions.length=0")
            row.dblclick()
            assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerPlayNow","value":1}
            publish(dict(selected=0,pending=-1,transport={**state["transport"],"crossfading":True}))
            assert page.locator("#playerSeek").is_disabled()
            assert page.locator("#playerCueIn").is_disabled()
            assert page.locator("#playerCueGoIn").is_disabled()
            publish(dict(transport=state["transport"],mediaRefreshQueued=True))
            assert page.locator("#playerSeek").is_enabled()
            assert page.locator("#playerCueIn").is_enabled()
            assert "Metadata refresh queued" in page.locator("#playerFeedback").inner_text()
            publish(dict(mediaRefreshQueued=False,chooseOutput=True,error="Choose a verified MAIN output"))
            page.evaluate("()=>window.__playerActions.length=0")
            page.locator("#playerChooseOutput").click()
            # Playwright serializes the JS value:undefined as Python None;
            # validate the actual native command and its absence of arguments.
            assert page.evaluate("()=>window.__playerActions.length===1 && window.__playerActions[0].action==='playerChooseOutput' && window.__playerActions[0].value===undefined")
            publish(dict(chooseOutput=False,error=""))
            page.evaluate("()=>window.__playerActions.length=0")
            row.click(button="right")
            menu=page.locator("#playerSongMenu")
            menu.wait_for(state="visible")
            assert page.evaluate("()=>window.__playerActions.length")==0
            mb=menu.bounding_box();pb=panel.bounding_box()
            assert mb and pb and mb["x"]>=pb["x"] and mb["y"]>=pb["y"] and mb["x"]+mb["width"]<=pb["x"]+pb["width"] and mb["y"]+mb["height"]<=pb["y"]+pb["height"],(mb,pb)
            hit(menu.get_by_role("menuitem",name="Add to queue"))
            page.screenshot(path=str(evidence/f"context-{width}-{height}.png"))
            menu.get_by_role("menuitem",name="Add to queue").click()
            assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerQueueAppend","value":1}
            # Native activity readback and genuine operator gestures.
            page.evaluate("""()=>window.__nativeListeners.get('arMixPlayerActivity')({
                schema:'arsonmix.activity.v1',favorites:[1],done:[1],recent:[0,1],repeat:1})""")
            assert page.locator('.ap-songrow[data-song-id="1"]').get_attribute("data-done")=="true"
            assert page.locator('.ap-songrow[data-song-id="1"]').get_attribute("data-favorite")=="true"
            assert page.locator('.ap-songrow[data-song-id="1"] .ap-index').evaluate("el=>getComputedStyle(el,\"::after\").content")=="\"DONE\""
            mode=page.locator("#playerAutoNext")
            assert mode.inner_text()=="Repeat one"
            mode.click()
            screen.evaluate("el=>el.dispatchEvent(new Event(\"scroll\"))")
            page.locator("#playerList").evaluate("el=>el.dispatchEvent(new Event(\"scroll\"))")
            assert menu.is_visible()
            menu.get_by_role("menuitem",name="Repeat all").click()
            assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerRepeat","value":2}
            page.locator("#playerLibraryView").click()
            page.evaluate("""rows=>window.__nativeListeners.get('arMixPlayerCatalogue')({schema:'arsonmix.catalogue.v1',rows})""",
                          [dict(catalogId=i,title="Track "+str(i+1)) for i in range(30)])
            page.locator("#playerLibraryFilter").select_option("favorites")
            assert page.locator(".ap-songrow").count()==1
            assert page.locator(".ap-songrow").get_attribute("data-song-id")=="1"
            page.locator("#playerLibraryFilter").select_option("recent")
            assert page.locator(".ap-songrow").count()==2
            if width==1366:
                import time
                start=time.perf_counter()
                page.evaluate("""()=>window.__nativeListeners.get('arMixPlayerCatalogue')({
                    schema:'arsonmix.catalogue.v1',rows:Array.from({length:64000},(_,catalogId)=>({catalogId,title:'Catalogue '+String(catalogId).padStart(5,'0')}))})""")
                page.locator("#playerLibraryFilter").select_option("all")
                search.fill("Catalogue 63999")
                assert page.locator(".ap-songrow").count()==1
                assert page.locator(".ap-songrow").get_attribute("data-song-id")=="63999"
                search.fill("")
                assert page.locator(".ap-songrow").count()<=80
                print("64k Chromium catalogue search seconds",time.perf_counter()-start,flush=True)
            page.locator("#playerSetlistView").click()
            # The Player is modeless, not an outside-dismissed popup. Exercise
            # real pointer and keyboard focus transfer plus native readback.
            before=page.evaluate("()=>window.__playerActions.length")
            panel_rect=panel.bounding_box()
            mixer_tab=page.locator("#ribbon-tab-mixer")
            mixer_tab.focus()
            assert panel.is_visible() and mixer_tab.evaluate("e=>e===document.activeElement")
            mixer_tab.click()
            assert panel.is_visible() and mixer_tab.get_attribute("aria-selected")=="true"
            page.locator("#ribbon-tab-player").click()
            assert panel.is_visible()
            # At narrow reference widths the modeless panel may legitimately
            # overlap its open-only ribbon button. Use a real pointer where
            # exposed, otherwise keyboard activation of the same focusable
            # command; never force-click through a floating window.
            open_player=page.locator("#playerToggle")
            exposed=open_player.evaluate("""e=>{const r=e.getBoundingClientRect();
                const hit=document.elementFromPoint(r.x+r.width/2,r.y+r.height/2);
                return !!hit && e.contains(hit)}""")
            if exposed:
                open_player.click()
            else:
                assert width<=640, "desktop ribbon open command unexpectedly occluded"
                open_player.focus()
                assert open_player.evaluate("e=>e===document.activeElement")
                open_player.press("Enter")
            assert panel.is_visible() and panel.bounding_box()==panel_rect
            search.focus()
            page.keyboard.press("Escape")
            assert panel.is_visible()
            page.evaluate("()=>window.dispatchEvent(new Event('blur'))")
            assert panel.is_visible()
            publish(dict(transport=state["transport"]))
            assert panel.is_visible()
            assert page.locator("#playerToggle").get_attribute("aria-expanded")=="true"
            assert page.evaluate("()=>window.__playerActions.length")==before
            page.locator("#playerClose").click()
            assert panel.is_hidden()
            page.locator("#ribbon-tab-mixer").click()
            mini=page.locator("#playerMini")
            mini.wait_for(state="visible")
            assert page.locator("#playerMiniArtworkFallback").get_attribute("data-art")==fallback.get_attribute("data-art")
            assert page.locator("#playerMiniArtworkFallback").is_hidden()  # Ribbon has no cover
            # Only Mini is visible here; the device and ribbon buttons are
            # hidden, so compare CSS tokens and glyph, not their zero rects.
            assert face("playerMiniPlay")==face("playerPlay"),(width,face("playerMiniPlay"),face("playerPlay"))
            assert face("playerMiniNext")==face("playerNext"),(width,face("playerMiniNext"),face("playerNext"))
            assert_center("playerMiniPlay")
            assert_center("playerMiniNext")
            page.locator("#playerMiniPlay").click()
            assert page.evaluate("()=>window.__playerActions.at(-1).action")=="playerPause"
            page.locator("#playerMiniExpand").click()
            assert panel.is_visible() and mini.is_hidden()
            page.locator("#playerMinimize").click()
            assert panel.is_hidden() and mini.is_visible()
            page.locator("#playerMiniExpand").click()
            assert panel.is_visible() and mini.is_hidden()
            assert not errors,errors
            page.close()
            print("PASS Player assembled GUI",width,height,scale,flush=True)
        # Separate native-only presentation, same Player loader, no mixer DOM.
        child=browser.new_page(viewport={"width":410,"height":720})
        errors=[]
        child.on("pageerror",lambda e:errors.append(str(e)))
        child.add_init_script(NATIVE_EVENTS+"""window.__windowEvents=[];window.__JUCE__={backend:{emitEvent(name,payload){window.__windowEvents.push({name,payload})},...window.__nativeListenerMethods}};""")
        child.goto((out/"player-window.html").as_uri())
        child.wait_for_function("()=>window.__windowEvents.some(e=>e.name==='arMixPlayerWindowReady')")
        panel=child.locator("#arsonPlayer")
        assert panel.is_visible() and panel.bounding_box()==dict(x=0,y=0,width=410,height=720)
        assert child.locator("#tracks").count()==0
        assert child.locator("#playerMini").is_hidden()
        child.evaluate("s=>window.__nativeListeners.get('arMixPlayerState')(s)",state)
        child.locator("#playerSearch").focus()
        child.keyboard.press("Escape")
        child.evaluate("()=>window.dispatchEvent(new Event('blur'))")
        assert panel.is_visible()
        assert child.evaluate("()=>!window.__windowEvents.some(e=>e.name==='arMixPlayerWindowHide')")
        child.locator("#playerPlay").click()
        assert child.evaluate("()=>window.__windowEvents.at(-1).payload.action")=="playerPause"
        child.locator("#playerClose").click()
        assert child.evaluate("()=>window.__windowEvents.at(-1).name")=="arMixPlayerWindowHide"
        assert child.evaluate("()=>window.__windowEvents.filter(e=>e.name==='arMixAction').map(e=>e.payload.action)")==["playerPause"]
        assert not errors,errors
        child.close()
        # Mixer capability negotiation: no clipped drawer on native OPEN,
        # even after repeated commands. Visibility comes from native receipt.
        host=browser.new_page(viewport={"width":1366,"height":768})
        host.add_init_script(NATIVE_EVENTS+"""window.__windowEvents=[];window.__JUCE__={backend:{emitEvent(name,payload){window.__windowEvents.push({name,payload})},...window.__nativeListenerMethods}};""")
        host.goto((out/"index.html").as_uri())
        host.wait_for_function("()=>window.__nativeListeners.has('arMixPlayerWindowState')")
        host.evaluate("()=>window.__nativeListeners.get('arMixPlayerWindowState')({available:true,visible:false})")
        host.locator("#ribbon-tab-player").click()
        host.locator("#playerToggle").click()
        assert host.locator("#arsonPlayer").is_hidden()
        assert host.evaluate("()=>window.__windowEvents.filter(e=>e.name==='arMixPlayerWindow'&&e.payload.operation==='OPEN').length")==1
        host.evaluate("()=>window.__nativeListeners.get('arMixPlayerWindowState')({available:true,visible:true})")
        assert host.locator("#playerToggle").get_attribute("aria-expanded")=="true"
        host.locator("#ribbon-tab-mixer").click()
        assert host.locator("#playerToggle").get_attribute("aria-expanded")=="true"
        host.evaluate("()=>window.__nativeListeners.get('arMixPlayerWindowState')({available:true,visible:false})")
        assert host.locator("#playerToggle").get_attribute("aria-expanded")=="false"
        host.close()
        print("PASS native Player window presentation and receipt seam",flush=True)
        browser.close()
if __name__=="__main__":
    main()
