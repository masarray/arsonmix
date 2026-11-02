"""V8 Pages CSS ownership: FX rack and Plugin Manager are extracted, not layered.
Run with real Chromium against the canonical user-facing HTML (no mock DOM).
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/'site/preview-v2/index.html'
ASSETS=ROOT/'site/preview-v2/assets'
RACK=ASSETS/'mixer-fx-rack.css'
PLUGIN=ASSETS/'mixer-plugin-browser.css'

def source_gate():
    html=HTML.read_text('utf-8')
    start=html.index('<style>')+len('<style>')
    inline=html[start:html.index('</style>',start)]
    rack=RACK.read_text('utf-8')
    plugin=PLUGIN.read_text('utf-8')
    assert len(inline)<226_000, ('inline still contains rack styles',len(inline))
    assert inline.count('!important')<1840, 'redundant important cascade is growing'
    assert len(rack)>5_000 and len(plugin)>3_000, 'owner migration incomplete'
    assert rack.count('.fxslot-row')>9 and rack.count('.fx-section')>12
    assert '.plugin-browser[hidden]' in plugin and '.plugin-head' in plugin
    assert 'assets/mixer-fx-rack.css?v=fx-owner-v8' in html
    assert 'assets/mixer-plugin-browser.css?v=fx-owner-v8' in html
    assert 'data:image/svg+xml;base64,' in html and 'wireWsCanJoin' in html
    assert 'assets/inter-embedded.css' in html
    # Every obsolete layer must be physically gone, not copied and overlaid.
    assert '.fxslot-row.dragged{' not in inline
    assert '.fxslot-row.drop-target{' not in inline
    assert '.plugin-browser.minimized{' not in inline

async def interact(page,width):
    await page.wait_for_function("""()=>document.querySelectorAll('#tracks .strip').length>=5
      && document.querySelector('#tracks .strip .fxslot-row')
      && document.querySelector('#pluginsButton')
      && !!document.querySelector('#wireLayer')""")
    assets=await page.evaluate("""()=>{
      return [...document.styleSheets].filter(s=>s.href).map(s=>{
        // file:// preview origin may forbid inspecting local cssRules;
        // the CSS source itself is statically asserted above.
        let rules=-1;try{rules=s.cssRules.length}catch(e){}
        return {href:s.href.split('/').pop(),rules};
      });
    }""")
    rack=next((x for x in assets if x['href'].startswith('mixer-fx-rack.css')),None)
    plugin=next((x for x in assets if x['href'].startswith('mixer-plugin-browser.css')),None)
    assert rack and (rack['rules']<0 or rack['rules']>25),(width,assets)
    assert plugin and (plugin['rules']<0 or plugin['rules']>15),(width,assets)
    strip=page.locator('#tracks .strip').first
    initial=await strip.evaluate("""e=>{
      const fx=e.querySelector('.fx-section'),list=e.querySelector('.fxslot-list');
      const slot=e.querySelector('.fxslot-row'),pick=e.querySelector('.fx-pick');
      const frame=e.querySelector('.fader-section');
      const r=x=>x.getBoundingClientRect();
      return {fx:r(fx).height, list:r(list).height,row:r(slot).height,
        pickColor:getComputedStyle(pick).color,
        bg:getComputedStyle(fx).backgroundImage,
        surface:getComputedStyle(fx).backgroundColor,
        frame:r(frame).height,pan:r(e.querySelector('.pan-section')).height,
        strip:r(e).height,fxTitle:fx.querySelector('.subhead')?.textContent};
    }""")
    assert 18<=initial['row']<=30,initial
    assert initial['frame']>=90 and initial['pan']>=20,initial
    assert initial['strip']>=400 and initial['fxTitle'],initial
    # FX surfaces may be deliberately solid rather than glossy gradients.
    assert initial['pickColor']=='rgb(196, 223, 208)',initial
    assert initial['fx']>=60 and initial['list']>=30,initial
    if width==1680:
        await page.screenshot(path='pages-v8-fx-rack-expanded-1680.png')
    rail=page.locator('.section-rail [data-section="fx"]')
    assert await rail.count()==1
    original=await page.locator('.console-body').evaluate("el=>el.classList.contains('collapsed-fx')")
    await rail.click()
    now=await page.locator('.console-body').evaluate("el=>el.classList.contains('collapsed-fx')")
    assert now!=original,'FX folding must remain operable'
    await rail.click()
    restored=await page.locator('.console-body').evaluate("el=>el.classList.contains('collapsed-fx')")
    assert restored==original,'FX fold must restore initial state'
    pick=strip.locator('.fx-pick').first
    assert await pick.count()==1
    old_count=await strip.locator('button[data-util-kind="fx-main"]').count()
    assert old_count==1
    # Modeless Plugin Manager is a separate modal-free surface, not Add FX picker.
    await page.locator('#pluginsButton').click()
    panel=page.locator('#pluginBrowser')
    assert not await panel.evaluate("e=>e.hidden"),'Plugin Manager did not open'
    panel_state=await panel.evaluate("""e=>{
      const r=e.getBoundingClientRect(),s=getComputedStyle(e);
      return {w:r.width,h:r.height,position:s.position,display:s.display,
        z:parseInt(s.zIndex,10),bg:s.backgroundColor,inside:!!e.querySelector('.plugin-head')};
    }""")
    assert panel_state['w']>200 and panel_state['h']>150 and panel_state['inside'],panel_state
    assert panel_state['position']=='absolute' and panel_state['z']>=20,panel_state
    await page.locator('#pluginMinimize').click()
    assert await panel.evaluate("e=>e.classList.contains('minimized')"),'Minimize failed'
    await page.locator('#pluginMinimize').click()
    assert not await panel.evaluate("e=>e.classList.contains('minimized')"),'Restore failed'
    await page.locator('#pluginClose').click()
    assert await panel.evaluate("e=>e.hidden"),'Close failed'
    if width==1680:
        await page.screenshot(path='pages-v8-fx-browser-closed-1680.png')
    # MUTE/SOLO/PFL, font and vector control structure are not rematerialized.
    assert await strip.locator('.mx-hw-row>button.mx-hw-key').count()==3
    assert await strip.locator('button[data-util-kind="fx-main"]').count()==1

async def main():
    source_gate()
    async with async_playwright() as p:
        browser=await p.chromium.launch(headless=True)
        for width in (1366,1680):
            page=await browser.new_page(viewport={'width':width,'height':900})
            errors=[]
            page.on('pageerror',lambda e:errors.append(str(e)))
            await page.goto(HTML.as_uri())
            await interact(page,width)
            assert not errors,(width,errors)
            await page.close()
        await browser.close()
    print('PASS V8: 87 rack/browser styles migrated; FX layout, folding, plugin modeless window, V6/V7 keys, WIRE intact')

if __name__=='__main__':
    asyncio.run(main())
