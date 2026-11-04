"""Physical micro-button PNG V1 — real DOM states and unchanged WIRE geometry."""
from pathlib import Path
import struct
import zlib
import subprocess
import sys
import tempfile
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
ASSET=ROOT/'site/preview/assets'
PAGE=ROOT/'site/preview/index.html'
SQUARE='micro-hardware-17x17-2x4.png'
FORMAT='format-hardware-25x15-2x1.png'

def source_gate():
    for filename,wh in ((SQUARE,(34,68)),(FORMAT,(50,15))):
        png=(ASSET/filename).read_bytes()
        assert png[:8]==bytes.fromhex('89504e470d0a1a0a')
        assert struct.unpack('>II',png[16:24])==wh
        assert png[25]==6, 'Must preserve RGBA channel'
    css=(ASSET/'mixer-utility-controls.css').read_text()
    context=(ASSET/'mixer-contextual-buttons.css').read_text()
    assert css.count('!important')==0
    assert ':hover' not in css and 'filter:blur' not in css.replace(' ','')
    assert 'micro-hardware-17x17-2x4.png?v=fx-green-v21' in css
    assert 'format-hardware-25x15-2x1.png?v=fx-green-v21' in css
    # Cross-owner ID-level V6 selectors used to override the micro cap.
    assert '.console-body .strip button.mx-util-key' not in context
    assert '.mx-util-key[data-util-kind=' not in context

def pixel_geometry_gate():
    """The actual 17x17 artwork must be square—not square including an underbar."""
    def pixels(path):
        png=(ASSET/path).read_bytes()
        w,h=struct.unpack('>II',png[16:24])
        p=8
        blocks=[]
        while p<len(png):
            n=struct.unpack('>I',png[p:p+4])[0]
            if png[p+4:p+8]==b'IDAT': blocks.append(png[p+8:p+8+n])
            p+=n+12
        raw=zlib.decompress(b''.join(blocks))
        stride=1+w*4
        assert len(raw)==h*stride
        assert all(raw[y*stride]==0 for y in range(h))
        def at(x,y):
            i=y*stride+1+x*4
            return tuple(raw[i:i+4])
        return at
    px=pixels(SQUARE)
    neutral=[px(x,y) for y in range(17) for x in range(17)]
    for row in range(4):
        for col in (0,1):
            x0=col*17;y0=row*17
            assert all(px(x0+x,y0+y)[3]==255 for y in range(1,16)
                       for x in range(1,16)),(row,col,'cap face not 15x15')
            assert all(px(x0+8,y0+y)[3]==255 for y in (0,1,8,15,16)),(
                row,col,'detached lower ornament or shadow')
        assert neutral==[px(x,y+17*row) for y in range(17) for x in range(17)],(
            row,'colored inactive underbar')
    # The FX-only ON tile has a visible luminous mint face, not dull teal.
    green=px(17+8,8)
    assert green[1]>=205 and green[1]-green[0]>=60 and green[1]-green[2]>=55,(
        'FX power active face is not bright green',green)
    fmt=pixels(FORMAT)
    for col in (0,1):
        assert all(fmt(col*25+x,y)[3]==255 for y in range(1,14)
                   for x in range(1,24)),(col,'format surface not full height')

def state(el):
    return el.evaluate("""el=>{
       const r=el.getBoundingClientRect(),s=getComputedStyle(el);
       return {x:r.x,y:r.y,w:r.width,h:r.height,
         bg:s.backgroundImage,pos:s.backgroundPosition,size:s.backgroundSize,
         color:s.color,shadow:s.boxShadow,transform:s.transform,
         duration:s.transitionDuration,alpha:s.backgroundColor,
         pressed:el.getAttribute('aria-pressed')};
    }""")

def stable(a,b):
    return all(abs(a[k]-b[k])<.1 for k in ('x','y','w','h'))

def test_page(browser,url,label):
    for width in (1366,1680,1920):
        page=browser.new_page(viewport={'width':width,'height':900})
        errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)))
        page.goto(url)
        page.wait_for_selector('#tracks .strip[data-id="1"] .mx-util-key')
        row=page.locator('#tracks .strip[data-id="1"]')
        kinds=('fx-main','fx-slot','phase','format','fx-remove')
        before={}
        for kind in kinds:
            loc=row.locator('[data-util-kind="'+kind+'"]').first
            assert loc.count()==1,(label,width,kind)
            s=state(loc)
            assert (FORMAT if kind=='format' else SQUARE) in s['bg'],(label,kind,s)
            assert s['alpha']=='rgba(0, 0, 0, 0)',(label,kind,s)
            assert s['transform']=='none' and s['duration']=='0s',(label,kind,s)
            assert s['shadow']=='none',(label,kind,s)
            assert s['h']==(15 if kind=='format' else 17),(label,kind,s)
            assert s['w']==(25 if kind=='format' else 17),(label,kind,s)
            assert s['pos'].startswith(('100%' if s['pressed']=='true' else '0%')+' '),(label,kind,s)
            before[kind]=s
            if kind in ('fx-main','fx-slot') and s['pressed']=='true':
                assert s['color']=='rgb(22, 62, 43)',(label,width,kind,'active glyph must stay dark')
            if kind=='fx-remove':
                assert s['color']=='rgb(164, 73, 78)',(label,width,'delete OFF must be muted red, not black',s['color'])
        for kind in ('fx-main','phase','format'):
            loc=row.locator('[data-util-kind="'+kind+'"]').first
            old=loc.get_attribute('aria-pressed')
            loc.click()
            loc=row.locator('[data-util-kind="'+kind+'"]').first
            on=state(loc)
            assert on['pressed']!=old,(label,kind,on)
            assert on['pos'].startswith(('0%' if old=='true' else '100%')+' '),(label,kind,on)
            assert stable(before[kind],on),(label,kind,'ON displaced physical key')
            if kind=='fx-main' and on['pressed']=='true':
                assert on['color']=='rgb(22, 62, 43)',(label,kind,'white icon on green cap',on['color'])
            loc.click()
            off=state(row.locator('[data-util-kind="'+kind+'"]').first)
            assert off['pressed']==old and stable(before[kind],off),(label,kind,off)
        # The live trash icon must match the supplied delete_forever SVG path.
        trash_path='M9.4,16.5L12,13.9l2.6,2.6L16,15.1l-2.6-2.6L16,9.9L14.6,8.5L12,11.1L9.4,8.5L8,9.9l2.6,2.6L8,15.1l1.4,1.4ZM7,21q-0.82,0-1.41-0.59T5,19V6H4V4H9V3h6V4h5V6H19V19q0,0.82-0.59,1.41T17,21H7ZM17,6H7V19H17V6ZM7,6V19V6Z'
        glyph=row.locator('[data-util-kind="fx-remove"] svg path').first
        assert glyph.get_attribute('d')==trash_path,(label,width,'delete graphic must be exact supplied SVG')
        # Remove is NEVER latched. It shows a brief red pressed frame.
        remove=row.locator('[data-util-kind="fx-remove"]').first
        box=remove.bounding_box()
        page.mouse.move(box['x']+box['width']/2,box['y']+box['height']/2)
        page.mouse.down()
        held=state(remove)
        assert held['pos'].startswith('100% '),(label,'remove momentary',held)
        assert held['color']=='rgb(255, 255, 255)',(label,'remove pressed icon',held['color'])
        assert stable(before['fx-remove'],held)
        page.mouse.up()
        assert page.locator('#tracks .strip[data-id="1"] .wire-node-out').count()==1
        assert page.locator('#tracks .strip[data-id="3"] .wire-node-in').count()==1
        assert page.locator('#wireLayer').count()==1
        toolbar=page.locator('#sofToggle')
        assert 'gradient' in state(toolbar)['bg'],(label,'flexible toolbar material replaced')
        mute=page.locator('#tracks .strip[data-id="1"] [data-hw-kind="mute"]')
        assert mute.count()==1,(label,'approved M/S/P removed')
        if label=='approved-png' and width==1680:
            page.screenshot(path='pages-native-micro-assets-1680.png')
        assert not errors,(label,width,errors)
        page.close()

def main():
    source_gate()
    pixel_geometry_gate()
    with tempfile.TemporaryDirectory(prefix='arsonmix-micro-') as temp:
        dist=Path(temp)/'pages'
        subprocess.run([sys.executable,str(ROOT/'scripts/build_pages.py'),
            '--out',str(dist),'--sha','micro-qa','--branch','pr'],
            cwd=ROOT,check=True,capture_output=True,text=True)
        # Only /preview/ publishes a mixer; old routes are redirects only.
        assets=dist/'preview/assets'
        for png in (SQUARE,FORMAT):
            assert (assets/png).read_bytes()==(ASSET/png).read_bytes(),png
        for legacy in ('preview-v2','preview-buttons','preview-v33','preview-v34'):
            assert sorted(x.name for x in (dist/legacy).iterdir())==['index.html']
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True)
            test_page(browser,PAGE.as_uri(),'internal-source')
            test_page(browser,(dist/'preview/index.html').as_uri(),'approved-png')
            browser.close()
    print('PASS micro RGBA frames, state depth, no movement, dynamic label and WIRE preservation')

if __name__=='__main__':main()
