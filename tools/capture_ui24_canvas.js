// Paste into DevTools Console on https://www.soundcraft.com/ui24-software-demo/mixer.html
// Download-only capture. No upload, no remote requests, no proprietary JS extraction.
(async function () {
  const canvases = [...document.querySelectorAll("canvas")];
  const stamp = new Date().toISOString().replace(/[:.]/g, "-");
  const manifest = {url: location.href, timestamp: new Date().toISOString(),
                    dpr: window.devicePixelRatio, canvases: []};
  function save(blob, filename) {
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url; a.download = filename;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
  }
  for (const [i, c] of canvases.entries()) {
    const r = c.getBoundingClientRect();
    const rec = {index:i, width:c.width, height:c.height,
       x:r.x,y:r.y,cssWidth:r.width,cssHeight:r.height};
    try {
      const blob = await new Promise((resolve,reject) => {
        try { c.toBlob(resolve,"image/png"); } catch(e) { reject(e); }
      });
      if (!blob) throw new Error("canvas cannot be exported");
      save(blob, "ui24-canvas-" + i + "-" + stamp + ".png");
      rec.capture = "downloaded"; rec.bytes = blob.size;
    } catch (err) { rec.capture = "failed"; rec.error = String(err); }
    manifest.canvases.push(rec);
  }
  save(new Blob([JSON.stringify(manifest,null,2)], {type:"application/json"}),
       "ui24-canvas-manifest-" + stamp + ".json");
  console.table(manifest.canvases);
})();
