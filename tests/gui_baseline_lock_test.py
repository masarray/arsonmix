"""Prevents resurrecting pre-v0.8 GUI or publishing stale preview branches."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
app=(root/'web/app.js').read_text(encoding='utf8')
css=(root/'web/styles.css').read_text(encoding='utf8')
standalone=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf8')
page=(root/'.github/workflows/pages-publish.yml').read_text(encoding='utf8')
ui=(root/'.github/workflows/ui-smoke.yml').read_text(encoding='utf8')
readme=(root/'README.md').read_text(encoding='utf8')
agents=(root/'AGENTS.md').read_text(encoding='utf8')
lock=(root/'docs/GUI_BASELINE_LOCK_2026-10-25.md').read_text(encoding='utf8')
assert '343692fc9b702661a8455cca30f0ffd108907324' in lock
assert 'checkpoint/gui-v0.8-approved-20261025' in lock
assert 'docs/GUI_BASELINE_LOCK_2026-10-25.md' in readme
assert 'docs/GUI_BASELINE_LOCK_2026-10-25.md' in agents
assert "ref: fix/fader-thumb-symmetry-polish" not in page
assert "branches: [main]" in page and "head_sha" in page
assert "branches: [main]" in ui and "pull_request:" in ui
assert 'masterPeakDb' not in app
assert 'data-master-tab' not in app
assert 'masterLufs' in app and 'masterDbtp' in app
assert '<style>'+css+'</style>' in standalone
assert '<script>'+app+'</script>' in standalone
assert 'i<12' not in app or 'master' in app
assert 'masterLeft' in app and 'data-fxpower' in app
assert 'saveFxPreset' in standalone and 'menuScene' in standalone
assert (root/'site/preview/index.html').is_file()
assert 'fix/fader-thumb-symmetry-polish' not in page
print('PASS: approved GUI contract, exactly qualified Pages main source, offline parity')
