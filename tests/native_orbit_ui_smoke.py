"""Native VST3 ORBIT dialog smoke with a fake JUCE bridge (no vendor plugin required).

Verifies actual browser interaction and event payloads. This tests the UI contract,
not the audio result or dearVR's vendor parameter mapping.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright


async def main():
    root = Path(__file__).resolve().parents[1]
    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        page = await browser.new_page(viewport={"width": 1440, "height": 820})
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        await page.add_init_script("""(() => {
          const listeners = new Map();
          const pathFor = shape => Array.from({length:65}, (_,i) => {
            const a=i*Math.PI*2/64;
            const rx=(shape===1||shape===4)?0.43:1.0;
            const ry=(shape===2||shape===5)?0.43:1.0;
            return {x:rx*Math.sin(a), y:ry*Math.cos(a),
              z:shape>=3?0.3*Math.sin(a):0};
          });
          const backend = {
            calls: [],
            addEventListener(name, cb) { listeners.set(name, cb); },
            emitEvent(name, payload) {
              this.calls.push({name, payload});
              if(name !== 'arMixAction') return;
              if(payload.action === 'refreshState') {
                queueMicrotask(() => listeners.get('arMixChain')?.([
                  {id:'dearvr-pro-2',name:'dearVR PRO 2',bypass:false,latency:0}
                ]));
              }
              if(payload.action === 'orbitInspect') {
                queueMicrotask(() => listeners.get('arMixOrbitState')?.({
                  slot:payload.slot, revision:'1', plugin:'dearVR PRO 2', active:false,
                  activeSlot:-1,activeParameter:-1,periodSeconds:20,
                  reverse:false,preferredParameter:2,
                  nearSetpoint:1,farSetpoint:3,distanceUnit:'m',distanceMin:0,distanceMax:10,
                  shape:0, previewShape:payload.shape??-1, phase:0.125,
                  motionPath:pathFor(payload.shape??0),
                  preferredDistance:4,preferredElevation:5,
                  parameters:[
                    {index:1,name:'Azimuth Scale',minText:'0.5',maxText:'5'},
                    {index:2,name:'Azimuth',minText:'-180°',maxText:'+180°'},
                    {index:3,name:'Gain',minText:'-60',maxText:'12'},
                    {index:4,name:'Distance',minText:'0 m',maxText:'10 m',unit:'m',minMetres:0,maxMetres:10},
                    {index:5,name:'Elevation',minText:'-90°',maxText:'90°'}
                  ]
                }));
              }
              if(payload.action === 'orbitStart') {
                queueMicrotask(() => listeners.get('arMixOrbitState')?.({
                  slot:payload.slot, revision:'1', plugin:'dearVR PRO 2', active:true,
                  activeSlot:payload.slot, activeParameter:payload.parameterIndex,
                  periodSeconds:payload.seconds,reverse:payload.reverse,
                  preferredParameter:payload.parameterIndex,
                  shape:payload.shape,distanceIndex:payload.distanceIndex,elevationIndex:payload.elevationIndex,
                  nearSetpoint:payload.nearDistance,farSetpoint:payload.farDistance,
                  nearNormalized:payload.nearDistance/10,farNormalized:payload.farDistance/10,
                  distanceUnit:payload.distanceUnit,distanceMin:0,distanceMax:10,
                  preferredDistance:4,preferredElevation:5,
                  previewShape:-1,phase:0.12,motionPath:pathFor(payload.shape),
                  parameters:[
                    {index:2,name:'Azimuth',minText:'-180°',maxText:'+180°'},
                    {index:4,name:'Distance',minText:'0 m',maxText:'10 m',unit:'m',minMetres:0,maxMetres:10},
                    {index:5,name:'Elevation',minText:'-90°',maxText:'90°'}
                  ]
                }));
              }
              if(payload.action === 'orbitStop') {
                queueMicrotask(() => listeners.get('arMixOrbitState')?.({
                  slot:payload.slot,revision:'1',plugin:'dearVR PRO 2',active:false,
                  activeSlot:-1,activeParameter:-1,periodSeconds:20,
                  reverse:false,preferredParameter:2,shape:0,previewShape:-1,phase:0,
                  nearSetpoint:1,farSetpoint:3,distanceUnit:'m',distanceMin:0,distanceMax:10,
                  motionPath:pathFor(0),preferredDistance:4,preferredElevation:5,
                  parameters:[{index:2,name:'Azimuth',minText:'-180°',maxText:'+180°'},
                    {index:4,name:'Distance',minText:'0 m',maxText:'10 m',unit:'m',minMetres:0,maxMetres:10},
                    {index:5,name:'Elevation',minText:'-90°',maxText:'90°'}]
                }));
              }
            }
          };
          window.__emitNativeChain=items=>listeners.get('arMixChain')?.(items);
          window.__JUCE__ = {backend};
        })();""")
        await page.goto((root / "web/index.html").as_uri())
        await page.wait_for_selector(".strip")
        # Suppress native browser menu where ArSonMix has no custom action.
        await page.locator("#mixer").evaluate("""el => {
          const event=new MouseEvent('contextmenu',{bubbles:true,cancelable:true,button:2});
          const allowed=el.dispatchEvent(event);
          if(allowed) throw new Error('Default browser context menu not prevented');
        }""")
        assert await page.locator("#nativeFxContext").is_hidden()
        assert await page.locator(".strip").first.locator('[data-native-fx="orbit"]').count()==0
        # Built-in context actions are target-aware; no generic "Orbit" on other FX.
        await page.evaluate("""()=>{
          window.__emitNativeChain([{id:'eq',name:'EQ',bypass:false,latency:0}]);
        }""")
        await page.locator(".strip").first.locator('[data-native-fx="editor"]').click(button="right")
        assert await page.locator("#nativeFxContext [data-context-fx='orbit']").count()==0
        await page.evaluate("""()=>{
          window.__emitNativeChain([{id:'dearvr-pro-2',name:'dearVR PRO 2',bypass:false,latency:0}]);
        }""")
                await page.locator(".strip").first.locator('[data-native-fx="editor"]').click(button="right")
        menu=page.locator("#nativeFxContext")
        await menu.get_by_role("menuitem",name="Orbit for dearVR PRO 2").click()
        assert await page.locator(".nav button.selected").get_attribute("data-view")=="MIX"
        modal = page.locator("#nativeOrbit")
        await modal.locator("#nativeOrbitParameter").wait_for(state="visible")
        # Popover visually belongs to the clicked plugin, never to the center screen.
        geometry=await page.evaluate("""() => {
          const fx=document.querySelector('.strip [data-native-fx="editor"]')?.getBoundingClientRect();
          const panel=document.querySelector('#nativeOrbit .native-dialog')?.getBoundingClientRect();
          const overlay=getComputedStyle(document.querySelector('#nativeOrbit'));
          return {fxBottom:fx.bottom,fxTop:fx.top,x:panel.x,y:panel.y,width:panel.width,
            height:panel.height,top:panel.top,bottom:panel.bottom,
            vw:innerWidth,vh:innerHeight,backdrop:overlay.backgroundColor,
            outside:overlay.pointerEvents};
        }""")
        assert geometry["y"] >= geometry["fxBottom"]-1 or geometry["bottom"] <= geometry["fxTop"]+1, geometry
        assert 0<=geometry["x"] and geometry["x"]+geometry["width"]<=geometry["vw"], geometry
        assert 0<=geometry["top"] and geometry["bottom"]<=geometry["vh"], geometry
        assert geometry["outside"]=="none", geometry
        assert await page.locator("#nativeOrbit .orbit-map").count()==1
        assert await page.locator("#nativeOrbit .material-icon").count()>=3
        assert await page.locator("#nativeOrbitDot").count()==1
        assert await page.locator("#nativeOrbit .native-dialog-footer").is_hidden()
        await page.wait_for_function("""()=>document.querySelector("#nativeOrbitParameter")?.value==="2" """)
        assert await modal.locator("#nativeOrbitParameter").input_value() == "2"
        assert await modal.locator("#nativeOrbitParameter option").count()==2, "Only Azimuth should be listed"
        assert await modal.locator("#nativeOrbitDistance option").count()==2, "Only Distance should be listed"
        assert (await modal.locator("#nativeOrbitDistance option").last.inner_text()).strip()=="Distance"
        assert "30.0 m" not in await modal.locator("#nativeOrbitDistance").inner_text(), "Vendor technical limit is not a dropdown label"
        assert "Gain" not in await modal.locator("#nativeOrbitParameter").inner_text(), "Unrelated VST parameters must be filtered"
        start = modal.locator("[data-orbit-start]")
        assert await modal.locator("#nativeOrbitConfirm").count()==0
        assert await start.is_enabled(), "START must be ready without checkbox"
        assert await modal.locator("#nativeOrbitNear").count()==0, "Circle needs one Distance setpoint"
        assert await modal.locator("#nativeOrbitFar").input_value()=="3"
        dot_before=await page.locator("#nativeOrbitDot").get_attribute("cx")
        await page.wait_for_timeout(150)
        assert await page.locator("#nativeOrbitDot").get_attribute("cx")==dot_before, "Dot must stay still before START"
        geometry = await modal.locator(".native-dialog").evaluate("""el=>({
          width:el.getBoundingClientRect().width,
          height:el.getBoundingClientRect().height,
          overflow:el.scrollWidth>el.clientWidth
        })""")
        assert geometry["width"] <= 480 and not geometry["overflow"], geometry
        await page.screenshot(path="native-orbit-ui-review.png", full_page=True)
        # Switch modes before START; unsupported mapping must never silently run.
        await modal.locator("#nativeOrbitShape").select_option("4")
        await page.wait_for_function("""()=>document.querySelector("#nativeOrbitPath")?.getAttribute("d")?.length>120""")
        assert await modal.locator("#nativeOrbitDistance").input_value()=="4"
        assert await modal.locator("#nativeOrbitElevation").input_value()=="5"
        assert await modal.locator("[data-orbit-start]").is_enabled()
        await modal.locator("#nativeOrbitNear").fill("2")
        await modal.locator("#nativeOrbitFar").fill("5")
        await modal.locator("#nativeOrbitSeconds").fill("15")
        await modal.locator("#nativeOrbitReverse").select_option("true")
        await modal.locator("[data-orbit-start]").click()
        await page.wait_for_function("""()=>window.__JUCE__.backend.calls.some(x=>
          x.payload.action==='orbitStart')""")
        start_payload = await page.evaluate("""()=>window.__JUCE__.backend.calls.findLast(x=>
          x.payload.action==='orbitStart').payload""")
        assert start_payload == {
            "action": "orbitStart", "slot": 0, "revision": "1", "pluginId": "dearvr-pro-2", "parameterIndex": 2,
            "seconds": 15, "reverse": True,
            "shape": 4, "distanceIndex": 4, "elevationIndex": 5,
            "nearDistance": 2, "farDistance": 5, "distanceUnit": "m",
        }, start_payload
        # Animated dot advances along the C++-supplied sampled path.
        first=await page.locator("#nativeOrbitDot").get_attribute("cx")
        await page.wait_for_timeout(180)
        second=await page.locator("#nativeOrbitDot").get_attribute("cx")
        assert first!=second, (first,second)
        # UPDATE must apply new Near/Far setpoints to the host and redraw the fields.
        await modal.locator("#nativeOrbitNear").fill("1.5")
        await modal.locator("#nativeOrbitFar").fill("6")
        await modal.locator("#nativeOrbitSeconds").fill("18")
        await modal.locator("[data-orbit-start]").click()
        await page.wait_for_function("""()=>window.__JUCE__.backend.calls.filter(x=>
          x.payload.action==='orbitStart').length===2""")
        update=await page.evaluate("""()=>window.__JUCE__.backend.calls.filter(x=>
          x.payload.action==='orbitStart').at(-1).payload""")
        assert update["nearDistance"]==1.5 and update["farDistance"]==6
        assert update["seconds"]==18
        assert await modal.locator("#nativeOrbitNear").input_value()=="1.5"
        assert await modal.locator("#nativeOrbitFar").input_value()=="6"
        assert await modal.locator("#nativeOrbitSeconds").input_value()=="18"
        await modal.locator("[data-orbit-stop]").click()
        await page.wait_for_function("""()=>window.__JUCE__.backend.calls.some(x=>
          x.payload.action==='orbitStop')""")
        frozen=await page.locator("#nativeOrbitDot").get_attribute("cx")
        await page.wait_for_timeout(160)
        assert await page.locator("#nativeOrbitDot").get_attribute("cx")==frozen, "Dot stops with engine"
        await modal.locator('[data-close-modal="nativeOrbit"]').click()
        await page.locator('[data-view="FX RACK"]').click()
        assert await page.locator(".native-node [data-native-fx='orbit']").count()==0
        await page.locator(".native-node [data-native-fx='editor']").first.click(button="right")
        await menu.get_by_role("menuitem",name="Orbit for dearVR PRO 2").click()
        await modal.locator("#nativeOrbitParameter").wait_for(state="visible")
        await page.keyboard.press("Escape")
        assert await modal.is_hidden(), "Escape closes contextual orbit popover"
        assert not errors, errors
        await browser.close()
    print("Native Orbit UX + real bridge event payload smoke PASS")


if __name__ == "__main__":
    asyncio.run(main())
