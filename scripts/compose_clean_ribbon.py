"""Compile owner-approved ribbon into existing Pages/native CSS owner."""
from pathlib import Path

def apply_clean_ribbon(html: str, assets: Path) -> str:
    ribbon_css = Path(__file__).with_name("clean_ribbon_v6.css").read_text(encoding="utf-8")
    chrome = assets / "mixer-chrome-material.css"
    chrome.write_text(chrome.read_text(encoding="utf-8")+"\n"+ribbon_css+"\n",encoding="utf-8")
    tag = '<script defer src="assets/clean-ribbon-v6.js"></script>'
    if html.count("</body>") != 1 or tag in html:
        raise ValueError("Clean Ribbon V6 assembly anchor drift")
    return html.replace("</body>",tag+"\n</body>",1)
