# SPDX-License-Identifier: AGPL-3.0-only
"""Fast source contract for *all* three native editor entrypoints."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
HOST=(ROOT/"src/Main.cpp").read_text(encoding="utf-8")
OWNER=(ROOT/"src/NativeOwnedFloatingWindow.h").read_text(encoding="utf-8")
WIN32=(ROOT/"src/NativeFxWin32Owner.h").read_text(encoding="utf-8")

class NativeFxWindowLifecycle(unittest.TestCase):
    def test_mixer_owned_not_global_topmost(self):
        self.assertIn("attachOwnedFloatingWindow (*this, mixer)",HOST)
        self.assertIn("attachOwnedFloatingWindow",OWNER)
        self.assertIn("win32::attachOwnedPopup",OWNER)
        self.assertIn("GWLP_HWNDPARENT",WIN32)
        self.assertIn("GW_OWNER",WIN32)
        self.assertIn("WS_CHILD",WIN32)
        self.assertNotIn("WS_EX_TOPMOST",WIN32)
        self.assertNotIn("::SetParent(",WIN32)
        self.assertNotIn("setAlwaysOnTop (true)",HOST)
    def test_exact_native_instance_reopen_is_not_recreation(self):
        self.assertIn("shownOwner==owner",HOST)
        self.assertIn("shownProcessor==owner->processor.get()",HOST)
        self.assertIn("shownWindow->present(true)",HOST)
        self.assertIn("shownWindow=dedicatedImmerSIB?immersibEditorWindow_:editorWindow_",HOST)
        self.assertIn("if(logicalVendorEditor_)",HOST)
        self.assertIn("existing.window->present(",HOST)
        self.assertIn("editorProcessor_ == processor",HOST)
        self.assertIn("if (!isVisible()) setVisible (true)",HOST)
        self.assertIn("toFront (true)",HOST)
        self.assertIn("editorWindow_.reset();",HOST)
        self.assertIn("routedEditorOwner_.reset();",HOST)
        self.assertIn("editorProcessor_=nullptr;",HOST)
    def test_immersib_editor_survives_vendor_window_switch_and_can_refocus(self):
        # Distinct accepted processor leases; only a graph/lease retirement
        # closes the ImmerSIB window, not opening a VST3 in the same rack.
        start=HOST.index('if (action == "routingFxEditor")')
        end=HOST.index('if(action=="nativeMixerFileSave"',start)
        opened=HOST[start:end]
        self.assertIn('dedicatedImmerSIB=dynamic_cast<',opened)
        self.assertIn('shownWindow=dedicatedImmerSIB?immersibEditorWindow_:editorWindow_',opened)
        self.assertIn('shownOwner=dedicatedImmerSIB?immersibEditorOwner_:routedEditorOwner_',opened)
        self.assertIn('shownWindow->present(true)',opened)
        self.assertIn('if(dedicatedImmerSIB)closeImmerSIBEditor();',opened)
        self.assertIn('closePluginEditor();',opened)
        self.assertIn('shownWindow=std::make_unique<PluginEditorWindow>',opened)
        self.assertIn('shownOwner=owner;',opened)
        self.assertIn('immersibEditorWindow_->getContentComponent()',HOST)
        self.assertIn('!engine_.retainsRoutedFxEditor(immersibEditorEpoch_,immersibEditorOwner_)',HOST)
        self.assertIn('closeAllPluginEditors();',HOST)
        self.assertIn('immersibEditorWindow_.reset();',HOST)
        self.assertLess(HOST.index('immersibEditorWindow_.reset();'),
                        HOST.index('immersibEditorOwner_.reset();'))
        self.assertNotIn('closeAllPluginEditors();',opened)

    def test_native_recenter_and_unplugged_monitor_recovery(self):
        self.assertIn("placeFloatingEditor (*this, mixer_, centreOnMixer)",HOST)
        self.assertIn('args.getProperty("recenter",false)',HOST)
        self.assertIn('request.getProperty("recenter",false)',HOST)
        self.assertIn("getDisplayForRect(current)",OWNER)
        self.assertIn("getDisplayForRect(mixerBounds)",OWNER)
        self.assertIn("display->userArea",OWNER)
        self.assertIn("editor.setTopLeftPosition(x,y)",OWNER)
        # WebView2 cannot escape its parent HWND. JS only forwards exact
        # insert selection; real native ownership remains in JUCE.
        context=(ROOT/"site/preview-v2/assets/mixer-contextual-buttons.js").read_text(encoding="utf-8")
        bridge=(ROOT/"web/approved-native-bridge.js").read_text(encoding="utf-8")
        self.assertIn("arMixFxRackDoubleClick",context)
        self.assertIn("arMixFxRackDoubleClick",bridge)
        self.assertIn("row?matched:windows",context)
        self.assertIn("centerInsert",context)
        self.assertIn('recenter:"true"',bridge)
        self.assertIn("openChannelIdFxEditor(editor,true)",bridge)
        self.assertIn("openChannelIdFxEditor(editor);",bridge)
        self.assertIn("nativeHooks.consumeAction(track",bridge)

    def test_exact_logical_vendor_identity_and_first_open_recenter(self):
        # Offline state save can outlive FX slot revisions. A stored VST3
        # blob may only be replaced when the original catalog/UID/path and
        # saved hash still match. Physical authority remains disconnected.
        self.assertIn("logicalVendorSourceCurrent(existing)",HOST)
        self.assertIn("existing.instanceId=requestedInstance;",HOST)
        self.assertIn("prepared.instanceId=requestedInstance;",HOST)
        self.assertIn("current->sha256==fx.insert.sha256",HOST)
        self.assertIn("current->uid==fx.insert.uid",HOST)
        self.assertIn("current->path==fx.insert.path",HOST)
        self.assertIn("if(!logicalVendorSourceCurrent(fx))",HOST)
        self.assertIn('FX slot or stored state changed:',HOST)
        self.assertIn("shownWindow->present(true); // new HWND",HOST)
        self.assertIn("prepared.window->present(true); // first-open",HOST)

    def test_multiple_exact_native_vendor_windows_and_retired_lease_cleanup(self):
        self.assertIn("struct AdditionalFxEditor final",HOST)
        self.assertIn("maxSimultaneousVendorEditors=16",HOST)
        self.assertIn("entry->owner==owner",HOST)
        self.assertIn("entry->processor==owner->processor.get()",HOST)
        self.assertIn("additionalFxEditors_.push_back(std::move(entry))",HOST)
        self.assertIn("(*extra)->channel=nativeChannel",HOST)
        self.assertIn("(*extra)->lane=lane;(*extra)->slot=slot",HOST)
        self.assertIn("(*extra)->instanceId=token",HOST)
        self.assertIn("keepOtherVendorWindow",HOST)
        self.assertIn("additionalFxEditors_.clear();",HOST)
        self.assertIn("std::erase_if(additionalFxEditors_",HOST)
        self.assertIn("!engine_.retainsRoutedFxEditor(entry->epoch,entry->owner)",HOST)
        self.assertLess(HOST.index("closeImmerSIBEditor();",HOST.index("void closeAllPluginEditors()")),
                        HOST.index("additionalFxEditors_.clear();"))
        self.assertLess(HOST.index("armix::mixer::LiveChannelFxGraph::InstanceLease owner;",
            HOST.index("struct AdditionalFxEditor final")),
            HOST.index("std::unique_ptr<PluginEditorWindow> window;",
                HOST.index("struct AdditionalFxEditor final")))
        self.assertIn("std::vector<std::unique_ptr<AdditionalFxEditor>> additionalFxEditors_;",HOST)
        self.assertIn("std::unique_ptr<PluginEditorWindow> window;",HOST)
        self.assertIn("JUCE editor",HOST)

    def test_all_six_builtins_escape_mixer_via_actual_native_owned_webview(self):
        script=(ROOT/"web/approved-floating-fx-bridge.js").read_text(encoding="utf-8")
        builder=(ROOT/"scripts/build_approved_native_gui.py").read_text(encoding="utf-8")
        cmake=(ROOT/"CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("class BuiltinFxDesktopWindow final : public juce::DocumentWindow",HOST)
        self.assertIn("withResourceProvider(assetFor)",HOST)
        self.assertIn("getResourceProviderRoot()+",HOST)
        self.assertIn('"fx-window.html"',HOST)
        self.assertIn("attachOwnedFloatingWindow(*this,mixer_)",HOST)
        self.assertIn("placeFloatingEditor(*this,mixer_,recenter)",HOST)
        self.assertIn("std::make_unique<BuiltinFxDesktopWindow>",HOST)
        self.assertIn("maxSimultaneousBuiltinWindows=16",HOST)
        self.assertIn("floatingBuiltinFx_.clear();",HOST)
        self.assertIn('if(action=="floatingBuiltinOpen")return openFloatingBuiltin(args);',HOST)
        self.assertIn("floatingBuiltinPresent(ref)",HOST)
        self.assertIn("if(!permitted.contains(action))return;",HOST)
        self.assertIn('permitted={"nativeEqSnapshot","nativeEqSetBand","nativeBuiltinFxUserPreset"};',HOST)
        self.assertIn("safe->onAction(request); // existing single native DSP request fence",HOST)
        self.assertIn("browser_.onNativeEvent",HOST)
        self.assertIn('event=="arMixConsoleSnapshot"',HOST)
        self.assertIn("floatingFxRelayTick_%4!=0",HOST)
        self.assertIn("void resizeToPanel(const juce::var& size)",HOST)
        for catalog in (
            "channel-eq","channel-compressor","graphic-eq31",
            "arson-harmony","vocal-throw","8d-sonku"
        ):
            self.assertIn("armix.builtin."+catalog+".v1",script)
            self.assertIn("armix.builtin."+catalog+".v1",HOST)
        self.assertIn("Object.freeze({",script)
        self.assertIn("selectNativeReadModel",script)
        self.assertIn('["CHANNEL_ID","LOGICAL_ONLY","RETAINED_OFFLINE"]',script)
        self.assertIn("fxEditorState:()=>current?.view||null",script)
        self.assertIn("script.src=\"assets/\"+entry[0]",script)
        self.assertNotIn("AudioContext(",script)
        self.assertNotIn("AudioWorklet",script)
        self.assertNotIn("createEditorIfNeeded",script)
        self.assertIn("def floating_fx_html()",builder)
        self.assertIn('"fx-window.html"',builder)
        self.assertIn("assets/arsonmix-approved-runtime.css",builder)
        self.assertIn("approved-floating-fx-bridge.js",builder)
        self.assertIn("${ARMIX_APPROVED_GUI_DIR}/fx-window.html",cmake)
        # The public Pages script never receives an alternate product GUI.
        public=(ROOT/"scripts/build_pages.py").read_text(encoding="utf-8")
        self.assertNotIn("fx-window.html",public)

    def test_all_editor_paths_use_one_host(self):
        self.assertEqual(HOST.count("std::make_unique<PluginEditorWindow>"),4)
        self.assertIn("prepared.processor->getName(),editor,*this)",HOST)
        self.assertIn("owner->processor->getName(),editor,*this)",HOST)
        self.assertIn("processor->getName(), editor, *this",HOST)
        self.assertIn("std::make_unique<AudioSetupWindow>",HOST)
        self.assertIn("}, *safe);",HOST)
if __name__=="__main__":unittest.main()
