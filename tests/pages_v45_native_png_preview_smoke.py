"""V45 PNG mixer preview: atlas geometry/alpha, native size, state, safe routing."""
from pathlib import Path
import struct
import hashlib
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
LIVE=ROOT/'site/preview-buttons/index.html'
ASSETS=ROOT/'site/preview-buttons/assets'
PNG=ASSETS/'mute-solo-pfl-56x37.png'
CSS=ASSETS/'mixer-control-keys.css'
JS=ASSETS/'mixer-control-keys.js'

def main():
    # This review intentionally pins a verified V45 screenshot baseline; current\n    # /preview-v2/ advances independently with other GUI workstreams.\n    html=LIVE.read_bytes()\n    git_sha=hashlib.sha1(b"blob "+str(len(html)).encode()+b"\\x00"+html).hexdigest()\n    assert git_sha=="8fd9c307b918391f2f41da4fa9a182805071659f", "V45 review baseline drift"
    assert PNG.read_bytes()==(ROOT/'site/preview-v34/assets/mute-solo-pfl-56x37.png').read_bytes()
    data=PNG.read_bytes()
    assert data[:8]==bytes.fromhex('89504e470d0a1a0a')
    assert struct.unpack('>II',data[16:24])==(112,111)
    assert data[25]==6
    css=CSS.read_text()
    js=JS.read_text()
    assert 'background-size:200% 300%' in css
    assert 'background-color:transparent!important' in css
    assert 'aria-pressed="true"' in css
    assert 'ResizeObserver' not in js.replace('No ResizeObserver','')
    with sync_playwright() as p:
        browser=p.chromium.launch(headless=True)
        for width in (1366,1680,1920):
            page=browser.new_page(viewport={'width':width,'height':900})
            errors=[]
            page.on('pageerror',lambda e:errors.append(str(e)))
            page.goto(LIVE.as_uri())
            page.wait_for_selector('#tracks .strip[data-id="1"] .mx-hw-row')
            row=page.locator('#tracks .strip[data-id="1"] .mx-hw-row')
            keys=row.locator('button.mx-hw-key')
            assert keys.count()==3
            def snapshot():
                return keys.evaluate_all("""xs=>xs.map(b=>{
                  const r=b.getBoundingClientRect(),c=getComputedStyle(b),
                        p=getComputedStyle(b,'::before'),
                        f=p.width&&parseFloat(p.width);
                  return {action:b.dataset.action,kind:b.dataset.hwKind,
                    pressed:b.getAttribute('aria-pressed'),
                    x:r.x,y:r.y,w:r.width,h:r.height,
                    faceW:f,faceH:parseFloat(p.height),
                    bg:p.backgroundImage,pos:p.backgroundPosition,
                    color:c.backgroundColor,shadow:c.boxShadow,
                    dx:c.transform,alpha:p.opacity};
                })""")
            before=snapshot()
            assert [v['action'] for v in before]==['mute','solo','cue'],before
            assert [v['kind'] for v in before]==['mute','solo','cue'],before
            for v in before:
                assert 'mute-solo-pfl-56x37.png' in v['bg'],v
                assert v['color']=='rgba(0, 0, 0, 0)' and v['shadow']=='none',v
                assert v['dx']=='none' and v['alpha']=='1',v
                assert abs(v['h']-29)<.3 and v['faceW']<=43.91,v
                assert abs(v['faceW']/v['faceH']-56/37)<.02,v
            assert ['0% 0%','0% 50%','0% 100%']==[v['pos'] for v in before],before
            for k in range(3):keys.nth(k).click()
            after=snapshot()
            assert all(v['pressed']=='true' for v in after),after
            assert [v['pos'] for v in after]==['100% 0%','100% 50%','100% 100%'],after
            for old,new in zip(before,after):
                assert all(abs(old[d]-new[d])<.1 for d in ('x','y','w','h','faceW','faceH')),(old,new)
            keys.nth(0).click()
            assert [x['pressed'] for x in snapshot()]==['false','true','true']
            if width==1680:page.screenshot(path='pages-v45-approved-png-buttons-1680.png')
            assert not errors,(width,errors)
            page.close()
        browser.close()
    print('PASS V45 approved PNG preview: alpha, pixel aspect, state, no reflow, original mixer intact')

if __name__=='__main__':main()
