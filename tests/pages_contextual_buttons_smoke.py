#!/usr/bin/env python3
"""V6 contextual button integration: page parity, PNG isolation, stable geometry,
momentary action + persistent app state, and pointer-focus scroll stability.
No mocking mixer state, no alternate UI renderer and no CSS screenshot assets.
"""
from pathlib import Path
import subprocess,sys,tempfile
from playwright.sync_api import sync_playwright
from approved_publication_fixture import published_console

ROOT=Path(__file__).resolve().parents[1]
OWNER='mixer-contextual-buttons.css'
SCRIPT='mixer-contextual-buttons.js'
CANON=ROOT/'site/preview-v2/index.html'


def css_for(el):
    return el.evaluate("""e=>{
      const s=getComputedStyle(e),r=e.getBoundingClientRect();
      return {background:s.backgroundImage, shadow:s.boxShadow,transform:s.transform,
        w:r.width,h:r.height,x:r.x,y:r.y,color:s.color,
        focus:document.activeElement===e,pressed:e.getAttribute('aria-pressed')};
    }""")


def check_page(browser,path,label):
    page=browser.new_page(viewport={'width':1440,'height':900},device_scale_factor=1)
    errors=[]
    page.on('pageerror',lambda error:errors.append(str(error)))
    page.goto(path.as_uri())
    page.wait_for_selector('#tracks .strip[data-id="1"] button[data-hw-kind="mute"]')
    # Explicitly protect the existing PNG MUTE/SOLO/PFL component: it must
    # never inherit a V6 alternate metallic key skin from this PR.
    legacy=page.locator('#tracks .strip[data-id="1"] button[data-hw-kind="mute"]')
    assert not legacy.evaluate("""e=>{
      return e.matches('.command-bar button.tool,.console-body .strip button.mx-util-key');
    }"""),label
    # Compare the non-audio action at rest, held down and released.
    action=page.locator('#clearCue')
    assert action.is_visible(),(label,'clearCue hidden')
    before=css_for(action)
    assert 'gradient' in before['background'],(label,before)
    position=page.evaluate("""()=>({
       scroll:window.scrollY,
       doc:document.documentElement.scrollTop,
       mix:document.querySelector('.tracks-viewport')?.scrollLeft||0
    })""")
    bounds=action.bounding_box()
    page.mouse.move(bounds['x']+bounds['width']/2,bounds['y']+bounds['height']/2)
    page.mouse.down()
    during=css_for(action)
    assert during['background']!=before['background'],(label,'momentary press invisible',during)
    assert during['transform']=='none',(label,'button moved on press',during)
    assert (during['w'],during['h'],during['x'],during['y'])==(
            before['w'],before['h'],before['x'],before['y']),(
                label,'pressed button reflowed')
    page.mouse.up()
    released=css_for(action)
    assert released['background']==before['background'],(
        label,'action remained ON after mouse release',before,released)
    assert not released['focus'],(label,'mouse focus-scroll was not prevented')
    assert page.evaluate("""()=>({
       scroll:window.scrollY,doc:document.documentElement.scrollTop,
       mix:document.querySelector('.tracks-viewport')?.scrollLeft||0
    })""")==position,(label,'viewport shifted after clicking action')
    # Real application latch with its REQUIRED WIRE source context; do not
    # weaken toggleDirectMain() or fake its aria-pressed state in the test.
    body=page.locator('.console-body')
    if 'collapsed-send' in (body.get_attribute('class') or ''):
        page.locator('.section-rail [data-section="send"]').click()
    source=page.locator('#tracks .strip[data-id="1"] .wire-node-out')
    source.click()
    # V50: MAIN is located inside modeless WIRE Configuration.
    page.locator('#wireConfigToggle').click()
    assert page.locator('#wireConfigWindow').is_visible(),(label,'WIRE configuration did not open')
    master=page.locator('#wireConfigWindow #wireMaster')
    on=css_for(master)
    assert on['pressed']=='true',(label,on)
    master.click()
    off=css_for(master)
    assert off['pressed']=='false',(label,off)
    assert on['background']!=off['background'],(label,'main latch material did not change')
    assert (on['x'],on['y'],on['w'],on['h'])==(
             off['x'],off['y'],off['w'],off['h']),(
                 label,'MAIN ON to OFF changed button geometry')
    master.click()
    back=css_for(master)
    assert back['pressed']=='true' and back['background']==on['background'],(label,back)
    assert (back['w'],back['h'])==(on['w'],on['h']),label
    # Micro controls are rendered dynamically; the cap MUST fit their old
    # 15-17px geometry and latch only through their own aria-pressed.
    micro=page.locator('#tracks .strip[data-id="1"] button.mx-util-key[data-util-kind="format"]')
    micro.wait_for()
    m=css_for(micro)
    assert m['h']<=18 and m['w']<=36,(label,'micro grew into full-size key',m)
    # Physical routing source and destination must remain outside style owner.
    assert page.locator('#tracks .strip[data-id="1"] .wire-node-out').count()==1
    assert page.locator('#tracks .strip[data-id="1"] .wire-node-in').count()==1
    assert not errors,(label,'page errors',errors)
    if label=='png':
        page.screenshot(path='pages-v6-contextual-buttons-1440.png')
    page.close()


def main():
    css=(ROOT/'site/preview-v2/assets'/OWNER).read_text('utf-8')
    js=(ROOT/'site/preview-v2/assets'/SCRIPT).read_text('utf-8')
    assert '.mx-hw-key' not in css.replace('(.mx-hw-key)',''), 'MUTE/SOLO/PFL owner was targeted'
    assert 'transform:none!important' in css and 'repeating-linear-gradient' in css
    assert "event.preventDefault()" in js and "event.button!==0" in js
    assert OWNER in CANON.read_text('utf-8') and SCRIPT in CANON.read_text('utf-8')
    with tempfile.TemporaryDirectory(prefix='arsonmix-context-v6-') as folder:
        dist=Path(folder)/'dist'
        subprocess.run([sys.executable,str(ROOT/'scripts/build_pages.py'),
                        '--out',str(dist),'--sha','v6-ci','--branch','main'],
                       cwd=ROOT,check=True,capture_output=True,text=True)
        published=published_console(dist)
        assets=published.parent/'assets'
        assert (assets/OWNER).read_bytes()==(ROOT/'site/preview-v2/assets'/OWNER).read_bytes()
        assert (assets/SCRIPT).read_bytes()==(ROOT/'site/preview-v2/assets'/SCRIPT).read_bytes()
        png_src=ROOT/'site/preview-buttons/assets'
        png_pub=assets
        for file in ('mixer-control-keys.css','mixer-control-keys.js','harmonice-thin-flat-v9-approved-off-shadow-57x30.png'):
            assert (png_pub/file).read_bytes()==(png_src/file).read_bytes(),file
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True)
            for file,label in (
                (CANON,'canonical'),
                (published,'png')
            ):
                check_page(browser,file,label)
            browser.close()
    print('PASS V6 OTHER buttons: authoring source + sole approved /preview/, PNG protected, action release, native latches,')
    print('micro geometry, fixed MAIN size, no pointer-focus or layout scroll')


if __name__=='__main__':
    main()
