"""V6 operator actions against the assembled native UI; emulated JUCE transport."""
from pathlib import Path
import importlib.util
import tempfile
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from approved_floating_fx_window_smoke import INIT, document, CHANNEL, INSTANCE
from playwright.sync_api import sync_playwright, expect
from native_event_fixture import NATIVE_EVENTS
from approved_native_offline_logical_smoke import snapshot, emit

ROOT=Path(__file__).resolve().parents[1]


# Compare actual header and rendered rows, including scrollbar/filtered states.
def assert_column_alignment(manager):
    result=manager.evaluate("""el=>{
      const headers=[...el.querySelectorAll('.plugin-heading-identity>span,.plugin-heading-status>span,.plugin-heading-icon')];
      const list=el.querySelector('.plugin-results');
      const visible=e=>getComputedStyle(e).display!=='none';
      const box=e=>{const r=e.getBoundingClientRect();return [r.left,r.width]};
      const textStart=e=>{const r=e.getBoundingClientRect(),s=getComputedStyle(e);return r.left+parseFloat(s.paddingLeft)+parseFloat(s.borderLeftWidth)};
      const h=headers.filter(visible);
      const scroll=el.querySelector('.plugin-table-scroll');
      const sample=[...list.querySelectorAll('.plugin-item')];
      const rows=sample.length>1?[sample[0],sample[Math.floor(sample.length/2)],sample.at(-1)]:sample;
      const errors=[];
      for(const row of rows){
        const cells=[...row.querySelectorAll('.plugin-choose>span,.plugin-labels>span,.plugin-flag')].filter(visible);
        if(cells.length!==h.length){errors.push(['column count',h.length,cells.length]);continue;}
        for(let i=0;i<h.length;i++){
          const [hx,hw]=box(h[i]),[cx,cw]=box(cells[i]);
          const isIcon=h[i].classList.contains('plugin-heading-icon');
          const delta=isIcon?Math.abs(hx+hw/2-cx-cw/2):Math.abs(textStart(h[i])-textStart(cells[i]));
          if(delta>0.5)errors.push([h[i].textContent,delta,hx,cx,hw,cw]);
        }
      }
      return {errors,rows:rows.length,columns:h.length,scroll:scroll.scrollHeight>scroll.clientHeight};
    }""")
    assert result["rows"]>0 and not result["errors"],result
    return result

def assert_manager_controls(manager, same_row=True):
    controls=manager.evaluate("""el=>{
      const ids=['pluginSearch','pluginCategory','pluginView','pluginImport','pluginScan','pluginAddPath'];
      return ids.map(id=>{
        const e=el.querySelector('#'+id),r=e.getBoundingClientRect(),s=getComputedStyle(e);
        return {id,y:r.y,h:r.height,w:r.width,radius:s.borderRadius,box:s.boxSizing,
          background:s.backgroundColor,border:s.borderTopColor,font:s.fontSize};
      });
    }""")
    assert all(abs(c["h"]-34)<=.5 and c["radius"]=="5px" and c["box"]=="border-box" and c["w"]>0 for c in controls),controls
    fields=controls[:3]
    if same_row:
        assert max(c["y"] for c in fields)-min(c["y"] for c in fields)<=.5,fields
    else:
        assert abs(fields[1]["y"]-fields[2]["y"])<=.5,fields
    for c in fields[1:]:
        for key in ("background","border","font"):
            assert c[key]==fields[0][key],(key,fields)
    return controls

CLOSE_VISUAL = """el=>{
 const props=['width','height','minWidth','maxWidth','minHeight','maxHeight','borderRadius',
  'borderTopColor','borderTopWidth','backgroundImage','boxShadow','color','fontSize','lineHeight','filter','opacity','transitionProperty','transitionDuration','transitionTimingFunction'];
 const icon=['backgroundImage','backgroundSize','backgroundPosition','display','opacity','inset','transform'];
 const read=(style,keys)=>Object.fromEntries(keys.map(k=>[k,style[k]]));
 return {face:read(getComputedStyle(el),props),icon:read(getComputedStyle(el,'::after'),icon),
  before:getComputedStyle(el,'::before').display};
}"""

def close_visual_states(page,button):
    page.mouse.move(0,0)
    expect(button).to_have_css('border-top-color','rgb(96, 125, 140)')
    expect(button).to_have_css('color','rgb(224, 237, 242)')
    normal=button.evaluate(CLOSE_VISUAL)
    button.hover()
    # Both surfaces use the same 100 ms transition. Sample its settled value,
    # not a different arbitrary frame of the same animation in each page.
    expect(button).to_have_css('border-top-color','rgb(178, 112, 120)')
    expect(button).to_have_css('color','rgb(255, 255, 255)')
    hover=button.evaluate(CLOSE_VISUAL)
    box=button.bounding_box()
    page.mouse.move(box["x"]+box["width"]/2,box["y"]+box["height"]/2)
    page.mouse.down()
    try: active=button.evaluate(CLOSE_VISUAL)
    finally:
        page.mouse.move(0,0)
        page.mouse.up()
    assert normal["face"]["width"]=="24px" and normal["face"]["height"]=="24px",normal
    assert "svg" in normal["icon"]["backgroundImage"],normal
    assert "rgb(239, 62, 80)" in active["face"]["backgroundImage"],active
    return [normal,hover,active]

def reference_close(browser,url):
    page=browser.new_page(viewport={"width":1580,"height":900},reduced_motion="reduce")
    try:
        page.add_init_script(INIT)
        page.goto(url)
        page.wait_for_function("()=>window.__fxOutbound?.some(x=>x.name==='arMixFloatingReady')")
        catalog="armix.builtin.channel-eq.v1"
        page.evaluate("(p)=>window.__fxDispatch('arMixFloatingOpen',p)",{
            "snapshot":document(catalog),"channelId":CHANNEL,"slot":0,
            "catalogId":catalog,"instanceId":INSTANCE})
        close=page.locator("#mxNativePeq .armix-builtin-fx-close")
        close.wait_for(state="visible")
        close.screenshot(path="v6-plugin-manager-close-reference.png")
        return close_visual_states(page,close)
    finally: page.close()

def exercise(browser,out,width,height,tap,close_reference):
    page=browser.new_page(viewport={"width":width,"height":height})
    errors=[]
    page.on("pageerror",lambda error:errors.append(str(error)))
    page.add_init_script(NATIVE_EVENTS + """window.__events=[];
      window.__JUCE__={backend:{emitEvent(name,payload){window.__events.push({name,payload});},
        ...window.__nativeListenerMethods}};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    emit(page,"arMixConsoleSnapshot",snapshot(workspace_token="direct-sof"))
    page.wait_for_function("()=>document.documentElement.dataset.armixNativeAuthority==='logical-editable'")
    page.locator("#sofToggle").click()
    page.locator("#sofTarget").select_option(label="AUX A")
    strip=page.locator('#tracks .strip[data-id="1"]')
    pre=strip.get_by_role("button",name="Assign PRE send")
    post=strip.get_by_role("button",name="Assign POST send")
    assert pre.is_enabled() and post.is_enabled()
    assert strip.locator(".mx-sof-tap-key").count()==0
    bounds=strip.bounding_box()
    for button in (pre,post):
        box=button.bounding_box()
        assert box and bounds and box["width"]>=26 and box["height"]>=23
        assert box["x"]>=bounds["x"] and box["x"]+box["width"]<=bounds["x"]+bounds["width"]+1
    assert "NO SEND" in strip.locator(".sof-badge").inner_text()
    assert page.evaluate("()=>window.__events.filter(e=>e.name==='arMixLogicalIntent').length")==0
    (pre if tap=="PRE_FADER" else post).click()
    page.wait_for_function("()=>window.__events.some(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='WIRE_TRANSACTION')")
    request=page.evaluate("()=>window.__events.find(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='WIRE_TRANSACTION').payload")
    assert request["channelId"]=="1" and request["baseRevision"]=="1"
    assert request["payload"]["changes"]==[{"op":"CONNECT","from":"1","to":"11","tap":tap,"gainLinear":0}]
    (post if tap=="PRE_FADER" else pre).click()
    assert page.evaluate("()=>window.__events.filter(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='WIRE_TRANSACTION').length")==1
    emit(page,"arMixLogicalAck",{"requestId":request["requestId"],"status":"ACCEPTED","errorCode":"NONE","revision":"2","physicalOutputAdmitted":False})
    assert "NO SEND" in strip.locator(".sof-badge").inner_text()
    assert not page.evaluate("()=>window.__events.some(e=>e.name==='arMixConsoleIntent')")
    page.screenshot(path=f"v6-direct-sof-{width}-{tap}.png")
    # New readback is the only event allowed to publish the new send.
    state=snapshot("2",workspace_token="direct-sof")
    edge={"edgeId":"123456","from":"1","to":"11","signalKind":"AUDIO","enabled":True,
        "sendEnabled":True,"gainLinear":0,"primaryOutput":False,
        "tap":"SPLIT_PRE" if tap=="PRE_FADER" else "POST_FADER","tapStage":tap}
    state["logicalMixer"]["graph"]["edges"].append(edge)
    state["logicalMixer"]["graph"]["channels"][0]["sends"].append(edge.copy())
    emit(page,"arMixConsoleSnapshot",state)
    strip.locator('[data-action="sofTap"]').wait_for(state="visible")
    assert strip.locator(".sof-assign-key").count()==0
    page.locator("#sofToggle").click()
    emit(page,"arMixCatalog",{"items":[],"builtIns":[{"id":"armix.builtin.fixture.v1","name":"Fixture processor","category":"Utility"}]})
    page.locator("#pluginsButton").click()
    manager=page.locator("#pluginBrowser")
    assert manager.is_visible()
    assert manager.locator(".plugin-results").evaluate("e=>getComputedStyle(e).backgroundColor")=="rgb(20, 38, 48)"
    page.screenshot(path=f"v6-plugin-manager-initial-{width}.png")
    assert manager.locator("#pluginScan").evaluate("e=>getComputedStyle(e).backgroundImage")=="none"
    manager.locator("#pluginSearch").fill("zzzz-no-match")
    assert manager.locator(".plugin-empty").is_visible()
    manager.locator("#pluginSearch").fill("")
    assert manager.locator(".plugin-item").count()>0
    # Populate a realistic catalog using the same native readback fixture.
    emit(page,"arMixCatalog",{"items":[{"id":f"fixture-{i}","name":f"Fixture VST {i:03d}","vendor":"Fixture Vendor","category":"Utility","format":"VST3"} for i in range(258)],"builtIns":[{"id":"armix.builtin.fixture.v1","name":"Fixture processor","category":"Utility"}]})
    page.wait_for_function("()=>document.querySelectorAll('#pluginResults .plugin-item').length>=258")
    manager.screenshot(path=f"v6-plugin-manager-detail-{width}.png")
    page.screenshot(path=f"v6-plugin-manager-{width}.png")
    geometry=manager.evaluate("""el=>{
      const rect=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height,right:r.right,bottom:r.bottom}};
      const side=el.querySelector('.plugin-sidebar'),cat=el.querySelector('.plugin-catalog'),list=el.querySelector('.plugin-table-scroll');
      const row=list.querySelector('.plugin-item'),headers=el.querySelector('.plugin-headings');
      return {panel:rect(el),side:rect(side),cat:rect(cat),list:rect(list),row:rect(row),
        headers:rect(headers),name:rect(row.querySelector('.plugin-name')),vendor:rect(row.querySelector('.plugin-meta')),
        category:rect(row.querySelector('.plugin-category')),scroll:list.scrollHeight>list.clientHeight};
    }""")
    assert geometry["panel"]["w"]>=min(950,width-30),geometry
    assert geometry["panel"]["right"]<=width and geometry["panel"]["bottom"]<=height,geometry
    assert geometry["side"]["right"]<=geometry["cat"]["x"]+1,geometry
    assert geometry["row"]["h"]==27 and geometry["list"]["h"]>=300,geometry
    assert geometry["vendor"]["x"]>=geometry["name"]["right"]-1 and geometry["category"]["x"]>=geometry["vendor"]["right"]-1,geometry
    assert geometry["scroll"],geometry
    assert assert_column_alignment(manager)["columns"]==7
    manager.locator(".plugin-table-scroll").evaluate("e=>{e.scrollTop=400}")
    assert assert_column_alignment(manager)["columns"]==7
    manager.locator(".plugin-table-scroll").evaluate("e=>{e.scrollTop=0}")
    assert manager.locator("#pluginScan").is_visible() and manager.locator("#pluginImport").is_visible()
    assert manager.locator("#pluginAddPath").is_visible()
    assert manager.locator("#pluginSearch").is_visible()
    assert manager.locator("#pluginNewPath,#pluginPaths,.plugin-disclaimer").count()==0
    assert manager.locator("#pluginNativePaths").count()==1
    emit(page,"arMixCatalog",{"items":[],"scanPaths":[{"path":"C:/System/VST3","custom":False},{"path":"D:/Custom VST3","custom":True}]})
    assert "Static browser preview" not in manager.inner_text()
    assert "Click a plugin to insert" not in manager.inner_text()
    assert_manager_controls(manager)
    # Actual native capture handlers still issue the same three actions.
    for control,action in (("pluginImport","importVst3"),("pluginScan","scanVst3"),("pluginAddPath","addScanFolder")):
        before=page.evaluate("(a)=>window.__events.filter(e=>e.name==='arMixAction'&&e.payload.action===a).length",action)
        manager.locator("#"+control).click()
        after=page.evaluate("(a)=>window.__events.filter(e=>e.name==='arMixAction'&&e.payload.action===a).length",action)
        assert after==before+1,(action,before,after)
    default_path = r"C:\Program Files\Common Files\VST3"
    custom_path = r"D:\Audio\VST3"
    folders = [{"path":default_path,"custom":False,"available":True},
               {"path":custom_path,"custom":True,"available":False}]
    emit(page,"arMixCatalog",{"items":[{"id":f"fixture-{i}","name":f"Fixture VST {i:03d}","vendor":"Fixture Vendor","category":"Utility","format":"VST3"} for i in range(258)],
                              "scanPaths":folders})
    folder_list=manager.get_by_role("list",name="VST3 scan folders")
    assert folder_list.get_by_role("listitem").count()==2
    expect(folder_list.get_by_role("listitem").nth(0)).to_contain_text("System")
    expect(folder_list.get_by_role("listitem").nth(1)).to_contain_text("Custom · Unavailable")
    remove=folder_list.get_by_role("button",name=f"Remove scan folder {custom_path}")
    assert folder_list.get_by_role("button").count()==1
    remove.click()
    confirmation=page.get_by_role("dialog",name="Remove scan folder")
    confirmation.get_by_role("button",name="Cancel").click()
    assert not page.evaluate("()=>window.__events.some(e=>e.payload?.action==='removeScanFolder')")
    assert manager.is_visible() and remove.is_visible()
    remove.click()
    confirmation.get_by_role("button",name="Remove folder",exact=True).click()
    assert page.evaluate("()=>window.__events.filter(e=>e.payload?.action==='removeScanFolder').at(-1).payload.path")==custom_path
    assert folder_list.get_by_role("listitem").count()==2,"wait for native folder readback"
    emit(page,"arMixCatalog",{"items":[{"id":f"fixture-{i}","name":f"Fixture VST {i:03d}","vendor":"Fixture Vendor","category":"Utility","format":"VST3"} for i in range(258)],
                              "scanPaths":folders[:1],"scanning":True})
    assert folder_list.get_by_role("listitem").count()==1
    assert manager.locator("#pluginScan").is_disabled()
    emit(page,"arMixScanProgress",{"reported":True,"active":True,"done":2,"total":5})
    expect(manager.locator("#pluginScanStatus")).to_contain_text("2/5")
    catalog=page.evaluate("()=>({items: Array.from({length:258},(_,i)=>({id:'fixture-'+i,name:'Fixture VST '+String(i).padStart(3,'0'),vendor:'Fixture Vendor',category:'Utility',format:'VST3'}))})")
    emit(page,"arMixCatalog",{**catalog,"scanPaths":folders[:1],"scanning":False})
    assert manager.locator("#pluginScan").is_enabled()
    emit(page,"arMixScanProgress",{"reported":True,"active":False,"found":5,"issues":True})
    expect(manager.locator("#pluginScanStatus")).to_contain_text("Scan finished · 5 found")
    expect(manager.locator("#pluginScanStatus")).to_contain_text("Diagnostics")
    assert "JUCE" not in manager.locator("#pluginScanStatus").inner_text()
    manager.locator("#pluginSearch").fill("Fixture VST 257")
    assert manager.locator(".plugin-item").count()==1
    assert manager.locator(".plugin-item .plugin-meta").inner_text()=="Fixture Vendor"
    assert not assert_column_alignment(manager)["scroll"]
    manager.locator("#pluginSearch").fill("")
    assert manager.locator(".plugin-item").count()>=258
    assert assert_column_alignment(manager)["columns"]==7
    # V6 faces and selectors must be real rendered controls, never emoji.
    page.mouse.move(0,0)
    controls=manager.evaluate("""el=>{
      const props=['height','borderRadius','backgroundColor','borderTopColor','color','fontSize','fontWeight','boxShadow'];
      const read=e=>Object.fromEntries(props.map(k=>[k,getComputedStyle(e)[k]]));
      const add=document.querySelector('#addTrack'),bank=document.querySelector('#bankSelect');
      const buttons=['pluginImport','pluginScan','pluginAddPath'].map(id=>read(document.getElementById(id)));
      const filters=['pluginCategory','pluginView'].map(id=>read(document.getElementById(id)));
      const icons=[...el.querySelectorAll('.plugin-heading-icon'),...el.querySelector('.plugin-item').querySelectorAll('.plugin-flag')].map(e=>{
        const r=e.getBoundingClientRect(),p=getComputedStyle(e,'::before');
        return {w:r.width,h:r.height,iw:p.width,ih:p.height,mask:p.maskImage,text:e.textContent.trim()};
      });
      return {buttons,filters,icons,add:add?read(add):null,bank:bank?read(bank):null};
    }""")
    for face in controls["buttons"]:
        assert face["height"]=="34px" and face["borderRadius"]=="5px",face
        assert face["backgroundColor"]=="rgb(40, 70, 83)",face
        assert face["borderTopColor"]=="rgb(86, 119, 136)",face
    for face in controls["filters"]:
        assert face["height"]=="34px" and face["borderRadius"]=="5px",face
        assert face["backgroundColor"]=="rgb(13, 32, 42)",face
    for icon in controls["icons"]:
        assert icon["w"]==24 and icon["h"]==24 and icon["iw"]=="16px" and icon["ih"]=="16px",icon
        assert icon["mask"]!="none" and icon["text"]=="",icon
    assert controls["icons"][0]["mask"]==controls["icons"][2]["mask"]
    assert controls["icons"][1]["mask"]==controls["icons"][3]["mask"]
    manager.locator("#pluginSearch").fill("Fixture VST 257")
    favorite=manager.locator('[data-plugin-action="fav"]')
    hidden=manager.locator('[data-plugin-action="hide"]')
    assert favorite.get_attribute("aria-pressed")=="false"
    favorite.click()
    assert favorite.get_attribute("aria-pressed")=="true"
    assert favorite.evaluate("e=>getComputedStyle(e,'::before').maskImage")!=controls["icons"][0]["mask"]
    manager.locator("#pluginView").select_option("favorites")
    assert manager.locator(".plugin-item").count()==1
    favorite.click()
    assert manager.locator(".plugin-empty").is_visible()
    manager.locator("#pluginView").select_option("all")
    hidden.click()
    assert manager.locator(".plugin-empty").is_visible()
    manager.locator("#pluginView").select_option("hidden")
    assert hidden.get_attribute("aria-pressed")=="true"
    hidden.click()
    assert manager.locator(".plugin-empty").is_visible()
    manager.locator("#pluginView").select_option("all")
    manager.locator("#pluginSearch").fill("")
    assert assert_column_alignment(manager)["columns"]==7
    print("PASS V6 controls, shared 16px Lucide masks, favorite/hide/filter round-trip",width,flush=True)
    close=manager.locator("#pluginClose")
    close.screenshot(path=f"v6-plugin-manager-close-{width}.png")
    actual_close=close_visual_states(page,close)
    assert actual_close==close_reference,{"manager":actual_close,"builtInEq":close_reference}
    print("PASS all 7 column coordinates <=0.5px (scroll, filter) and exact built-in EQ Close normal/hover/active",width,flush=True)
    print("PASS professional plugin workspace density and catalog geometry",width,geometry,flush=True)
    page.screenshot(path=f"v6-plugin-manager-{width}.png")
    # Preserve responsive layout after a real viewport resize, not just a CSS query.
    page.set_viewport_size({"width":720,"height":768})
    page.wait_for_function("""()=>{const r=document.querySelector("#pluginBrowser").getBoundingClientRect();return r.left>=0&&r.top>=0&&r.right<=innerWidth+.5&&r.bottom<=innerHeight+.5}""")
    assert assert_column_alignment(manager)["columns"]==6
    assert_manager_controls(manager,same_row=False)
    page.screenshot(path=f"v6-plugin-manager-narrow-{width}.png")
    manager.locator("#pluginClose").click()
    assert manager.is_hidden()
    assert not errors,errors
    page.close()

def main():
    spec=importlib.util.spec_from_file_location("native_bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="v6-operator-") as d:
        out=Path(d);bundle.stage(out)
        class Handler(SimpleHTTPRequestHandler):
            def log_message(self,*args):pass
        server=ThreadingHTTPServer(("127.0.0.1",0),partial(Handler,directory=str(out)))
        Thread(target=server.serve_forever,daemon=True).start()
        try:
            with sync_playwright() as pw:
                browser=pw.chromium.launch(headless=True)
                try:
                    reference=reference_close(browser,f"http://127.0.0.1:{server.server_address[1]}/fx-window.html")
                    exercise(browser,out,1366,900,"PRE_FADER",reference)
                    exercise(browser,out,1024,768,"POST_FADER",reference)
                finally: browser.close()
        finally:
            server.shutdown()
            server.server_close()
    print("PASS V6 direct PRE/POST, no speculative route or duplicate pending intent, manager controls (two viewports)")

if __name__=="__main__":main()
