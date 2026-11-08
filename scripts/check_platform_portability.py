"""Fail cheaply before scheduling native Linux/macOS hosts on Windows-only source.

This gate is intentionally conservative: actual OS compilation + tests are still
required after it passes. It cannot certify ASIO/CoreAudio/PipeWire field audio.
"""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def blockers(platform, root=ROOT):
    main = (root / "src/Main.cpp").read_text("utf-8")
    cmake = (root / "CMakeLists.txt").read_text("utf-8")
    engine = (root / "src/LiveAudioEngine.h").read_text("utf-8")
    found = []

    begin = main.find("juce::WebBrowserComponent::Options webViewOptions()")
    end = main.find("void showWebViewStartupError", begin)
    webview = main[begin:end] if begin >= 0 and end >= 0 else ""
    if not webview or ("withWinWebView2Options" in webview and
                       "#if JUCE_WINDOWS" not in webview and
                       "#if defined(_WIN32)" not in webview):
        found.append("WebView2-only startup needs platform-specific WebView adapter")
    start = cmake.find("target_compile_definitions(ARMixConsole PRIVATE")
    finish = cmake.find("\nif(WIN32)", start)
    definitions = cmake[start:finish] if start >= 0 and finish > start else cmake
    if "JUCE_USE_WIN_WEBVIEW2=1" in definitions or "JUCE_ASIO=1" in definitions:
        found.append("Windows WebView2/ASIO compile settings remain unconditional")
    fallback = engine.find('setCurrentAudioDeviceType ("Windows Audio"')
    if fallback >= 0:
        gate = engine.rfind("#if defined(_WIN32)", 0, fallback)
        end_gate = engine.find("#endif", gate) if gate >= 0 else -1
        if gate < 0 or end_gate < fallback:
            found.append("Windows Audio startup fallback not platform-guarded")
    if "NEEDS_WEBVIEW2 ${WIN32}" not in cmake:
        found.append("JUCE WebView2 target requirement not Windows-only")
    if "Backend::defaultBackend" not in webview:
        found.append("No platform-native WebKit fallback for Linux/macOS")
    if "JUCE_PLUGINHOST_VST3=1" not in cmake:
        found.append("Native VST3 hosting unavailable in cross-platform target")
    package = {
        "linux": "scripts/package_native_linux.sh",
        "macos": "scripts/package_native_macos.sh",
    }[platform]
    if not (root / package).is_file():
        found.append("No verified native package builder: " + package)
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", required=True, choices=["linux", "macos"])
    args = parser.parse_args()
    problems = blockers(args.platform)
    if problems:
        print("ARSONMIX_" + args.platform.upper() + "_NATIVE_NOT_READY")
        for problem in problems:
            print("  - " + problem)
        raise SystemExit(2)
    print("ARSONMIX_" + args.platform.upper() + "_PORTABILITY_PREFLIGHT_PASS"
          " (native build, package and audio verification still required)")


if __name__ == "__main__":
    main()
