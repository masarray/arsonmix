"""Compile owner-approved ribbon into existing Pages/native CSS owner."""
from pathlib import Path

def apply_clean_ribbon(html: str, assets: Path) -> str:
    # Replace only the two historical bank vectors at assembly time. The
    # original button IDs, event handlers and approved source remain intact.
    for old, new in (("M14,17L9,12l5-5V17Z", "M15 5L8 12l7 7"),
                     ("M10,17V7l5,5l-5,5Z", "M9 5l7 7-7 7")):
        anchor = '<path d="' + old + '"/>'
        if html.count(anchor) != 1:
            raise ValueError("Clean Ribbon V6 bank glyph anchor drift: " + old)
        html = html.replace(anchor, '<path d="' + new + '"/>', 1)
    # Navigating a category is not an unrelated mixer command. Keep the
    # pinned OUT through the cascade until a real action or click-away;
    # scope the exemption to actual category buttons, not the whole menu.
    old = "#moreTools > summary,[data-wire-open],#routeChoice"
    new = "#moreTools > summary,#moreTools [data-menu-category],[data-wire-open],#routeChoice"
    if html.count(old) != 1:
        raise ValueError("Clean Ribbon V6 WIRE navigation anchor drift")
    html = html.replace(old, new, 1)
    ribbon_css = Path(__file__).with_name("clean_ribbon_v6.css").read_text(encoding="utf-8")
    chrome = assets / "mixer-chrome-material.css"
    chrome.write_text(chrome.read_text(encoding="utf-8")+"\n"+ribbon_css+"\n",encoding="utf-8")
    tag = '<script defer src="assets/clean-ribbon-v6.js"></script>'
    if html.count("</body>") != 1 or tag in html:
        raise ValueError("Clean Ribbon V6 assembly anchor drift")
    return html.replace("</body>",tag+"\n</body>",1)
