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
        self.assertIn("routedEditorOwner_==owner",HOST)
        self.assertIn("editorProcessor_==owner->processor.get()",HOST)
        self.assertIn("editorWindow_->present()",HOST)
        self.assertIn("if(logicalVendorEditor_)",HOST)
        self.assertIn("existing.window->present()",HOST)
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
        self.assertIn('shownWindow->present()',opened)
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

    def test_all_editor_paths_use_one_host(self):
        self.assertEqual(HOST.count("std::make_unique<PluginEditorWindow>"),3)
        self.assertIn("prepared.processor->getName(),editor,*this)",HOST)
        self.assertIn("owner->processor->getName(),editor,*this)",HOST)
        self.assertIn("processor->getName(), editor, *this",HOST)
        self.assertIn("std::make_unique<AudioSetupWindow>",HOST)
        self.assertIn("}, *safe);",HOST)
if __name__=="__main__":unittest.main()
