/* Static Pages download catalog: user never navigates GitHub release UI. */
(() => {
  "use strict";
  const cards = [...document.querySelectorAll("[data-platform]")];
  const summary = document.getElementById("release-summary");
  if (!summary || cards.length !== 3) return;
  const labels = { windows: "Windows", linux: "Linux", macos: "macOS" };
  const unsafe = /unqualified|debug|symbols?|sources?|manifest|test-fixture/i;

  function validUrl(value, name, tag) {
    try {
      const url = new URL(value);
      return url.protocol === "https:" && url.hostname === "github.com"
        && url.pathname === "/masarray/arsonmix/releases/download/"
          + encodeURIComponent(tag) + "/" + encodeURIComponent(name);
    } catch { return false; }
  }
  function validAsset(data, platform) {
    if (!data || typeof data !== "object"
        || !/^v\d+\.\d+\.\d+(?:-(?:alpha|beta|rc)\.\d+)?$/.test(data.tag)
        || !/^[a-f0-9]{64}$/.test(data.sha256)
        || !Number.isSafeInteger(data.size) || data.size < 2097152
        || typeof data.name !== "string" || unsafe.test(data.name.toLowerCase())) return false;
    const suffixes = {
      windows: ["Windows-x64-Setup.exe"],
      linux: ["Linux-x86_64.AppImage", "Linux-x86_64.tar.gz"],
      macos: ["macOS-universal.dmg", "macOS-arm64.dmg", "macOS-x64.dmg"]
    };
    return suffixes[platform].some(s => data.name === "ArSonMix-" + data.tag + "-" + s)
      && validUrl(data.url, data.name, data.tag);
  }
  function unavailable(card, msg) {
    card.classList.remove("is-available");
    const link = card.querySelector("[data-release-link]");
    link.removeAttribute("href");
    link.removeAttribute("target");
    link.setAttribute("aria-disabled", "true");
    link.setAttribute("tabindex", "-1");
    card.querySelector("[data-release-state]").textContent = "COMING SOON";
    card.querySelector("[data-link-label]").textContent = "Not available yet";
    card.querySelector("[data-release-note]").textContent =
      msg || "No verified native package has been published yet.";
  }
  function available(card, item) {
    const link = card.querySelector("[data-release-link]");
    card.classList.add("is-available");
    card.querySelector("[data-release-state]").textContent =
      item.prerelease ? "PREVIEW AVAILABLE" : "AVAILABLE";
    card.querySelector("[data-link-label]").textContent =
      "Download for " + labels[card.dataset.platform];
    link.href = item.url;
    link.removeAttribute("target"); // Asset response downloads; never open release UI.
    link.removeAttribute("aria-disabled");
    link.removeAttribute("tabindex");
    link.setAttribute("aria-label", "Download " + item.name);
    card.querySelector("[data-release-note]").textContent =
      item.tag + " · " + (item.size / 1048576).toFixed(1) + " MB" +
      (item.prerelease ? " · Test release" : "");
  }
  async function load() {
    try {
      const response = await fetch("./release-catalog.json", { cache: "no-store" });
      if (!response.ok) throw Error("Release catalog unavailable");
      const catalog = await response.json();
      if (!catalog || catalog.schemaVersion !== 1 || !catalog.platforms
          || typeof catalog.platforms !== "object") throw Error("Invalid catalog");
      let count = 0;
      for (const card of cards) {
        const item = catalog.platforms[card.dataset.platform];
        if (validAsset(item, card.dataset.platform)) { available(card, item); ++count; }
        else unavailable(card);
      }
      summary.textContent = count
        ? "Latest verified downloads · " + count + " platform" +
          (count === 1 ? "" : "s") + " available"
        : "No supported desktop package is published yet. Explore the mixer preview.";
    } catch {
      cards.forEach(card => unavailable(card, "Availability temporarily unavailable."));
      summary.textContent = "Could not check downloads. Please refresh this page.";
    }
  }
  cards.forEach(card => unavailable(card));
  load();
})();
