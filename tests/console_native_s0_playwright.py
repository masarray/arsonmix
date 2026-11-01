"""S0 Playwright consumer: SAME C++-generated native JSON used by FAT validator."""
import argparse,asyncio,json
from pathlib import Path
from playwright.async_api import async_playwright
ROOT=Path(__file__).resolve().parents[1]
async def check(f):
    ev=json.loads(Path(f).read_text(encoding='utf8'))['events']
    assert [e['event'] for e in ev]==['arMixConsoleSnapshot','arMixConsoleIntent','arMixConsoleAck','arMixConsoleSnapshot']
    s,cmd,ack,n=[e['payload'] for e in ev]
    assert s==n and ack['status']=='REJECTED' and not s['productionCallbackLive']
    async with async_playwright() as p:
        b=await p.chromium.launch(headless=True)
        page=await b.new_page(viewport={'width':1366,'height':768})
        await page.add_init_script("""(() => {
          const listeners={},out=[];
          window.__s0={out,publish:(e,p)=>(listeners[e]||[]).forEach(fn=>fn(p))};
          window.__JUCE__={backend:{emitEvent:(e,p)=>out.push({event:e,payload:p}),
            addEventListener:(e,fn)=>(listeners[e]||=[]).push(fn)}};
        })()""")
        await page.goto((ROOT/'web/index.html').as_uri())
        assert await page.evaluate("()=>window.__s0.out.some(x=>x.event==='arMixConsoleSubscribe')")
        await page.evaluate("x=>window.__s0.publish('arMixConsoleSnapshot',x)",s)
        state=await page.evaluate("()=>window.__arMixV6ReadOnly()")
        assert state['nativePublisher'] is True and state['revision']==s['graph']['revision']
        assert state['visibleChannelIds']==[], 'GUI manufactured S0 ghost channels'
        assert await page.locator('#arV6Console').is_hidden(), 'GUI promoted G0 as LIVE'
        await page.evaluate("x=>window.__s0.publish('arMixConsoleAck',x)",ack)
        await page.evaluate("x=>window.__s0.publish('arMixConsoleSnapshot',x)",n)
        after=await page.evaluate("()=>window.__arMixV6ReadOnly()")
        assert after['revision']==state['revision'] and after['pendingCount']==0
        await b.close()
    print('S0_CPP_TO_GUI_SCHEMA_PASS_NOT_PCM')
if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--fixture',required=True)
    args=p.parse_args()
    asyncio.run(check(args.fixture))
