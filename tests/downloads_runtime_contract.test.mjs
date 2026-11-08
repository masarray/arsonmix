import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

const code = readFileSync(new URL('../site/downloads.js', import.meta.url), 'utf8');
const tag = n => 'v0.5.0-alpha.' + n;
const asset = (t, suffix, bytes=4200000) => {
  const name = 'ArSonMix-' + t + '-' + suffix;
  return { name, size:bytes, state:'uploaded', digest:'sha256:'+'a'.repeat(64),
    browser_download_url:'https://github.com/masarray/arsonmix/releases/download/'+t+'/'+name };
};
const sidecar = (t,name) => ({
  name, size:200, state:'uploaded', digest:'sha256:'+'b'.repeat(64),
  browser_download_url:'https://github.com/masarray/arsonmix/releases/download/'+t+'/'+name
});
const release = (n, files=['Windows-x64-Setup.exe']) => {
  const t=tag(n);
  return { tag_name:t, target_commitish:'f'.repeat(40), prerelease:true,
    published_at:'2026-11-08T02:00:00Z', draft:false,
    assets:[sidecar(t,'SHA256SUMS.txt'),sidecar(t,'INSTALLER_PROVENANCE.json'),
      ...files.map(f=>asset(t,f))] };
};
function local(n, suffix='Windows-x64-Setup.exe') {
  const a=asset(tag(n),suffix);
  return {tag:tag(n),name:a.name,size:a.size,sha256:'a'.repeat(64),
    url:a.browser_download_url,prerelease:true,publishedAt:'2026-11-08T02:00:00Z'};
}
function node() {
  const attr={};
  return {textContent:'',href:undefined,attr,setAttribute(n,v){attr[n]=v;},
    removeAttribute(n){delete attr[n];if(n==='href')delete this.href;}};
}
function card(platform) {
  const nodes={};
  for(const id of ['[data-release-link]','[data-release-state]','[data-link-label]',
                   '[data-release-note]'])nodes[id]=node();
  return {dataset:{platform},classList:{add(){},remove(){}},
    querySelector(sel){if(!nodes[sel])throw Error('bad selector '+sel);return nodes[sel];},
    nodes};
}
async function render(staticCatalog, releases, failLive=false, failStatic=false) {
  const cards=['windows','linux','macos'].map(card),summary=node(),calls=[];
  const fetch=async url=>{
    calls.push(url);
    if(url==='./release-catalog.json'){
      if(failStatic)throw Error('Static catalog unavailable');
      return {ok:true,json:async()=>({schemaVersion:1,
        platforms:{windows:staticCatalog,linux:null,macos:null}})};
    }
    if(!url.startsWith('https://api.github.com/repos/masarray/arsonmix/releases?per_page=40'))
      throw Error('Unexpected fetch '+url);
    if(failLive)throw Error('Rate limit/offline');
    return {ok:true,json:async()=>releases};
  };
  runInNewContext(code,{document:{
    querySelectorAll:()=>cards,getElementById:()=>summary
  },fetch,URL});
  await new Promise(resolve=>setImmediate(resolve));
  return {cards,summary,calls};
}
test('new public ZIP replaces stale Pages installer without a redeploy',async()=>{
  const result=await render(local(4),[release(5,['Windows-x64-Setup.exe','Windows-x64-Setup.zip'])]);
  assert.match(result.cards[0].nodes['[data-release-link]'].href,/alpha\.5-Windows-x64-Setup\.zip$/);
  assert.equal(result.cards[0].nodes['[data-link-label]'].textContent,'Download installer ZIP');
  assert.match(result.summary.textContent,/Checked GitHub Releases/);
  assert.equal(result.calls.length,2);
});
test('GitHub API outage retains an existing verified installer',async()=>{
  const result=await render(local(4),[],true);
  assert.match(result.cards[0].nodes['[data-release-link]'].href,/alpha\.4-Windows-x64-Setup\.exe$/);
});
test('older or malicious asset never displaces newer valid download',async()=>{
  const forged=release(6,['Windows-x64-Setup.zip']);
  forged.assets.find(a=>a.name.endsWith('Setup.zip')).browser_download_url='https://attacker.invalid/file.zip';
  const result=await render(local(5,'Windows-x64-Setup.zip'),[release(3),forged]);
  assert.match(result.cards[0].nodes['[data-release-link]'].href,/alpha\.5-Windows-x64-Setup\.zip$/);
});
test('valid public release recovers when static Pages catalog is unavailable',async()=>{
  const result=await render(null,[release(5,['Windows-x64-Setup.zip'])],false,true);
  assert.match(result.cards[0].nodes['[data-release-link]'].href,/alpha\.5-Windows-x64-Setup\.zip$/);
});
test('no fake Linux/macOS link from a Windows-only release',async()=>{
  const result=await render(null,[release(5)],false,true);
  assert.equal(result.cards[1].nodes['[data-release-link]'].href,undefined);
  assert.equal(result.cards[2].nodes['[data-release-link]'].href,undefined);
  assert.equal(result.cards[1].nodes['[data-release-link]'].attr['aria-disabled'],'true');
});
