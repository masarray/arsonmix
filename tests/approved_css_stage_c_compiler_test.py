"""P0 Stage C: deterministic CSS cascade loser removal and immutable source assets."""
from pathlib import Path
import importlib.util
import hashlib
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location(
    "css_compiler", ROOT/"scripts/compile_approved_css.py")
css=importlib.util.module_from_spec(spec)
# dataclasses needs module to exist in sys.modules when constructing classes.
import sys
sys.modules[spec.name]=css
spec.loader.exec_module(css)


class StageCCssCompiler(unittest.TestCase):
    def test_same_selector_same_property_keeps_only_cascade_winner(self):
        originals=[
            ("a.css", ".strip{color:#111; margin:2px; background:#000;}\n"),
            ("b.css", ".strip { color:#222; }\n.strip{color:#333;}\n")
        ]
        compiled, data=css.optimize_css(originals)
        self.assertNotIn("color:#111",compiled["a.css"])
        self.assertIn("margin:2px",compiled["a.css"])
        self.assertNotIn("color:#222",compiled["b.css"])
        self.assertIn("color:#333",compiled["b.css"])
        self.assertEqual(data["removedDeclarations"],2)
        self.assertEqual(data["removedCrossFileDeclarations"],1)
        self.assertEqual(data["addedOverrideStylesheets"],0)

    def test_important_wins_even_if_ordinary_is_later(self):
        compiled,stats=css.optimize_css([
            ("a.css", ".strip{color:red!important;color:orange}\n"),
            ("b.css", ".strip{color:blue}\n"),
        ])
        self.assertIn("color:red!important",compiled["a.css"])
        self.assertNotIn("color:orange",compiled["a.css"])
        self.assertNotIn("color:blue",compiled["b.css"])
        self.assertEqual(stats["removedDeclarations"],2)

    def test_variable_and_modern_color_keep_old_runtime_fallbacks(self):
        original=[
            ("a.css",".strip{color:#ededed;color:var(--unbound)}"),
            ("b.css",".meter{background:#153028;"
                     "background:color-mix(in srgb,#153028 80%,#000)}")
        ]
        compiled,stats=css.optimize_css(original)
        self.assertEqual(compiled,dict(original),
                         "WebView2 versions and unset custom properties need fallback")
        self.assertEqual(stats["removedDeclarations"],0)
        self.assertEqual(stats["preservedFallbackDeclarations"],2)
        self.assertEqual(stats["lockedCandidateDeclarations"],0)

    def test_redundant_non_fallback_still_pruned(self):
        compiled,stats=css.optimize_css([
            ("a.css",".strip{border-radius:2px;border-radius:3px}")
        ])
        self.assertNotIn("2px",compiled["a.css"])
        self.assertIn("3px",compiled["a.css"])
        self.assertEqual(stats["removedDeclarations"],1)

    def test_media_and_supports_conditions_never_confused(self):
        compiled,stats=css.optimize_css([
            ("a.css", ".meter{color:red}@media(max-width:700px){.meter{color:orange}}"),
            ("b.css", "@media(min-width:701px){.meter{color:blue}}"
                      "@media(max-width:700px){.meter{color:green}}"
                      "@supports (display:grid){.meter{color:cyan}}")
        ])
        self.assertIn("color:red",compiled["a.css"])
        self.assertNotIn("color:orange",compiled["a.css"])
        self.assertIn("color:green",compiled["b.css"])
        self.assertIn("color:blue",compiled["b.css"])
        self.assertIn("color:cyan",compiled["b.css"])
        self.assertEqual(stats["removedDeclarations"],1)

    def test_preserves_comments_semicolon_in_strings_font_face_and_keyframes(self):
        source=('/* A { } */\n'
                '@font-face{src:url("data:font/woff2;base64,AB;{CD}");font-weight:300}\n'
                '@keyframes blink{0%{opacity:0}100%{opacity:1}}\n'
                '.strip{content:"text; {bar}";opacity:0.4;opacity:0.7}\n')
        optimized,stats=css.optimize_css([("a.css",source)])
        self.assertEqual(stats["removedDeclarations"],1)
        self.assertIn('data:font/woff2;base64,AB;{CD}',optimized["a.css"])
        self.assertIn("@keyframes blink",optimized["a.css"])
        self.assertIn('content:"text; {bar}"',optimized["a.css"])
        self.assertIn("opacity:0.7",optimized["a.css"])
        self.assertNotIn("opacity:0.4",optimized["a.css"])

    def test_png_buttons_and_wire_css_are_literal_bytes(self):
        css_assets=[
            ("a.css",".mx-hw-key{background:#aaa}"),
            ("mixer-control-keys.css",".mx-hw-key{background:#bbb}"),
            ("mixer-wire-dots.css",".wire-node{color:red}"),
            ("c.css",".wire-node{color:blue}")
        ]
        compiled,stats=css.optimize_css(css_assets)
        self.assertEqual(compiled["mixer-control-keys.css"],css_assets[1][1])
        self.assertEqual(compiled["mixer-wire-dots.css"],css_assets[2][1])
        self.assertIn("color:red",compiled["mixer-wire-dots.css"])
        self.assertIn("color:blue",compiled["c.css"])
        self.assertEqual(stats["lockedCandidateDeclarations"],1)
        self.assertEqual(stats["removedDeclarations"],1)

    def test_custom_property_case_and_quoted_attribute_spaces_are_distinct(self):
        authored=('.x{--Ch:red;--ch:blue}'
                  '.x[title="a  b"]{opacity:.5}'
                  '.x[title="a b"]{opacity:.7}')
        built,stats=css.optimize_css([("a.css",authored)])
        self.assertEqual(stats["removedDeclarations"],0,
                         "CSS custom properties are case-sensitive and quoted selector whitespace matters")
        self.assertEqual(built["a.css"],authored)

    def test_idempotent_and_no_new_link_or_authority(self):
        with tempfile.TemporaryDirectory() as root:
            assets=Path(root)
            (assets/"a.css").write_text(".x{width:1px;width:2px}\n",encoding="utf-8")
            (assets/"b.css").write_text(".x{width:3px}\n",encoding="utf-8")
            markup=('<link rel="stylesheet" href="assets/a.css?v=1">'
                    '<link rel="stylesheet" href="assets/b.css?v=2">')
            compiled,stats=css.optimize_assets(markup,assets)
            self.assertEqual(stats["sourceStylesheetCount"],2)
            self.assertEqual(compiled.count("armixc="),2)
            self.assertEqual(compiled.count('<link rel="stylesheet"'),2)
            self.assertEqual(stats["removedDeclarations"],2)
            for name in ("a.css","b.css"):
                self.assertIn("armixc="+hashlib.sha256((assets/name).read_bytes()).hexdigest()[:16],compiled)
            with self.assertRaisesRegex(ValueError,"twice"):
                css.optimize_assets(compiled,assets)
            first=[(name,(assets/name).read_text()) for name in ("a.css","b.css")]
            second,again=css.optimize_css(first)
            self.assertEqual(dict(first),second)
            self.assertEqual(again["removedDeclarations"],0)


if __name__=="__main__":
    unittest.main()
