# Protected main required CI gate — verified 18 November 2026

The active GitHub ruleset ID `26568033` (`ArSonMix - Protected Main`)
targets `main` with strict required status `ArSonMix / Required CI Merge Gate`.
The rule was configured by the repository owner in GitHub Settings; no
administration bypass or protection downgrade was used.

## Positive and negative evidence

- Implemented by [PR #754](https://github.com/masarray/arsonmix/pull/754),
  refined by [PR #758](https://github.com/masarray/arsonmix/pull/758).
- Docs-only test [PR #755](https://github.com/masarray/arsonmix/pull/755):
  exact source `d3c10334e3a5a49e63bc1644c8c56e4a1535cc9d` received
  merge-gate **success** without requiring native Windows or Chromium.
  A later **native-only** test change on that disposable PR received
  **pending** without the corresponding Windows CTest.
- Deliberately red [PR #761](https://github.com/masarray/arsonmix/pull/761):
  exact source `13d9c8b2ecb8160800f1566824ed0c501fa790fe`, a guarded Python
  test with `REQUIRED_CI_RED_BLOCK_PROBE` failed; combined GitHub status
  `ArSonMix / Required CI Merge Gate = failure`, and the GitHub PR API
  returned `mergeable_state=blocked`. The PR was closed unmerged.
- THIS PR is the normal documentation-only **merge-positive** acceptance
  after enforcement was enabled; successful status followed by normal
  squash merge proves the rule is practical for low-risk docs updates.

## Separate qualification boundaries

The required status proves related automated source/build gates have passed.
It does **not** prove physical ASIO/WASAPI audio, product packaging features
outside the compiler, or licensed vendor VST3. Those are independently
covered by the FAT and release-specific integrity gates. In particular,
Milestone D's incomplete ImmerSIB installer #756 was superseded by the
binary-verifying #759 and corrected Windows candidate #760.
