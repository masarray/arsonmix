#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Optional Chromium fixture: real editor and host JS, injected backend (not PCM)."""
from pathlib import Path
import os
from playwright.sync_api import sync_playwright

assets = Path(__file__).resolve().parents[2] / 'site/preview-v2/assets'
with sync_playwright() as pw:
    browser = pw.chromium.launch(executable_path='/usr/bin/chromium' if Path('/usr/bin/chromium').is_file() else None, args=['--no-sandbox'])
    page = browser.new_page(viewport={'width': 800, 'height': 600})
    page.set_default_timeout(5000)
    errors = []
    page.on('pageerror', lambda e: errors.append(str(e)))
    # Real existing PNG/JPG, served from the same relative /assets/ path as the
    # assembled native bundle. No data URI, substitute image or network dependency.
    allowed = {'mixer-gain-knob-brushed-steel.png', 'geq31-brushed-metal.jpg'}
    loaded = set()
    def serve(route):
        name = route.request.url.rsplit('/', 1)[-1]
        if route.request.url == 'https://fixture.invalid/':
            route.fulfill(content_type='text/html', body='<body style="background:#151718"></body>')
        elif name in allowed:
            loaded.add(name)
            route.fulfill(path=str(assets / name))
        else:
            route.abort()
    page.route('https://fixture.invalid/**', serve)
    page.goto('https://fixture.invalid/')
    page.add_style_tag(path=str(assets/'mixer-contextual-buttons.css'))
    page.evaluate('''()=>{
      window.listeners={};window.events=[];window.crypto.randomUUID ||= ()=>String(Math.random()).slice(2);
      window.view={admitted:true,device:{epoch:'12'},graph:{revision:'4',channels:[{id:'42',inserts:[{id:'fx-a',catalogId:'armix.builtin.de-ezzer.v1'}]}]}};
      window.__ARMIX_NATIVE_M2__={fxEditorState:()=>view,nativeIdFor:t=>t.nativeId};
      window.autoSnapshot=true;
      window.__JUCE__={backend:{emitEvent:(n,p)=>{
        events.push(p);
        // Real editor polls continually. Keep ordinary gestures independent
        // of runner speed; manual correlated observations below disable this.
        if(p.action==='nativeDeEzzerSnapshot'&&window.autoSnapshot&&window.s&&events.filter(x=>x.action==='nativeDeEzzerSnapshot').length>1)
          queueMicrotask(()=>{s={...s,frames:String(BigInt(s.frames)+128n)};listeners.arMixBuiltinDeEzzerSnapshot({...p,ok:true,snapshot:s});});
      },addEventListener:(n,cb)=>listeners[n]=cb}};
      // Fixture only: verify the shared bar adapter's exact accepted values and receipt.
      window.presetCalls=[];window.ArSonMixBuiltinFxPresetBar={create(a){window.adapter=a;a.subscribeResult(p=>presetCalls.push(['reply',p]));return {paint(){},refresh(){},selectUser(){return false;},onAccepted(s,id){presetCalls.push(['accepted',s,id]);},factoryIntent(id){presetCalls.push(['factory',id]);},tick(){},close(){}};}};
      window.commands=()=>events.filter(p=>p.action==='nativeDeEzzerSet');
      window.ack=()=>{const r=commands().at(-1);listeners.arMixBuiltinDeEzzerQueue({...r,ok:true});s={...s,frames:String(BigInt(s.frames)+128n),appliedSequence:r.command.sequence,settings:r.command.settings};listeners.arMixBuiltinDeEzzerAck({...r,ok:true,ack:{requested:r.command.id,sequence:r.command.sequence,code:'Applied',actual:s}});};
    }''')
    for name in ('mixer-native-de-ezzer.js', 'mixer-native-de-ezzer-host.js'):
        page.add_script_tag(path=str(assets/name))
    page.evaluate('''()=>{window.s={id:{instance:'9',epoch:'3'},appliedSequence:'0',frames:'0',settings:{...ArSonMixNativeDeEzzer.defaults,enabled:false},audition:'Normal',gainReductionDb:2.5,lowerWeight:.7};ArSonMixNativeDeEzzer.open({nativeId:'42'},0);const r=events.find(p=>p.action==='nativeDeEzzerSnapshot');listeners.arMixBuiltinDeEzzerSnapshot({...r,ok:true,snapshot:s});}''')
    dial = page.locator('.dial').first
    output = page.locator('.knobs output').first
    # The production editor uses the original assets, decoded in Chromium.
    assert page.locator('.dial-face img').count() == 3
    assert page.locator('.dial-well').count() == 3
    assert page.locator('.dial-arc').count() == 3
    assert page.locator('.dial-art').count() == 0
    page.wait_for_function("Array.from(document.querySelectorAll('.dial-face img')).every(i=>i.complete&&i.naturalWidth>0)")
    page.wait_for_function("getComputedStyle(document.querySelector('#mxNativeDeEzzer')).backgroundImage.includes('geq31-brushed-metal.jpg')")
    assert page.evaluate("""async () => {
      const im=new Image();im.src='assets/geq31-brushed-metal.jpg';await im.decode();
      return im.naturalWidth>0&&im.naturalHeight>0;
    }""")
    assert loaded == allowed, loaded
    assert dial.bounding_box()['width'] == 80
    assert page.locator('.dial-face').first.evaluate('(e)=>getComputedStyle(e).transform') == 'none'
    assert page.locator('.dial-face').first.evaluate('(e)=>getComputedStyle(e).boxShadow') != 'none'
    assert page.locator('.lcd-glass').evaluate('(e)=>getComputedStyle(e).pointerEvents') == 'none'
    assert page.locator('.lcd-glass').evaluate('(e)=>getComputedStyle(e,"::before").clipPath').startswith('polygon(')
    # Actual optical plane has white gradient stops with a monotonic fade.
    # CSSOM quantizes each alpha to 8-bit; check parsed values with one-half LSB tolerance.
    glass = page.locator('.lcd-glass')
    stops = glass.evaluate(r"""e=>[...getComputedStyle(e,'::before').backgroundImage.matchAll(/rgba\(([^)]+)\)/g)].map(m=>m[1].split(',').map(Number))""")
    assert len(stops) == 3, stops
    assert all(x[:3] == [255,255,255] for x in stops), stops
    assert all(abs(x[3]-a) <= .5/255+.0005 for x,a in zip(stops,[.18,.105,.018])), stops
    assert stops[0][3]>stops[1][3]>stops[2][3]>0
    assert glass.evaluate('(e)=>getComputedStyle(e,"::before").clipPath') == 'polygon(0px 0px, 48% 0px, 16% 100%, 0px 100%)' or glass.evaluate('(e)=>getComputedStyle(e,"::before").clipPath').startswith('polygon(')
    assert glass.evaluate('(e)=>getComputedStyle(e,"::after").borderTopStyle') == 'solid'
    assert glass.evaluate('(e)=>getComputedStyle(e,"::after").borderBottomColor') != 'rgba(0, 0, 0, 0)'
    assert glass.evaluate('(e)=>getComputedStyle(e,"::after").backgroundImage').count('radial-gradient') == 4
    assert glass.evaluate('(e)=>getComputedStyle(e,"::after").boxShadow') != 'none'
    assert glass.evaluate('(e)=>getComputedStyle(e,"::after").pointerEvents') == 'none'
    assert glass.evaluate('(e)=>getComputedStyle(e,"::before").pointerEvents') == 'none'
    assert page.locator('.knobs output').first.evaluate('(e)=>getComputedStyle(e,"::before").clipPath').startswith('polygon(')
    # Worst-case text contrast: calculate against the brightest interior of
    # the actual glass (edge glints are confined to the unused perimeter).
    assert page.evaluate("""alpha => {
      const rgb=c=>c.match(/[0-9.]+/g).slice(0,3).map(Number);
      const l=v=>{v=v.map(n=>n/255).map(n=>n<=.04045?n/12.92:((n+.055)/1.055)**2.4);return .2126*v[0]+.7152*v[1]+.0722*v[2]};
      const base=rgb(getComputedStyle(document.querySelector('.display')).backgroundColor);
      const bg=l(base.map(n=>n*(1-alpha)+255*alpha));
      return ['.lcd-title','.lcd-subtitle','.analysis-foot b','.gr','.axis','[data-weight]','[data-analysis-status]'].every(sel=>(l(rgb(getComputedStyle(document.querySelector(sel)).color))+.05)/(bg+.05)>=4.5);
    }""", stops[0][3])
    # The optical layer is above content but never captures input.
    assert glass.evaluate("""e=>{const r=e.getBoundingClientRect(),hit=document.elementFromPoint(r.left+r.width*.25,r.top+r.height*.25);return hit!==e&&!e.contains(hit)}""")
    # Same computed chrome as other built-ins using the shared style, not local copies.
    def chrome_parity(state):
        return page.evaluate("""state=>{
          const peer=document.createElement('div');peer.style.cssText='position:fixed;left:-1000px;top:0;display:flex';
          peer.innerHTML='<button class="armix-builtin-fx-power" data-fx-state="'+state+'">⏻</button><button class="armix-builtin-fx-close">×</button>';document.body.append(peer);
          const props=['width','height','fontSize','borderColor','borderRadius','backgroundImage','color','boxShadow','textShadow'];
          const get=e=>Object.fromEntries(props.map(k=>[k,getComputedStyle(e)[k]]));
          const p=document.querySelector('[data-power]'),c=document.querySelector('[data-close]');
          const result=JSON.stringify(get(p))===JSON.stringify(get(peer.children[0]))&&JSON.stringify(get(c))===JSON.stringify(get(peer.children[1]))&&getComputedStyle(c,'::after').backgroundImage===getComputedStyle(peer.children[1],'::after').backgroundImage;
          peer.remove();return result;
        }""", state)
    assert chrome_parity('off')
    assert page.locator('[data-power]').bounding_box()['width'] == 26
    assert page.locator('[data-close]').bounding_box()['width'] == 24
    assert page.locator('.knobs output').first.evaluate('(e)=>getComputedStyle(e,"::after").pointerEvents') == 'none'
    box = dial.bounding_box(); x=box['x']+box['width']/2; y=box['y']+box['height']/2
    page.mouse.move(x,y);page.mouse.down();page.mouse.move(x,y-32)
    assert page.evaluate('commands().length') == 1 # must apply before mouseup
    assert output.inner_text() == '70%' # requested display; accepted separately
    assert page.locator('.draft').first.inner_text() == 'Applied 50%'
    assert dial.locator('.draft-pointer').is_visible()
    assert dial.locator('.dial-face').evaluate('(e)=>getComputedStyle(e).transform') == 'none'
    assert dial.evaluate('(e)=>getComputedStyle(e).getPropertyValue("--accepted-angle").trim()') == '0deg'
    assert page.locator('input[type=range]').count()==0
    assert page.locator('[data-power]').get_attribute('data-fx-state')=='off'
    page.mouse.move(x,y-64);page.mouse.move(x,y-80)
    assert page.evaluate('commands().length') == 1 # held behind ACK
    page.evaluate('ack()');page.wait_for_timeout(70)
    assert page.evaluate('commands().length') == 2
    page.mouse.up();page.evaluate('ack()');page.wait_for_timeout(70)
    assert output.inner_text() == '100%'
    assert page.evaluate('commands().length') == 2
    # fine gesture and Escape discard unsent draft, keep already accepted sound.
    page.mouse.move(x,y);page.mouse.down();page.keyboard.down('Shift');page.mouse.move(x,y+32);page.keyboard.up('Shift')
    assert page.evaluate('commands().at(-1).command.settings.amount') == .98
    page.mouse.move(x,y+100);page.keyboard.press('Escape');page.mouse.up();page.evaluate('ack()');page.wait_for_timeout(70)
    assert output.inner_text()=='98%' and page.evaluate('commands().length')==3
    dial.dblclick();page.evaluate('ack()');assert output.inner_text()=='50%'
    page.locator('[role=slider][aria-label="AMOUNT"]').focus();page.keyboard.press('ArrowRight');page.evaluate('ack()');assert output.inner_text()=='51%'
    page.locator('.preset-row select').select_option('FOCUSED');assert page.evaluate('commands().at(-1).command.settings.enabled') is False
    assert page.locator('.preset-row select').input_value()=='CUSTOM'
    page.evaluate('ack()');assert page.locator('.preset-row select').input_value()=='FOCUSED'
    assert page.evaluate('presetCalls.at(-1)[0]')=='accepted'
    assert page.evaluate('presetCalls.at(-1)[1].enabled===undefined')
    # Recalled user preset is a single full packet, excluding power.
    result=page.evaluate('adapter.apply({...ArSonMixNativeDeEzzer.factory.VOICE,amount:.42})')
    assert isinstance(result,str) and page.evaluate('commands().at(-1).command.sequence')==result
    page.evaluate('ack()');assert output.inner_text()=='42%'
    assert page.evaluate('presetCalls.at(-1)[2]')==result
    page.locator('[data-power]').click()
    assert page.locator('[data-power]').get_attribute('data-fx-state')=='off' # accepted state only
    page.evaluate('ack()')
    assert page.locator('[data-power]').get_attribute('data-fx-state')=='on'
    assert chrome_parity('on')
    page.evaluate('''()=>{let p={...adapter.context(),requestId:'fxpres-1'};delete p.slot;listeners.arMixBuiltinFxPresetResult({...p,instanceId:'other'});listeners.arMixBuiltinFxPresetResult(p);}''')
    assert page.evaluate('presetCalls.filter(x=>x[0]==="reply").length')==1
    # No fabricated meter before native observation. Inject a correlated snapshot.
    page.evaluate('window.autoSnapshot=false')
    assert page.locator('[data-ess]').inner_text()=='—'
    assert page.locator('.bars .led-lane').count()==10
    assert page.locator('.tap-meter').count()==3
    assert page.locator('.led-lane').count()==15
    assert page.locator('.bars .led-peak').evaluate_all("(nodes)=>nodes.every(e=>getComputedStyle(e).opacity==='0')")
    page.wait_for_timeout(70)
    page.evaluate('''()=>{const req=events.filter(p=>p.action==='nativeDeEzzerSnapshot').at(-1);s={...s,frames:String(BigInt(s.frames)+2400n),observation:{inputDb:[-55,-30,-12,-20,-41],wetDb:[-56,-32,-20,-25,-42],essDb:-20,sidechainDb:-16,deltaDb:-24}};listeners.arMixBuiltinDeEzzerSnapshot({...req,ok:true,snapshot:s});}''')
    assert page.locator('[data-ess]').inner_text()=='-20.0'
    lane = page.locator('.band').nth(2).locator('.led-lane').first
    assert lane.locator('.led-fill').evaluate("(e)=>parseFloat(e.style.getPropertyValue('--lit'))") == 8/9*100
    assert lane.locator('.led-peak').evaluate("(e)=>getComputedStyle(e).opacity") == '1'
    assert lane.locator('.led-fill').evaluate("(e)=>getComputedStyle(e).transitionDuration") == '0s'
    assert lane.evaluate("(e)=>getComputedStyle(e,'::after').backgroundImage.includes('repeating-linear-gradient')")
    assert lane.evaluate("(e)=>Number(getComputedStyle(e,'::after').zIndex)>Number(getComputedStyle(e.querySelector('.led-fill')).zIndex)")
    assert lane.evaluate("(e)=>getComputedStyle(e.querySelector('.led-peak')).backgroundImage===getComputedStyle(e.querySelector('.led-fill')).backgroundImage")
    # Independent held cell survives a falling native readback (not simulated PCM).
    before = lane.locator('.led-peak').evaluate("(e)=>e.style.getPropertyValue('--peak-end')")
    page.wait_for_timeout(70)
    page.evaluate('''()=>{const req=events.filter(p=>p.action==='nativeDeEzzerSnapshot').at(-1);s={...s,frames:String(BigInt(s.frames)+2400n),observation:{...s.observation,inputDb:[-55,-30,-60,-20,-41]}};listeners.arMixBuiltinDeEzzerSnapshot({...req,ok:true,snapshot:s});}''')
    assert lane.locator('.led-fill').evaluate("(e)=>parseFloat(e.style.getPropertyValue('--lit'))") == 2/9*100
    assert lane.locator('.led-peak').evaluate("(e)=>e.style.getPropertyValue('--peak-end')") == before
    assert 'Wet -20.0 dBFS' in page.locator('.band').nth(2).get_attribute('title')
    assert page.locator('[data-analysis-status]').inner_text()=='Measured · pre rack bypass'
    assert page.locator('[data-history] i').last.evaluate('(e)=>parseFloat(e.style.height)>0')
    assert page.locator('[data-cue="Ess"]').is_disabled()
    page.wait_for_timeout(70)
    page.evaluate('''()=>{let req=events.filter(p=>p.action==='nativeDeEzzerSnapshot').at(-1);s={...s,frames:String(BigInt(s.frames)+2400n),cue:{available:true,requested:'Mix',applied:'Mix'}};listeners.arMixBuiltinDeEzzerSnapshot({...req,ok:true,snapshot:s});}''')
    assert page.locator('#mxNativeDeEzzer').bounding_box()['height'] <= 600
    panel = page.locator('#mxNativeDeEzzer')
    assert panel.evaluate('(e)=>e.scrollWidth<=e.clientWidth+1')
    assert panel.evaluate('(e)=>e.scrollHeight<=e.clientHeight+1'), panel.evaluate('(e)=>[e.scrollHeight,e.clientHeight]')
    assert page.locator('.lcd-main').bounding_box()['x'] < page.locator('.lcd-analysis').bounding_box()['x']
    for selector in ('.analysis-foot b','.analysis-head','.lcd-title','.lcd-subtitle','.gr'):
        assert page.locator(selector).evaluate_all('(nodes)=>nodes.every(e=>e.scrollWidth<=e.clientWidth+1 || getComputedStyle(e).display==="inline")'), selector
    page.set_viewport_size({'width': 400, 'height': 640})
    assert panel.evaluate('(e)=>e.scrollWidth<=e.clientWidth+1')
    assert page.locator('.dial').count()==3
    assert panel.evaluate('(e)=>e.scrollHeight<=e.clientHeight+1'), panel.evaluate('(e)=>[e.scrollHeight,e.clientHeight]')
    page.set_viewport_size({'width': 800, 'height': 600})
    page.locator('[data-cue="Ess"]').click()
    page.evaluate('''()=>{let req=events.filter(p=>p.action==='nativeDeEzzerCue').at(-1);listeners.arMixBuiltinDeEzzerCue({...req,ok:true,cue:{available:true,requested:'Ess',applied:'Mix'}});}''')
    assert page.locator('[data-cue="Ess"]').get_attribute('data-pending')=='true'
    page.wait_for_timeout(70)
    page.evaluate('''()=>{let req=events.filter(p=>p.action==='nativeDeEzzerSnapshot').at(-1);s={...s,frames:String(BigInt(s.frames)+2400n),cue:{available:true,requested:'Ess',applied:'Ess'}};listeners.arMixBuiltinDeEzzerSnapshot({...req,ok:true,snapshot:s});}''')
    assert page.locator('[data-cue="Ess"]').get_attribute('aria-pressed')=='true'
    if os.environ.get('DE_EZZER_SCREENSHOT'):
        page.locator('#mxNativeDeEzzer').screenshot(path=os.environ['DE_EZZER_SCREENSHOT'])
    page.locator('[data-cue="Mix"]').click()
    assert page.evaluate("events.filter(p=>p.action==='nativeDeEzzerCue').at(-1).mode")=='Mix'
    page.evaluate('view.graph.revision="5"');page.wait_for_timeout(160)
    assert page.locator('[data-status]').inner_text()=='Disconnected'
    assert page.locator('[data-power]').get_attribute('data-fx-state')=='unknown'
    assert chrome_parity('unknown')
    assert page.locator('[data-ess]').inner_text()=='—'
    assert page.locator('[data-history] i').last.evaluate('(e)=>parseFloat(e.style.height)===0')
    assert page.locator('.led-peak').evaluate_all("(nodes)=>nodes.every(e=>getComputedStyle(e).opacity==='0')")
    assert page.locator('.led-fill').evaluate_all("(nodes)=>nodes.every(e=>parseFloat(e.style.getPropertyValue('--lit'))===0)")
    page.locator('[data-close]').click();assert page.locator('#mxNativeDeEzzerHost').is_hidden()
    # Reopen with the real shared preset library (not only the ACK test stub).
    page.add_script_tag(path=str(assets/'mixer-builtin-fx-preset-bar.js'))
    page.evaluate('''()=>{view.graph.revision="6";window.autoSnapshot=false;
      s={...s,id:{instance:'10',epoch:'4'},frames:'0',appliedSequence:'0'};
      ArSonMixNativeDeEzzer.open({nativeId:'42'},0);
      const req=events.filter(p=>p.action==='nativeDeEzzerSnapshot').at(-1);
      listeners.arMixBuiltinDeEzzerSnapshot({...req,ok:true,snapshot:s});
      window.autoSnapshot=true;
    }''')
    assert page.locator('.mx-fxp-save-as').is_visible()
    assert page.locator('.mx-fxp-manage').is_visible()
    assert panel.evaluate('(e)=>e.scrollHeight<=e.clientHeight+1'), panel.evaluate('(e)=>[e.scrollHeight,e.clientHeight]')
    assert panel.evaluate('(e)=>e.scrollWidth<=e.clientWidth+1')
    page.set_viewport_size({'width':400,'height':640})
    assert panel.evaluate('(e)=>e.scrollWidth<=e.clientWidth+1')
    page.set_viewport_size({'width':800,'height':600})
    if os.environ.get('DE_EZZER_SCREENSHOT'):
        panel.screenshot(path=os.environ['DE_EZZER_SCREENSHOT'])
    page.locator('[data-close]').click()
    assert page.locator('#mxNativeDeEzzerHost').is_hidden()
    assert not errors,errors
    print('PASS: live coalesced drag, fine adjustment, cancellation, reset, keyboard, preset ACK/identity, disconnect/close')
    browser.close()
