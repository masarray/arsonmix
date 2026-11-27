"""Offline real-DOM A/B profiler for WIRE readback. No audio/hardware claims."""
import json
import os
import statistics
import subprocess
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
BASE = "d0d40ace87ce9d122f81b05d768af1f4e6a41cbe"
PATH = "web/approved-native-bridge.js"

def extract(text):
    start = text.index("  function paintNativeMeters(next) {")
    end = text.index("    if (!scope?.displayMeterDbfs)return;", start)
    return text[start:end] + "  }"

def main():
    before = extract(subprocess.check_output(
        ["git", "show", BASE + ":" + PATH], cwd=ROOT, text=True, encoding="utf-8"))
    after = extract((ROOT / PATH).read_text(encoding="utf-8"))
    with sync_playwright() as p:
        executable = os.environ.get("CHROMIUM_EXECUTABLE")
        browser = p.chromium.launch(**({"executable_path": executable} if executable else {}))
        print("Chromium", browser.version)
        page = browser.new_page()
        data = page.evaluate("""({before,after})=>{
          const runs=[];
          for(const [edges,visible] of [[12,0],[48,12],[96,24]]){
            document.body.replaceChildren();
            for(let i=0;i<24;i++){
              const strip=document.createElement('section');
              for(let j=0;j<50;j++)strip.append(document.createElement('span'));
              if(i<visible){
                const row=document.createElement('div'),input=document.createElement('input'),label=document.createElement('span');
                input.className='wire-gain';input.dataset.wireId=String(i+1);
                label.className='wire-gain-caption';row.append(input,label);strip.append(row);
              }
              document.body.append(strip);
            }
            const uiByWireId=new Map(Array.from({length:edges},(_,i)=>[String(i),i+1]));
            const graph={edges:Array.from({length:edges},(_,i)=>({edgeId:String(i),primaryOutput:i%2===0})),channels:[]};
            const f=[before,after].map(src=>new Function('uiByWireId',src+'\\nreturn paintNativeMeters')(uiByWireId));
            const read=()=>Array.from(document.querySelectorAll('.wire-gain')).map(e=>[e.disabled,e.title,e.getAttribute('aria-label'),e.parentElement.className,e.nextElementSibling.textContent]);
            f[0]({graph,retained:true});const old=JSON.stringify(read());
            f[1]({graph,retained:false});f[1]({graph,retained:true});
            if(old!==JSON.stringify(read()))throw Error('retained parity');
            f[0]({graph,retained:false});const live=JSON.stringify(read());
            f[1]({graph,retained:true});f[1]({graph,retained:false});
            if(live!==JSON.stringify(read()))throw Error('live parity');
            const state={graph,retained:false};
            for(const fn of f)for(let i=0;i<200;i++)fn(state);
            const samples=[[],[]];
            for(let trial=0;trial<9;trial++)for(const mode of trial%2?[1,0]:[0,1]){
              const t=performance.now();
              for(let i=0;i<1000;i++)f[mode](state);
              samples[mode].push((performance.now()-t)/1000);
            }
            const mutations=f.map(fn=>{
              const m=new MutationObserver(()=>{});
              m.observe(document.body,{attributes:true,subtree:true});
              for(let i=0;i<120;i++)fn(state);
              const records=m.takeRecords();m.disconnect();
              return records.filter(x=>x.attributeName==='aria-label').length;
            });
            if(mutations[0]!==visible*120||mutations[1]!==0)throw Error('mutation regression');
            runs.push({edges,visible,samples,mutations,parity:true});
          }
          return runs;
        }""", {"before": before, "after": after})
        for row in data:
            med = [statistics.median(s)*1000 for s in row["samples"]]
            print(json.dumps({"edges": row["edges"], "visible": row["visible"],
                "before_us": round(med[0],2), "after_us": round(med[1],2),
                "ratio": round(med[0]/med[1],2) if med[1]>0 else None,
                "aria_mutations": row["mutations"], "parity": row["parity"],
                "samples_us": [[round(v*1000,2) for v in s] for s in row["samples"]]}))
        browser.close()

if __name__ == "__main__":
    main()
