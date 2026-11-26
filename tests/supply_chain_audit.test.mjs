// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {inspectWorkflow,audit} from '../scripts/supply_chain_audit.mjs';
const sha='a'.repeat(40);
const base='permissions:\n  contents: read\njobs:\n  test:\n    steps:\n';
test('immutable external action and local action allowed',()=>{
  const r=inspectWorkflow(base+'      - uses: actions/checkout@'+sha+' # v4\n      - uses: ./.github/actions/local\n',sha);
  assert.equal(r.issues.length,0);assert.equal(r.actions.length,1);
});
test('mutable tag, branch, short SHA, docker and dynamic action ref are rejected',()=>{
  for(const ref of ['actions/checkout@v4','actions/checkout@main','actions/checkout@abc123','docker://example:latest','actions/checkout@'+'$'+'{{ inputs.ref }}']){
    assert.equal(inspectWorkflow(base+'      - uses: '+ref+'\n',sha).issues.length,1,ref);
  }
});
test('missing explicit permission and wrong JUCE version rejected',()=>{
  assert.ok(inspectWorkflow('jobs:\n',sha).issues.length);
  const text=base+'      - uses: actions/checkout@'+sha+'\n        with:\n          repository: juce-framework/JUCE\n          ref: ';
  assert.equal(inspectWorkflow(text+'8.0.15\n',sha).issues.length,1);
  assert.equal(inspectWorkflow(text+sha+' # JUCE 8.0.15\n',sha).issues.length,0);
});
test('all current workflows and locked vendor hash satisfy source contract',()=>{
  const r=audit();assert.deepEqual(r.issues,[]);assert.ok(r.workflowCount>0);assert.ok(r.actionReferences>0);
});
