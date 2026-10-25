import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const html=read('web/index.html'),app=read('web/app.js'),css=read('web/styles.css'),bridge=read('web/bridge.js'),monitor=read('web/monitor.js'),cpp=read('src/Main.cpp');
test('native tools open directly without Setup workspace',()=>{
 for(const id of ['audioSetupBtn','arSignalOpen'])assert.equal(html.split('id="'+id+'"').length-1,1);
 for(const s of ['data-native-action="audioSetup"','data-plugin-manager="1"','data-my-sounds="1"'])assert.ok(html.includes(s));
 assert.ok(bridge.includes('if(action==="audioSetup"){send("audioSetup");return;}'));
 assert.ok(!bridge.includes('nativeAudioSetup')&&!bridge.includes('renderSetup'));
 const action=cpp.indexOf('if (action == "audioSetup")');
 const open=cpp.indexOf('showDeviceSetup();',action);
 const nextAction=cpp.indexOf('else if (action == "diagnosticRoute")',action);
 assert.ok(action>=0 && open>action && nextAction>open, 'SETUP opens directly after restoring any temporary audio test route');
 assert.ok(monitor.includes("querySelector('#audioSetupBtn')"));
});
test('demo EQ/Setup code is gone; fader double-click returns unity',()=>{
 for(const name of ['editorGraph','renderEditor','data-activeband','data-band',"setView('EDIT')",'renderSetup'])assert.ok(!app.includes(name),name);
 assert.ok(!html.includes('id="editor"')&&!css.includes('content:"FX CHAIN"'));
 assert.ok(app.includes('updateFader(f.dataset.fader,0)'));
});
test('status bar is informational and not an action rack',()=>{
 const footer=html.match(/<footer[^>]*>[\s\S]*?<\/footer>/)?.[0];
 assert.ok(footer&&!footer.includes('<button'));
 assert.ok(!bridge.includes('nativeP2')&&!bridge.includes('addPluginManagerToolbar'));
 assert.ok(!monitor.includes('insertAdjacentHTML'));
 assert.ok(html.includes('id="arSignalBadge"'));
 assert.ok(bridge.includes('getElementById("headerRate")'));
});
