#!/usr/bin/env python3
"""Apply JUCE's 2026 WaveShell host-context ordering fix to pinned JUCE 8.

The repository intentionally pins JUCE 8.0.15 for the verified native host.
JUCE upstream 3fa28d0 (2026-09-03) fixes WaveShell class-list mutation:
the factory must receive setHostContext BEFORE resolving a class index.
Patch only the three pinned-JUCE anchors, idempotently, or fail closed.
Never patch a different/unknown JUCE implementation silently.
"""
import argparse
from pathlib import Path

RELATIVE = Path("modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h")

OLD = (
    "static VST3ModuleHandle create (const File& pluginFile, const PluginDescription& desc)",
    "        const auto numClasses = factory->countClasses();\n"
    "        result.classIndex = findClassMatchingDescription (factory, desc);",
    "const auto module = VST3ModuleHandle::create (file, description);",
)
NEW = (
    "static VST3ModuleHandle create (const File& pluginFile, const PluginDescription& desc, FUnknown* hostContext)",
    "        // JUCE upstream 3fa28d0: WaveShell changes class indices in setHostContext.\n"
    "        // Resolve the class index only AFTER the same host context is set.\n"
    "        if (VSTComSmartPtr<IPluginFactory3> pf3; pf3.loadFrom (factory.get()))\n"
    "            pf3->setHostContext (hostContext);\n"
    "        const auto numClasses = factory->countClasses();\n"
    "        result.classIndex = findClassMatchingDescription (factory, desc);",
    "const auto module = VST3ModuleHandle::create (file, description, host->getFUnknown());",
)


def apply_patch(source: str) -> tuple[str, bool]:
    # We deliberately support only the audited JUCE 8.0.15 implementation.
    if all(source.count(after) == 1 for after in NEW):
        return source, False
    if any(source.count(before) != 1 for before in OLD):
        raise ValueError("JUCE_VST3_WAVESHELL_PATCH_UNSUPPORTED_SOURCE")
    if any(after in source for after in NEW):
        raise ValueError("JUCE_VST3_WAVESHELL_PATCH_PARTIAL_SOURCE")
    updated = source
    for before, after in zip(OLD, NEW):
        updated = updated.replace(before, after, 1)
    if not all(updated.count(after) == 1 for after in NEW):
        raise ValueError("JUCE_VST3_WAVESHELL_PATCH_POSTCONDITION_FAILED")
    return updated, True


def patch_root(root: Path) -> bool:
    target = root / RELATIVE
    if not target.is_file():
        raise FileNotFoundError(f"JUCE_VST3_WAVESHELL_SOURCE_MISSING: {target}")
    original = target.read_text(encoding="utf-8")
    changed, modified = apply_patch(original)
    if modified:
        target.write_text(changed, encoding="utf-8")
    return modified


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--juce-root", type=Path, required=True)
    args = parser.parse_args()
    try:
        modified = patch_root(args.juce_root)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"{exc}\n")
    print("JUCE_VST3_WAVESHELL_CONTEXT_ORDER=" + ("PATCHED" if modified else "VERIFIED"))
