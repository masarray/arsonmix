# Lean native editor screenshot evidence

This optional diagnostic builds the **exact production** `GuitarRigEditor.h`
and `GuitarRigProcessor.h` in a standalone JUCE 8 console target, using the
**same 11 production RGBA PNG bytes**. It creates a native Windows peer before
capturing the full JUCE component hierarchy.

It is independent of the full application's CMake configure and does **not**
compile ArSonMixConsole, WebView2, installer, VST3 scanning or second PCM engine.
The source is not an HTML mockup. The suite emits ten evidence files (Gate,
Drive, Amp, Cab, Gate before/after toggle, two viewport sizes and A/B).

Run on Windows with pinned JUCE 8:

```powershell
cmake -S tests/guitar-fx-native -B build-gfx -G "Visual Studio 17 2022" -A x64 -DJUCE_ROOT="<path to JUCE 8>"
cmake --build build-gfx --config Release --target ARMixGuitarEditorCapture --parallel 2
.\build-gfx\ARMixGuitarEditorCapture_artefacts\Release\ARMixGuitarEditorCapture.exe "$PWD\guitar-native-screenshots"
```

This checks real GUI composition, not ASIO physical sound or screenshot visual
approval. Snapshot artifacts must be inspected by a person and compared at
different DPI settings. Headless/offscreen-only rendering is not a substitute
for a native peer; failure to create one is a test failure.
