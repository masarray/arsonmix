"""Product UX FAT (real Chromium) for the SINGLE assembled /preview/ and native WebView HTML.

Checks rendering in CSS-pixel viewports corresponding to 100/125/150% Windows
display scaling. DeviceScaleFactor alone does not reduce usable CSS height,
so each smaller viewport is explicitly tested. Never emulates DSP/hardware.
"""
import importlib.util
import json
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
CASES=((1680,900,1),(1366,768,1),(1255,700,1.25),
       (1024,576,1.25),(900,540,1.5))

def metrics(page):
    return page.evaluate("""() => {
      const get=selector=>document.querySelector(selector)?.getBoundingClientRect();
      const root=get('.console-body'), strip=document.querySelector('#tracks .strip');
      const item=cls=>strip?.querySelector('.'+cls)?.getBoundingClientRect();
      const top=item('route-section'), fx=item('fx-section'),
            wire=item('send-section'), pan=item('pan-section'),
            switches=item('switch-section'), fader=item('fader-section'),
            footer=item('bottom-section');
      if(!root||!top||!fx||!wire||!pan||!fader||!footer)return null;
      return {viewport:innerHeight, root:root.height, viewportWidth:innerWidth,
        route:top.height,fx:fx.height,wire:wire.height,pan:pan.height,
        switches:switches.height,fader:fader.height,footer:footer.height,
        upper:pan.top-root.top,lower:footer.bottom-pan.top,
        gap:footer.bottom-root.bottom,
        overflow:document.querySelector('.console-pane').scrollHeight-
                 document.querySelector('.console-pane').clientHeight,
        density:document.querySelector('.console-body').dataset.density,
        title:get('.window-title').height,
        hasLiveBar:!!document.querySelector('.mixer-title'),
        status:document.querySelector('#trackCount')?.textContent,
        pluginStyle:document.querySelector('#pluginBrowser')?.getAttribute('style')
      };
    }""")

def assert_key(page, selector):
    button=page.locator(selector)
    box=button.bounding_box()
    assert box and abs(box['width']-box['height'])<=1,(selector,box)
    assert button.locator('svg path').count()==1,selector
    assert button.locator('svg path').get_attribute('d').startswith('M6.4,19'),selector
    # A pressed close must have a visually red background before release.
    box=button.bounding_box()
    page.mouse.move(box['x']+box['width']/2,box['y']+box['height']/2)
    page.mouse.down()
    color=button.evaluate("e=>getComputedStyle(e).backgroundColor")
    active=button.evaluate("e=>e.matches(':active')")
    print("UX_CLOSE_PRESSED",selector,"active",active,"color",color,flush=True)
    rgb=[int(float(x.strip())) for x in color.replace("rgba(","").replace("rgb(","").split(")")[0].split(",")[:3]]
    assert active and rgb[0]>=110 and rgb[0]>rgb[1]*1.3 and rgb[0]>rgb[2]*1.2,(
        selector,active,color)
    page.mouse.up()

def wait_balanced(page):
    page.wait_for_function("""() => {
      const root=document.querySelector('.console-body'),strip=document.querySelector('#tracks .strip');
      const pan=strip?.querySelector('.pan-section'),footer=strip?.querySelector('.bottom-section');
      if(!root||!pan||!footer)return false;
      const r=root.getBoundingClientRect(),p=pan.getBoundingClientRect(),
            f=footer.getBoundingClientRect();
      return Math.abs((p.top-r.top)-(f.bottom-p.top))<=2;
    }""",timeout=4000)

def inspect_page(page, width, height, scale, label):
    page.goto((Path(label)).as_uri())
    page.wait_for_selector('#tracks .strip[data-id="1"]')
    page.evaluate("document.fonts.ready")
    assert page.locator('.mixer-title').count()==0,"redundant LIVE MIXER bar"
    assert page.locator('.window-title #trackCount').count()==1
    assert page.locator('.window-title').inner_text().find("OPERATOR-FIRST")<0
    assert page.locator('#densitySelect').is_hidden()
    assert page.locator('#pluginMinimize').count()==0
    assert page.evaluate("()=>document.querySelector('.console-body').__armixBalancedSplitInstalled")
    wait_balanced(page)
    m=metrics(page)
    print("UX_BASE",width,height,scale,json.dumps(m,sort_keys=True),flush=True)
    assert m and m['density']=='comfort'
    assert m['root']>=min(540,height-90),(width,height,m)
    assert m['fader']>=120 and m['fx']>=100,(width,height,m)
    # All controls must fit in their strip without moving the nameplate below
    # the console; short screens scroll the content, not truncate it.
    assert abs(m['gap'])<=3,(width,height,m)
    assert m['overflow']>=-3
    assert m['upper']>0 and m['lower']>0
    assert abs(m['upper']-m['lower'])<=2,(width,height,m)
    # Expand all components and verify coherent 50:50 allocation.
    for area in ('route','fx','send'):
        if page.locator('.console-body').evaluate("(e,a)=>e.classList.contains('collapsed-'+a)",area):
            page.locator(f'.section-rail [data-section="{area}"]').click()
    wait_balanced(page)
    m=metrics(page)
    print("UX_EXPANDED",width,height,scale,json.dumps(m,sort_keys=True),flush=True)
    assert m['route']>=20 and m['fx']>=100 and m['wire']>=20,(width,height,m)
    assert m['upper']>0 and m['lower']>0
    assert abs(m['gap'])<=3,(width,height,m)
    assert abs(m['upper']-m['lower'])<=2,(width,height,m)
    # Fold the FX; sections/fader must preserve mechanical labels and avoid
    # a second panel/overflow, even when all three upper parts are collapsed.
    for area in ('fx','route','send'):
        page.locator(f'.section-rail [data-section="{area}"]').click()
    m=metrics(page)
    print("UX_COLLAPSED",width,height,scale,json.dumps(m,sort_keys=True),flush=True)
    assert m['fader']>=120 and m['footer']>=32,(width,height,m)
    assert abs(m['gap'])<=3,(width,height,m)

    # Genuine draggable Plugin Manager with no minimize, correct Close state.
    page.locator('#pluginsButton').click()
    win=page.locator('#pluginBrowser')
    assert win.is_visible()
    assert win.evaluate("e=>getComputedStyle(e).position")=="fixed"
    original=win.bounding_box()
    handle=page.locator('#pluginDragHandle')
    hb=handle.bounding_box()
    page.mouse.move(hb['x']+40,hb['y']+13)
    page.mouse.down()
    page.mouse.move(max(30,min(width-150,hb['x']-110)),max(70,min(height-80,hb['y']+75)),steps=6)
    page.mouse.up()
    moved=win.bounding_box()
    assert moved and original and abs(moved['x']-original['x'])+abs(moved['y']-original['y'])>12,(
        "plugin drag inert",width,height,original,moved)
    assert moved['x']>=-2 and moved['y']>=-2 and moved['x']+moved['width']<=width+4,(
        "plugin escaped viewport",width,height,moved)
    page.screenshot(path=str(ROOT/f'operator-ux-drag-{width}x{height}-{scale}.png'))
    check_identity_zero_reflow(page,width,height,scale,1)  # plugin open/dragged
    assert_key(page,'#pluginClose')
    assert win.is_hidden()
    # Identity window and the other modeless tool share the exact square key.
    page.locator('#tracks .strip[data-id="1"] .strip-identity-top').dblclick()
    assert page.locator('#channelIdentity').is_visible()
    assert_key(page,'#identityClose')
    assert page.locator('#channelIdentity').is_hidden()
    check_identity_zero_reflow(page,width,height,scale,10)  # right-edge clamp
    # All other close keys use the SAME SVG and dimensions; hidden popups
    # cannot be pointer-tested here, but the CSS/DOM invariants are checked.
    for id in ('fastClose','wireConfigClose','wireWsClose','routeChoiceClose'):
        el=page.locator('#'+id)
        assert el.locator('svg path').count()==1,id
        assert el.evaluate("e=>getComputedStyle(e).width")=="29px",id
    page.screenshot(path=str(ROOT / f'operator-ux-{width}x{height}-{scale}.png'),
                    full_page=True)
    return m


# Every significant console coordinate and scroll extent must stay unchanged.
# This intentionally tests MORE than 'editor.is_visible()' at all five scales.
IDENTITY_GEOMETRY = """() => {
  const selectors=[
    'html','body','.app','.window-title','.command-bar','.console-pane',
    '.console-body','.section-rail','#viewport','#tracks','#master',
    '#master .strip','#tracks .strip','#tracks .strip .strip-identity-top',
    '#tracks .strip .route-section','#tracks .strip .fx-section',
    '#tracks .strip .send-section','#tracks .strip .pan-section',
    '#tracks .strip .switch-section','#tracks .strip .fader-section',
    '#tracks .strip .bottom-section','#tracks .strip .fader',
    '#tracks .strip .meter-lane','#tracks .strip .wire-node-in',
    '#tracks .strip .wire-node-out','#master .fader',
    '#master .meter-lane','#pluginBrowser'
  ];
  const rect=e=>{const r=e.getBoundingClientRect();
    return [r.x,r.y,r.width,r.height];};
  const rects={};
  for(const selector of selectors)
    rects[selector]=Array.from(document.querySelectorAll(selector))
      .filter(e=>!e.hidden&&getComputedStyle(e).display!=='none').map(rect);
  const scroll={};
  for(const selector of ['html','body','.app','.console-pane',
                          '.console-body','#viewport','#tracks','#master']){
    const e=document.querySelector(selector);
    if(e)scroll[selector]=[e.scrollTop,e.scrollLeft,e.clientWidth,
                           e.clientHeight,e.scrollWidth,e.scrollHeight];
  }
  return {rects,scroll,viewport:[innerWidth,innerHeight,
      visualViewport?.width||innerWidth,visualViewport?.height||innerHeight,
      visualViewport?.offsetLeft||0,visualViewport?.offsetTop||0],
    density:document.querySelector('.console-body').dataset.density,
    bank:document.getElementById('bankSelect').value,
    editorParent:document.getElementById('channelIdentity').parentElement.tagName};
}"""

def assert_still(before,after,where):
    assert after['editorParent']=='BODY',(where,'editor lives inside mixer')
    for key in ('scroll','viewport','density','bank'):
        assert before[key]==after[key],(where,key,before[key],after[key])
    for selector,lhs in before['rects'].items():
        rhs=after['rects'][selector]
        assert len(lhs)==len(rhs),(where,selector,len(lhs),len(rhs))
        for index,(a,b) in enumerate(zip(lhs,rhs)):
            for axis,(x,y) in enumerate(zip(a,b)):
                assert abs(x-y)<=0.5,(where,selector,index,axis,x,y)

def check_identity_zero_reflow(page,width,height,scale,channel):
    strip=page.locator(f'#tracks .strip[data-id="{channel}"]')
    strip.locator('.strip-identity-top').click()
    # Baseline after the intentional fold/selection layout has been painted.
    # Folded panels are NOT 50:50, and that is unrelated to editor stability.
    page.evaluate("() => new Promise(done => requestAnimationFrame(done))")
    before=page.evaluate(IDENTITY_GEOMETRY)
    stem=ROOT/f'operator-identity-{width}x{height}-{scale}-ch{channel}'
    page.screenshot(path=str(stem)+'-before.png')
    strip.locator('.strip-identity-top').dblclick()
    editor=page.locator('#channelIdentity')
    assert editor.is_visible(),(width,height,channel)
    assert editor.evaluate("e=>getComputedStyle(e).position")=='fixed'
    assert page.locator('#identityName').evaluate("e=>document.activeElement===e")
    box=editor.bounding_box()
    assert box and box['x']>=-0.5 and box['y']>=-0.5,box
    assert box['x']+box['width']<=width+0.5,box
    assert box['y']+box['height']<=height+0.5,box
    assert page.locator('#identityClose').is_visible()
    assert page.locator('#identitySave').is_visible()
    opened=page.evaluate(IDENTITY_GEOMETRY)
    assert_still(before,opened,(width,height,scale,channel,'open'))
    page.screenshot(path=str(stem)+'-open.png')
    page.locator('#identityCancel').click()
    assert editor.is_hidden()
    closed=page.evaluate(IDENTITY_GEOMETRY)
    assert_still(before,closed,(width,height,scale,channel,'close'))
    page.screenshot(path=str(stem)+'-closed.png')
    print('IDENTITY_ZERO_REFLOW',width,height,scale,channel,
          json.dumps({'before':before['scroll'],'opened':opened['scroll'],
                      'closed':closed['scroll'],'editor':box},sort_keys=True),
          flush=True)

def main():
    import sys
    sys.path.insert(0,str(ROOT/'scripts'))
    import build_approved_native_gui as build
    with tempfile.TemporaryDirectory(prefix="arsonmix-ux-fat-") as td, sync_playwright() as p:
        staged=Path(td)/'native'
        build.stage(staged)
        browser=p.chromium.launch(headless=True)
        try:
            for width,height,scale in CASES:
                page=browser.new_page(viewport={'width':width,'height':height},
                                       device_scale_factor=scale)
                errors=[]
                page.on('pageerror',lambda err:errors.append(str(err)))
                inspect_page(page,width,height,scale,staged/'index.html')
                assert not errors,(width,height,errors)
                page.close()
        finally:browser.close()
    print('PASS operator UX: actual native-assembled Chromium layout/interaction across display scales')

if __name__=='__main__':main()
