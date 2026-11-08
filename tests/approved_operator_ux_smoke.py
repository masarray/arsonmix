"""V29 operator-console regression: one public/native GUI, clean source-owner evolution."""
from __future__ import annotations
from pathlib import Path
import sys
import shutil
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from compose_operator_ux import apply_operator_ux, CLOSE_ICON

class OperatorConsoleTests(unittest.TestCase):
    def test_single_composed_gui_stays_faithful_and_editable(self):
        source=ROOT/"site/preview-v2"
        html_before=(source/"index.html").read_text(encoding="utf-8")
        chrome_before=(source/"assets/mixer-chrome-material.css").read_bytes()
        with tempfile.TemporaryDirectory(prefix="armix-operator-") as tmp:
            assets=Path(tmp)
            for name in ("mixer-chrome-material.css","mixer-plugin-browser.css"):
                shutil.copy2(source/"assets"/name,assets/name)
            compiled=apply_operator_ux(html_before,assets)
            chrome=(assets/"mixer-chrome-material.css").read_text(encoding="utf-8")
            plugin=(assets/"mixer-plugin-browser.css").read_text(encoding="utf-8")
            self.assertEqual(compiled.count('id="trackCount"'),1)
            self.assertIn('<header class="window-title">',compiled)
            self.assertNotIn("OPERATOR-FIRST",compiled)
            self.assertNotIn('<div class="mixer-title">',compiled)
            self.assertNotIn('id="pluginMinimize"',compiled)
            self.assertIn("const density='comfort'",compiled)
            self.assertIn('id="densitySelect"',compiled) # stable legacy JS contract, hidden
            self.assertIn('id="densitySelect" class="density-select" hidden',compiled)
            self.assertEqual(compiled.count(CLOSE_ICON),6)
            self.assertIn("V29 adaptive 50/50 console",chrome)
            self.assertIn("position:fixed;z-index:115;left:8px",chrome)
            self.assertIn("max-height:calc(100dvh - 16px);overflow-y:auto",chrome)
            self.assertEqual(compiled.count('id="channelIdentity"'),1)
            self.assertLess(
                compiled.index('</div>\n    <div id="channelIdentity"'),
                compiled.index('<script src="assets/wire-graph-model.js'))
            self.assertIn("focus({preventScroll:true})",compiled)
            self.assertIn("positionChannelIdentity(dialog,anchor)",compiled)
            self.assertIn("installConsoleSplit",compiled)
            self.assertIn("ResizeObserver",compiled)
            self.assertIn("MutationObserver",compiled)
            self.assertIn("--ux-fx-balanced-h',fx+'px'",compiled)
            self.assertIn("--ux-fx-balanced-h",chrome)
            self.assertIn("calc(50%",chrome)
            self.assertIn(".console-body.collapsed-route{--route-h:23px",chrome)
            self.assertIn("position:fixed!important",plugin)
            self.assertNotIn(".plugin-browser.minimized",plugin)
            self.assertIn("calc(100vw - 28px)",plugin)
            self.assertIn("calc(100dvh - 90px)",plugin)
            self.assertNotIn("#pluginBrowser #pluginClose:active",plugin)
            shared=(source/"assets/mixer-contextual-buttons.css").read_text(encoding="utf-8")
            self.assertIn("Single authoritative modal Close",shared)
            self.assertIn("#pluginClose,#fastClose,#wireConfigClose",shared)
            self.assertIn("flex:0 0 24px!important",shared)
            self.assertIn("background:linear-gradient(#cc4753,#982a36)",shared)
            # No extra CSS file, one canonical assembler, source history untouched.
            self.assertEqual((source/"index.html").read_text(encoding="utf-8"),html_before)
            self.assertEqual((source/"assets/mixer-chrome-material.css").read_bytes(),chrome_before)
            self.assertEqual(len(list(assets.iterdir())),2)
    def test_all_floating_windows_share_close_and_readback_power_chrome(self):
        assets=ROOT/"site/preview-v2/assets"
        shared=(assets/"mixer-contextual-buttons.css").read_text(encoding="utf-8")
        fx=(assets/"mixer-fx-rack.css").read_text(encoding="utf-8")
        eq=(assets/"mixer-native-peq.js").read_text(encoding="utf-8")
        comp=(assets/"mixer-native-compressor.js").read_text(encoding="utf-8")
        for name in ("identityClose","pluginClose","fastClose",
                     "wireConfigClose","wireWsClose","routeChoiceClose",
                     "mx-peq-close","mx-comp-close","armix-floating-close"):
            self.assertIn(name,shared)
        for state in ("on","off","rack-off","unknown"):
            self.assertIn('data-fx-state="'+state+'"',shared)
        self.assertIn("flex:0 0 24px!important",shared)
        self.assertIn("linear-gradient(#cc4753,#982a36)",shared)
        self.assertNotIn(".mx-peq .mx-peq-close:active",fx)
        self.assertNotIn(".mx-comp-power.bypassed",fx)
        self.assertIn('power.dataset.fxState=fxState;',eq)
        self.assertIn('power.dataset.fxState=fxState;',comp)
        self.assertIn('else updateKnobs();',eq)
        self.assertIn('syncPower(); // accepted graph readback',comp)
        bridge=(ROOT/"web/approved-native-bridge.js").read_text(encoding="utf-8")
        self.assertNotIn(".armix-audio-dialog .armix-floating-close{",bridge)

    def test_native_hooks_and_audio_input_default(self):
        build=(ROOT/"scripts/build_approved_native_gui.py").read_text(encoding="utf-8")
        bridge=(ROOT/"web/approved-native-bridge.js").read_text(encoding="utf-8")
        engine=(ROOT/"src/LiveAudioEngine.h").read_text(encoding="utf-8")
        self.assertIn("openChannelIdentity,closeChannelIdentity",build)
        self.assertIn("?.applyIdentity?.())return;",build)
        self.assertIn("openNativeIdentity(t,event.target)",bridge)
        self.assertIn('if(action==="editIdentity"||action==="rename")',bridge)
        self.assertIn("return true;\n      }\n      if ([\"wireExpand\",\"wireSource\",\"wireRoute\"].includes(action))",bridge)
        self.assertIn("openNativeIdentity(t,event.target)",bridge)
        self.assertIn('event.key!=="F2"',bridge)
        self.assertIn("identityChannelFor=t=>",bridge)
        self.assertIn("Channel updated and stored in native workspace",bridge)
        self.assertIn("channelIcons:CHANNEL_ICONS",build)
        self.assertIn("appearanceFromNative(next)",bridge)
        self.assertIn("arMixChannelIdentitySet",bridge)
        self.assertIn("arMixChannelIdentityAck",bridge)
        self.assertIn('const legacyIdentityKey="arsonmix.native.channel-presentation.v1"',bridge)
        self.assertIn("retireLegacyRow(p.channelId)",bridge)
        self.assertNotIn("const identityStoreKey=",bridge)
        appearance=(ROOT/"src/NativeChannelAppearanceStore.h").read_text(encoding="utf-8")
        self.assertIn("CANNOT_ATOMICALLY_COMMIT_CHANNEL_APPEARANCE",appearance)
        self.assertIn("STALE_CHANNEL_APPEARANCE_REVISION",appearance)
        self.assertIn("CHANNEL_NAME_ALREADY_IN_USE",appearance)
        self.assertIn("deviceManager_.initialise (2, 2, savedSettings, !savedRoute)",engine)
        self.assertIn("active.enabledInputs",bridge)
        self.assertIn("data-audio-asio4all-hint",bridge)
        self.assertIn("armix-floating-close",bridge)
        self.assertIn("viewportWidth:Math.round(window.innerWidth)",bridge)
        self.assertIn("presentNativeOnlyTools()",bridge)
        main=(ROOT/"src/Main.cpp").read_text(encoding="utf-8")
        self.assertIn("browser_.resized(); // first visible HWND",main)
        self.assertIn("available.getWidth() - 24",main)
        self.assertNotIn("A virtual cable does not automatically play through headphones",bridge)

if __name__=="__main__":
    unittest.main()
