// SPDX-License-Identifier: AGPL-3.0-only
// Only select real test suites. No PR owner, labels, collision or GitHub API.
import {execFileSync} from 'node:child_process';
import {appendFileSync} from 'node:fs';
import {requiresUiSmoke,requiresNativeQualification} from './check_workstream_coordination.mjs';
const base=process.argv[2];
if(!base || !/^[a-f0-9]{40}$/.test(base))
  throw Error('Expected exact Git base SHA');
const changed=execFileSync('git',
  ['diff','--name-only','--diff-filter=ACMRT',base,'HEAD'],
  {encoding:'utf8',maxBuffer:8*1024*1024})
  .split(/\r?\n/).filter(Boolean);
const ui=requiresUiSmoke(changed),native=requiresNativeQualification(changed);
if(process.env.GITHUB_OUTPUT)
  appendFileSync(process.env.GITHUB_OUTPUT,
    'run_ui='+(ui?'true':'false')+'\n'+
    'run_native='+(native?'true':'false')+'\n');
console.log('Changed '+changed.length+' paths: Chromium '+ui+', Windows CTest '+native);
