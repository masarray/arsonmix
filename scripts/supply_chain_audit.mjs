// SPDX-License-Identifier: AGPL-3.0-only
// Read-only, zero dependency source contract. No package downloads or secret values.
import {readFileSync,readdirSync} from 'node:fs';
import {pathToFileURL} from 'node:url';
export function inspectWorkflow(source,juceSha){
  const issues=[],actions=[];
  if(!/^permissions:\s*$/m.test(source))issues.push('missing explicit workflow permissions');
  const lines=source.split(/\r?\n/);
  for(let i=0;i<lines.length;i++){
    const m=lines[i].match(/^\s*(?:-\s*)?uses:\s*(.+?)\s*$/);
    if(m){
      const target=m[1].replace(/\s+#.*$/,'').trim().replace(/^(['"])(.*)\1$/,'$2');
      if(target.startsWith('./'))continue;
      if(!/^[\w.-]+\/[\w.-]+(?:\/[\w.-]+)*@[a-f0-9]{40}$/.test(target))
        issues.push('line '+(i+1)+': non-immutable external action '+target);
      else actions.push(target);
    }
    if(/^\s*repository:\s*juce-framework\/JUCE\s*(?:#.*)?$/.test(lines[i])){
      const n=lines[i].match(/^\s*/)[0].length;
      let ref;
      for(let j=i+1;j<lines.length;j++){
        if(!lines[j].trim()||lines[j].trimStart().startsWith('#'))continue;
        if(lines[j].match(/^\s*/)[0].length!==n)break;
        const r=lines[j].match(/^\s*ref:\s*(\S+)/);
        if(r){ref=r[1];break;}
      }
      if(ref!==juceSha)issues.push('line '+(i+1)+': JUCE checkout does not match inventory commit');
    }
  }
  return {issues,actions};
}
export function audit(root='.'){
  const read=p=>readFileSync(root+'/'+p,'utf8');
  const inventory=JSON.parse(read('config/supply-chain-baseline.json'));
  const juce=inventory.components.find(x=>x.id==='juce');
  const vb=inventory.components.find(x=>x.id==='vb-cable');
  if(!/^[a-f0-9]{40}$/.test(juce?.commit??''))throw Error('Invalid JUCE baseline commit');
  const vbLock=JSON.parse(read('config/vbcable-official-package.json'));
  if(!/^[a-f0-9]{64}$/.test(vb?.sha256??'')||vb.sha256!==vbLock.sha256)throw Error('VB-CABLE lock mismatch');
  const folder='.github/workflows';
  const files=readdirSync(root+'/'+folder).filter(x=>/\.ya?ml$/.test(x)).sort();
  if(!files.length)throw Error('Missing workflows');
  const all=[],issues=[];
  for(const file of files){
    const r=inspectWorkflow(read(folder+'/'+file),juce.commit);
    all.push(...r.actions);
    issues.push(...r.issues.map(x=>file+': '+x));
  }
  return {workflowCount:files.length,actionReferences:all.length,uniquePins:[...new Set(all)].sort(),issues};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href){
  try{
    const r=audit();console.log(JSON.stringify(r,null,2));
    if(r.issues.length)process.exitCode=1;
  }catch(e){console.error(e.message);process.exitCode=1;}
}
