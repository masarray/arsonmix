"""Pages V5 backlit controls regression: LIVE /preview-v2/ source, not stale GUI PR preview.
No audio backend here. Preserve V36 WIRE route code and independent MUTE/SOLO/PFL.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/'site/preview-v2/index.html'

async def main():
    source=HTML.read_text(encoding='utf-8')
    assert 'wireWsCanJoin' in source, 'WIRE V36 source missing'
    assert 'codepen-buttons-wrapper' in source
    assert 'ArSonMix V5 backlit hardware material' in source
    async with async_playwright() as p:
        browser=await p.chromium.launch(headless=True)
        page=await browser.new_page(viewport={'width':1680,'height':900})
        errors=[]
        page.on('pageerror',lambda e: errors.append(str(e)))
        await page.goto(HTML.as_uri())
        await page.wait_for_function("""()=>window.ArSonMixHardwareKeys &&
          document.querySelectorAll('.strip .switch-section .mx-hw-key').length>=3""")
        first=page.locator('.strip[data-id]').first
        buttons=first.locator('.switch-section button')
        assert await buttons.count()==3
        labels=await buttons.evaluate_all("""nodes=>nodes.map(b=>({
          label:b.querySelector('svg.mx-hw-face mask text')?.textContent,
          color:getComputedStyle(b).getPropertyValue('--mx-hw-led').trim(),
          maskCount:b.querySelectorAll('svg.mx-hw-face mask text').length,
          overlays:b.querySelectorAll('svg.mx-hw-face > text').length,
          old:!b.classList.contains('mx-hw-key'),
          height:b.getBoundingClientRect().height,
          width:b.getBoundingClientRect().width,
          left:b.getBoundingClientRect().left,
          right:b.getBoundingClientRect().right,
          shine:b.querySelector('linearGradient[id^=mx-hw-shine]')!==null,
          glow:getComputedStyle(b,'::before').opacity,
          lamp:b.querySelector('.mx-hw-lamp')?.getAttribute('fill')
        }))""")
        assert [x['label'] for x in labels]==['MUTE','SOLO','PFL'],labels
        assert [x['color'] for x in labels]==['255,64,85','255,208,44','90,200,231'],labels
        assert all(x['maskCount']==1 and x['overlays']==0 and not x['old'] and x['shine'] for x in labels),labels
        assert all(abs(x['height']-29)<1 for x in labels),labels
        assert all(x['glow']=='0' for x in labels),labels
        assert all(x['right']<=labels[i+1]['left']+.3 for i,x in enumerate(labels[:-1])),labels
        assert all(x['width']>16 for x in labels),labels
        await page.screenshot(path='pages-wire-v5-backlit-OFF-1680.png')
        mute=first.locator('button[data-action="mute"]')
        solo=first.locator('button[data-action="solo"]')
        cue=first.locator('button[data-action="cue"]')
        await mute.click()
        await solo.click()
        await cue.click()
        await page.wait_for_function("""()=>[...document.querySelectorAll('.strip[data-id]')[0]
          .querySelectorAll('.switch-section button')].every(b=>b.getAttribute('aria-pressed')==='true' &&
          !!b.querySelector('svg.mx-hw-face'))""")
        assert await first.locator('button[data-action="mute"]').get_attribute('aria-pressed')=='true'
        assert await first.locator('button[data-action="solo"]').get_attribute('aria-pressed')=='true'
        assert await first.locator('button[data-action="cue"]').get_attribute('aria-pressed')=='true'
        on=await first.locator('.switch-section button').evaluate_all("""nodes=>nodes.map(b=>({
            glow:getComputedStyle(b,'::before').opacity,
            lamp:getComputedStyle(b.querySelector('.mx-hw-lamp')).opacity,
            fill:getComputedStyle(b.querySelector('linearGradient[id^="mx-hw-led"] stop')).stopColor
        }))""")
        assert all(x['glow']=='1' and x['lamp']=='1' for x in on),on
        assert [x['fill'] for x in on]==['rgb(255, 64, 85)','rgb(255, 208, 44)','rgb(90, 200, 231)'],on
        await page.screenshot(path='pages-wire-v5-backlit-ON-1680.png')
        # Functional toggle OFF: no mutual exclusion and no DOM-text layer duplication.
        await first.locator('button[data-action="mute"]').click()
        assert await first.locator('button[data-action="mute"]').get_attribute('aria-pressed')=='false'
        assert await first.locator('button[data-action="solo"]').get_attribute('aria-pressed')=='true'
        assert await first.locator('button[data-action="cue"]').get_attribute('aria-pressed')=='true'
        assert not errors,errors
        await browser.close()
    print('PASS: V36 WIRE source intact; V5 single masks, semantic LEDs, 3 equal-height buttons, gap-only glow, independent click states')

if __name__=='__main__':
    asyncio.run(main())
