/* Verified official ArSonMix direct downloads. GitHub API repairs stale Pages releases. */
(() => {
  "use strict";
  const cards = [...document.querySelectorAll("[data-platform]")];
  const summary = document.getElementById("release-summary");
  // All top-level Download links are normal #download anchors.
  // Only the three OS-specific cards may point to verified release files.
  if (!summary || cards.length !== 3) return;
  const keys = ["windows", "linux", "macos"];
  const labels = { windows: "Windows", linux: "Linux", macos: "macOS" };
  const suffixes = {
    windows: ["Windows-x64-Setup.zip", "Windows-x64-Setup.exe"],
    linux: ["Linux-x86_64.AppImage", "Linux-x86_64.tar.gz"],
    macos: ["macOS-universal.dmg", "macOS-arm64.dmg", "macOS-x64.dmg"]
  };
  const provenance = { windows: "INSTALLER_PROVENANCE.json",
    linux: "LINUX_PROVENANCE.json", macos: "MACOS_PROVENANCE.json" };
  const unsafe = /unqualified|debug|symbols?|sources?|manifest|test-fixture/i;
  const latestApi = "https://api.github.com/repos/masarray/arsonmix/releases?per_page=40";
  const tagPattern = /^v(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|rc)\.(\d+))?$/;

  function versionParts(tag) {
    const found = typeof tag === "string" && tagPattern.exec(tag);
    if (!found) return null;
    const stage = { alpha: 0, beta: 1, rc: 2 };
    return [Number(found[1]), Number(found[2]), Number(found[3]),
      found[4] ? stage[found[4]] : 3, found[5] ? Number(found[5]) : 0];
  }
  function compareTags(a, b) {
    const left = versionParts(a), right = versionParts(b);
    if (!left || !right) return 0;
    for (let i = 0; i < left.length; i++) {
      if (left[i] !== right[i]) return left[i] > right[i] ? 1 : -1;
    }
    return 0;
  }
  function validUrl(value, name, tag) {
    try {
      const url = new URL(value);
      return url.protocol === "https:" && url.hostname === "github.com"
        && url.pathname === "/masarray/arsonmix/releases/download/"
          + encodeURIComponent(tag) + "/" + encodeURIComponent(name)
        && !url.search && !url.hash && !url.username && !url.password;
    } catch { return false; }
  }
  function validAsset(data, platform) {
    if (!data || typeof data !== "object" || !versionParts(data.tag)
        || !/^[a-f0-9]{64}$/.test(data.sha256)
        || !Number.isSafeInteger(data.size) || data.size < 2097152
        || typeof data.name !== "string" || unsafe.test(data.name.toLowerCase())) return false;
    return suffixes[platform].some(s => data.name === "ArSonMix-" + data.tag + "-" + s)
      && validUrl(data.url, data.name, data.tag);
  }
  function isPreferred(candidate, current, platform) {
    if (!validAsset(candidate, platform)) return false;
    if (!validAsset(current, platform)) return true;
    const order = compareTags(candidate.tag, current.tag);
    if (order !== 0) return order > 0;
    const rank = item => suffixes[platform].findIndex(s => item.name.endsWith("-" + s));
    return rank(candidate) >= 0 && rank(candidate) < rank(current);
  }
  function emptyCatalog() { return { windows: null, linux: null, macos: null }; }
  function signedAsset(asset, name, tag, minSize = 1) {
    return asset && asset.name === name && asset.state === "uploaded"
      && Number.isSafeInteger(asset.size) && asset.size >= minSize
      && typeof asset.digest === "string" && /^sha256:[a-f0-9]{64}$/.test(asset.digest)
      && validUrl(asset.browser_download_url, name, tag);
  }
  function liveCatalog(releases) {
    if (!Array.isArray(releases)) throw Error("Invalid GitHub Releases response");
    const selected = emptyCatalog();
    for (const release of releases) {
      if (!release || release.draft || !versionParts(release.tag_name)
          || !/^[a-f0-9]{40}$/.test(release.target_commitish)
          || !Array.isArray(release.assets)) continue;
      const tag = release.tag_name, assets = release.assets;
      if (assets.filter(a => signedAsset(a, "SHA256SUMS.txt", tag)).length !== 1) continue;
      for (const platform of keys) {
        if (assets.filter(a => signedAsset(a, provenance[platform], tag)).length !== 1) continue;
        for (const suffix of suffixes[platform]) {
          const name = "ArSonMix-" + tag + "-" + suffix;
          const matches = assets.filter(a => signedAsset(a, name, tag, 2097152));
          if (matches.length !== 1) continue;
          const candidate = { tag, name, url: matches[0].browser_download_url,
            sha256: matches[0].digest.slice(7), size: matches[0].size,
            prerelease: Boolean(release.prerelease), publishedAt: release.published_at };
          if (isPreferred(candidate, selected[platform], platform))
            selected[platform] = candidate;
          break;
        }
      }
    }
    return selected;
  }
  function unavailable(card, message) {
    card.classList.remove("is-available");
    const link = card.querySelector("[data-release-link]");
    link.removeAttribute("href");
    link.removeAttribute("target");
    link.setAttribute("aria-disabled", "true");
    link.setAttribute("tabindex", "-1");
    card.querySelector("[data-release-state]").textContent = "COMING SOON";
    card.querySelector("[data-link-label]").textContent = "Not available yet";
    card.querySelector("[data-release-note]").textContent =
      message || "No verified native package has been published yet.";
  }
  function available(card, item) {
    const link = card.querySelector("[data-release-link]");
    const zipped = card.dataset.platform === "windows" && item.name.endsWith("-Setup.zip");
    card.classList.add("is-available");
    card.querySelector("[data-release-state]").textContent =
      item.prerelease ? "EARLY ACCESS" : "AVAILABLE";
    card.querySelector("[data-link-label]").textContent =
      zipped ? "Download installer ZIP" : "Download for " + labels[card.dataset.platform];
    link.href = item.url;
    link.removeAttribute("target");
    link.removeAttribute("aria-disabled");
    link.removeAttribute("tabindex");
    link.setAttribute("aria-label", "Download verified " + item.name);
    card.querySelector("[data-release-note]").textContent =
      item.tag + " · " + (item.size / 1048576).toFixed(1) + " MB" +
      (zipped ? " · Extract ZIP, run Setup" : "");
  }
  function render(selected, source) {
    let count = 0;
    for (const card of cards) {
      const key = card.dataset.platform, item = selected[key];
      if (validAsset(item, key)) { available(card, item); ++count; }
      else unavailable(card);
    }
    const availablePlatforms = keys.filter(k => validAsset(selected[k], k));
    summary.textContent = count
      ? "Ready to download · " + availablePlatforms.map(k => labels[k]).join(" · ")
      : "Downloads are being prepared. Explore the mixer in your browser.";
  }
  async function load() {
    let selected = emptyCatalog(), hasStatic = false;
    try {
      const response = await fetch("./release-catalog.json", { cache: "no-store" });
      if (!response.ok) throw Error("Static catalog unavailable");
      const data = await response.json();
      if (!data || data.schemaVersion !== 1 || !data.platforms
          || typeof data.platforms !== "object") throw Error("Invalid catalog");
      for (const key of keys)
        if (validAsset(data.platforms[key], key)) selected[key] = data.platforms[key];
      hasStatic = true;
    } catch { /* Recover from public GitHub Releases if Pages is stale/offline. */ }
    render(selected, "static");
    try {
      // Read-only public release metadata. No user token, no workflow or paid CI.
      // Static same-origin catalog remains usable if GitHub rate limits this API.
      const response = await fetch(latestApi, {
        cache: "no-store", headers: { Accept: "application/vnd.github+json" }
      });
      if (!response.ok) throw Error("Live release query unavailable");
      const latest = liveCatalog(await response.json());
      for (const key of keys)
        if (isPreferred(latest[key], selected[key], key)) selected[key] = latest[key];
      render(selected, "live");
    } catch {
      if (!hasStatic && !keys.some(k => validAsset(selected[k], k)))
        summary.textContent = "Downloads are temporarily unavailable. Please try again.";
    }
  }
  cards.forEach(card => unavailable(card));
  load();
})();
