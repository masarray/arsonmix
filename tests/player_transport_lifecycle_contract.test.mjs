// SPDX-License-Identifier: AGPL-3.0-only
// Fast source-lifecycle guard: playback must not be owned by WebView state.
// JUCE native decoder behaviour is checked by player_native_decode_test.cpp.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const source=readFileSync('src/Main.cpp','utf8');
const begin=source.indexOf(
  '// A hidden/reloading WebView MUST NOT suspend the native player');
const after=source.indexOf('const double statusStart = timing ?',begin);
assert.ok(begin>0 && after>begin,'live player timer boundary missing');
const timer=source.slice(begin,after);
const service=timer.indexOf('engine_.servicePlayerTransition()');
const next=timer.indexOf('const bool preEnd=playerFadeMilliseconds_>0');
const loop=timer.indexOf('if(playerLoopCurrent_ && activeCurrent');
const uiGate=timer.indexOf('if(!browser_.loaded)return;');
assert.ok(service>=0 && next>service && loop>service,
          'native transport must be maintained on JUCE control timer');
assert.ok(uiGate>next && uiGate>loop,
          'WebView availability must gate ONLY UI publishing, not native ACK/loop/NEXT');
assert.match(timer,/if \(browser_\.loaded && !initialStateSent_\)/);
assert.match(timer,/if\(browser_\.loaded && timerTicks_%3==0\)emitPlayerState\(\)/);
assert.doesNotMatch(timer,/lastLoopAttemptGeneration_/,
  'same-generation A/B decks must never suppress second A-B loop');
console.log('M4 PASS: transport, Loop A-B and NEXT survive WebView unload/reload');
