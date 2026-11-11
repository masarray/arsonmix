# SPDX-License-Identifier: AGPL-3.0-only
"""Fixture regression: WaveShell cannot load a class from pre-context index."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from patch_juce_vst3_waveshell import OLD, NEW, RELATIVE, apply_patch, patch_root


def vulnerable_fixture():
    return """
struct VST3ModuleHandle final {
    static VST3ModuleHandle create (const File& pluginFile, const PluginDescription& desc)
    {
        auto factory = result.handle->getPluginFactory();
        if (factory == nullptr) return {};
        const auto numClasses = factory->countClasses();
        result.classIndex = findClassMatchingDescription (factory, desc);
    }
};
static auto createVST3Instance(VSTComSmartPtr<VST3HostContextHeadless> host) {
    const auto module = VST3ModuleHandle::create (file, description);
}
"""


class WaveShellContextOrderTest(unittest.TestCase):
    def test_patches_known_pinned_juce_source_and_is_idempotent(self):
        fixed, modified = apply_patch(vulnerable_fixture())
        self.assertTrue(modified)
        self.assertIn(NEW[0], fixed)
        self.assertIn(NEW[2], fixed)
        self.assertLess(fixed.index("pf3->setHostContext (hostContext)"),
                        fixed.index("findClassMatchingDescription (factory, desc)"))
        self.assertEqual((fixed, False), apply_patch(fixed))

    def test_unknown_or_partial_source_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "UNSUPPORTED_SOURCE"):
            apply_patch("unknown JUCE source")
        partial = vulnerable_fixture().replace(OLD[0], NEW[0])
        with self.assertRaisesRegex(ValueError, "UNSUPPORTED_SOURCE"):
            apply_patch(partial)

    def test_patches_disk_copy_without_touching_other_files(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            target = root / RELATIVE
            target.parent.mkdir(parents=True)
            target.write_text(vulnerable_fixture(), encoding="utf-8")
            self.assertTrue(patch_root(root))
            self.assertFalse(patch_root(root))
            self.assertIn("host->getFUnknown()", target.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
