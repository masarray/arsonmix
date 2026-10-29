# ArSonMix 2026-10-30 — Mixer I/O / FX / AUX architecture reform

This is the canonical entrypoint for the user-approved mixer workflow: independent MIX, FX RACK, ROUTING; device-bound INPUT/OUTPUT and native PRE/POST sends and AUX. Full design follows in a later revision of this file. Never hide a working VST rack when routing activates. Do not treat four AUX returns as four independent hardware output ports. Validate audio source epoch and native ACK, MUTE before topology change, and do not claim live hardware FAT from CI alone.
