# RTR-4A — accepted graph latency evidence (read only)

A source-only or otherwise potential direct-output lane can contribute to the current global alignment even without an armed direct output. The prepared DAG already computes each sink's pre-alignment arrival and potential direct-tap maximum. Export these exact values from the same accepted epoch/revision, with the first maximum direct candidate's stable ChannelId and tie count. If the potential tap maximum strictly exceeds both sink arrivals it raises the common alignment target. No claim that a tap is armed, that its rack is the sole cause, or that this is measured physical RTL. A bypass request remains aligned.

No audio callback code, delay allocation policy, route, auto-bypass, buffer, or approved GUI changes. This is the read-only first slice of RTR-4, not automatic low-latency mode. Next: explicitly authorized topology planning and safe transitions; actual vendor/ASIO qualification still required.

Regression extends the existing exact PDC impulse + heap guard test to PRE/POST, source-only 19-sample lane vs 10-sample MAIN and zero-sample MONITOR, stale epoch/revision and rejected candidate preservation. Test execution pending exact CI. No canonical RC or release mutation.

The existing cheap Logical Mixer C++20 workflow now explicitly compiles and executes this PDC test (including test-only changes in its path filter), so a skipped Windows job cannot be confused with its result. Export the read-only planner keys through the existing compact health snapshot as well; #930's Main.cpp editor/session hunks do not overlap this key list. No new build workflow or FAT dispatch.
