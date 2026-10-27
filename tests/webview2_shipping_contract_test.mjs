// SPDX-License-Identifier: AGPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
const read = path => readFileSync(path, "utf8");
test("JUCE WebView2 loader statically linked, independent of target machine DLL", () => {
  const cmake = read("CMakeLists.txt");
  assert.match(cmake, /NEEDS_WEBVIEW2 TRUE/);
  assert.match(cmake, /JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1/);
  const pack = read("scripts/package_native_windows.ps1");
  const verify = read("scripts/verify_native_windows_artifact.ps1");
  assert.match(pack, /webview2LoaderMode = "STATIC_LINKED_IN_EXE_NO_LOADER_DLL_REQUIRED"/);
  assert.match(verify, /static WebView2 loader contract missing/);
});
test("WebView2 failure never counts the browser error page as a loaded native mixer", () => {
  const main = read("src/Main.cpp");
  assert.match(main, /pageLoadHadNetworkError/);
  assert.match(main, /url\.startsWith \(juce::WebBrowserComponent::getResourceProviderRoot\(\)\)/);
  assert.match(main, /guiHandshakeReceived_ = true/);
  assert.match(main, /timerTicks_ >= 120/);
  assert.match(main, /engine_\.setMonitorEnabled \(false\)/);
  assert.match(main, /WEBVIEW2_STARTUP_FAILED/);
  const bridge = read("web/bridge.js");
  assert.match(bridge, /send\("refreshState"\)/);
});
