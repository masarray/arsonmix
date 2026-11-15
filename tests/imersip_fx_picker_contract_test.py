"""ImerSip deployment contract: DSP registration must reach the real picker.

A DSP unit test alone never protects against an omitted native catalog row.
The Windows JUCE CTest asserts the actual compiled registry. This lightweight
source-integrity check protects the independent CMake -> Main -> bridge -> GUI
contract and prevents the wrong-ifdef error reported on g80c0af68.
"""
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT/path).read_text(encoding="utf-8")

class ImerSipPickerContract(unittest.TestCase):
    def test_same_build_define_controls_pcm_and_native_catalog(self):
        cmake=read("CMakeLists.txt")
        registry=read("src/NativeBuiltInFx.h")
        self.assertRegex(cmake,re.compile(
            r"target_compile_definitions\(ARMixConsole PRIVATE.*?"
            r"\$<\$<BOOL:\$\{ARMIX_SONKU_EXPERIMENTAL_P82_ROOM\}>:"
            r"SONKU_P82_MULTIBAND_ROOM_RESEARCH=1>",
            re.S))
        self.assertEqual(registry.count(
            "#if defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) && "
            "SONKU_P82_MULTIBAND_ROOM_RESEARCH"),2)
        self.assertNotIn(
            "#if defined(ARMIX_SONKU_EXPERIMENTAL_P82_ROOM)",
            registry)
        self.assertIn("ImerSipProcessor::catalogIdentity",registry)
        self.assertRegex(registry,re.compile(
            r'ImerSipProcessor::catalogIdentity,\s*'
            r'"ImerSip","Spatial",true,44100\.,48000\.'))
        self.assertIn("prepareNativeBuiltInFx<ImerSipProcessor>",registry)

    def test_host_emits_exact_native_catalog_no_duplicated_ui_authority(self):
        main=read("src/Main.cpp")
        bridge=read("web/approved-native-bridge.js")
        composer=read("scripts/compose_native_peq.py")
        stage=read("scripts/build_approved_native_gui.py")
        self.assertIn('entry->setProperty("id",fx.id)',main)
        self.assertIn('entry->setProperty("showInPicker",fx.showInPicker)',main)
        self.assertIn('setProperty("builtIns",juce::var(builtIns))',main)
        self.assertIn('emitEventIfBrowserIsVisible("arMixCatalog",catalog)',main)
        self.assertIn("nativeBuiltins=catalog.builtIns.filter(",bridge)
        self.assertIn("nativeBuiltins.filter(fx=>fx.showInPicker!==false)",bridge)
        self.assertIn("scope?.setNativeCatalog?.(",bridge)
        self.assertIn("setNativeCatalog(rows){",stage)
        self.assertIn("renderPluginCatalog();renderFastPicker();",stage)
        self.assertIn("p.format==='Native'",composer)
        self.assertIn("!specializedIds.has(p.id)",composer)
        self.assertIn("nativeExtra+eqBuiltin+compBuiltin",composer)
        self.assertIn('data-fast-id=',composer)
        # Exact host-owned graph: never fake a visual-only accepted FX in JUCE.
        self.assertIn(
            "window.__ARMIX_NATIVE_M2__?.insertPlugin?.(id,activeFxTarget)",stage)
        self.assertIn("engine_.installNativeBuiltInFx(",main)
        self.assertIn("armix::builtin_fx::findNativeBuiltInFx(catalogId)",main)
        self.assertIn("const plugin=nativeCatalog.get(id) || nativeBuiltins.find",bridge)

    def test_native_windows_artifact_requires_compiled_picker_registry(self):
        cmake=read("CMakeLists.txt")
        cpp=read("tests/imersip_native_state_test.cpp")
        windows=read(".github/workflows/sonku-p82-acoustic-windows-field.yml")
        self.assertIn("ARMixImerSipNativeTests",cmake)
        self.assertIn("SONKU_P82_MULTIBAND_ROOM_RESEARCH=1)",cmake)
        self.assertIn("findNativeBuiltInFx(",cpp)
        self.assertIn("assert(descriptor->showInPicker)",cpp)
        self.assertIn("descriptor->prepare(48000.,128,nullptr,0)",cpp)
        self.assertIn("ARMixConsole ARMixSonKuP72SourceTests",windows)
        self.assertIn("ARMixImerSipNativeTests",windows)
        self.assertIn("ImerSipNativeJuceP82AcceptedStateAckAndRestore",windows)
        self.assertIn("ArSonMix-ImerSip-P87-StereoFirst-v5-Windows-x64-",windows)
        self.assertIn("imrsSchemaVersion = 5",windows)
        self.assertIn("imrsStateBytes = 52",windows)
        self.assertIn("sourceDistanceRangeMeters = @(1.6,7.4)",windows)
        self.assertIn("earHrtfAlignRangeDegrees = @(-30,20)",windows)
        self.assertIn("binauralEarlyMidAndSide = $true",windows)

if __name__=="__main__":
    unittest.main()
