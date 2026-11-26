# RTR-4B1 — independent MONITOR latency domain, engine qualification slice

The existing global PDC aligns MAIN, MONITOR and all potential direct taps to one target, including unpatched latent lanes. This can add avoidable terminal padding to a singer's separately routed monitor.

Introduce a prepared-graph MonitorLatencyPolicy: aligned (unchanged default), independent (remove ONLY the MONITOR sink's terminal alignment pad). Every upstream fan-in, every FX latency, all MAIN/direct output alignment, gain, route and tail remain unchanged. No new render graph, FIFO, callback work, threshold or automatic bypass. Shared feeds to MAIN and MONITOR are still processed once. Different physical output timing is intentional; do not combine the independent MONITOR acoustically/electrically with MAIN without evaluating comb filtering.

Native publisher admits entering/leaving independent mode only when all outputs are explicitly disarmed. Even same-policy structural commits require disarm while independent mode is active; existing scalar controls are unaffected. Reject no-MONITOR or invalid policy. A rejected candidate preserves the accepted epoch/revision. The existing graph publication and retirement mechanism remains authoritative.

Health snapshot exposes actual MONITOR output samples/padding, accepted policy and maximum terminal saving versus global alignment (not physical round-trip latency). Legacy global latency remains the conservative common MAIN/direct reference.

Portable regressions cover PRE/POST, isolated latent direct lane, 0/5 samples intrinsic MONITOR FX, aligned/independent, direct output reconnect, stale revision, heap guard, armed WIRE/FX bypass attempts, same-policy armed rebuild and rejected candidates. Physical ASIO/Windows and production UX not yet qualified.

This slice deliberately does not expose a live GUI toggle or persist mode in Shows: all shipping request builders still default to aligned. Next: explicit disarm/rearm operator transaction with native ACK and durable session policy, followed by integrated hardware qualification. Do not promote or advertise it as an available user feature yet. Canonical #899 unchanged.
