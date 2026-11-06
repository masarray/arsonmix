"""Resolve the sole shipped console after checking its exact build provenance.

Authoring HTML is a compiler input, and legacy URLs are redirect shells.
Browser regressions must exercise /preview/index.html, not demand a second
published skin or compare generated HTML with an unassembled input file.
"""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def published_console(dist):
    dist = Path(dist)
    # This existing independent contract checks the complete assembled HTML,
    # compiled CSS bytes/hashes, approved PNG assets and legacy-only redirects.
    subprocess.run(
        [sys.executable, str(ROOT / "tests/single_preview_publication_contract_test.py"),
         str(dist)], cwd=ROOT, check=True, capture_output=True, text=True)
    return dist / "preview/index.html"
