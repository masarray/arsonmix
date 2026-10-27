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
                  parameters:[
                    {index:1,name:'Azimuth Scale',minText:'0.5',maxText:'5'},
                    {index:2,name:'Azimuth',minText:'-180°',maxText:'+180°'},
                    {index:3,name:'Gain',minText:'-60',maxText:'12'}
                  ]
                }));
              }
              if(payload.action === 'orbitStart') {
                queueMicrotask(() => listeners.get('arMixOrbitState')?.({
                  slot:payload.slot, plugin:'dearVR PRO 2', active:true,
                  activeSlot:payload.slot, activeParameter:payload.parameterIndex,
                  periodSeconds:payload.seconds,reverse:payload.reverse,
                  preferredParameter:payload.parameterIndex,
                  parameters:[
                    {index:2,name:'Azimuth',minText:'-180°',maxText:'+180°'}
                  ]
                }));
              }
              if(payload.action === 'orbitStop') {
                queueMicrotask(() => listeners.get('arMixOrbitState')?.({
                  slot:payload.slot, plugin:'dearVR PRO 2', active:false,
                  activeSlot:-1,activeParameter:-1,periodSeconds:20,
                  reverse:false,preferredParameter:2,
                  parameters:[{index:2,name:'Azimuth',minText:'-180°',maxText:'+180°'}]
                }));
              }
            }
          };
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
        await page.locator(".strip").first.locator('[data-native-fx="editor"]').click(button="right")
        menu=page.locator("#nativeFxContext")
        await menu.get_by_role("menuitem",name="Auto Orbit").click()
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
        assert await page.locator("#nativeOrbit .orbit-graphic").count()==1
        assert await page.locator("#nativeOrbit .native-dialog-footer").is_hidden()
        await page.wait_for_function("""()=>document.querySelector("#nativeOrbitParameter")?.value==="2" """)
        assert await modal.locator("#nativeOrbitParameter").input_value() == "2"
        assert "-180" in await modal.inner_text(), "Vendor endpoint range should be visible"
        start = modal.locator("[data-orbit-start]")
        assert await start.is_disabled(), "Start must require range confirmation"
        await modal.locator("#nativeOrbitConfirm").check()
        assert await start.is_enabled()
        geometry = await modal.locator(".native-dialog").evaluate("""el=>({
          width:el.getBoundingClientRect().width,
          height:el.getBoundingClientRect().height,
          overflow:el.scrollWidth>el.clientWidth
        })""")
        assert geometry["width"] <= 480 and not geometry["overflow"], geometry
        await page.screenshot(path="native-orbit-ui-review.png", full_page=True)
        await modal.locator("#nativeOrbitSeconds").fill("15")
        await modal.locator("#nativeOrbitReverse").select_option("true")
        await start.click()
        await page.wait_for_function("""()=>window.__JUCE__.backend.calls.some(x=>
          x.payload.action==='orbitStart')""")
        start_payload = await page.evaluate("""()=>window.__JUCE__.backend.calls.findLast(x=>
          x.payload.action==='orbitStart').payload""")
        assert start_payload == {
            "action": "orbitStart", "slot": 0, "revision": "1", "pluginId": "dearvr-pro-2", "parameterIndex": 2,
            "seconds": 15, "reverse": True,
        }, start_payload
        await modal.locator("[data-orbit-stop]").click()
        await page.wait_for_function("""()=>window.__JUCE__.backend.calls.some(x=>
          x.payload.action==='orbitStop')""")
        await modal.locator('[data-close-modal="nativeOrbit"]').click()
        await page.locator('[data-view="FX RACK"]').click()
        assert await page.locator(".native-node [data-native-fx='orbit']").count()==0
        await page.locator(".native-node [data-native-fx='editor']").first.click(button="right")
        await menu.get_by_role("menuitem",name="Auto Orbit").click()
        await modal.locator("#nativeOrbitParameter").wait_for(state="visible")
        await page.keyboard.press("Escape")
        assert await modal.is_hidden(), "Escape closes contextual orbit popover"
        assert not errors, errors
        await browser.close()
    print("Native Orbit UX + real bridge event payload smoke PASS")


if __name__ == "__main__":
    asyncio.run(main())
