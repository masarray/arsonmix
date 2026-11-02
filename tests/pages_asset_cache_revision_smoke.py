"""Pages deployment regression: CSS/JS cache revision + both public GUI routes.

The source HTML used a fixed ?v=inter-utility-v7 even after the user-supplied
PNG-backed MUTE was removed, leaving browsers free to reuse cached old CSS/JS.
Only real published pages (not source alone) prove this is fixed.
"""
import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/"site/preview-v2/assets"
ASSET_RE=re.compile(r"""(?:href|src)=["']assets/(?P<name>[A-Za-z0-9._-]+\.(?:css|js))\?rev=(?P<rev>[0-9a-f]{16})["']""")
RAW_ASSET_RE=re.compile(r"""(?:href|src)=["']assets/[A-Za-z0-9._-]+\.(?:css|js)(?:\?[^"']*)?["']""")


def main():
    with tempfile.TemporaryDirectory(prefix="arsonmix-pages-") as work:
        output=Path(work)/"dist"
        subprocess.run([
            sys.executable,str(ROOT/"scripts/build_pages.py"),
            "--out",str(output),"--sha","cache-test","--branch","main"
        ],cwd=ROOT,check=True,capture_output=True,text=True)
        public=(output/"preview/mixer.html").read_text(encoding="utf-8")
        direct=(output/"preview-v2/index.html").read_text(encoding="utf-8")
        assert public==direct,"/preview/ and /preview-v2/ HTML must be the same tested revision"
        matches=list(ASSET_RE.finditer(public))
        raw=list(RAW_ASSET_RE.finditer(public))
        assert len(matches)>=10,(len(matches),len(raw))
        assert len(matches)==len(raw), "Unversioned CSS/JS or obsolete ?v= token remains in output"
        names=set()
        for m in matches:
            name=m.group("name")
            names.add(name)
            src=ASSETS/name
            assert src.is_file(),("missing source",name)
            digest=hashlib.sha256(src.read_bytes()).hexdigest()[:16]
            assert m.group("rev")==digest,(name,"wrong content hash")
            assert (output/"preview/assets"/name).read_bytes()==src.read_bytes(),("wrong public asset",name)
            assert (output/"preview-v2/assets"/name).read_bytes()==src.read_bytes(),("wrong direct asset",name)
        required={"mixer-control-keys.css","mixer-control-keys.js","mixer-wire-dots.css"}
        assert required<=names,("missing required styles",required-names)
        css=(output/"preview/assets/mixer-control-keys.css").read_text(encoding="utf-8")
        js=(output/"preview/assets/mixer-control-keys.js").read_text(encoding="utf-8")
        assert "MUTE is rendered entirely from CSS/HTML" in css
        assert "mx-mute-face" in js
        assert "M-MUTE-OFF.png" not in css+js and "M-MUTE-ON.png" not in css+js
        for name in ("M-MUTE-OFF.png","M-MUTE-ON.png"):
            assert not (output/"preview/assets"/name).exists(),"reference screenshot shipped in public preview"
        print(f"PASS: {len(matches)} hash-versioned CSS/JS, no cached PNG MUTE, both Pages routes identical")


if __name__=="__main__":
    main()
