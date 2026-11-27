"""Actual assembled native Player UI. Test-only JUCE events; no audio claim."""
from pathlib import Path
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
            page.locator("#playerToggle").click()
            panel=page.locator("#arsonPlayer")
            panel.wait_for(state="visible")
            page.evaluate("document.fonts.ready")
            box=panel.bounding_box();assert box and box["x"]>=0 and box["x"]+box["width"]<=width+2,(width,box)
            play=page.locator("#playerPlay")
            geometry=play.evaluate("e=>({w:e.offsetWidth,h:e.offsetHeight,r:getComputedStyle(e).borderRadius})")
            assert geometry["w"]==44 and geometry["h"]==44 and geometry["r"]=="50%",geometry
            fallback=page.locator("#playerArtworkFallback")
            assert fallback.is_visible()
            assert fallback.get_attribute("data-art") in ("blue-hour","prism","tide","dune","bloom")
            assert fallback.evaluate("""async el=>{
                const url=getComputedStyle(el).backgroundImage.slice(5,-2);
                const image=new Image();image.src=url;await image.decode();
                return image.naturalWidth===512&&image.naturalHeight===512;
            }""")
            for name in ("blue-hour","prism","tide","dune","bloom","still","unavailable"):
                assert (out/"assets"/f"player-cover-{name}.svg").is_file()
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
            search.fill("")

            row=page.locator('.ap-songrow[data-song-id="1"] .ap-song')
            row.scroll_into_view_if_needed()
            page.evaluate("()=>window.__playerActions.length=0")
            row.click(button="right")
            menu=page.locator("#playerSongMenu")
            menu.wait_for(state="visible")
            assert page.evaluate("()=>window.__playerActions.length")==0
            mb=menu.bounding_box();assert mb and mb["x"]>=0 and mb["y"]>=0 and mb["x"]+mb["width"]<=width+1 and mb["y"]+mb["height"]<=height+1,mb
            page.screenshot(path=str(evidence/f"context-{width}-{height}.png"))
            menu.get_by_role("menuitem",name="Add to queue").click()
            assert page.evaluate("()=>window.__playerActions.at(-1)")=={"action":"playerQueueAppend","value":1}
            page.locator("#playerClose").click()
            mini=page.locator("#playerMini")
            mini.wait_for(state="visible")
            assert page.locator("#playerMiniArtworkFallback").get_attribute("data-art")==fallback.get_attribute("data-art")
            assert page.locator("#playerMiniArtworkFallback").is_visible()
            page.locator("#playerMiniPlay").click()
            assert page.evaluate("()=>window.__playerActions.at(-1).action")=="playerPause"
            page.locator("#playerMiniExpand").click()
            assert panel.is_visible() and mini.is_hidden()
            assert not errors,errors
            page.close()
            print("PASS Player assembled GUI",width,height,scale,flush=True)
        browser.close()
if __name__=="__main__":
    main()
