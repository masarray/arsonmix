#!/usr/bin/env python3
"""V34 Smart WIRE: non-destructive multipage navigator, bus mixing and matrix."""
from pathlib import Path
from playwright.sync_api import sync_playwright
PAGE=Path(__file__).with_name('index.html')

def must(value,message):
    if not value: raise AssertionError(message)

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={'width':width,'height':height})
            errors=[]
            page.on('pageerror',lambda e:errors.append(str(e)))
            page.goto(PAGE.as_uri())
            rail=page.locator('.section-rail [data-section="send"]')
            rail.click()
            ch=lambda n:page.locator(f'#tracks .strip[data-id="{n}"]')
            route=lambda a,b:page.locator(f'#wireLayer path[data-from="{a}"][data-to="{b}"]')
            panel=page.locator('#wireWorkspace')
            must(panel.is_hidden(),f'{width}: workspace must not disturb normal mixer')
            initial=ch(3).locator('.wire-node-in').bounding_box()
            page.locator('#wireNavigatorToggle').click()
            must(panel.is_visible(),f'{width}: ROUTES button did not open workspace')
            must(panel.locator('[data-ws-mode="find"]').get_attribute('aria-selected')=='true',
                 f'{width}: FIND must be default')
            page.locator('#wireWsSourceSelect').select_option('1')
            must('CH01' in page.locator('#wireWsSource').inner_text(),
                 f'{width}: source OUT not pinned in navigator')
            page.locator('#wireWsSearch').fill('Bus A')
            rows=page.locator('#wireWsList .wire-ws-row')
            must(rows.count()==1 and 'CH11' in rows.first.inner_text(),
                 f'{width}: global destination search did not find off-bank Bus A')
            must('Bank 2' in rows.first.inner_text(),f'{width}: wrong bank location')
            rows.first.locator('[data-ws-inspect]').click()
            must('LEGACY' in page.locator('#wireWsInspector').inner_text().upper(),
                 f'{width}: existing CH01→CH11 legacy SEND not read-only')
            must(page.locator('#wireWsInspector [data-ws-remove]').count()==0,
                 f'{width}: duplicate legacy route exposed disconnect')
            page.locator('#wireWsSourceSelect').select_option('5')
            must(ch(5).count()==1 and ch(11).count()==0,
                 f'{width}: source selection changed current mixer bank')
            page.locator('#wireWsList [data-ws-join="5:11"]').click()
            must(page.locator('#routeChoice').is_visible(),
                 f'{width}: remote BUS choice must not be silently bypassed')
            page.locator('#routeParallel').click()
            must(not page.locator('#routeChoice').is_visible(),
                 f'{width}: parallel Bus choice did not close')
            must(ch(11).count()==0 and ch(5).count()==1,
                 f'{width}: remote patch forcibly switched mixer page')
            must('CH05' in page.locator('#wireWsSource').inner_text(),
                 f'{width}: committed source not retained')
            page.locator('[data-ws-mode="routes"]').click()
            must(page.locator('#wireWsList [data-ws-inspect="5:11"]').count()==1,
                 f'{width}: outgoing remote route missing')
            page.locator('#wireWsList [data-ws-inspect="5:11"]').click()
            must(page.locator('#wireWsInspector [data-ws-gain]').count()==1,
                 f'{width}: remote Route Inspector cannot edit real WIRE edge')
            page.locator('#wireWsGain').fill('-12')
            page.locator('#wireWsTap').select_option('split')
            must(page.locator('#wireWsGain').input_value()=='-12' and
                 page.locator('#wireWsTap').input_value()=='split',
                 f'{width}: editable gain/tap was not preserved')
            # Destination jump must show a real receiver and off-bank source portal.
            page.locator('#wireWsInspector [data-ws-jump="11"]').click()
            must(ch(11).count()==1 and ch(5).count()==0,
                 f'{width}: jump to destination did not open remote bank')
            must('FROM CH05' in page.locator('#wireLegend').inner_text(),
                 f'{width}: source lost during remote-bank jump')
            must(ch(11).locator('.wire-node-in').get_attribute('aria-pressed')=='true',
                 f'{width}: remote receiver did not show committed route')
            must(route(5,11).count()>=1,
                 f'{width}: cross-bank edge lacks entry portal in focused SVG')
            page.locator('#wireWsInspector [data-ws-jump="5"]').click()
            must(ch(5).count()==1 and ch(11).count()==0,
                 f'{width}: jump back to source failed')
            # Matrix is same graph, only a different view. Use one crosspoint.
            page.locator('#wireMatrixToggle').click()
            must('is-matrix' in panel.get_attribute('class') and panel.is_visible(),
                 f'{width}: matrix not accessible')
            matrix=page.locator('#wireWsList .wire-ws-matrix')
            must(matrix.count()==1 and matrix.locator('button').count()>0,
                 f'{width}: global matrix missing crosspoints')
            must(matrix.locator('[data-ws-inspect="5:11"]').count()==1,
                 f'{width}: matrix does not share existing remote wire graph')
            # TO BUS destination-first: choose CH06 as source without bank navigation.
            page.locator('[data-ws-mode="bus"]').click()
            page.locator('#wireWsSearch').fill('CH06')
            page.locator('#wireWsBus').select_option('11')
            join=page.locator('#wireWsList [data-ws-join="6:11"]')
            must(join.count()==1,f'{width}: destination-first Bus send unavailable')
            join.click()
            must(page.locator('#routeChoice').is_visible(),
                 f'{width}: destination-first bypassed Bus choice')
            page.locator('#routeParallel').click()
            must(page.locator('#wireWsList [data-ws-inspect="6:11"]').count()==1,
                 f'{width}: Bus send not shown connected')
            page.locator('[data-ws-mode="routes"]').click()
            page.locator('#wireWsSourceSelect').select_option('5')
            page.locator('#wireWsList [data-ws-inspect="5:11"]').click()
            page.locator('#wireWsInspector [data-ws-remove="5:11"]').click()
            must('Confirm remove' in page.locator('#wireWsInspector').inner_text(),
                 f'{width}: disconnect needs two-step confirmation')
            page.locator('#wireWsInspector [data-ws-remove="5:11"]').click()
            must(page.locator('#wireWsList [data-ws-inspect="5:11"]').count()==0,
                 f'{width}: targeted disconnect did not remove edge')
            page.locator('#wireWsSourceSelect').select_option('6')
            must(page.locator('#wireWsList [data-ws-inspect="6:11"]').count()==1,
                 f'{width}: disconnect damaged independent CH06→Bus A route')
            page.locator('#wireWsClose').click()
            must(panel.is_hidden(),f'{width}: close leaked persistent panel')
            after=ch(3).locator('.wire-node-in').bounding_box()
            must(all(abs(initial[k]-after[k])<=1 for k in ('x','y','width','height')),
                 f'{width}: opening WIRE workspace shifted mixer geometry: {initial} -> {after}')
            if width==1680:
                page.locator('#wireMatrixToggle').click()
                page.screenshot(path=str(PAGE.with_name('preview-v34-global-matrix-1680.png')))
            must(not errors,f'{width}: uncaught browser exceptions: {errors}')
            page.close()
        browser.close()
    print('PASS V34 finder, remote CH05->CH11 inspector, portal, Bus mix, matrix and isolated disconnect on 3 screens')

if __name__=='__main__':main()
