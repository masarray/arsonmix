"""Non-regression: approved /preview/ may eliminate, never add cross-owner CSS stacks."""
import importlib.util
from pathlib import Path
from unittest.mock import patch
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "css_owner", ROOT / "scripts/check_preview_css_ownership.py")
OWNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(OWNER)


class ApprovedCssOwnership(unittest.TestCase):
    def test_approved_png_owner_and_locked_cross_file_stack(self):
        result = OWNER.verify()
        self.assertEqual(result["stylesheets"], 18)
        self.assertEqual(result["crossOwnerBaseline"], 114)
        self.assertEqual(result["newCrossOwnerConflicts"], 0)
        self.assertTrue(str(OWNER.source_path("mixer-control-keys.css"))
                        .endswith("site/preview-buttons/assets/mixer-control-keys.css"))

    def test_new_cross_owner_declaration_is_rejected(self):
        names, current = OWNER.inspect()
        added = dict(current)
        added[".new-overlap||filter"] = ["mixer-meter-display.css",
                                           "mixer-chrome-material.css"]
        with patch.object(OWNER, "inspect", return_value=(names, added)):
            with self.assertRaisesRegex(ValueError, "NEW CROSS-OWNER"):
                OWNER.verify()

    def test_new_owner_of_old_overlapping_declaration_is_rejected(self):
        names, current = OWNER.inspect()
        key = next(iter(current))
        added = dict(current)
        added[key] = [*current[key], "mixer-fx-rack.css"]
        if "mixer-fx-rack.css" in current[key]:
            added[key] = [*current[key], "mixer-meter-display.css"]
        with patch.object(OWNER, "inspect", return_value=(names, added)):
            with self.assertRaisesRegex(ValueError, "NEW CROSS-OWNER"):
                OWNER.verify()

    def test_late_stylesheet_and_inline_css_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "inline CSS"):
            OWNER.stylesheet_names('<style>.strip{filter:blur(2px)}</style>')
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            OWNER.stylesheet_names('<link rel="stylesheet" href="assets/a.css">'
                                   '<link rel="stylesheet" href="assets/a.css">')


if __name__ == "__main__":
    unittest.main()
