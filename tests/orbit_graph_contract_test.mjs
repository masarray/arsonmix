// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
test('APPEND does not STOP the active vendor Orbit',()=>{
 const main=read('src/Main.cpp');
 const add=main.split('else if (action == "addCatalog")')[1]?.split('else if (action == "fxRemove")')[0];
 assert.ok(add,'Add branch present');
 assert.doesNotMatch(add,/stopOrbit\s*\(/);
 assert.match(add,/pauseForGraphEdit/);
 assert.match(add,/resumeAfterGraphEdit/);
 assert.match(add,/if \(orbit_\.active\(\)\) emitOrbitState/);
});
test('Native motion requires explicit vendor parameter mapping',()=>{
 const main=read('src/Main.cpp'),bridge=read('web/bridge.js'),core=read('engine/core/OrbitMotion.h');
 assert.match(main,/needsOrbitDistance\(shape\)/);
 assert.match(main,/needsOrbitElevation\(shape\)/);
 assert.match(main,/metresToNormalized/);
 assert.match(main,/nearDistance/);
 assert.match(main,/distanceMeterBounds/);
 assert.doesNotMatch(bridge,/nativeOrbitConfirm/);
 assert.match(bridge,/nativeOrbitNear/);
 assert.match(bridge,/nativeOrbitFar/);
 assert.match(bridge,/nearDistance:near,farDistance:far/);
 assert.match(bridge,/nativeOrbitDistance/);
 assert.match(bridge,/nativeOrbitElevation/);
 assert.match(bridge,/nativeOrbitDot/);
 assert.match(bridge,/requestAnimationFrame\(animateOrbitPreview\)/);
 assert.match(core,/orbitShapePhaseForAzimuth/);
});
