"""V9 CSS ownership and physical geometry, exercised in real Chromium."""
import asyncio
import re
from pathlib import Path
from playwright.async_api import async_playwright

ROOT = Path(__file__).resolve().parents[1]
HTML = ROOT / 'site/preview-v2/index.html'
OWNERS = [('mixer-pan-controls.css', 30, '.center-pan'),
          ('mixer-fader-assembly.css', 100, '.fader-travel'),
          ('mixer-meter-display.css', 45, '.meter-box')]

def source_gate():
    html = HTML.read_text('utf-8')
    inline = html.split('<style>', 1)[1].split('</style>', 1)[0]
    assert len(inline) < 191_000, ('owner styles still inline', len(inline))
    assert inline.count('!important') < 1500, 'inline specificity growing'
    for name, rules, selector in OWNERS:
        css = (HTML.parent / 'assets' / name).read_text('utf-8')
        assert css.count('{') >= rules and selector in css, name
        assert html.count('assets/' + name + '?v=css-owner-v9') == 1, name
    assert '.console-body .fader-travel .cap{' not in inline
    assert '.pan-control.is-centered::after{' not in inline
    pan_css=(HTML.parent/'assets/mixer-pan-controls.css').read_text('utf-8')
    assert not any('background:' in rule for rule in re.findall(r'\.pan-section\{([^}]*)\}', pan_css)), 'PAN face material must remain owned by strip layout'
    assert 'wireWsCanJoin' in html and 'assets/inter-embedded.css' in html

async def inspect(page, width, height):
    await page.goto(HTML.as_uri())
    await page.wait_for_function('''() => document.querySelectorAll('#tracks .strip').length>=5 &&
        !!document.querySelector('#master .strip') && !!document.querySelector('#wireLayer')''')
    metrics = await page.evaluate('''() => {
       const r=e=>{const x=e.getBoundingClientRect();return {y:x.y,h:x.height,w:x.width}};
       const rail=document.querySelector('.section-rail');
       const strips=[...document.querySelectorAll('#tracks .strip')];
       const out=[];
       for(const strip of [strips[0],strips[strips.length-1],document.querySelector('#master .strip')]){
           const pan=strip.querySelector('.pan-section'), slider=strip.querySelector('.center-pan');
           const fader=strip.querySelector('.fader-section'), input=strip.querySelector('input.fader');
           const cap=strip.querySelector('.fader-travel .cap'),meter=strip.querySelector('.meter-box');
           const vu=strip.querySelector('.meter-box .vu-bars');
           if(!pan||!slider||!fader||!input||!cap||!meter||!vu)return {error:'missing component'};
           out.push({pan:r(pan),slider:r(slider),fader:r(fader),input:r(input),cap:r(cap),
              meter:r(meter),vu:getComputedStyle(vu).display,
              railPan:r(rail.querySelector('.rail-cell.pan')),
              railFader:r(rail.querySelector('.rail-cell.fader'))});
       }
       return {out, links:[...document.styleSheets].filter(x=>x.href).map(x=>x.href)};
    }''')
    assert not metrics.get('error'), (width, metrics)
    for name,_,_ in OWNERS:
        assert any('/'+name+'?' in link for link in metrics['links']), (width, name)
    for m in metrics['out']:
        assert m['pan']['h'] >= 20 and m['fader']['h'] >= 80, (width, 'geometry', m)
        assert 21 <= m['cap']['w'] <= 25 and 45 <= m['cap']['h'] <= 50, (width, 'fader cap', m)
        assert m['meter']['h'] > 70 and m['vu'] != 'none', (width, 'meter', m)
        assert abs(m['pan']['y']-m['railPan']['y']) < 1.6, (width, 'PAN rail', m)
        assert abs(m['fader']['y']-m['railFader']['y']) < 1.6, (width, 'fader rail', m)
        assert m['slider']['w'] > 5 and m['input']['h'] > 30, (width, 'hitbox', m)
    pan = page.locator('#tracks .strip .center-pan').first
    await pan.evaluate("el=>{el.value='0';el.dispatchEvent(new Event('change',{bubbles:true}))}")
    await pan.focus()
    await pan.press('ArrowRight')
    assert await pan.input_value() == '1', (width, 'PAN keyboard')
    if width in (1366,1680):
        await page.screenshot(path=f'pages-v9-pan-fader-meter-{width}.png')

async def main():
    source_gate()
    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page = await browser.new_page(viewport={'width':width,'height':height})
            errors=[]
            page.on('pageerror', lambda exc:errors.append(str(exc)))
            await inspect(page,width,height)
            assert not errors, (width,errors)
            await page.close()
        await browser.close()
    print('PASS V9 component styles, geometry, and PAN keyboard')

if __name__=='__main__':
    asyncio.run(main())
