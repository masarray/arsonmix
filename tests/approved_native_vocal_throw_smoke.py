"""Real approved /preview/ bundle: ArSon Vocal Throw picker, insert, native ACK.
The JUCE event transport is emulated, NOT the native DSP nor singing quality.
DSP is independently tested in standalone CTest on Windows."""
import copy
import importlib.util
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, ack, SOURCE, RECEIVER

ROOT=Path(__file__).resolve().parents[1]
ID="armix.builtin.vocal-throw.v1"
INSTANCE="vocal-throw-test-instance"
DEFAULTS={"mode":2,"bpm":120,"division":0,"repeats":3,
 "captureMs":460,"silenceMs":220,"thresholdDb":-35,
 "wet":.42,"decay":.55,"ducking":.7}

def last(page,action):
    records=page.evaluate("name=>window.__actions.filter(x=>x.action===name)",action)
    assert records, "No "+action+" event"
    return records[-1]

def reply_snapshot(page,state,settings,applied=None,count=0,tail=False,music=None):
    act=last(page,"nativeThrowSnapshot")
    data={"requestId":act["requestId"],"channelId":SOURCE,"slot":0,
        "instanceId":INSTANCE,"status":"ACCEPTED","schemaVersion":1,
        "deviceEpoch":state["device"]["epoch"],
        "graphRevision":state["graph"]["revision"],"sampleRate":48000,
        "addedLatencySamples":0,"throwCount":count,"tailActive":tail,
        "musicSourceChannelId":music["source"] if music else "0",
        "musicSourceActive":bool(music),
        "musicLocked":bool(music),
        "musicBpm":music["bpm"] if music else 0,
        "musicConfidence":music["confidence"] if music else 0,
        "musicGridAvailable":music.get("grid",False) if music else False,
        "musicPhaseConfidence":music["confidence"] if music and music.get("grid",False) else 0,
        "settings":copy.deepcopy(settings)}
    if applied:data["appliedRequestId"]=applied
    emit(page,"arMixBuiltinThrowSnapshot",data)

def main():
    spec=importlib.util.spec_from_file_location(
        "bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    source_svg=(ROOT/"site/preview-v2/assets/arson-vocal-throw-performance.svg").read_text()
    assert "data:image/webp;base64," in source_svg, "original user Illustrator icon must be embedded"
    assert 'clip-path="url(#aiLens)"' in source_svg
    assert 'M128 29 L120 43' not in source_svg, "no 4-way navigation markers on THROW"
    with tempfile.TemporaryDirectory(prefix="armix-throw-") as directory,sync_playwright() as pw:
        out=Path(directory);bundle.stage(out)
        browser=pw.chromium.launch(headless=True)
        # Desktop, constrained desktop and portrait operator control panel.
        for width,height in [(1366,768),(650,720),(390,844)]:
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.add_init_script("""window.__nativeListeners=new Map();
              window.__nativeIntents=[];window.__actions=[];
              window.__JUCE__={backend:{
                emitEvent(name,payload){
                  if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
                  if(name==='arMixAction')window.__actions.push(payload);},
                addEventListener(name,callback){
                  const old=window.__nativeListeners.get(name);
                  window.__nativeListeners.set(name,old?
                    payload=>{old(payload);callback(payload);}:callback);}}};""")
            page.goto((out/"index.html").as_uri())
            page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled && window.ArSonMixNativeVocalThrow")
            state=snapshot()
            state["graph"]["capabilities"]["supportedOps"]+=["LOAD_INSERT","REMOVE_INSERT","MOVE_INSERT"]
            emit(page,"arMixConsoleSnapshot",state)
            if width<=500:page.locator("#bankSelect").select_option("0")
            strip=page.locator('#tracks .strip[data-id="1"]')
            strip.locator('.fx-pick.empty').first.click()
            picker=page.locator("#fastResults [data-fast-native-throw]")
            picker.wait_for(state="visible")
            assert picker.count()==1,"exactly one Vocal Throw built-in picker option"
            picker.click()
            intents=page.evaluate("""()=>window.__nativeIntents.filter(
                x=>x.operation==='LOAD_INSERT')""")
            assert len(intents)==1,intents
            assert intents[0]["payload"]["catalogId"]==ID
            assert page.locator("#mxNativeVocalThrow").is_hidden(),"no editor before accepted insert"
            state["graph"].update(revision="3",rosterRevision="3")
            state["graph"]["channels"][0]["fxRackBypassed"]=False
            state["graph"]["channels"][0]["inserts"].append({
                "id":INSTANCE,"catalogId":ID,"name":"ArSon Vocal Throw","bypassed":False})
            state["mixerSession"].update(routeRevision="3",rosterRevision="3",
                channels=copy.deepcopy(state["graph"]["channels"]))
            ack(page,state,intents[0]);emit(page,"arMixConsoleSnapshot",state)
            row=strip.locator('.fxslot-row[data-insert-id="'+INSTANCE+'"]')
            row.wait_for(state="visible")
            page.keyboard.press("Escape")
            row.locator('[data-action="openPlugin"]').click()
            editor=page.locator("#mxNativeVocalThrow")
            editor.wait_for(state="visible")
            assert editor.locator("#vtPreset").count()==1
            # If a name exceeds the available header cell it may ellipsize,
            # but the actual full source channel remains directly accessible.
            channel=editor.locator("#vtChannel")
            # DOM textContent is the canonical identity; innerText can
            # reflect CSS text-transform/whitespace on the rendered label.
            assert channel.get_attribute("title")==channel.text_content(), \
                (channel.get_attribute("title"),channel.text_content())
            assert channel.get_attribute("aria-label")==channel.text_content()
            assert editor.locator("#vtClose.mx-comp-close.armix-floating-close").count()==1
            assert editor.locator("#vtPower.mx-comp-power.armix-builtin-fx-power").count()==1
            assert editor.locator("#vtClose.armix-builtin-fx-close").count()==1
            assert page.locator("#mxNativePeq .armix-builtin-fx-power").count()==1
            assert page.locator("#mxNativePeq .armix-builtin-fx-close").count()==1
            assert page.locator("#mxNativeComp .armix-builtin-fx-power").count()==1
            assert page.locator("#mxNativeComp .armix-builtin-fx-close").count()==1
            assert editor.locator("#vtPower").get_attribute("data-fx-state")=="on"
            assert editor.locator("#vtPower").get_attribute("aria-pressed")=="true"
            # EQ and Compressor have ONE CSS owner for 24px Close and 26px
            # Power hit boxes, the same as Vocal Throw (no local overrides).
            close_box=editor.locator("#vtClose").bounding_box()
            power_box=editor.locator("#vtPower").bounding_box()
            assert close_box and power_box
            assert abs(close_box["width"]-24)<.1 and abs(close_box["height"]-24)<.1
            assert abs(power_box["width"]-26)<.1 and abs(power_box["height"]-24)<.1
            assert power_box["x"]+power_box["width"]<=close_box["x"]+1
            assert editor.locator("#vtMode").input_value()=="2"
            initial=last(page,"nativeThrowSnapshot")
            page.wait_for_timeout(300)
            assert last(page,"nativeThrowSnapshot")["requestId"]==initial["requestId"],\
                "snapshot polling overwrote outstanding request too early"
            reply_snapshot(page,state,DEFAULTS)
            assert editor.locator("#vtStatus").inner_text()=="LIVE"
            assert editor.locator("#vtCount").inner_text()=="0"
            box=editor.bounding_box()
            assert box and box["width"]<=778 and box["width"]<=width and \
                box["x"]>=0 and box["x"]+box["width"]<=width+1,(width,box)
            assert editor.evaluate("(el)=>el.scrollWidth<=el.clientWidth+1"),\
                "premium FX editor overflows horizontal viewport"
            assert editor.locator("#vtDisplayBpm").inner_text()=="120"
            assert editor.locator("#vtClockLabel").inner_text()=="MAN"
            # BPM is one integrated LCD number, not a detached duplicate.
            # Click the LCD to enter editing; Escape restores the accepted
            # native setting without issuing a new DSP command.
            assert editor.locator("#vtBpm").is_hidden()
            editor.locator("#vtDisplayBpm").click()
            assert editor.locator("#vtBpm").is_visible()
            assert editor.locator("#vtBpm").input_value()=="120"
            editor.locator("#vtBpm").fill("133")
            editor.locator("#vtBpm").press("Escape")
            assert editor.locator("#vtBpm").is_hidden()
            assert editor.locator("#vtDisplayBpm").inner_text()=="120"
            assert editor.locator("#vtBeatRail i[data-on='true']").count()==3
            assert editor.locator(".vt-encoder-bank .vt-encoder-face").count()==6
            assert editor.locator("#vtWetDial[role='slider']").count()==1
            assert editor.locator(".vt-throw-button").count()==1
            trigger=editor.locator("#vtThrow")
            assert trigger.get_attribute("aria-label")=="Throw captured vocal phrase"
            assert trigger.inner_text().strip()=="","Singer cue has no noisy visible words"
            tb=trigger.bounding_box()
            assert tb and tb["width"]>=160 and tb["height"]>=160
            assert editor.locator(".vt-throw-engraving").inner_text()=="THROW"
            assert editor.locator("#vtTap").is_visible()
            assert editor.locator("#vtMusicStatus").inner_text()=="TAP"
            assert "FIRE PHRASE" not in editor.inner_text()
            assert "SMART PHRASE / LIVE ENGINE" not in editor.inner_text()
            assert "ZERO DRY LATENCY" not in editor.inner_text()
            assert editor.locator(".vt-mode-buttons button").count()==3
            assert editor.locator(".vt-division-buttons button").count()==4
            # No browser form hiding of basic parameters: real accessible
            # hardware rotary controls are displayed at desktop AND mobile.
            assert editor.locator("#vtWetDial").is_visible()
            assert editor.locator('.vt-encoder[data-key="captureMs"] button').is_visible()
            # Every control, including MIX, has the exact SAME stationary
            # knob body, pointer and rounded level arc geometry.
            assert editor.locator(".vt-encoder-face").count()==7
            assert editor.locator(".vt-meter-fill").count()==7
            assert editor.locator(".vt-knob-meter").count()==7
            for dial in editor.locator(".vt-encoder-face").all():
                bg=dial.evaluate("(el)=>getComputedStyle(el).backgroundImage")
                assert "arson-vocal-hardware-encoder.svg" in bg,bg
                assert dial.locator(".vt-meter-fill").get_attribute("pathLength")=="100"
            # Inspect the actual rendered SVG control backgrounds. WebView
            # file:// disables fetch() for SVG files, not CSS backgrounds.
            for selector,name in [("#vtWetDial","encoder"),
                                  ('.vt-encoder-face',"encoder"),
                                  ("#vtThrow","performance")]:
                bg=editor.locator(selector).first.evaluate(
                    "(el)=>getComputedStyle(el).backgroundImage")
                assert ("arson-vocal-throw-performance.svg" if name=="performance"
                        else "arson-vocal-hardware-"+name+".svg") in bg,(selector,bg)
            # Bounded physical layout QA, not just a screenshot. The large
            # singer switch must be centered, the two encoder banks in
            # alignment, and tempo LCD and TAP must be physically adjacent.
            throw=editor.locator("#vtThrow").bounding_box()
            left=editor.locator("#vtEncodersLeft").bounding_box()
            right=editor.locator("#vtEncodersRight").bounding_box()
            lcd=editor.locator(".vt-clock-screen").bounding_box()
            tap=editor.locator("#vtTap").bounding_box()
            assert all([throw,left,right,lcd,tap])
            assert abs((throw["x"]+throw["width"]/2)-(box["x"]+box["width"]/2))<4, \
                ("THROW is off center",width,throw,box)
            if width>600:
                assert abs(left["y"]-right["y"])<3, ("uneven encoder rows",left,right)
                assert left["x"]+left["width"]<=throw["x"]+1 and \
                    throw["x"]+throw["width"]<=right["x"]+1, \
                    ("encoders invade singer button",left,throw,right)
            else:
                assert throw["y"]+throw["height"]<left["y"] and \
                    left["y"]+left["height"]<=right["y"]+1, \
                    ("portrait controls not ordered cleanly",throw,left,right)
            assert lcd["x"]+lcd["width"]<=tap["x"] and \
                tap["x"]-(lcd["x"]+lcd["width"])<=17, \
                ("LCD must sit beside TAP",lcd,tap)
            assert abs((lcd["y"]+lcd["height"]/2)-(tap["y"]+tap["height"]/2))<=3, \
                ("LCD/TAP must share a physical control centerline",lcd,tap)
            assert lcd["width"]>=145 and lcd["height"]>=70
            # Text must have a real cell, not be squeezed against a knob.
            # Three columns in each bank share exact name/rotary/value
            # baselines; this caught the original tiny asymmetric 8px text.
            for bank in ("#vtEncodersLeft","#vtEncodersRight"):
                names=editor.locator(bank+" .vt-encoder-name")
                dials=editor.locator(bank+" .vt-encoder-face")
                values=editor.locator(bank+" .vt-encoder-value")
                assert names.count()==dials.count()==values.count()==3
                labels=[el.bounding_box() for el in names.all()]
                knobs=[el.bounding_box() for el in dials.all()]
                nums=[el.bounding_box() for el in values.all()]
                assert max(x["y"] for x in labels)-min(x["y"] for x in labels)<1
                assert max(x["y"] for x in knobs)-min(x["y"] for x in knobs)<1
                assert max(x["y"] for x in nums)-min(x["y"] for x in nums)<1
                for n,k,v in zip(labels,knobs,nums):
                    center=lambda b:b["x"]+b["width"]/2
                    assert abs(center(n)-center(k))<1.5 and \
                           abs(center(v)-center(k))<1.5, \
                           ("rotary name/value not centered",width,n,k,v)
                    assert n["y"]+n["height"]<=k["y"]+1 and \
                           k["y"]+k["height"]<=v["y"]+1, \
                           ("control text collides with rotary",width,n,k,v)
                for name in names.all():
                    assert name.evaluate("""el=>
                        parseFloat(getComputedStyle(el).fontSize)>=9 &&
                        el.scrollWidth<=el.clientWidth+1"""), \
                        ("encoder caption is too small or clipped",width,
                         name.inner_text())
            wet=editor.locator("#vtWetDial").bounding_box()
            mode=editor.locator(".vt-mode-buttons").bounding_box()
            assert wet and mode
            assert abs((wet["y"]+wet["height"]/2)-
                       (mode["y"]+mode["height"]/2))<11, \
                       ("MIX and trigger controls are vertically off-axis",width,wet,mode)
            # Every song-source selector has a deliberate text space.
            song=editor.locator("#vtMusicSource").bounding_box()
            assert song and song["width"]>=180,("MUSIC selector too narrow",width,song)
            assert editor.locator("#vtMusicSource").evaluate("""el=>
                el.scrollWidth<=el.clientWidth+1""")
            if width>600:
                rhythm=editor.locator(".vt-pulse .vt-small-label").bounding_box()
                music=editor.locator(".vt-music-source .vt-small-label").bounding_box()
                assert rhythm and music and abs(rhythm["y"]-music["y"])<1, \
                    ("RHYTHM and MUSIC section labels not aligned",width,rhythm,music)
            rail=editor.locator(".vt-status-rail").bounding_box()
            assert rail and rail["y"]+rail["height"]<=box["y"]+box["height"]+2, \
                ("footer clipped beneath mobile controls",width,rail,box)
            # No control may draw past the horizontal plugin bounds.
            for control in ["#vtThrow","#vtTap","#vtWetDial",
                            "#vtClockLabel","#vtDisplayBpm"]:
                rect=editor.locator(control).bounding_box()
                assert rect and rect["x"]>=box["x"]-1 and \
                    rect["x"]+rect["width"]<=box["x"]+box["width"]+1,(control,rect,box)
            page.screenshot(path="arsonmix-vocal-throw-"+str(width)+".png")
            editor.screenshot(path="arsonmix-vocal-throw-editor-"+str(width)+".png")
            if width==1366:
                editor.locator('.vt-mode-buttons button[data-mode="0"]').click()
                edit=last(page,"nativeThrowSet")
                assert edit["settings"]["mode"]==0 and edit["instanceId"]==INSTANCE
                assert edit["channelId"]==SOURCE
                emit(page,"arMixBuiltinThrowQueue",{"requestId":edit["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinThrowAck",{"requestId":edit["requestId"],"status":"APPLIED"})
                reply_snapshot(page,state,{**DEFAULTS,"mode":0},applied=edit["requestId"])
                assert editor.locator("#vtMode").input_value()=="0"
                assert editor.locator('.vt-mode-buttons button[data-mode="0"]').get_attribute("aria-pressed")=="true"
                # Rotary wheel edits actually send settings through the native
                # request/ACK/readback contract, not a CSS-only animation.
                dial=editor.locator("#vtWetDial")
                before=dial.evaluate("""el=>({
                   offset:parseFloat(getComputedStyle(el.querySelector('.vt-meter-fill')).strokeDashoffset),
                   transform:getComputedStyle(el.querySelector('.vt-encoder-needle')).transform,
                   bg:getComputedStyle(el).backgroundImage
                })""")
                dial.hover()
                page.mouse.wheel(0,-120)
                after=dial.evaluate("""el=>({
                   offset:parseFloat(getComputedStyle(el.querySelector('.vt-meter-fill')).strokeDashoffset),
                   transform:getComputedStyle(el.querySelector('.vt-encoder-needle')).transform,
                   bg:getComputedStyle(el).backgroundImage
                })""")
                assert float(after["offset"])<float(before["offset"]),(before,after)
                assert after["transform"]!=before["transform"]
                assert after["bg"]==before["bg"],"knob chassis must not rotate"
                rotary=last(page,"nativeThrowSet")
                assert rotary["settings"]["wet"]>.42 and rotary["settings"]["wet"]<=.44
                emit(page,"arMixBuiltinThrowQueue",{"requestId":rotary["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinThrowAck",{"requestId":rotary["requestId"],"status":"APPLIED"})
                updated={**DEFAULTS,"mode":0,"wet":rotary["settings"]["wet"]}
                reply_snapshot(page,state,updated,applied=rotary["requestId"])
                assert "%" in editor.locator("#vtWetValue").inner_text()
                editor.locator("#vtThrow").click()
                assert editor.locator("#vtThrow").is_disabled(), "no repeated stage cue until native ACK"
                cue=last(page,"nativeThrowTrigger")
                assert cue["instanceId"]==INSTANCE and cue["channelId"]==SOURCE
                assert cue["slot"]==0
                emit(page,"arMixBuiltinThrowQueue",{"requestId":cue["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinThrowAck",{"requestId":cue["requestId"],"status":"APPLIED"})
                reply_snapshot(page,state,{**DEFAULTS,"mode":0,"wet":rotary["settings"]["wet"]},count=1,tail=True)
                assert editor.locator("#vtCount").inner_text()=="1"
                assert editor.locator("#vtStatus").inner_text()=="TAIL"
                assert editor.locator("#vtThrow").get_attribute("data-tail")=="true"
                assert not editor.locator("#vtThrow").is_disabled()
            # Real native tempo-source command (not a browser-only BPM guess).
            source_menu=editor.locator("#vtMusicSource")
            assert source_menu.locator('option[value="'+RECEIVER+'"]').count()==1
            source_menu.select_option(RECEIVER)
            request=last(page,"nativeThrowTempoSource")
            assert request["sourceChannelId"]==RECEIVER and request["channelId"]==SOURCE
            assert editor.locator("#vtMusicStatus").inner_text()=="CONNECT"
            emit(page,"arMixBuiltinThrowTempoAck",{
                "requestId":request["requestId"],"status":"APPLIED",
                "sourceChannelId":RECEIVER})
            reply_snapshot(page,state,
                {**DEFAULTS,"mode":0,"wet":rotary["settings"]["wet"]} if width==1366 else DEFAULTS,
                count=1 if width==1366 else 0,tail=width==1366,
                music={"source":RECEIVER,"bpm":120,"confidence":.85})
            assert editor.locator("#vtMusicStatus").inner_text()=="120 · BPM"
            assert editor.locator("#vtClockLabel").inner_text()=="AUTO"
            # Snapshot transport is request/response, not unsolicited push.
            # Wait for a fresh native read request before testing a second
            # DSP status update; a replay of the prior request is ignored.
            previous=last(page,"nativeThrowSnapshot")["requestId"]
            page.wait_for_function(
                """old=>window.__actions.filter(x=>x.action==='nativeThrowSnapshot').at(-1)?.requestId!==old""",
                arg=previous,timeout=4000)
            # Confidence-approved grid is a separate native fact; the GUI
            # must never fabricate it from BPM alone.
            reply_snapshot(page,state,
                {**DEFAULTS,"mode":0,"wet":rotary["settings"]["wet"]} if width==1366 else DEFAULTS,
                count=1 if width==1366 else 0,tail=width==1366,
                music={"source":RECEIVER,"bpm":120,"confidence":.85,"grid":True})
            assert editor.locator("#vtMusicStatus").inner_text()=="120 · GRID"
            assert editor.locator("#vtClockLabel").inner_text()=="LOCK"
            if width==1366:
                # The next changed graph invalidates the old editor instance.
                state["graph"]["channels"][0]["inserts"][0]["id"]="replaced-instance"
                state["graph"].update(revision="4",rosterRevision="4")
                state["mixerSession"].update(routeRevision="4",rosterRevision="4",
                    channels=copy.deepcopy(state["graph"]["channels"]))
                emit(page,"arMixConsoleSnapshot",state)
                page.wait_for_function("""()=>document.querySelector('#vtThrow')?.disabled""",timeout=2000)
            editor.locator("#vtClose").click()
            assert editor.is_hidden(),"standard close must dismiss editor"
            assert not errors,(width,errors)
            page.close()
        browser.close()
    print("PASS: ArSon Vocal Throw native picker/ACK/manual cue/compact editor in 2 viewports")

if __name__=="__main__":
    main()
