## ArSonMix — one product integration owner

**Current main SHA:**  
**Branch origin / existing PRs reconciled:**  
**Product behavior fixed:**  
**Native signal path, device and VST3 impact:**  

## Implementation and evidence

- Code paths changed (and existing implementation reused):
- Audio callback owner remains ONE JUCE producer:
- Real PCM test and expected input → MAIN → output behavior:
- Windows MSVC / CTest / Chromium results at the **exact PR SHA**:
- Windows EXE / ZIP source SHA and integrity:
- Physical ASIO/WASAPI listening FAT: **PASS with evidence** or **PENDING** (never inferred from CI):

Protect the approved /preview/ GUI, proper physical port enumeration, native PCM, and user-safe output mute. No parallel browser DSP or speculative hardcoded devices. A single integrator may edit any required subsystem; no legacy workstream marker or owner approval is required.
