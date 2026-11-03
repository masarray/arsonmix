"""Approved MUTE/SOLO/PFL sprite, untouched legacy utility SVGs, real UI handlers.

Browser smoke covers the WebView2 source and generated offline standalone.
Pure UI validation: never claims actual PCM or hardware sound.
"""
import asyncio
import base64
import struct
from pathlib import Path
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
ATLAS=ROOT/'web/assets/buttons/mute-solo-pfl-56x37.png'


def check_assets():
    data=ATLAS.read_bytes()
    assert data[:8]==b'\x89PNG\r\n\x1a\n'
    assert struct.unpack('>II',data[16:24])==(112,111), "all six frames 56x37"
    assert data[25]==6, "sprite requires RGBA transparency"
    css=(ROOT/'web/styles-src/43-hardware-backlit.css').read_text()
    bundle=(ROOT/'web/styles.css').read_text()
    standalone=(ROOT/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text()
    encoded=base64.b64encode(data).decode()
    for name,source in [('source CSS',css),('generated CSS',bundle),('offline',standalone)]:
        assert ('data:image/png;base64,'+encoded) in source,name
    assert 'if(button.matches(SPRITE_CONTROLS))' in (ROOT/'web/hardware-keys.js').read_text()


async def audit(page):
    await page.wait_for_function("""() => !!window.ArSonMixHardwareKeys &&
      document.querySelector('[data-mute="0"]')?.classList.contains('mx-hw-sprite') &&
      !!document.querySelector('[data-pre="0"] svg.mx-hw-face')""")

    initial=await page.evaluate("""() => {
      const inspect=q=>{
        const b=document.querySelector(q),s=getComputedStyle(b),r=b.getBoundingClientRect();
        return {sprite:b.classList.contains('mx-hw-sprite'),
          legacy:b.classList.contains('btn-180'),
          svg:!!b.querySelector(':scope > svg.mx-hw-face'),
          bg:s.backgroundImage,pos:s.backgroundPosition,
          bgColor:s.backgroundColor,shadow:s.boxShadow,
          filter:s.filter,width:r.width,height:r.height,
          pressed:b.getAttribute('aria-pressed')};
      };
      return {mute:inspect('[data-mute="0"]'),
        solo:inspect('[data-solo="0"]'),master:inspect('#masterMute'),
        phase:inspect('[data-pre="0"]'),hpf:inspect('[data-hpf="0"]'),
        mode:inspect('#masterMode')};
    }""")
    for k in ('mute','solo','master'):
        v=initial[k]
        assert v['sprite'] and not v['svg'] and not v['legacy'],(k,v)
        assert v['bg'].startswith('url("data:image/png;base64,'),(k,v['bg'][:75])
        assert v['bgColor']=='rgba(0, 0, 0, 0)',(k,v)
        assert v['shadow']=='none' and v['filter']=='none',(k,v)
        assert 18<v['width']<=56 and 18<v['height']<=37,(k,v)
    for k in ('phase','hpf','mode'):
        v=initial[k]
        assert not v['sprite'] and v['svg'],(k,v)

    mute=page.locator('[data-mute="0"]')
    solo=page.locator('[data-solo="0"]')
    master=page.locator('#masterMute')
    before_pos=[initial['mute']['pos'],initial['solo']['pos']]
    await mute.click()
    await solo.click()
    assert await mute.get_attribute('aria-pressed')=='true'
    assert await solo.get_attribute('aria-pressed')=='true'
    on=await page.evaluate("""() => ['[data-mute="0"]','[data-solo="0"]']
      .map(q=>{const b=document.querySelector(q),s=getComputedStyle(b),r=b.getBoundingClientRect();
        return {pos:s.backgroundPosition,width:r.width,height:r.height}})""")
    assert all('100%' in e['pos'].split(' ')[0] for e in on),on
    assert all(a!=b['pos'] for a,b in zip(before_pos,on)),(before_pos,on)
    assert abs(on[0]['width']-initial['mute']['width'])<.1,on
    assert abs(on[1]['height']-initial['solo']['height'])<.1,on
    # Independent toggles, existing app JS continues to own behavior.
    await mute.click()
    assert await mute.get_attribute('aria-pressed')=='false'
    assert await solo.get_attribute('aria-pressed')=='true'
    await solo.click()

    await master.click()
    assert await master.get_attribute('aria-pressed')=='true'
    await master.click()

    # Non-sprite hardware keys still have dynamic masked SVG legends.
    mode=page.locator('#masterMode')
    await mode.click()
    await page.wait_for_function("""()=>document.querySelector('#masterMode')
      ?.querySelector('svg.mx-hw-face mask text')?.textContent==='MONO'""")
    await mode.click()
    await page.wait_for_function("""()=>document.querySelector('#masterMode')
      ?.querySelector('svg.mx-hw-face mask text')?.textContent==='STEREO'""")

    await page.evaluate('renderMixer()')
    await page.wait_for_function("""()=>[...document.querySelectorAll('#mixer .channel-actions button')]
      .every(b=>b.classList.contains('mx-hw-sprite') &&
        !b.querySelector('svg.mx-hw-face'))""")
    assert await page.locator('#mixer .channel-actions button').count()==22

    # Native owner uses the same PNG; topology/ChannelId and native ACK unchanged.
    await page.evaluate("""()=>{
      const node=document.createElement('section');
      node.className='arV6Console';node.id='native-key-fixture';
      node.innerHTML='<div class="arV6Top"><button class="arV6Button">DEVICE</button></div>'+
        '<div class="arV6Controls">'+
        '<button class="arV6Button" data-op="SET_MUTE" aria-pressed="false">M</button>'+
        '<button class="arV6Button" data-op="SET_SOLO" aria-pressed="true">S</button>'+
        '<button class="arV6Button" data-op="SET_CUE" aria-pressed="true">C</button>'+
        '</div>'+
        '<button data-op="SET_INSERT_BYPASS" aria-pressed="true">ON</button>'+
        '<button data-op="SET_SEND_TAP">PRE</button>';
      document.querySelector('#app').appendChild(node);
    }""")
    await page.wait_for_function("""()=>document.querySelectorAll('#native-key-fixture .mx-hw-sprite').length===3 &&
      document.querySelectorAll('#native-key-fixture svg.mx-hw-face').length===3""")
    native=await page.evaluate("""()=>[...document.querySelectorAll(
       '#native-key-fixture .arV6Controls button')].map(b=>({
       op:b.dataset.op,pos:getComputedStyle(b).backgroundPosition,
       aria:b.getAttribute('aria-label'),text:b.textContent,
       pressed:b.getAttribute('aria-pressed')}))""")
    assert [v['op'] for v in native]==['SET_MUTE','SET_SOLO','SET_CUE'],native
    assert '100%' in native[1]['pos'].split(' ')[0],native
    assert '100%' in native[2]['pos'].split(' ')[0] and '100%' in native[2]['pos'].split(' ')[1],native
    assert native[2]['aria']=='PFL',native
    await page.locator('#native-key-fixture').evaluate('(el)=>el.remove()')


async def main():
    check_assets()
    async with async_playwright() as p:
        browser=await p.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900)):
            page=await browser.new_page(viewport={'width':width,'height':height})
            errors=[]
            page.on('pageerror',lambda e:errors.append(str(e)))
            await page.goto((ROOT/'web/index.html').as_uri())
            await audit(page)
            assert not errors,(width,errors)
            await page.close()
        page=await browser.new_page(viewport={'width':1366,'height':768})
        await page.set_content((ROOT/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text())
        await audit(page)
        await page.close()
        await browser.close()
    print('PASS RGBA sprite MUTE SOLO PFL across WebView2/offline, ON/OFF no reflow, non-sprite SVG')


if __name__=='__main__':
    asyncio.run(main())
