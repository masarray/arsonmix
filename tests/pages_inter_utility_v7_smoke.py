"""V7 ArSonMix Pages: one embedded Inter and semantic utility controls.
Validates real published source, SVG labels and audio-free GUI actions.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
PAGE=ROOT/'site/preview-v2/index.html'
ASSET=ROOT/'site/preview-v2/assets'
ENGINE_FONT=ROOT/'web/styles-src/00-font-and-assets.css'

def source_contract():
    html=PAGE.read_text(encoding='utf-8')
    inter=(ASSET/'inter-embedded.css').read_text(encoding='utf-8')
    utility=(ASSET/'mixer-utility-controls.css').read_text(encoding='utf-8')
    canonical=ENGINE_FONT.read_text(encoding='utf-8')
    face=canonical[canonical.index('@font-face'):canonical.index('}',canonical.index('@font-face'))+1]
    assert face in inter,'Pages must use the exact embedded Inter face from native GUI'
    assert inter.count('@font-face')==1 and inter.count('data:font/woff2;base64,')==1
    for asset in ('inter-embedded.css','mixer-utility-controls.css'):
        assert 'assets/'+asset in html
    import re
    assert 'https://fonts.googleapis.com' not in html
    assert not re.search(r'(?m)^\\s*@import\\b',inter), 'Font must not import external CSS'
    for kind in ('fx-main','fx-slot','format','phase','fx-remove'):
        assert f'data-util-kind="{kind}"' in html
    assert 'class="mini power ' not in html and 'class="mini phase ' not in html
    assert 'class="mini format-badge"' not in html and 'class="fx-remove"' not in html
    assert 'filter:blur' not in utility.replace(' ','').lower()
    assert ':hover' not in utility
    assert utility.count('!important')==0,'Do not add new utility CSS cascade overrides'
    assert 'wireWsCanJoin' in html and 'data:image/svg+xml;base64,' in html

async def visual(page,width):
    await page.wait_for_function("""()=>{
      const b=document.querySelector('#tracks .strip[data-id="1"] button[data-util-kind="fx-main"]');
      return b && !!document.querySelector('button[data-hw-kind="mute"] svg.mx-hw-face');
    }""")
    loaded=await page.evaluate("""async()=>{
      await document.fonts.load('500 11px "ArSonMix Inter"');
      return document.fonts.check('500 11px "ArSonMix Inter"');
    }""")
    assert loaded,'Embedded Inter font did not load from local CSS'
    typography=await page.evaluate("""()=>{
      const selectors=['html','body','.window-title','.tool','.strip .identity-name',
        '.strip .fx-pick','.strip .route-select','.strip .fader-readout',
        '.strip .mx-hw-face mask text','.strip button[data-util-kind="phase"]',
        '.strip button[data-util-kind="format"]','.strip button[data-util-kind="fx-main"]'];
      return selectors.map(s=>{
        const el=document.querySelector(s);
        return {selector:s,exists:!!el,font:el?getComputedStyle(el).fontFamily:null};
      });
    }""")
    assert all(x['exists'] and x['font'].startswith('"ArSonMix Inter"') for x in typography),typography
    row=page.locator('#tracks .strip[data-id="1"]')
    selectors=[
      ('fx-main', '[data-util-kind="fx-main"]',17),
      ('fx-slot', '[data-util-kind="fx-slot"]',17),
      ('phase','[data-util-kind="phase"]',15),
      ('format','[data-util-kind="format"]',15),
      ('fx-remove','[data-util-kind="fx-remove"]',16)
    ]
    evidence={}
    for kind,selector,h in selectors:
      loc=row.locator(selector).first
      assert await loc.count()==1,(width,kind)
      state=await loc.evaluate("""b=>{
        const s=getComputedStyle(b),rect=b.getBoundingClientRect();
        return {w:rect.width,h:rect.height,font:s.fontFamily,
          filter:s.filter,transition:s.transitionDuration,
          on:s.color,tag:b.tagName,pressed:b.getAttribute('aria-pressed'),
          svg:b.querySelectorAll('svg.mi-svg path').length,
          hasOldMini:b.classList.contains('mini'),
          hasOwner:b.classList.contains('mx-util-key'),
          name:b.getAttribute('aria-label'),
          led:s.getPropertyValue('--mx-util-accent').trim()}
      }""")
      assert abs(state['h']-h)<1,(kind,state)
      assert state['w']>=12 and state['filter']=='none',(kind,state)
      assert state['transition']=='0s' and state['hasOwner'] and not state['hasOldMini'],(kind,state)
      assert state['font'].startswith('"ArSonMix Inter"'),(kind,state)
      assert state['name'],(kind,state)
      if kind.startswith('fx-') and kind!='fx-remove':assert state['svg']>=1,(kind,state)
      evidence[kind]=state
    assert evidence['fx-main']['led']=='114,226,181',evidence
    assert evidence['phase']['led']=='255,190,98',evidence
    assert evidence['format']['led']=='131,222,241',evidence
    assert evidence['fx-remove']['pressed'] is None
    # Semantic icon classes and actions are original, only material changed.
    phase=row.locator('[data-util-kind="phase"]')
    old=await phase.get_attribute('aria-pressed')
    await phase.click()
    await page.wait_for_function("""old=>{
      const e=document.querySelector('#tracks .strip[data-id="1"] button[data-util-kind="phase"]');
      return e&&e.getAttribute('aria-pressed')!==old;
    }""",arg=old)
    current=await row.locator('[data-util-kind="phase"]').get_attribute('aria-pressed')
    assert current!=old
    await row.locator('[data-util-kind="phase"]').click()
    assert await row.locator('[data-util-kind="phase"]').get_attribute('aria-pressed')==old
    format_button=row.locator('[data-util-kind="format"]')
    old=await format_button.inner_text()
    await format_button.click()
    assert await row.locator('[data-util-kind="format"]').inner_text()!=old
    await row.locator('[data-util-kind="format"]').click()
    assert await row.locator('[data-util-kind="format"]').inner_text()==old
    fx=row.locator('[data-util-kind="fx-main"]')
    before=await fx.get_attribute('aria-pressed')
    await fx.click()
    assert await row.locator('[data-util-kind="fx-main"]').get_attribute('aria-pressed')!=before
    await row.locator('[data-util-kind="fx-main"]').click()
    assert await row.locator('[data-util-kind="fx-main"]').get_attribute('aria-pressed')==before
    if width==1680:
      await page.screenshot(path='pages-v7-embedded-inter-utility-1680.png')
    # V6 keys survive and use the same embedded Inter without changing their switches.
    for k in ('mute','solo','cue'):
      key=row.locator(f'.mx-hw-row button[data-hw-kind="{k}"]')
      assert await key.count()==1
      assert await key.locator('svg.mx-hw-face mask text').count()==1

async def main():
    source_contract()
    async with async_playwright() as p:
      browser=await p.chromium.launch(headless=True)
      for width in (1366,1680):
        page=await browser.new_page(viewport={'width':width,'height':900})
        errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)))
        await page.goto(PAGE.as_uri())
        await visual(page,width)
        assert not errors,(width,errors)
        await page.close()
      await browser.close()
    print('PASS V7: offline embedded Inter, all GUI/SVG fonts, context-based FX/phase/format/remove keys, functional toggles, WIRE preserved')

if __name__=='__main__':
    asyncio.run(main())
