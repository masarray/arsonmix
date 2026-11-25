#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Optional Chromium fixture: real editor and host JS, injected backend (not PCM)."""
from pathlib import Path
from playwright.sync_api import sync_playwright

assets = Path(__file__).resolve().parents[2] / 'site/preview-v2/assets'
with sync_playwright() as pw:
    browser = pw.chromium.launch(executable_path='/usr/bin/chromium', args=['--no-sandbox'])
    page = browser.new_page(viewport={'width': 800, 'height': 600})
    errors = []
    page.on('pageerror', lambda e: errors.append(str(e)))
    page.set_content('<body style="background:#151718"></body>')
    page.evaluate('''()=>{
      window.listeners={};window.events=[];window.crypto.randomUUID ||= ()=>String(Math.random()).slice(2);
      window.view={admitted:true,device:{epoch:'12'},graph:{revision:'4',channels:[{id:'42',inserts:[{id:'fx-a',catalogId:'armix.builtin.de-ezzer.v1'}]}]}};
      window.__ARMIX_NATIVE_M2__={fxEditorState:()=>view,nativeIdFor:t=>t.nativeId};
      window.__JUCE__={backend:{emitEvent:(n,p)=>events.push(p),addEventListener:(n,cb)=>listeners[n]=cb}};
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
    box = dial.bounding_box(); x=box['x']+box['width']/2; y=box['y']+box['height']/2
    page.mouse.move(x,y);page.mouse.down();page.mouse.move(x,y-32)
    assert page.evaluate('commands().length') == 1 # must apply before mouseup
    assert output.inner_text() == '50%' # no optimistic readback
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
    page.locator('input[aria-label="AMOUNT"]').focus();page.keyboard.press('ArrowRight');page.evaluate('ack()');assert output.inner_text()=='51%'
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
    page.evaluate('''()=>{let p={...adapter.context(),requestId:'fxpres-1'};delete p.slot;listeners.arMixBuiltinFxPresetResult({...p,instanceId:'other'});listeners.arMixBuiltinFxPresetResult(p);}''')
    assert page.evaluate('presetCalls.filter(x=>x[0]==="reply").length')==1
    page.evaluate('view.graph.revision="5"');page.wait_for_timeout(160)
    assert page.locator('[data-status]').inner_text()=='Disconnected'
    page.locator('[data-close]').click();assert page.locator('#mxNativeDeEzzerHost').is_hidden()
    assert not errors,errors
    print('PASS: live coalesced drag, fine adjustment, cancellation, reset, keyboard, preset ACK/identity, disconnect/close')
    browser.close()
