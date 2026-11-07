/* Public GitHub Releases are the only source of download links.
   Never send visitors to an experimental/unqualified Actions artifact. */
(() => {
  "use strict";
  const ROOT = "https://github.com/masarray/arsonmix";
  const RELEASES = ROOT + "/releases";
  const API = "https://api.github.com/repos/masarray/arsonmix/releases?per_page=15";
  const cards = [...document.querySelectorAll("[data-platform]")];
  const summary = document.getElementById("release-summary");
  if (!summary || cards.length !== 3) return;

  const unsafe = /unqualified|debug|symbols?|sources?|checksum|sha256|manifest|test-fixture/i;
  const kinds = {
    windows: name => /(windows|win[-_. ]?(?:x64|amd64|arm64))/.test(name) && /\.(exe|msi|zip)$/.test(name),
    linux: name => /linux/.test(name) && /(\.appimage|\.deb|\.rpm|\.tar\.gz|\.zip)$/.test(name),
    macos: name => /(macos|osx|darwin|mac[-_. ]?(?:arm64|x64|universal))/.test(name) && /\.(dmg|pkg|zip)$/.test(name)
  };
  const priority = {
    windows: name => name.endsWith(".zip") ? 0 : name.endsWith(".msi") ? 1 : 2,
    linux: name => name.endsWith(".appimage") ? 0 : name.endsWith(".deb") ? 1 : 2,
    macos: name => name.endsWith(".dmg") ? 0 : name.endsWith(".pkg") ? 1 : 2
  };
  const validUrl = (url) => {
    try {
      const p = new URL(url);
      return p.protocol === "https:" && p.hostname === "github.com"
        && p.pathname.startsWith("/masarray/arsonmix/releases/download/");
    } catch { return false; }
  };

  function selectedAsset(releases, platform) {
    for (const release of releases) {
      if (!release || release.draft || !Array.isArray(release.assets)) continue;
      const options = release.assets.filter(asset => {
        const name = String(asset.name || "").toLowerCase();
        return asset.size > 0 && !unsafe.test(name) && kinds[platform](name)
          && validUrl(asset.browser_download_url);
      });
      options.sort((a, b) => priority[platform](a.name.toLowerCase()) - priority[platform](b.name.toLowerCase()));
      if (options.length) return { asset: options[0], release };
    }
    return null;
  }

  const setAvailable = (card, match) => {
    const label = card.querySelector("[data-link-label]");
    const link = card.querySelector("[data-release-link]");
    const status = card.querySelector("[data-release-state]");
    const note = card.querySelector("[data-release-note]");
    const platform = card.dataset.platform;
    card.classList.add("is-available");
    status.textContent = match.release.prerelease ? "PREVIEW AVAILABLE" : "AVAILABLE";
    label.textContent = "Download for " + (platform === "macos" ? "macOS" : platform === "linux" ? "Linux" : "Windows");
    link.href = match.asset.browser_download_url;
    link.setAttribute("aria-label", "Download " + match.asset.name + " from official GitHub Releases");
    const size = match.asset.size / (1024 * 1024);
    note.textContent = String(match.release.tag_name || "Release") + " · " + size.toFixed(1) + " MB · GitHub Releases";
  };

  async function verifyReleases() {
    try {
      const response = await fetch(API, { headers: { Accept: "application/vnd.github+json" } });
      if (!response.ok) throw new Error("GitHub release lookup unavailable");
      const releases = await response.json();
      if (!Array.isArray(releases)) throw new Error("Invalid response");
      let available = 0;
      cards.forEach(card => {
        const match = selectedAsset(releases, card.dataset.platform);
        if (match) { setAvailable(card, match); available += 1; }
      });
      summary.textContent = available
        ? "Verified against public GitHub Releases · " + available + " platform" + (available === 1 ? "" : "s") + " available"
        : "No public desktop installer has been published yet. Preview the mixer while development continues.";
    } catch {
      /* A blocked API/rate limit must not become a false claim of availability. */
      summary.textContent = "Live release lookup unavailable. Check the official GitHub Releases page.";
      cards.forEach(card => {
        const link = card.querySelector("[data-release-link]");
        link.href = RELEASES;
      });
    }
  }

  verifyReleases();
})();