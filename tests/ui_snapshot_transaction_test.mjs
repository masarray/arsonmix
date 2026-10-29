// P0 #132: exercise production app.js UI-only storage handlers with a
// deterministic localStorage fault injector. No second CI runner required.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const source=readFileSync('web/app.js','utf8');
const begin=source.indexOf('function durableSession()');
const end=source.indexOf('function exportSession()',begin);
assert.ok(begin>=0&&end>begin,'expected source-owned snapshot implementation');
const code=source.slice(begin,end);

function harness(){
 const records=new Map(),writes=[],messages=[];
 const mk=(i)=>({id:i,name:'CH '+(i+1),icon:'mic',color:'gray',inserts:[],fader:0,pan:0,trim:0,
                mute:false,solo:false,hpf:false,stereo:i>=6});
 const channels=Array.from({length:10},(_,i)=>mk(i));
 const ctx={
  channels,initialFactoryChannels:channels.map(c=>({...c,inserts:[...c.inserts]})),
  sceneIdx:0,scenes:['Vocal Live','Karaoke Warm','Clean Voice','Studio Session'],
  currentChannel:0,fxParams:{},bypassed:{},savedChains:{},userSnapshots:{},
  masterGain:0,masterPan:0,masterMute:false,masterMono:false,masterInserts:[],
  masterBypassed:{},pluginEditorIndex:null,view:'MIX',
  channelIcons:['mic'],channelPalette:['gray'],structuredClone,
  quickPresets:{'Clean Vocal':[],'Warm Karaoke':[],'Live Broadcast':[]},
  clamp:(v,min,max)=>Math.max(min,Math.min(max,v)),
  prompt:()=>ctx.nextName,confirm:()=>true,nextName:'First Snapshot',
  createSide(){},renderMixer(){},renderMaster(){},renderRackWorkspace(){},
  send(){},flash(m){messages.push(m)},
  normalChain:()=>[],getFxParams:()=>({}),fxKey:()=>'',Date,
  blockWrites:false,
  localStorage:{
   setItem(k,v){if(ctx.blockWrites)throw Error('QuotaExceededError');writes.push(k);records.set(k,v)},
   getItem:k=>records.get(k)||null
  }
 };
 vm.createContext(ctx);
 vm.runInContext(code,ctx,{filename:'web/app.js:snapshot'});
 return {ctx,records,writes,messages};
}

test('P0 #132 snapshot save commits one versioned key and no split storage writes',()=>{
 const {ctx,records,writes}=harness();
 assert.equal(ctx.saveMixerSnapshot(),true);
 assert.equal(ctx.sceneIdx,4);
 assert.equal(ctx.scenes.length,5);
 assert.deepEqual(writes,['armix-v03-workspace-v2']);
 const saved=JSON.parse(records.get(writes[0]));
 assert.equal(saved.schemaVersion,2);
 assert.equal(saved.current.sceneIdx,4);
 assert.ok(saved.snapshots['First Snapshot']);
 assert.deepEqual(Object.keys(saved.chains),[]);
});

test('P0 #132 blocked storage cannot mutate in-memory snapshot list/index or false-success',()=>{
 const {ctx,writes,messages}=harness();
 ctx.blockWrites=true;
 const was=JSON.stringify({snapshots:ctx.userSnapshots,scenes:ctx.scenes,index:ctx.sceneIdx});
 assert.equal(ctx.saveMixerSnapshot(),false);
 assert.equal(JSON.stringify({snapshots:ctx.userSnapshots,scenes:ctx.scenes,index:ctx.sceneIdx}),was);
 assert.equal(writes.length,0);
 assert.ok(messages.some(m=>m.includes('NOT saved')));
});

test('P0 #132 deletion failure preserves durable snapshot and selected scene',()=>{
 const {ctx,records}=harness();
 assert.equal(ctx.saveMixerSnapshot(),true);
 const prior=records.get('armix-v03-workspace-v2');
 ctx.blockWrites=true;
 assert.equal(ctx.deleteMixerSnapshot('First Snapshot'),false);
 assert.equal(ctx.sceneIdx,4);
 assert.ok(ctx.userSnapshots['First Snapshot']);
 assert.ok(ctx.scenes.includes('First Snapshot'));
 assert.equal(records.get('armix-v03-workspace-v2'),prior);
});

test('P0 #132 corrupted snapshot cannot select scene, alter channel, or persist',()=>{
 const {ctx,writes}=harness();
 const s=ctx.durableSession();
 s.channels[3]=null;
 ctx.userSnapshots.Bad=s;
 ctx.scenes.push('Bad');
 const original=ctx.channels[0].fader;
 assert.equal(ctx.loadSnapshotByIndex(4),false);
 assert.equal(ctx.sceneIdx,0);
 assert.equal(ctx.channels[0].fader,original);
 assert.equal(writes.length,0);
 assert.equal(ctx.applySession(s),false);
});

test('P0 #132 snapshot recall write rejection retains previous scene and channel',()=>{
 const {ctx,records}=harness();
 const s=ctx.durableSession();
 s.channels[0].fader=-24;
 ctx.userSnapshots.Quiet=s;ctx.scenes.push('Quiet');
 ctx.blockWrites=true;
 assert.equal(ctx.loadSnapshotByIndex(4),false);
 assert.equal(ctx.sceneIdx,0);
 assert.equal(ctx.channels[0].fader,0);
 assert.equal(records.size,0);
 ctx.blockWrites=false;
 assert.equal(ctx.loadSnapshotByIndex(4),true);
 assert.equal(ctx.sceneIdx,4);
 assert.equal(ctx.channels[0].fader,-24);
 assert.equal(JSON.parse(records.get('armix-v03-workspace-v2')).current.channels[0].fader,-24);
});

test('P0 #132 invalid snapshot names and FX chain storage failure are fail-closed',()=>{
 const {ctx,writes}=harness();
 for(const name of ['<script>','__proto__','constructor','   ']){
   ctx.nextName=name;
   assert.equal(ctx.saveMixerSnapshot(),false);
 }
 assert.equal(writes.length,0);
 ctx.nextName='My Chain';ctx.blockWrites=true;
 ctx.saveFxChainPreset();
 assert.equal(Object.keys(ctx.savedChains).length,0);
 assert.equal(writes.length,0);
});

test('P0 #132 no old multi-key writes or browser-only state portrayed as native Show',()=>{
 assert.ok(source.includes("localStorage.setItem(UI_STORAGE_KEY,payload)"));
 assert.ok(source.includes("localStorage.getItem('armix-v03-current')"));
 assert.ok(source.includes("localStorage.getItem('armix-v03-snapshots')"));
 assert.ok(!source.includes("localStorage.setItem('armix-v03-current'"));
 assert.ok(!source.includes("localStorage.setItem('armix-v03-chains'"));
 assert.ok(source.includes('needsLegacyMigration'));
 assert.ok(source.includes('native VST3 state unchanged'));
});
