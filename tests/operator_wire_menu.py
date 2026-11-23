"""Playwright actions through ArSonMix's visible routing More menu.

A hidden legacy toolbar button retains its DOM identity for workspace
state/readback but is NOT an operator click target. Tests must follow the
approved /preview/ UI, never force-click hidden compatibility controls.
"""


def _choose_wire_menu(page, kind):
    more = page.locator("#moreTools")
    if not more.evaluate("(el) => el.open"):
        more.locator(":scope > summary").click()
    choice = more.locator(f'[data-wire-open="{kind}"]')
    assert choice.is_visible(), f"More > WIRE TOOLS > {kind} unavailable"
    choice.click()
    assert not more.evaluate("(el) => el.open"), "Wire command did not dismiss More"


def open_wire_config(page):
    """Expose modeless Wire Setup without toggling a currently open editor off."""
    panel = page.locator("#wireConfigWindow")
    if panel.is_visible():
        return
    _choose_wire_menu(page, "config")
    assert panel.is_visible(), "WIRE Setup did not open from More"


def toggle_wire_routes(page):
    """Toggle operator Routes with visible Close button or More > Routes."""
    panel = page.locator("#wireWorkspace")
    if panel.is_visible():
        panel.locator("#wireWsClose").click()
        assert panel.is_hidden(), "WIRE Routes did not close"
        return
    _choose_wire_menu(page, "routes")
    assert panel.is_visible(), "WIRE Routes did not open from More"
