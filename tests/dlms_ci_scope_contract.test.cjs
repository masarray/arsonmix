// SPDX-License-Identifier: AGPL-3.0-only
"use strict";
const test=require("node:test");
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const root=path.resolve(__dirname,"..");
const read=p=>fs.readFileSync(path.join(root,p),"utf8");
test("DLMS speaker fast CI compiles only speaker DSP and runs only speaker UX",()=>{
  const yaml=read(".github/workflows/dlms-fast.yml");
  const cxx=[...yaml.matchAll(/lms_[a-z_]+_test/g)].map(x=>x[0]);
  assert.ok(cxx.length>=7);
  assert.match(yaml,/engine\/lms\/\*\*/);
  assert.match(yaml,/lms_system_model\.test\.cjs/);
  assert.match(yaml,/lms_native_bridge\.test\.cjs/);
  assert.match(yaml,/g\+\+ -std=c\+\+20.*-Werror/);
  assert.doesNotMatch(yaml,/harmony_dsp_test|harmony_pitch_synchronous_test|sonku_spatial_core_test|VocalThrowProcessor/);
  assert.doesNotMatch(yaml,/setup-python|playwright install|cmake --build/);
});
test("vocal-harmony DSP gate no longer runs for generic CMake or DLMS changes",()=>{
  const yaml=read(".github/workflows/harmony-fast-dsp.yml");
  assert.doesNotMatch(yaml,/^\s+- 'CMakeLists\.txt'\s*$/m);
  assert.match(yaml,/engine\/builtin_fx\/Harmony\*\.h/);
  assert.match(yaml,/tests\/harmony\*\.cpp/);
});
