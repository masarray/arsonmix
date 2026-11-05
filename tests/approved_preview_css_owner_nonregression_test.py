"""Non-regression: approved /preview/ may eliminate, never add cross-file or same-file CSS stacks."""
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
        self.assertEqual(result["sameOwnerRepeatedBaseline"], 252)
        self.assertEqual(result["sameOwnerRepeatedCurrent"], 252)
        self.assertEqual(result["newSameOwnerStacking"], 0)
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

    def test_new_same_file_stack_is_rejected_without_visual_patch(self):
        names, _ = OWNER.inspect()
        present = OWNER.count_same_file_repeated_declarations(names)
        invented = dict(present)
        invented["mixer-fader-assembly.css"] += 1
        with patch.object(OWNER, "count_same_file_repeated_declarations",
                          return_value=invented):
            with self.assertRaisesRegex(ValueError, "NEW SAME-OWNER"):
                OWNER.verify()

    def test_deleting_existing_same_file_duplicates_is_allowed(self):
        names, _ = OWNER.inspect()
        present = OWNER.count_same_file_repeated_declarations(names)
        reduced = dict(present)
        reduced["mixer-fader-assembly.css"] -= 1
        with patch.object(OWNER, "count_same_file_repeated_declarations",
                          return_value=reduced):
            result = OWNER.verify()
            self.assertEqual(result["newSameOwnerStacking"], 0)
            self.assertEqual(result["sameOwnerRepeatedCurrent"], 251)

    def test_late_stylesheet_and_inline_css_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "inline CSS"):
            OWNER.stylesheet_names('<style>.strip{filter:blur(2px)}</style>')
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            OWNER.stylesheet_names('<link rel="stylesheet" href="assets/a.css">'
                                   '<link rel="stylesheet" href="assets/a.css">')


if __name__ == "__main__":
    unittest.main()
