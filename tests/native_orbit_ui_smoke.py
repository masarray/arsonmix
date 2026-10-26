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
        await page.add_init_script("""() => {
          // Wrapper function intentionally invoked below: init scripts execute in window.
        }""")
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
                  slot:payload.slot, plugin:'dearVR PRO 2', active:false,
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
        await page.locator('[data-view="FX RACK"]').click()
        await page.get_by_role("button", name="ORBIT").click()
        modal = page.locator("#nativeOrbit")
        await modal.locator("#nativeOrbitParameter option").first.wait_for()
        assert await modal.locator("#nativeOrbitParameter").input_value() == "2"
        assert "-180" in await modal.inner_text(), "Vendor endpoint range should be visible"
        start = modal.locator("[data-orbit-start]")
        assert await start.is_disabled(), "Start must require range confirmation"
        await modal.locator("#nativeOrbitConfirm").check()
        assert await start.is_enabled()
        await modal.locator("#nativeOrbitSeconds").fill("15")
        await modal.locator("#nativeOrbitReverse").select_option("true")
        await start.click()
        await page.wait_for_function("""()=>window.__JUCE__.backend.calls.some(x=>
          x.payload.action==='orbitStart')""")
        start_payload = await page.evaluate("""()=>window.__JUCE__.backend.calls.findLast(x=>
          x.payload.action==='orbitStart').payload""")
        assert start_payload == {
            "action": "orbitStart", "slot": 0, "parameterIndex": 2,
            "seconds": 15, "reverse": True,
        }, start_payload
        await modal.locator("[data-orbit-stop]").click()
        await page.wait_for_function("""()=>window.__JUCE__.backend.calls.some(x=>
          x.payload.action==='orbitStop')""")
        assert not errors, errors
        await browser.close()
    print("Native Orbit UX + real bridge event payload smoke PASS")


if __name__ == "__main__":
    asyncio.run(main())
