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
      ...(files.includes('Linux-x86_64.tar.gz')?[sidecar(t,'LINUX_PROVENANCE.json')]:[]),
      ...(files.includes('macOS-universal.dmg')?[sidecar(t,'MACOS_PROVENANCE.json')]:[]),
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
  const cards=['windows','linux','macos'].map(card),summary=node(),hero=node(),calls=[];
  hero.href='#download';
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
    querySelectorAll:()=>cards,
    getElementById:id=>id==='release-summary' ? summary : id==='hero-download' ? hero : null
  },navigator:{platform:'Win32'},fetch,URL});
  await new Promise(resolve=>setImmediate(resolve));
  return {cards,summary,hero,calls};
}
test('current Windows installer ZIP replaces stale Pages version without redeploy',async()=>{
  const result=await render(local(4),[release(5,['Windows-x64-Setup.exe','Windows-x64-Setup.zip'])]);
  assert.match(result.cards[0].nodes['[data-release-link]'].href,/alpha\.5-Windows-x64-Setup\.zip$/);
  assert.equal(result.cards[0].nodes['[data-link-label]'].textContent,'Download installer ZIP');
  assert.match(result.summary.textContent,/Ready to download/);
  assert.equal(result.hero.href,'#download');
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

test('hero never auto-downloads a platform even when Windows is detected', async()=>{
  const result=await render(null,[],false,true);
  assert.equal(result.hero.href,'#download');
  assert.match(result.summary.textContent,/Downloads are being prepared/);
});

test('three actual OS artifacts expose separate download buttons without automatic platform redirect',async()=>{
  const result=await render(local(5,'Windows-x64-Setup.zip'),[
    release(5,['Windows-x64-Setup.zip','Linux-x86_64.tar.gz','macOS-universal.dmg'])
  ]);
  const links=result.cards.map(c=>c.nodes['[data-release-link]'].href);
  assert.match(links[0],/Windows-x64-Setup\.zip$/);
  assert.match(links[1],/Linux-x86_64\.tar\.gz$/);
  assert.match(links[2],/macOS-universal\.dmg$/);
  assert.notEqual(links[0],links[1]);
  assert.notEqual(links[1],links[2]);
  assert.equal(result.hero.href,'#download');
  assert.match(result.summary.textContent,/Windows v0\.5\.0-alpha\.5 · Linux v0\.5\.0-alpha\.5 · macOS v0\.5\.0-alpha\.5/);
});

test('older platform builds show their own version instead of claiming Windows parity', async()=>{
  const result=await render(local(9),[release(9),release(5,['Linux-x86_64.tar.gz','macOS-universal.dmg'])]);
  assert.match(result.summary.textContent,/Windows v0\.5\.0-alpha\.9/);
  assert.match(result.summary.textContent,/Linux v0\.5\.0-alpha\.5/);
  assert.match(result.summary.textContent,/macOS v0\.5\.0-alpha\.5/);
});

// Latest public Windows release must win even if same-origin Pages has cached a
// previous version. Different OS releases remain on their own verified versions.
test('alpha.23 Windows Setup ZIP supersedes older static and live releases',async()=>{
  const newer=release(23,['Windows-x64-Setup.exe','Windows-x64-Setup.zip']);
  const obsolete=release(22,['Windows-x64-Setup.zip']);
  const draft=release(24,['Windows-x64-Setup.zip']);draft.draft=true;
  const linux=release(5,['Linux-x86_64.tar.gz','macOS-universal.dmg']);
  const result=await render(local(22,'Windows-x64-Setup.zip'),[obsolete,newer,draft,linux]);
  const windows=result.cards[0].nodes;
  assert.match(windows['[data-release-link]'].href,/v0\.5\.0-alpha\.23\/ArSonMix-v0\.5\.0-alpha\.23-Windows-x64-Setup\.zip$/);
  assert.equal(windows['[data-link-label]'].textContent,'Download installer ZIP');
  assert.match(windows['[data-release-note]'].textContent,/alpha\.23/);
  assert.match(result.summary.textContent,/Windows v0\.5\.0-alpha\.23/);
  assert.match(result.summary.textContent,/Linux v0\.5\.0-alpha\.5/);
  assert.match(result.summary.textContent,/macOS v0\.5\.0-alpha\.5/);
  assert.equal(result.hero.href,'#download');
});
