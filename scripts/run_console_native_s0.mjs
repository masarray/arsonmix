// SPDX-License-Identifier: AGPL-3.0-only
// S0: only the actual C++ emitter may create the source fixture.
// The single canonical schema implementation is PR #279 puzzle-seam.
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {snapshot,intent,ack} from '../integration/console-v6/puzzle-seam.mjs';
const block=s=>{throw Error('S0 BLOCKED: '+s)};
const req=(x,s)=>{if(!x)block(s)};
export function verifyNativeS0(events){
 req(Array.isArray(events)&&events.length===4,'C++ source must emit 4 events');
 const order=['arMixConsoleSnapshot','arMixConsoleIntent','arMixConsoleAck','arMixConsoleSnapshot'];
 events.forEach((e,i)=>req(e?.event===order[i]&&e.payload,
  'native event '+i+' expected '+order[i]));
 const [s,cmd,reject,next]=events.map(e=>e.payload);
 snapshot(s);snapshot(next);intent(cmd);ack(reject,cmd,s,next);
 req(!s.productionCallbackLive&&!next.productionCallbackLive&&
  !s.graph.channels.length&&!s.graph.capabilities.supportedOps.length,
  'G0 cannot declare installed ChannelId callback');
 req(cmd.deviceEpoch===s.device.epoch&&
  cmd.baseGraphRevision===s.graph.revision&&
  cmd.baseRosterRevision===s.graph.rosterRevision,
  'operator request not based on native accepted state');
 req(reject.status==='REJECTED'&&reject.errorCode&&
  reject.graphRevision===s.graph.revision&&
  reject.rosterRevision===s.graph.rosterRevision,'native ACK must reject and preserve revision');
 req(JSON.stringify(s.graph)===JSON.stringify(next.graph)&&
   s.device.epoch===next.device.epoch&&
   JSON.stringify(s.device.enabledInputs)===JSON.stringify(next.device.enabledInputs)&&
   JSON.stringify(s.device.enabledOutputs)===JSON.stringify(next.device.enabledOutputs)&&
   JSON.stringify(s.device.inputEndpoints)===JSON.stringify(next.device.inputEndpoints)&&
   JSON.stringify(s.device.outputEndpoints)===JSON.stringify(next.device.outputEndpoints)&&
   s.device.outputArmed===next.device.outputArmed,
  'rejected native intent changed accepted graph/device/output ARM');
 return {gate:'S0_CXX_SOURCE_SOCKET',callbackPcmProven:false,
  deviceEpoch:s.device.epoch,revision:s.graph.revision,
  requestId:cmd.requestId};
}
export function generateS0({generator,output,sourceSha}){
 req(/^[a-f0-9]{40}$/.test(sourceSha||''),'full source SHA required');
 req(generator&&output,'C++ exporter and single output JSON required');
 const buf=execFileSync(resolve(generator),['--emit-console-s0-json'],
  {encoding:null,maxBuffer:8*1024*1024,timeout:15000,windowsHide:true});
 let data;try{data=JSON.parse(buf.toString('utf8'))}
 catch{block('C++ stdout must contain a single JSON object')}
 const status=verifyNativeS0(data.events);
 writeFileSync(resolve(output),buf);
 const sha=createHash('sha256').update(buf).digest('hex');
 req(createHash('sha256').update(readFileSync(resolve(output))).digest('hex')===sha,
  'native fixture bytes changed');
 return {...status,sourceSha,nativeFixtureSha256:sha,
  path:resolve(output),cppGenerator:resolve(generator),uatReady:false};
}
if(process.argv[1]&&fileURLToPath(import.meta.url)===resolve(process.argv[1])){
 const argv=process.argv.slice(2),a=x=>argv.includes(x)?argv[argv.indexOf(x)+1]:undefined;
 try{
  const actual=execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim();
  req(a('--source-sha')===actual,'generator build source SHA must equal current git HEAD');
  console.log(JSON.stringify(generateS0({generator:a('--generator'),
   output:a('--output'),sourceSha:actual}),null,2));
 }catch(e){console.error(e.message);process.exitCode=1}
}
