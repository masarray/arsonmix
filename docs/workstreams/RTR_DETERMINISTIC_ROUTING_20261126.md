# RTR-4A continuation — deterministic SEND / dormant direct-tap DSP

Previous exact b3f3002 logical CI failed before executing PDC test: GCC 15 inlined the intentional test replacement new/delete and emitted mismatched-new-delete under -Werror. Preserve heap counting and warnings, use portable noinline on replacement functions. No test waiver.

Actual audio hot path: zero-PDC PRE/POST WIRE accumulation uses settled coefficients or the exact existing 64-sample ramp; multiplication order unchanged, no zero-gain clear or threshold. Disconnected direct taps with nonzero PDC update only chronological ring history, without reading discarded old PCM or route blending. If a block covers the ring, retain only the last delay-length samples. Reconnect keeps the existing fade and full history, no change to PDC amount or routes. No callback allocation, extra FIFO, or plugin lifecycle policy change.

Regression: 9000 variable delay-history blocks vs original per-sample delay, 4000 variable SEND blocks vs exact original loop, actual accepted graph reconnect after disconnected input with nonzero PDC, previous PRE/POST impulse and stale epoch/failed admission, heap guard. No benchmark/physical ASIO result claimed. GUI, canonical #899, dearVR default conservative behavior untouched. Exact CI required.
