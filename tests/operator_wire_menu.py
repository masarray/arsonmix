"""Playwright actions through ArSonMix's visible routing More menu.

A hidden legacy toolbar button retains its DOM identity for workspace
state/readback but is NOT an operator click target. Tests must follow the
approved /preview/ UI, never force-click hidden compatibility controls.
"""


def open_application_menu(page, category=None):
    """Use the real current category controls; legacy authoring fixture has no cascade."""
    more = page.locator("#moreTools")
    if not more.evaluate("(el) => el.open"):
        more.locator(":scope > summary").click()
    if page.locator("#armix-ribbon-commands").count():
        if category is not None:
            row = more.locator(f'[data-menu-category="{category}"]')
            assert row.is_visible(), f"ArSonMix > {category} unavailable"
            row.click()
            assert row.get_attribute("aria-expanded") == "true"
            assert more.locator(f"#armix-menu-{category}").is_visible()
    return more


def click_demo_meters(page):
    more = open_application_menu(page, "view")
    command = more.locator("#preview")
    assert command.is_visible()
    command.click()
    if not page.locator("#armix-ribbon-commands").count() and more.evaluate("e=>e.open"):
        # The historical authoring fixture requires explicit dismissal.
        more.locator(":scope > summary").click()
    assert not more.evaluate("e=>e.open"), "Demo command left the mixer covered"


def _choose_wire_menu(page, kind):
    more = open_application_menu(page, "settings")
    choice = more.locator(f'[data-wire-open="{kind}"]')
    assert choice.is_visible(), f"ArSonMix > Settings > {kind} unavailable"
    choice.click()
    assert not more.evaluate("(el) => el.open"), "Wire command did not dismiss More"


def open_wire_tool(page, kind):
    """Open the approved operator command; never click a hidden legacy ID."""
    assert kind in ("config", "routes", "matrix"), kind
    panel = page.locator("#wireConfigWindow" if kind == "config" else "#wireWorkspace")
    if panel.is_visible():
        if kind == "config":
            return
        if panel.evaluate("e=>e.classList.contains('is-matrix')") == (kind == "matrix"):
            return
    _choose_wire_menu(page, kind)
    assert panel.is_visible(), f"WIRE {kind} did not open from More"
    if kind != "config":
        assert panel.evaluate("e=>e.classList.contains('is-matrix')") == (kind == "matrix")


def open_wire_config(page):
    """Expose modeless Wire Setup without toggling a currently open editor off."""
    open_wire_tool(page, "config")


def toggle_wire_routes(page):
    """Toggle operator Routes with visible Close button or More > Routes."""
    panel = page.locator("#wireWorkspace")
    if panel.is_visible():
        panel.locator("#wireWsClose").click()
        assert panel.is_hidden(), "WIRE Routes did not close"
        return
    _choose_wire_menu(page, "routes")
    assert panel.is_visible(), "WIRE Routes did not open from More"
