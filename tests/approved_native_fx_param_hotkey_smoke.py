"""H2: actual assembled /preview/ native-only FX parameter-browser journey.

The JUCE bridge is mocked only to verify GUI intent/ACK/readback and stale
identity. Real hosted VST3 parameter-ID access is validated by MSVC CTest.
No fake audio PCM or vendor UI interception is claimed.
"""
import copy
import importlib.util
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, SOURCE

ROOT=Path(__file__).resolve().parents[1]


def main():
    spec=importlib.util.spec_from_file_location(
        "native_bundle", ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="armix-h2-ui-") as tmp, sync_playwright() as p:
        out=Path(tmp)
        bundle.stage(out)
        browser=p.chromium.launch(headless=True)
        page=browser.new_page(viewport={"width":1366,"height":768})
        errors=[]
        page.on("pageerror",lambda e:errors.append(str(e)))
        page.add_init_script("""window.__nativeListeners=new Map();window.__events=[];
            window.__JUCE__={backend:{
              emitEvent(name,payload){window.__events.push({name,payload});},
              addEventListener(name,handler){window.__nativeListeners.set(name,handler);}
            }};""")
        page.goto((out/"index.html").as_uri())
        page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
        state=snapshot()
        state["graph"]["channels"][0]["inserts"]=[{
            "id":"g5c-real-001","catalogId":"vendor-vst3|class-7",
            "name":"Vendor VST3","bypassed":False,"latencySamples":0}]
        state["graph"]["channels"][0]["fxRackBypassed"]=False
        state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
        emit(page,"arMixConsoleSnapshot",state)
        page.wait_for_selector('#tracks .strip[data-id="1"] .fxslot-row:not(.is-empty)')
        emit(page,"arMixHotkeyRegistry",{
            "schema":"arsonmix.hotkey-registry.v1",
            "sessionId":"11111111-2222-4333-8444-555555555555",
            "entries":[]})
        row=page.locator('#tracks .strip[data-id="1"] .fxslot-row:not(.is-empty)').first

        row.click(button="right")
        menu=page.locator("#armixContext")
        assert menu.is_visible()
        assert menu.get_by_role("menuitem",name="Browse Parameters…").count()==1
        assert menu.get_by_role("menuitem",name="Last Touched Hotkey…").count()==1
        menu.get_by_role("menuitem",name="Browse Parameters…").click()
        page.wait_for_function("""()=>window.__events.some(e=>
            e.name==='arMixFxParameterHotkeyInspect')""")
        request=page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixFxParameterHotkeyInspect').at(-1).payload""")
        assert request["channelId"]==SOURCE
        assert request["insertId"]=="g5c-real-001"
        assert request["catalogId"]=="vendor-vst3|class-7"
        assert request["deviceEpoch"]==state["device"]["epoch"]
        assert request["revision"]==state["graph"]["revision"]
        params=[
            {"id":"class:77","name":"Stereo Width","index":4,
             "toggle":False,"steps":2147483647,"value":0.5},
            {"id":"class:8","name":"Stereo Mode","index":0,
             "toggle":True,"steps":2,"value":0.0}]
        emit(page,"arMixFxParameterHotkeyState",{
            **request,"ok":True,"error":"NONE","slot":0,
            "parameters":params,"lastTouchedId":""})
        panel=page.locator("#armixFxParamBrowse")
        assert panel.is_visible()
        assert panel.locator(".armix-param-row").count()==2
        panel.get_by_role("button",name="Assign Toggle hotkey for Stereo Mode").click()
        card=page.locator("#armixHotkeyCard")
        assert card.is_visible()
        assert "Stereo Mode" in card.locator("#armixHkTarget").inner_text()
        page.keyboard.press("Control+Shift+F5")
        card.locator("[data-hk-save]").click()
        reg=page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixHotkeyIntent'&&e.payload.operation==='REGISTER')
            .at(-1).payload""")
        mapping=reg["mapping"]
        assert mapping["channelId"]==SOURCE and mapping["slot"]==0
        assert mapping["insertId"]=="g5c-real-001"
        assert mapping["catalogId"]=="vendor-vst3|class-7"
        assert mapping["paramId"]=="class:8" and mapping["paramName"]=="Stereo Mode"
        assert mapping["operation"]=="SET_FX_PARAM_TOGGLE"
        assert mapping["vk"]==0x74 and mapping["modifiers"]==6
        # A native ACK alone is not accepted as persistent success.
        assert card.is_visible()
        registered={**mapping,"status":"REGISTERED"}
        emit(page,"arMixHotkeyRegistry",{
            "schema":"arsonmix.hotkey-registry.v1",
            "sessionId":mapping["sessionId"],"entries":[registered],
            "requestId":reg["requestId"],"status":"ACCEPTED"})
        assert card.is_visible()
        emit(page,"arMixHotkeyRegistry",{
            "schema":"arsonmix.hotkey-registry.v1",
            "sessionId":mapping["sessionId"],"entries":[registered]})
        assert card.is_hidden()

        # Last Touched is NEVER fabricated: without a vendor gesture the
        # browser offers a genuine parameter browse fallback.
        row.click(button="right")
        menu.get_by_role("menuitem",name="Last Touched Hotkey…").click()
        request2=page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixFxParameterHotkeyInspect').at(-1).payload""")
        emit(page,"arMixFxParameterHotkeyState",{
            **request2,"ok":True,"error":"NONE","slot":0,
            "parameters":params,"lastTouchedId":""})
        assert "No vendor gesture yet" in panel.inner_text()
        assert panel.locator(".armix-param-row").count()==2
        panel.locator("[data-fxparam-close]").click()
        row.click(button="right")
        menu.get_by_role("menuitem",name="Last Touched Hotkey…").click()
        request3=page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixFxParameterHotkeyInspect').at(-1).payload""")
        emit(page,"arMixFxParameterHotkeyState",{
            **request3,"ok":True,"error":"NONE","slot":0,
            "parameters":params,"lastTouchedId":"class:77"})
        assert panel.locator(".armix-param-row").count()==1
        assert "Stereo Width" in panel.locator(".armix-param-row").inner_text()
        # Native parameter-ID mismatches/replaced VST instances cannot
        # rebind from the old row to a different plugin.
        replaced=copy.deepcopy(state)
        replaced["graph"]["revision"]="3"
        replaced["graph"]["channels"][0]["inserts"][0]["id"]="g5c-replaced"
        replaced["mixerSession"]["routeRevision"]="3"
        replaced["mixerSession"]["channels"]=copy.deepcopy(replaced["graph"]["channels"])
        emit(page,"arMixConsoleSnapshot",replaced)
        before=page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixHotkeyIntent'&&e.payload.operation==='REGISTER').length""")
        panel.get_by_role("button",name="Assign + hotkey for Stereo Width").click()
        assert panel.is_hidden()
        assert page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixHotkeyIntent'&&e.payload.operation==='REGISTER').length""")==before
        assert not errors,errors
        browser.close()
    print("H2 compiled native VST3 ParamID Browse/Last Touched/ACK identity: PASS")


if __name__=="__main__":
    main()
