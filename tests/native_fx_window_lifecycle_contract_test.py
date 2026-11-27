# SPDX-License-Identifier: AGPL-3.0-only
"""Fast source contract for *all* three native editor entrypoints."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
HOST=(ROOT/"src/Main.cpp").read_text(encoding="utf-8")
OWNER=(ROOT/"src/NativeOwnedFloatingWindow.h").read_text(encoding="utf-8")
WIN32_HEADER=(ROOT/"src/NativeFxWin32Owner.h").read_text(encoding="utf-8")
WIN32=(ROOT/"src/NativeFxWin32Owner.cpp").read_text(encoding="utf-8")

class NativeFxWindowLifecycle(unittest.TestCase):
    def test_juce_active_editor_registration_released_before_deletion(self):
        self.assertIn("editor->processor.editorBeingDeleted(editor);",OWNER)
        self.assertLess(OWNER.index("editor->processor.editorBeingDeleted(editor);"),
                        OWNER.index("delete editor;"))
        self.assertIn("processor.createEditorAndMakeActive()",OWNER)
        self.assertIn("if(processor.getActiveEditor()!=nullptr)return {};",OWNER)
        self.assertEqual(HOST.count("armix::ui::createHostedAudioEditor("),3)
        self.assertNotIn("createEditorIfNeeded()",HOST)
        self.assertIn("setContentNonOwned (editor, true);",HOST)
        self.assertIn("ownedEditor_.reset();",HOST)
        self.assertIn("std::move(ownedEditor),*this",HOST)

    def test_win32_sdk_isolation_and_real_build_wiring(self):
        self.assertNotIn("#include",WIN32_HEADER)
        self.assertNotIn("#undef",WIN32_HEADER)
        self.assertNotIn("static_cast<HWND>",OWNER)
        self.assertIn("#include <windows.h>",WIN32)
        self.assertNotIn("#undef",WIN32)
        cmake=(ROOT/"CMakeLists.txt").read_text(encoding="utf-8")
        self.assertEqual(cmake.count("src/NativeFxWin32Owner.cpp"),2)
        isolated=(ROOT/"tests/native_fx_window_owner/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("../../src/NativeFxWin32Owner.cpp",isolated)
        self.assertIn("../native_fx_win32_sdk_order_test.cpp",isolated)

    def test_mixer_owned_not_global_topmost(self):
        self.assertIn("attachOwnedFloatingWindow (*this, mixer)",HOST)
        self.assertIn("attachOwnedFloatingWindow",OWNER)
        self.assertIn("win32::attachOwnedPopup",OWNER)
        self.assertIn("win32::raiseOwnedPopup",OWNER)
        self.assertIn("SWP_NOACTIVATE|SWP_NOOWNERZORDER",WIN32)
        self.assertIn("SWP_NOMOVE|SWP_NOSIZE",WIN32)
        self.assertIn("GetAncestor(foreground,GA_ROOTOWNER)!=mixer",WIN32)
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
        self.assertIn("centreOnMixer && !everPresented_",HOST)
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

    def test_primary_out_wire_preserves_unity_and_independent_sum(self):
        bridge=(ROOT/"web/approved-native-bridge.js").read_text(encoding="utf-8")
        vm=(ROOT/"web/approved-wire-view-model.js").read_text(encoding="utf-8")
        self.assertIn('const fixed=edge.primaryOutput===true',bridge)
        self.assertIn('const disabled=!!next.retained||fixed',bridge)
        self.assertIn('label.textContent=fixed?"UNITY":"LVL"',bridge)
        self.assertNotIn('controller.intent("SET_FADER",primary.source,{valueDb:db})',bridge)
        self.assertIn('primaryChannelLevelPlan(snapshot,projected,sourceId,edgeId)',vm)
        self.assertIn('edge.primaryOutput!==true||edge.gainLinear!==1',vm)
        self.assertIn('Parallel SEND level · editable via native EdgeId ACK/readback',bridge)
        assembler=(ROOT/"scripts/build_approved_native_gui.py").read_text(encoding="utf-8")
        self.assertIn("wireDisplayDb=",assembler)
        self.assertIn("if(edge.kind!='primary')",assembler.replace("!==","!="))
        self.assertIn("nativePrimaryDb",assembler)
        self.assertIn("edge.kind==='primary')return; // commit once on change",assembler)

    def test_geq_repeat_rack_click_and_f11_preserve_position(self):
        bridge=(ROOT/"web/approved-native-bridge.js").read_text(encoding="utf-8")
        self.assertIn("const identities=new WeakMap()",bridge)
        self.assertIn("identities.get(root)!==identity",bridge)
        self.assertIn("host,surface.windowKey,t,slot,editor.instanceId",bridge)
        self.assertIn("if(!offsets.has(root))center(root)",bridge)
        self.assertIn("else keepReachable(root)",bridge)
        self.assertIn("requestAnimationFrame(()=>keepReachable(root))",bridge)
        self.assertNotIn("if(recenter||!offsets.has(root))center(root)",bridge)
        self.assertIn("bool everPresented_=false",HOST)
        self.assertIn("centreOnMixer && !everPresented_",HOST)
        self.assertIn("placeFloatingEditor(*this,mixer_,false)",HOST)

    def test_f11_uses_reversible_juce_fullscreen(self):
        bridge=(ROOT/"web/approved-native-bridge.js").read_text(encoding="utf-8")
        self.assertIn('window.addEventListener("keydown",event=>{',bridge)
        self.assertIn('event.key==="F11"||event.code==="F11"||event.keyCode===122',bridge)
        self.assertIn('event.repeat||event.altKey||event.ctrlKey',bridge)
        self.assertIn('backend.emitEvent("arMixWindowFullscreen",{operation:"TOGGLE"})',bridge)
        self.assertIn('withEventListener ("arMixWindowFullscreen"',HOST)
        self.assertIn('toggleOperatorFullscreen();',HOST)
        self.assertIn('maximisedBeforeOperatorFullscreen_=isFullScreen()',HOST)
        self.assertIn('setTitleBarHeight(0);',HOST)
        self.assertIn('setFullScreen(true);',HOST)
        self.assertIn('setFullScreen(false);',HOST)
        self.assertIn('setTitleBarHeight(31);',HOST)
        self.assertIn('browser_.resized();',HOST)
        self.assertNotIn('requestFullscreen(',bridge)

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

    def test_native_receipt_relay_survives_session_open_and_fx_replacement(self):
        # Session Open, preset recall and graph handoff all call
        # closeAllPluginEditors() while the mixer remains running.
        # Unregistering the WebView2 DSP ACK relay there permanently
        # strands newly reopened PEQ/GEQ/Comp/Harmony/SonKu/Throw windows.
        close=HOST.split("void closeAllPluginEditors() noexcept {",1)[1]
        close=close.split("\\n    }",1)[0]
        shutdown=HOST.split("~ConsoleWindow() override",1)[1]
        shutdown=shutdown.split("void closeButtonPressed() override",1)[0]
        self.assertIn("floatingBuiltinFx_.clear()",close)
        self.assertNotIn("browser_.onNativeEvent=nullptr;",close)
        self.assertIn("browser_.onNativeEvent=nullptr;",shutdown)
        self.assertIn("closeAllPluginEditors();",shutdown)
        self.assertLess(shutdown.index("closeAllPluginEditors();"),
                        shutdown.index("browser_.onNativeEvent=nullptr;"))
        self.assertIn("browser_.onNativeEvent=[this]",HOST)
        self.assertIn('event.startsWith("arMixBuiltin")',HOST)
        self.assertIn('owned->window->emit(event,payload)',HOST)

    def test_offline_floating_power_uses_typed_payload_and_correlated_rekey(self):
        # The first FUX popup sent only top-level value/slot and the logical
        # workspace rejected every Power click (payload.slot/enabled missing).
        # A successful bypass bumps logical revision, so the original HWND
        # must follow ONLY its own accepted native ACK.
        bridge=(ROOT/"web/approved-floating-fx-bridge.js").read_text(encoding="utf-8")
        self.assertIn('payload->setProperty("slot",ref->slot);',HOST)
        self.assertIn('payload->setProperty("enabled",request.getProperty("value",{}));',HOST)
        self.assertIn('outgoing->setProperty("payload",juce::var(payload.release()));',HOST)
        self.assertIn('outgoing->setProperty("insertId",ref->instanceId);',HOST)
        self.assertIn('void rekeyFloatingLogicalPower(',HOST)
        self.assertIn('ack.getProperty("status","").toString()!="ACCEPTED"',HOST)
        self.assertIn('owner->instanceId!=oldId',HOST)
        self.assertIn('owner->rekeyFrom=oldId;',HOST)
        self.assertIn('owner->instanceId=newId;',HOST)
        self.assertIn('safe->rekeyFloatingLogicalPower(origin,ack);',HOST)
        self.assertIn('args->setProperty("rekeyFrom",owner.rekeyFrom);',HOST)
        self.assertIn('payload.rekeyFrom===identity.instanceId',bridge)
        self.assertIn('next.view.workspaceToken===payload.workspaceToken',bridge)
        self.assertIn('FX identity changed · reopen from FX Rack',bridge)

    def test_retained_offline_smix_save_uses_durable_logical_export(self):
        # A disconnected retained session used to reject manual Save even
        # after the isolated VST3 edit had committed to the .smix workspace.
        # The policy must allow explicit export only when the control owner
        # reports a committed logical FX workspace, NEVER recapture old PCM.
        policy=(ROOT/"src/NativeMixerSessionRecovery.h").read_text(encoding="utf-8")
        unit=(ROOT/"tests/native_mixer_session_store_test.cpp").read_text(encoding="utf-8")
        self.assertIn("save.retainedOfflinePending=retainedOfflineFxActive_",HOST)
        self.assertIn("logicalMixer_.loaded() && !logicalMixer_.readOnly()",HOST)
        self.assertIn("if(c.retainedOfflinePending) {",policy)
        branch=policy.split("if(c.retainedOfflinePending) {",1)[1].split(
            "if(!c.channelIdInstalled",1)[0]
        self.assertIn("SaveAction::exportLogical",branch)
        self.assertIn("SaveAction::unchanged",branch)
        self.assertIn("if(c.secondaryArmed)",branch)
        self.assertIn("logicalMixer_.exportTo(exportFile",HOST)
        self.assertIn("save.retainedOfflinePending=true;",unit)
        self.assertIn("save.channelIdInstalled=true; // Reconnect",unit)
        self.assertIn("save.secondaryArmed=true;",unit)
        self.assertIn("save.workerBusy=true;",unit)

    def test_borderless_single_surface_fx_and_os_window_drag(self):
        builder=(ROOT/"scripts/build_approved_native_gui.py").read_text(encoding="utf-8")
        script=(ROOT/"web/approved-floating-fx-bridge.js").read_text(encoding="utf-8")
        block=HOST.split("class BuiltinFxDesktopWindow final :",1)[1].split(
            "class AudioSetupWindow final",1)[0]
        self.assertIn("public juce::TopLevelWindow",block)
        self.assertIn("setUsingNativeTitleBar(false)",block)
        self.assertIn("addAndMakeVisible(browser_)",block)
        self.assertIn("browser_.setBounds(getLocalBounds())",block)
        self.assertNotIn("setTitleBarButtonsRequired",block)
        self.assertNotIn("setContentNonOwned",block)
        self.assertNotIn("setResizable(",block)
        self.assertNotIn("juce::DocumentWindow",block)
        self.assertIn("catalog==\"armix.builtin.graphic-eq31.v1\"",block)
        self.assertIn("juce::Point<int>(1430,280)",block)
        self.assertIn("WM_NCLBUTTONDOWN,HTCAPTION",block)
        self.assertIn("TopLevelWindow(label,false)",block)
        self.assertIn("addToDesktop();",block)
        self.assertIn("setTopLeftPosition(-20000,-20000)",block)
        self.assertIn("startTimer(1800)",block)
        self.assertIn("stopTimer()",block)
        self.assertIn("getDesktopWindowStyleFlags() const override",block)
        self.assertIn("windowAppearsOnTaskbar",block)
        self.assertIn("::PostMessageW(",block)
        self.assertIn("requestedW);",block)
        self.assertIn("requestedH);",block)
        self.assertNotIn("requestedH+50",block)
        self.assertNotIn("requestedW+16",block)
        self.assertIn("arMixFloatingDrag",block)
        self.assertIn('document.addEventListener("pointerdown"',script)
        self.assertIn('backend.emitEvent("arMixFloatingDrag",{})',script)
        self.assertIn('event.stopImmediatePropagation()',script)
        self.assertIn("lastGeometry=geometry;",script)
        self.assertIn("measure();",script)
        self.assertIn('backend.emitEvent("arMixFloatingHide",{})',script)
        self.assertIn("fxWindowAbort",builder)

    def test_wire_v66_reel_restored_only_on_native_accepted_remove(self):
        # The V66 330ms physics survives in approved Pages, but native
        # consumeWire() returned false from the demo graph transaction and
        # swallowed the original pointerup animation. Never fake PCM success.
        html=(ROOT/"site/preview-v2/index.html").read_text(encoding="utf-8")
        bridge=(ROOT/"web/approved-native-bridge.js").read_text(encoding="utf-8")
        builder=(ROOT/"scripts/build_approved_native_gui.py").read_text(encoding="utf-8")
        self.assertIn("const WIRE_RETRACT_MS=330",html)
        self.assertIn("function wireRetractPathData(",html)
        self.assertIn("if(replugWireEdge(g.sourceId,g.originDestId,snap?.track.id??null,",html)
        self.assertIn('("function replugWireEdge(sourceId,oldDestId,nextDestId){',builder)
        self.assertIn("consumeNativeWireReplug?.(",builder)
        self.assertIn("animateAcceptedWireRetract:startWireRetraction",builder)
        self.assertIn("consumeNativeWireReplug(sourceId,oldDestId,nextDestId,releasePoint,movingEdgeId)",bridge)
        self.assertIn("g.movingEdgeId)){",html)
        self.assertIn("releasePoint!==undefined",builder)
        self.assertIn("Workspace Remove (no pointer) continues into native wireGraphCommit",builder)
        self.assertIn("e.id===movingEdgeId && e.enabled",bridge)
        self.assertNotIn("scope?.wireEdges?.find(e=>e.enabled &&\n        e.sourceId===sourceId",bridge)
        self.assertIn("pendingNativeWireRetract=removal?",bridge)
        self.assertIn("pendingNativeWireRetract=null; // rejected routing must never reel",bridge)
        self.assertIn('change.op==="REMOVE"',bridge)
        self.assertIn('change.op==="DISCONNECT"',bridge)
        self.assertIn("scope?.animateAcceptedWireRetract?.(held.uiSource,held.point)",bridge)
        self.assertIn("return false; // Native ACK+readback, NEVER the browser reducer",bridge)
        import subprocess
        # Execute the real bridge method in bounded Node, not just strings.
        done=subprocess.run(["node",str(ROOT/"tests/native_wire_replug_identity_node_test.js")],
                            capture_output=True,text=True,timeout=15)
        self.assertEqual(done.returncode,0,done.stdout+"\n"+done.stderr)

    def test_all_six_builtins_escape_mixer_via_actual_native_owned_webview(self):
        script=(ROOT/"web/approved-floating-fx-bridge.js").read_text(encoding="utf-8")
        builder=(ROOT/"scripts/build_approved_native_gui.py").read_text(encoding="utf-8")
        cmake=(ROOT/"CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("class BuiltinFxDesktopWindow final : public juce::TopLevelWindow",HOST)
        self.assertIn("withResourceProvider(assetFor)",HOST)
        self.assertIn("getResourceProviderRoot()+",HOST)
        self.assertIn('"fx-window.html"',HOST)
        self.assertIn("attachOwnedFloatingWindow(*this,mixer_)",HOST)
        self.assertIn("placeFloatingEditor(*this,mixer_,false)",HOST)
        self.assertIn("std::make_unique<BuiltinFxDesktopWindow>",HOST)
        self.assertIn("maxSimultaneousBuiltinWindows=16",HOST)
        self.assertIn("floatingBuiltinFx_.clear();",HOST)
        self.assertIn('if(action=="floatingBuiltinOpen")return;',HOST)
        self.assertIn("floatingBuiltinPresent(ref)",HOST)
        self.assertIn("if(!permitted.contains(action))return;",HOST)
        self.assertIn('permitted={"nativeEqSnapshot","nativeEqSetBand","nativeBuiltinFxUserPreset"};',HOST)
        self.assertIn("safe->onAction(request); // existing single native DSP request fence",HOST)
        self.assertIn("browser_.onNativeEvent",HOST)
        self.assertIn('event=="arMixConsoleSnapshot"',HOST)
        self.assertIn("floatingFxRelayTick_%4!=0",HOST)
        self.assertIn("void resizeToPanel(const juce::var& size)",HOST)
        self.assertIn("recenterAfterInitialPanelSize_=recenter && !hasMeasuredPanel_",HOST)
        self.assertIn("hasMeasuredPanel_=true;",HOST)
        self.assertIn("placeFloatingEditor(*this,mixer_,firstPlacement)",HOST)
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

    def test_inline_fx_clip_single_native_webview_and_exact_instance(self):
        bridge=(ROOT/"web/approved-native-bridge.js").read_text(encoding="utf-8")
        self.assertIn('const inlineFxClip=connected &&',bridge)
        self.assertIn('typeof document.createElement==="function"',bridge)
        self.assertIn("width:1430px!important;min-width:1430px!important",bridge)
        self.assertIn('root.style.setProperty("translate",next.x+',bridge)
        self.assertIn("fxRackVM.editorNavigationLeaseMatches(view,editor",bridge)
        self.assertIn("inlineFxClip?.openExact(",bridge)
        self.assertNotIn('action:"floatingBuiltinOpen"',bridge)
        self.assertIn('if(action=="floatingBuiltinOpen")return;',HOST)
        self.assertIn("event.stopImmediatePropagation(); // prevent old viewport-clamped",bridge)
        self.assertIn("event.altKey",bridge)
        self.assertIn("event.shiftKey",bridge)
        # The old unbounded per-open layer was the field defect. Verify
        # the shared manager, including pointer/focus refocus and bounded order.
        self.assertNotIn("root.style.zIndex=String(++layer)",bridge)
        self.assertIn("window.ArSonMixFxWorkspace?.raise(root)",bridge)
        shared=(ROOT/"site/preview-v2/assets/mixer-contextual-buttons.js").read_text(encoding="utf-8")
        self.assertIn("const layerBase=9000",shared)
        self.assertIn("String(layerBase+i)",shared)
        self.assertIn("document.addEventListener('pointerdown'",shared)
        self.assertIn("document.addEventListener('focusin'",shared)
        self.assertIn("if(!root.hidden&&sameBinding)",bridge)

    def test_all_editor_paths_use_one_host(self):
        self.assertEqual(HOST.count("std::make_unique<PluginEditorWindow>"),4)
        self.assertIn("prepared.processor->getName(),std::move(editor),*this)",HOST)
        self.assertIn("owner->processor->getName(),std::move(ownedEditor),*this)",HOST)
        self.assertIn("processor->getName(), std::move(editor), *this",HOST)
        self.assertNotIn("std::make_unique<AudioSetupWindow>",HOST)
        self.assertNotIn("AudioDeviceSelectorComponent",HOST)
        self.assertIn('emitEventIfBrowserIsVisible("arMixOpenAudioIO"',HOST)
        self.assertIn("device->showControlPanel()",HOST)
if __name__=="__main__":unittest.main()

