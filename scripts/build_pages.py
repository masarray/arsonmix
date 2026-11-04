"""Build ONE public approved /preview/ from ONE physical source directory.

The checked-in site/preview/ files are the approved GUI with PNG MUTE/SOLO/PFL
skin. Never assemble a second renderer or source CSS from retired preview-v2.
The native JUCE packager invokes this same builder for byte-for-byte parity.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "site" / "preview"
KEY_ASSETS = ("mixer-control-keys.css", "mixer-control-keys.js")
REDIRECTS = ("preview-v2", "preview-buttons", "preview-v33", "preview-v34")

p = argparse.ArgumentParser()
p.add_argument("--out", default="dist-pages")
p.add_argument("--sha", default=os.environ.get("GITHUB_SHA", "local"))
p.add_argument("--branch", default=os.environ.get("GITHUB_REF_NAME", "local"))
args = p.parse_args()
dst = Path(args.out).resolve()
if dst in (ROOT, ROOT / "site", SOURCE) or ROOT in dst.parents and dst.parts[-1] in ("site", "preview"):
    raise SystemExit("unsafe Pages output path")
if not (SOURCE / "index.html").is_file():
    raise SystemExit("canonical approved /preview/ source missing")
subprocess.run([sys.executable, str(ROOT / "scripts/build_ui_css.py"), "--check"], check=True)
dst.mkdir(parents=True, exist_ok=True)
shutil.copy2(ROOT / "site/index.html", dst / "index.html")
dest = dst / "preview"
if dest.exists():
    shutil.rmtree(dest)
shutil.copytree(SOURCE, dest)

source_html = (SOURCE / "index.html").read_text(encoding="utf-8")
html = source_html
revisions = {}
for name in KEY_ASSETS:
    asset = SOURCE / "assets" / name
    if not asset.is_file():
        raise SystemExit("approved PNG-button component missing: " + name)
    token = "assets/" + name
    pattern = re.compile(re.escape(token) + r'(?:\?[^"\s>]+)?')
    matches = list(pattern.finditer(html))
    if len(matches) != 1:
        raise SystemExit("expected one approved GUI asset link for " + name)
    digest = hashlib.sha256(asset.read_bytes()).hexdigest()[:16]
    match = matches[0]
    html = html[:match.start()] + token + "?rev=" + digest + html[match.end():]
    revisions[name] = digest
    if (dest / "assets" / name).read_bytes() != asset.read_bytes():
        raise SystemExit("source-to-published PNG-button asset drift: " + name)
if "console-body" not in html or "<iframe" in html:
    raise SystemExit("approved GUI missing or obsolete iframe renderer included")
for essential in ("mixer-wire-dots.css", "knob-ICON.svg", "mute-solo-pfl-56x37.png"):
    if (dest / "assets" / essential).read_bytes() != (SOURCE / "assets" / essential).read_bytes():
        raise SystemExit("approved physical GUI asset drift: " + essential)
(dest / "index.html").write_text(html, encoding="utf-8")
(dest / "build.json").write_text(json.dumps({
    "canonical_url": "/preview/",
    "sha": args.sha,
    "branch": args.branch,
    "kind": "APPROVED_SINGLE_GUI",
    "live_audio": False,
    "wire_source": "site/preview/index.html",
    "wire_source_sha256": hashlib.sha256(source_html.encode("utf-8")).hexdigest(),
    "button_asset_source": "site/preview/assets/",
    "button_css_js_revisions": revisions,
}, indent=2), encoding="utf-8")

redirect_html = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
                 '<meta http-equiv="refresh" content="0;url=../preview/">'
                 '<link rel="canonical" href="../preview/">'
                 '<title>ArSonMix — Official Preview</title></head>'
                 '<body><a href="../preview/">Open the only approved ArSonMix preview</a></body></html>')
for old in REDIRECTS:
    route = dst / old
    if route.exists():
        shutil.rmtree(route)
    route.mkdir(parents=True)
    (route / "index.html").write_text(redirect_html, encoding="utf-8")
(dest / "mixer.html").write_text(
    '<!doctype html><meta http-equiv="refresh" content="0;url=./">'
    '<link rel="canonical" href="./">', encoding="utf-8")
(dst / ".nojekyll").touch()
print("Built ONE /preview/ GUI from site/preview/ source:",
      len(list((dest / "assets").iterdir())), "assets", args.sha)
