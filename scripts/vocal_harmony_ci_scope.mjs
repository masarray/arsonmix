// SPDX-License-Identifier: AGPL-3.0-only
// Path classification for the Vocal Harmony plugin's dedicated CI.
// The broader JUCE mixer/ArSon Player/GUI workflows must never be treated
// as changes to this plugin merely because they share CMakeLists.txt.
import {readFileSync} from 'node:fs';
import {pathToFileURL} from 'node:url';

const dspPaths = [
  /^engine\/builtin_fx\/Harmony[^/]*\.h$/,
  /^engine\/builtin_fx\/ChannelHarmonyProcessor\.h$/,
  /^tests\/harmony[^/]*\.(?:cpp|py)$/,
  /^tools\/harmony_offline_ab\.cpp$/
];
const uiPaths = [
  /^site\/preview-v2\/assets\/mixer-native-harmony\.js$/,
  /^tests\/(?:native_harmony_assembly_test|approved_native_harmony_smoke)\.py$/
];

export function classifyVocalHarmonyChanges(files) {
  const changed = Array.isArray(files) ? files : [];
  const dsp = changed.some(file => typeof file === 'string' &&
    dspPaths.some(pattern => pattern.test(file)));
  const uiOnly = changed.some(file => typeof file === 'string' &&
    uiPaths.some(pattern => pattern.test(file)));
  return {dsp, ui: dsp || uiOnly};
}

// stdin: git diff --name-only --no-renames BASE HEAD
// stdout: GitHub Actions $GITHUB_OUTPUT-compatible names; no network/API.
if (process.argv[1] &&
    import.meta.url === pathToFileURL(process.argv[1]).href) {
  const files = readFileSync(0, 'utf8').split(/\r?\n/).filter(Boolean);
  const result = classifyVocalHarmonyChanges(files);
  process.stdout.write('dsp=' + result.dsp + '\nui=' + result.ui + '\n');
}
