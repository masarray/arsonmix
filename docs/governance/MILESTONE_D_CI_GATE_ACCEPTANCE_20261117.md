# Milestone D CI acceptance notes (17 November 2026)

This is a **non-product CI acceptance fixture**. It demonstrates that a
documentation-only pull request receives the trusted merge-gate status
without an ASIO runner, while a native-code diff must be qualified on an
exact SHA before it can become eligible.

No hardware FAT, publisher sound or installer is claimed here.
Canonical production regression evidence for D1: PR #753 / 100 CTest.
