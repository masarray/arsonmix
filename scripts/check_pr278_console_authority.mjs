// SPDX-License-Identifier: AGPL-3.0-only
// PR #278 approved visual contract; NEVER an audio/PCM certification.
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
export const DESIGN_REFERENCE={pr:278,sha:'c849847d42c78cf9aaf8d2d41ed667805e91921d',
 gitBlob:'e9aa131fbf984cad443e4018f4d71fbd426260a9'};
function rule(css,key){const i=css.indexOf(key);if(i<0)return '';
 const a=css.indexOf('{',i),b=css.indexOf('}',a+1);
 return a>=0&&b>=0?css.slice(a+1,b):'';}
export function inspectPr278(x={}){
 const js=x.js||'',css=x.css||'',ref=x.reference||'',old=x.legacy||'',test=x.test||'';
 const strip=rule(css,'#app .arV6Strip{'),main=rule(css,'#app .arV6Main{');
 const top=rule(css,'#app .arV6Top{'),head=rule(css,'#app .arV6StripHead{');
 const foot=rule(css,'#app .arV6StripStatus{');
 const checks=[];
 const add=(id,ok,action)=>checks.push({id,pass:!!ok,action});
 add('REFERENCE_278',ref.includes('id="tracks"')&&ref.includes('id="master"')&&ref.includes('id="pluginBrowser"'),'Include pinned #278 design study in integration source only');
 add('ONE_CONSOLE',js.includes("root.id='arV6Console'")&&js.includes("main.id='arV6Main'"),'Single approved Console, pinned MAIN');
 add('DIRECT_NATIVE_IO',js.includes("selectField('IN','SET_SOURCE'")&&js.includes("selectField('OUT','SET_PRIMARY_OUT'")&&js.includes('fxSection(ch)')&&js.includes('sendSection(ch)'),'Keep direct IN/OUT/FX/sends on each channel');
 add('NATIVE_ACK_NO_DEMO',js.includes('arMixConsoleAck')&&js.includes('arMixConsoleSnapshot')&&js.includes('supportedOps')&&js.includes('productionCallbackLive===true'),'Use actual native accepted snapshot and PCM only');
 const identityColor=js.includes("elStrip.style.setProperty('--ch'")||js.includes("elStrip.style.setProperty('--v6-strip-color'");
 const colorHead=head.includes('var(--ch')||head.includes('var(--v6-strip-color');
 const colorFoot=foot.includes('var(--ch')||foot.includes('var(--v6-strip-color');
 add('TOP_BOTTOM_COLOR_ICON',identityColor&&(js.includes('ch.icon')||js.includes('channelIcons'))&&colorHead&&colorFoot,'Port #278 exact channel name/color/icon at both top and bottom');
 const material=css.includes('--am-metal-panel:#14232C');
 const stripMetal=strip.includes('var(--am-metal-panel)')||strip.includes('var(--am-metal-base)');
 const topMetal=top.includes('var(--am-metal-panel)')||top.includes('var(--am-metal-raised)');
 add('ONE_GRAPHITE_CHASSIS',material&&stripMetal&&topMetal,'Use same #278 graphite metal material above and below fader');
 add('BTN180_HEADER',js.includes('btn-180')&&css.includes('#app .arV6Top .arV6Button.btn-180'),'Use original brighter-off seven-shadow BTN-180 in universal header');
 const stripWidth=strip.includes('var(--channel-w)')||strip.includes('var(--v6-channel-w)');
 const mainWidth=main.includes('var(--channel-w)')||main.includes('var(--v6-channel-w)');
 add('PINNED_MAIN_EQUAL_WIDTH',stripWidth&&mainWidth,'One shared computed width for source and MAIN strip at all viewports');
 add('PAN_CUE',js.includes('paintPan(')&&js.includes('--v6-pan-start')&&js.includes("'SET_CUE'"),'Center-detent pan and native-only Cue/PFL');
 add('LINK_VS_GROUP',js.includes("button('LINK FADERS'")&&js.includes("button('ROUTE TO GROUP BUS'"),'Separate fader-link control from serial Group DSP');
 add('MODELLESS_NATIVE_FX',js.includes('arV6PluginPanel')&&js.includes('arV6QuickPanel')&&js.includes('Search native scanned plugins'),'F8 modeless plugin manager must use actual VST3 inventory');
 add('OLD_PALETTE',old.includes('channelIcons=')&&old.includes('channelPalette='),'Preserve legacy 12 channel colors/icons');
 add('NATIVE_CONSUMER_TEST',test.includes('console-v8-native-unready.json')&&test.includes('arMixConsoleAck'),'Visual UI also requires native fixture/ACK regression');
 return {reference:DESIGN_REFERENCE,sourceOnly:true,audioCertified:false,
  checks,blocked:checks.filter(c=>!c.pass)};
}
const base=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>{try{return readFileSync(resolve(base,p),'utf8')}catch{return ''}};
export function inspectCheckout(){return inspectPr278({
 reference:read('design-references/audition-mixer-study/index.html'),
 js:read('web/mixer-layout.js'),css:read('web/mixer-layout.css'),
 legacy:read('web/app.js'),test:read('tests/operator_console_v6_smoke.py')});}
if(process.argv[1]&&fileURLToPath(import.meta.url)===resolve(process.argv[1])){
 const v=inspectCheckout();
 for(const c of v.checks)console.log((c.pass?'MATCH ':'BLOCK ')+c.id+(c.pass?'':' -> '+c.action));
 if(process.argv.includes('--release')&&v.blocked.length)process.exitCode=1;
}
