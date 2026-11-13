"""Integrate LMS in the SINGLE approved /preview/ assembler, never a second page.

LMS is an offline logical DESIGN workspace only. Native PCM is neither
created nor touched; production hardware must implement ACK/readback separately.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LMS_STYLE = ROOT / "site" / "preview-v2" / "assets" / "lms-workspace.css"
LMS_SCRIPTS = ("lms-system-model.js", "lms-workspace.js")


def _once(source: str, target: str, replacement: str, label: str) -> str:
    if source.count(target) != 1:
        raise ValueError("LMS approved /preview/ anchor drift: " + label)
    return source.replace(target, replacement, 1)


def apply_lms(html: str, assets: Path) -> str:
    """Compose one scoped workspace beside the mixer, not over its PCM state."""
    button = ('<button class="tool primary" id="pluginsButton" '
              'title="Search and assign FX plugins (F8)" '
              'aria-controls="pluginBrowser" aria-expanded="false">Plugins · F8</button>')
    html = _once(
        html, button,
        button + '\n    <button type="button" class="tool" id="lmsOpen" '
        'aria-controls="lmsWorkspace" aria-pressed="false" '
        'title="Open Loudspeaker Management offline design workspace">'
        'Speakers</button>',
        "primary toolbar button")
    html = _once(
        html, '  <main class="console-pane">',
        '  <div id="lmsWorkspace" class="lms-workspace" '
        'aria-label="ArSonMix Loudspeaker Management" hidden></div>\n'
        '  <main class="console-pane">',
        "single in-app workspace")
    scripts = "".join('<script defer src="assets/' + name + '?v=lms-m1"></script>\n'
                      for name in LMS_SCRIPTS)
    for name in LMS_SCRIPTS:
        if not (ROOT / "site" / "preview-v2" / "assets" / name).is_file():
            raise FileNotFoundError("LMS asset missing: " + name)
    html = _once(html, '</body>', scripts + '</body>', "script order")

    chrome = assets / "mixer-chrome-material.css"
    current = chrome.read_text(encoding="utf-8")
    scoped = LMS_STYLE.read_text(encoding="utf-8")
    if ".lms-workspace{" in current or "\n.console-pane{" in scoped:
        raise ValueError("LMS CSS ownership collision")
    if not scoped.startswith("/* ArSonMix LMS:"):
        raise ValueError("Unexpected LMS CSS source")
    chrome.write_text(current.rstrip() + "\n" + scoped.rstrip() + "\n",
                      encoding="utf-8")
    return html
