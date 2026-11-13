"""Guard that Windows bundles the literal ONE approved Pages /preview/ GUI."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("approved_native_bundle", ROOT / "scripts/build_approved_native_gui.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class ApprovedNativeGuiBundleTests(unittest.TestCase):
    def test_native_is_exact_assembled_public_pages_plus_nonvisual_native_seam(self):
        with tempfile.TemporaryDirectory(prefix="armix-approved-test-") as tmp:
            out, published = Path(tmp) / "bundle", Path(tmp) / "published"
            source = bundle.build_public_pages(published)
            manifest = bundle.stage(out)
            self.assertEqual(manifest["authoritativePagesRoute"], "/preview/")
            self.assertEqual(manifest["authoritativePagesOutput"], "preview/index.html")
            self.assertEqual(manifest["adapterState"], "SCAFFOLD_ONLY__NOT_LIVE_PCM")
            approved = (source / "index.html").read_text("utf-8")
            native = (out / "index.html").read_text("utf-8")
            self.assertEqual(bundle.public_from_native(native), approved)
            self.assertNotIn("__ARMIX_NATIVE_M2__", approved)
            self.assertIn("__ARMIX_NATIVE_M2__", native)
            # Never let a missing ASIO driver or delayed native snapshot
            # hide the entire mixer. Readback controls remain separately gated.
            self.assertNotIn('id="native-mixer-startup"', native)
            self.assertNotIn('html:not([data-armix-native-roster]) :is(.console-pane,#trackCount){visibility:hidden}', native)
            # M3 native-only scope is embedded inside the existing Pages IIFE:
            # no visual duplication; it must be stripped in public_from_native.
            self.assertIn("window.__ARMIX_NATIVE_SCOPE__=Object.freeze({", native)
            self.assertNotIn("__ARMIX_NATIVE_SCOPE__", approved)
            self.assertIn("nativeMeterPainted", native)
            self.assertNotIn("nativeMeterPainted", approved)
            self.assertIn("nativeWireBounds", native)
            self.assertNotIn("nativeWireBounds", approved)
            # No new CSS override or second mixer: native HTML differs only
            # through exactly reversible data/events/DOM-write optimizations.
            self.assertIn("make,render,findTrack,displayMeterDbfs", native)
            self.assertIn("installGraph(channels,main,edges)", native)
            self.assertIn("if(window.__JUCE__?.backend)return true;", native)
            # V60 public pointerup emits exactly one final change. The
            # native-only handleField seam owns that event/ACK; a second
            # synthetic callback here would duplicate the release.
            self.assertEqual(native.count("handleField({target:g.input,type:'change',altKey:!!ev.altKey});"),1)
            self.assertIn("window.__ARMIX_NATIVE_M2__?.consumeField?.(event);return;", native)
            self.assertNotIn("consumeField?.({target:g.input,type:'change'})", native)
            self.assertEqual(manifest["approvedPagesHtmlSha256"], bundle.sha256((source / "index.html").read_bytes()))
            self.assertNotEqual(native, (ROOT / "web/index.html").read_text("utf-8"))
            self.assertIn("approved-native-bridge.js", native)
            self.assertIn("approved-native-context-menu.js", native)
            context_menu = (out / "assets/approved-native-context-menu.js").read_text("utf-8")
            self.assertIn('document.addEventListener("contextmenu"', context_menu)
            self.assertIn("requestDeleteTrack", context_menu)
            self.assertIn("orbitAction", context_menu)
            # H1 uses the SAME native-only /preview/ context handler. Never
            # intercept vendor HWNDs or implement a JS-only global shortcut.
            self.assertIn("arMixHotkeyIntent", context_menu)
            self.assertIn("arMixHotkeyRegistry", context_menu)
            self.assertIn("Please press Hotkey", context_menu)
            self.assertIn("hotkey-add", context_menu)
            # Native-only right-click for channel/rack/slot Power is real
            # RegisterHotKey input, not fake demo power or a WebView click.
            self.assertIn("nativeFxHotkeyTarget", context_menu)
            self.assertIn("SET_FX_RACK_BYPASS", context_menu)
            self.assertIn("SET_FX_SLOT_BYPASS", context_menu)
            self.assertIn("Assign Hotkey", context_menu)
            self.assertIn("Change Hotkey", context_menu)
            self.assertIn("Remove Hotkey", context_menu)
            self.assertIn('data-action="slotPower"', context_menu)
            self.assertIn('data-action="fxOn"', context_menu)
            native_host = (ROOT / "src/Main.cpp").read_text("utf-8")
            self.assertIn("HOTKEY_FX_INSTANCE_NOT_ADMITTED", native_host)
            self.assertIn("Browse Parameters…", context_menu)
            self.assertIn("Last Touched Hotkey…", context_menu)
            self.assertIn("arMixFxParameterHotkeyInspect", context_menu)
            self.assertIn("arMixFxParameterHotkeyState", context_menu)
            self.assertIn("SET_FX_PARAM_TOGGLE", context_menu)
            self.assertIn("SET_FX_PARAM_UP", context_menu)
            self.assertIn("SET_FX_PARAM_DOWN", context_menu)
            self.assertIn("resolveFxHotkey", native_host)
            self.assertIn("FxParameterPolicy::find", native_host)
            self.assertIn("lastTouchedParameter", native_host)
            self.assertIn("HOTKEY_PARAMETER_ID_OR_INSTANCE_UNAVAILABLE", native_host)
            self.assertIn("HostedAudioProcessorParameter",
                (ROOT / "src/NativeFxParameterHotkey.h").read_text("utf-8"))
            self.assertIn('routingNativeFxInsertBypass', native_host)
            self.assertIn('routingNativeFxRackBypass', native_host)
            self.assertIn("hotkey-register", context_menu)
            self.assertIn("requestDeleteTrack", context_menu)
            self.assertIn("Orbit…", context_menu)
            self.assertIn('withEventListener ("arMixHotkeyIntent"', native_host)
            self.assertIn("fireNativeHotkey", native_host)
            self.assertIn("handleApprovedNativeControl(request)", native_host)
            self.assertIn("WM_HOTKEY", (ROOT / "src/NativeHotkeyRegistry.h").read_text("utf-8"))
            self.assertNotIn("approved-native-context-menu.js", approved)
            self.assertIn("arMixGuiReady", (out / "assets/approved-native-bridge.js").read_text("utf-8"))
            self.assertIn("nativeBridgeAttached:true",
                (out / "assets/approved-native-bridge.js").read_text("utf-8"))
            # Do not reintroduce #511's no-device hide/overlay regression.
            self.assertNotIn('id="armix-native-boot-guard"', native)
            for asset in (source / "assets").iterdir():
                self.assertEqual((out / "assets" / asset.name).read_bytes(), asset.read_bytes(),
                                 "Native /preview/ asset differs: " + asset.name)
            for name in ("mixer-control-keys.css", "mixer-control-keys.js", "mixer-keys-thin-flat-v9-approved-off-shadow-57x30.png"):
                button = (ROOT / "site/preview-buttons/assets" / name).read_bytes()
                self.assertEqual((out / "assets" / name).read_bytes(), button)
            # Stage D uses ONE compiled CSSOM for both Pages and WebView2.
            self.assertEqual(native.count('<link rel="stylesheet"'), 1)
            self.assertIn('assets/arsonmix-approved-runtime.css?sha=', native)
            self.assertNotIn('href="assets/mixer-control-keys.css', native)
            self.assertNotIn('href="assets/mixer-wire-dots.css', native)
            self.assertNotIn("console-native.html", native)

    def test_manifest_reproducibility_and_byte_map(self):
        with tempfile.TemporaryDirectory(prefix="armix-approved-test-") as tmp:
            a, b = Path(tmp) / "a", Path(tmp) / "b"
            first = bundle.stage(a)
            second = bundle.stage(b)
            self.assertEqual(first, second)
            self.assertEqual(bundle.verify(a), first)
            header = (a / "ApprovedGuiResources.h").read_text("utf-8")
            self.assertIn("BinaryData::index_html", header)
            self.assertIn("BinaryData::mixercontrolkeys_css", header)
            self.assertIn("BinaryData::arsonmixapprovedruntime_css", header)
            self.assertIn("BinaryData::mixercontrolkeys_js", header)
            self.assertIn('"/assets/mixer-keys-thin-flat-v9-approved-off-shadow-57x30.png"', header)
            for entry in first["files"]:
                self.assertEqual(bundle.sha256((a / entry["path"]).read_bytes()), entry["sha256"])
            (a / "index.html").write_text("WRONG GUI", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "STALE OR WRONG APPROVED GUI BUNDLE"):
                bundle.verify(a)

    def test_native_binary_never_routes_to_another_renderer(self):
        cmake = (ROOT / "CMakeLists.txt").read_text("utf-8")
        cpp = (ROOT / "src/Main.cpp").read_text("utf-8")
        self.assertIn("build_approved_native_gui.py", cmake)
        self.assertIn("ARMIX_APPROVED_GUI_DIR", cmake)
        self.assertNotIn("juce_add_binary_data(ARMixAssets SOURCES\n    web/index.html", cmake)
        self.assertNotIn("web/console-native.html web/console-native.css", cmake)
        self.assertIn("resolveApprovedGuiResource (path, data, size, mime)", cpp)
        self.assertNotIn("BinaryData::consolenative_html", cpp)
        self.assertNotIn("BinaryData::index_html", cpp)
        self.assertIn("withResourceProvider (assetFor)", cpp)


if __name__ == "__main__":
    unittest.main()
