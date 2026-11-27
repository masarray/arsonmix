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
assert.match(timer,/playerLoopCurrent_ \|\| playerPendingIndex_>=0/,
  'A-B must be serviced at 12 Hz when Auto Next is off');
assert.ok(service>=0 && next>service && loop>service,
          'native transport must be maintained on JUCE control timer');
assert.ok(uiGate>next && uiGate>loop,
          'WebView availability must gate ONLY UI publishing, not native ACK/loop/NEXT');
assert.match(timer,/if \(browser_\.loaded && !initialStateSent_\)/);
assert.match(timer,/if\(browser_\.loaded && timerTicks_%3==0\)emitPlayerState\(\)/);
assert.doesNotMatch(timer,/lastLoopAttemptGeneration_/,
  'same-generation A/B decks must never suppress second A-B loop');
console.log('M4 PASS: transport, Loop A-B and NEXT survive WebView unload/reload');

// Native cancellation without decoder failure (e.g. device reopen) must
// release the UI NEXT latch. Failed or cancelled loops cannot retry-storm.
assert.match(timer,/engine_\.playerTransitionFailed\(\) \|\|\s*!static_cast<bool>\(engine_\.playerStatus\(\)\.getProperty\("transitionPending",false\)\)/);
assert.match(timer,/playerPendingIndex_=-1;playerAutomaticPending_=false;\s*playerLoopCurrent_=false;/);
assert.match(timer,/playerPendingIndex_<0 && playerPendingQueueFile_!=juce::File\{\}/);
assert.match(timer,/phase=="PLAYING" \|\| phase=="ENDED"\) \{\s*if\(armix::player::PlayerShowTransitionPolicy::commitQueuedFront/);
const nextBody=source.slice(source.indexOf("void playNextMusic("),source.indexOf("void onAction ("));
assert.doesNotMatch(nextBody,/commitQueuedFront/,"queue cannot be consumed before native PCM");
assert.match(timer,/!playerAutoAdvanceGate_\.claimed\(\)/);
console.log("Player cancellation, queue PCM commit, retry gate source contracts PASS");

assert.match(timer,/playerLibrary_\[static_cast<std::size_t>\(playerCurrent_\)\]==playerPendingQueueFile_/,"only the acknowledged song may consume its queue entry");
assert.match(source,/if\(playerMediaRefreshRequested_\)\{\s*if\(playerMediaBusy_\|\|playerCoverBusy_\)return;/);
assert.doesNotMatch(source,/playerError_="Media scan is in progress"/,"refresh should coalesce, not reject while scanning");
console.log("Queued song identity and coalesced refresh contracts PASS");

assert.match(timer,/loopDue\(phase=="PLAYING",phase=="ENDED"/);
assert.match(timer,/playerError_="Loop standby failed; press Play or Next to retry";\s*playerLoopCurrent_=false;\s*\(void\)playerAutoAdvanceGate_\.claim\(\)/);
assert.match(source,/playerLoopCurrent_ && playerPendingIndex_>=0 &&\s*playerPendingIndex_==playerCurrent_ &&\s*engine_\.cancelPlayerPendingTransition\(\)/);
assert.match(source,/setProperty\("mediaRefreshQueued",playerMediaRefreshRequested_\)/);
console.log("P1 loop EOF fallback, pending cancel and refresh readback contracts PASS");
