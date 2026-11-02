"""V5 mixer hardware keys: one masked legend, semantic LEDs, native parity.
Runs against the REAL web/index.html and GENERATED offline standalone.
No DSP/audio claims: this validates UI skin and existing button handlers only.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]

async def audit(page):
    await page.wait_for_function("""() => !!window.ArSonMixHardwareKeys &&
      !!document.querySelector('[data-mute="0"] svg.mx-hw-face')""")
    keys=await page.evaluate("""() => {
      const read=q=>{
        const b=document.querySelector(q),s=getComputedStyle(b),p=getComputedStyle(b,'::before');
        const face=b.querySelector('svg.mx-hw-face');
        const text=face?.querySelectorAll('mask text');
        return {className:b.className,pressed:b.getAttribute('aria-pressed'),
          oldSkin:b.classList.contains('btn-180'),svg:!!face,
          masks:face?.querySelectorAll('mask').length,
          textCount:text?.length,visualText:face?.querySelectorAll(':scope > text').length,
          led:s.getPropertyValue('--mx-hw-led').trim(),gap:p.opacity,
          lamp:face?getComputedStyle(face.querySelector('.mx-hw-lamp')).opacity:null,
          filter:s.filter,blur:s.backdropFilter,shadow:s.boxShadow};
      };
      return {mute:read('[data-mute="0"]'),solo:read('[data-solo="0"]'),
        master:read('#masterMute'),phase:read('[data-pre="0"]'),
        hpf:read('[data-hpf="0"]')};
    }""")
    for name,item in keys.items():
        assert not item['oldSkin'],(name,item)
        assert item['svg'] and item['masks']==1 and item['textCount']==1,(name,item)
        assert item['visualText']==0,(name,item)
        assert item['gap']=='0' and item['lamp']=='0',(name,item)
        assert item['filter']=='none' and item['blur']=='none',(name,item)
    assert keys['mute']['led']=='255,64,85',keys
    assert keys['solo']['led']=='255,208,44',keys
    assert keys['phase']['led']=='243,164,81',keys
    assert keys['hpf']['led']=='87,211,189',keys
    mute=page.locator('[data-mute="0"]')
    solo=page.locator('[data-solo="0"]')
    await mute.hover()
    assert await mute.evaluate("(e)=>getComputedStyle(e).filter")=='none'
    await mute.click()
    await solo.click()
    assert await mute.get_attribute('aria-pressed')=='true'
    assert await solo.get_attribute('aria-pressed')=='true'
    lit=await page.evaluate("""()=>[...document.querySelectorAll('[data-mute="0"],[data-solo="0"]')].map(b=>({
      gap:getComputedStyle(b,'::before').opacity,
      lamp:getComputedStyle(b.querySelector('.mx-hw-lamp')).opacity
    }))""")
    assert lit==[{'gap':'1','lamp':'1'}]*2,lit
    # Only native/app JS changes channel state: decorating cannot dispatch audio.
    await mute.click()
    assert await mute.get_attribute('aria-pressed')=='false'
    assert await solo.get_attribute('aria-pressed')=='true'
    await solo.click()
    # Every MASTER control occupies the same 29px row as regular MUTE/SOLO.
    dimensions=await page.evaluate("""()=>{
      const rect=q=>document.querySelector(q).getBoundingClientRect();
      const master=rect('#masterMute'),mono=rect('#masterMode'),channel=rect('[data-mute="0"]');
      return {masterH:master.height,modeH:mono.height,channelH:channel.height,
              masterMid:master.top+master.height/2,modeMid:mono.top+mono.height/2,
              channelMid:channel.top+channel.height/2};
    }""")
    assert all(abs(dimensions[k]-29)<.1 for k in ('masterH','modeH','channelH')),dimensions
    assert max(dimensions[k] for k in ('masterMid','modeMid','channelMid'))-min(
      dimensions[k] for k in ('masterMid','modeMid','channelMid'))<1,dimensions
    # SVG gradient LED stops are resolved colors, never unsupported CSS var syntax.
    stops=await page.evaluate("""()=>[...document.querySelectorAll('[data-mute="0"],[data-solo="0"]')]
      .map(b=>getComputedStyle(b.querySelector('.mx-hw-lamp').ownerSVGElement
          .querySelector('linearGradient[id^="mx-hw-led"] stop')).stopColor)""")
    assert stops==['rgb(255, 64, 85)','rgb(255, 208, 44)'],stops
    # Master mode changes textContent; observer reinstalls ONE SVG legend.
    mode=page.locator('#masterMode')
    await mode.click()
    await page.wait_for_function("""()=>document.querySelector('#masterMode')
      ?.querySelector('svg.mx-hw-face mask text')?.textContent==='MONO'""")
    assert await mode.get_attribute('aria-pressed')=='true'
    await mode.click()
    await page.wait_for_function("""()=>document.querySelector('#masterMode')
      ?.querySelector('svg.mx-hw-face mask text')?.textContent==='STEREO'""")
    # Channel redraw does not leave a raw/old BTN-180 switch behind.
    await page.evaluate('renderMixer()')
    await page.wait_for_function("""()=>[...document.querySelectorAll('#mixer .channel-actions button')]
      .every(b=>!!b.querySelector('svg.mx-hw-face'))""")
    assert await page.locator('#mixer .channel-actions button').count()==22
    # Added native controls get the SAME renderer without poll/per-meter observer.
    await page.evaluate("""()=>{
      const node=document.createElement('section');node.className='arV6Console';
      node.id='v5-native-fixture';
      node.innerHTML='<button data-op="SET_MUTE" aria-pressed="true">M</button>'+
        '<button data-op="SET_SOLO" aria-pressed="true">S</button>'+
        '<button data-op="SET_CUE" aria-pressed="true">C</button>'+
        '<button data-op="SET_INSERT_BYPASS" aria-pressed="true">ON</button>'+
        '<button data-op="SET_SEND_TAP">PRE</button>';
      document.querySelector('#app').appendChild(node);
    }""")
    await page.wait_for_function("""()=>document.querySelectorAll('#v5-native-fixture .mx-hw-face').length===5""")
    native=await page.evaluate("""()=>[...document.querySelectorAll('#v5-native-fixture button')].map(b=>({
      led:getComputedStyle(b).getPropertyValue('--mx-hw-led').trim(),
      lamp:getComputedStyle(b.querySelector('.mx-hw-lamp')).opacity,
      gap:getComputedStyle(b,'::before').opacity
    }))""")
    assert [x['led'] for x in native]==[
      '255,64,85','255,208,44','90,200,231','87,211,189','90,200,231'],native
    assert all(x['lamp']=='1' and x['gap']=='1' for x in native),native
    await page.locator('#v5-native-fixture').evaluate('(el)=>el.remove()')

async def main():
    async with async_playwright() as p:
        browser=await p.chromium.launch(headless=True)
        for width,height in [(1366,768),(1680,900)]:
            page=await browser.new_page(viewport={'width':width,'height':height})
            errors=[]
            page.on('pageerror',lambda e:errors.append(str(e)))
            await page.goto((ROOT/'web/index.html').as_uri())
            await audit(page)
            assert not errors,(width,errors)
            if width==1680:
                await page.screenshot(path='mixer-backlit-v5-native-1680.png')
                await page.locator('[data-mute="0"]').click()
                await page.locator('[data-solo="0"]').click()
                await page.screenshot(path='mixer-backlit-v5-native-ON-1680.png')
                assert await page.locator('[data-mute="0"]').get_attribute('aria-pressed')=='true'
                assert await page.locator('[data-solo="0"]').get_attribute('aria-pressed')=='true'
            await page.close()
        page=await browser.new_page(viewport={'width':1366,'height':768})
        await page.set_content((ROOT/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text())
        await audit(page)
        await page.close()
        await browser.close()
    print('PASS V5: single mask, asymmetric static light, semantic LEDs, native/legacy dynamic renderer, independent toggles, OFF/ON redraw & offline parity')

if __name__=='__main__':
    asyncio.run(main())
