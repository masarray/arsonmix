"""V60 native WebView2 fader contract: reversible exact Pages source and one ACK.

No audio hardware is claimed. This cheap assembly gate prevents pointer-source
changes from silently breaking JUCE HTML or moving the native unity detent.
"""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "arsonmix_native_v60_bundle", ROOT / "scripts/build_approved_native_gui.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


def run():
    public = bundle.accepted_pages_html()
    native = bundle.native_html(public)
    assert bundle.public_from_native(native) == public, "Native Pages not reversible"
    assert " let gain=Math.round((max-(max-min)*frac)*10)/10;" in public
    assert " let gain=Math.round(faderDbFromTravelPercent(100*(1-frac),max===6)*10)/10;" in native
    assert " const unityY=travel.top+travel.height*(max/(max-min));" in public
    assert " const unityY=travel.top+travel.height*(1-faderPercent(0,max===6)/100);" in native
    assert "FADER_TAPER=Object.freeze" in native
    assert native.count("handleField({target:g.input,type:'change',altKey:!!ev.altKey});")==1
    assert native.count("if(window.__JUCE__?.backend){window.__ARMIX_NATIVE_M2__?.consumeField?.(event);return;}")==1
    assert "consumeField?.({target:g.input,type:'change'})" not in native
    assert native.count("FADER_UNITY_CAPTURE_PX=7,FADER_UNITY_RELEASE_PX=12")==1
    print("PASS V60 native fader taper/zero detent/final native ACK and reversible Pages")


if __name__ == "__main__":
    run()
