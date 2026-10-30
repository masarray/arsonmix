// SPDX-License-Identifier: AGPL-3.0-only
// Native output diagnostics: JS control only, never audio callback work.
(() => {
  const api = window.__JUCE__?.backend;
  if (!api?.emitEvent || !api?.addEventListener) return;
  let state = {};
  let catalogItems = []; // native VST3 catalog, never a WebView file path
  const catalogNames=new Map();
  const css = document.createElement("style");
  css.textContent = "#arSignalOverlay{display:none;position:fixed;inset:0;z-index:990;background:#020a10d9;align-items:center;justify-content:center}" +
    "#arSignalOverlay.open{display:flex}.arSignalPanel{width:min(710px,93vw);padding:16px;background:linear-gradient(#263d4b,#10212c 80%);border:1px solid #6a92a5;border-radius:9px;color:#d9eaf3;box-shadow:0 15px 55px #000c}" +
    ".arSignalHeader{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}.arSignalHeader h2{font-size:18px;font-weight:500;margin:0}" +
    ".arSignalGrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:9px}.arSignalStage{padding:10px;background:#0d202b;border:1px solid #365565;border-radius:6px;min-width:0}" +
    ".arSignalStage small{display:block;color:#80d4e8;font-size:10px}.arSignalStage b{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:500;font-size:11px;margin:10px 0}" +
    ".arSignalMeter{background:#041019;height:9px;border:1px solid #37576a;border-radius:4px;overflow:hidden}.arSignalMeter i{display:block;width:1%;height:100%;background:linear-gradient(90deg,#14bbaa,#b6d851,#edac38)}" +
    "#arSignalReason{font-size:12px;line-height:1.6;padding:12px;background:#1c3645;border:1px solid #4d7284;border-radius:5px;margin:13px 0}" +
    ".arSignalActions{display:flex;flex-wrap:wrap;gap:8px}.arSignalActions button,.arSignalHeader button{color:#e0f5ff;padding:8px 12px;border:1px solid #57849a;border-radius:4px;background:linear-gradient(#395b70,#143447);cursor:pointer}" +
    "#arSignalFooter{margin-top:12px;font-size:11px;color:#9bbbbf;line-height:1.6}#arSignalBadge{color:#edbf7d;font-size:10px;margin-left:6px;white-space:nowrap}" +
    "#arSignalOpen{padding:4px 7px;font-size:10px}" +
    "#arDiagnosticLab{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-top:10px;font-size:11px}" +
    "#arDiagnosticMode{color:#d9eaf3;background:#142a36;border:1px solid #57849a;border-radius:4px;padding:6px;font-size:11px}" +
    "#arDiagnosticMark{color:#f9e5ca;background:#503926;border:1px solid #b9854a;border-radius:4px;padding:7px 10px;cursor:pointer}" +
    "#arDiagnosticCopy{color:#e1f7f4;background:#166052;border:1px solid #34c8ac;border-radius:4px;padding:7px 12px;cursor:pointer}" +
    "#arDiagnosticCopyStatus{color:#8cdec9;font-size:10px}" +
    "#arSmartClockToggle{color:#f5eadb;background:#284961;border:1px solid #9bb2c5;border-radius:4px;padding:7px 12px;cursor:pointer}";
  css.textContent +=
    "#arStreamStrip{margin-top:9px;padding:8px 10px;border:1px solid #385b67;background:#10252f;color:#c3e2e9;font-size:10px}" +
    "#arStreamStrip strong{font-size:11px;font-weight:500;color:#b0e9e2}" +
    "#arStreamControls{display:flex;gap:5px;align-items:center;flex-wrap:wrap;margin-top:6px}" +
    "#arStreamDevice{flex:1;min-width:175px;max-width:290px;background:#102d3b;color:#e4f0f4;border:1px solid #547582;border-radius:3px;padding:5px;font-size:10px}" +
    "#arStreamControls button{border:1px solid #527682;background:#214051;color:#e4f2f5;border-radius:3px;padding:5px 8px;font-size:10px;cursor:pointer}" +
    "#arStreamControls button:disabled{opacity:.42;cursor:default}" +
    "#arStreamStatus{margin-top:6px;color:#accad2;line-height:1.45;overflow-wrap:anywhere}";
  // R0 routing workspace. One static native-only skin sharing the main
  // console's graphite/steel/teal vocabulary. No mouse-hover repaint,
  // compositor filters, animated shadows, canvas or extra scheduler.
  css.textContent += `
#arRoutingLabOverlay{display:none;position:fixed;inset:62px 7px 30px 168px;z-index:995;background:#111c24;min-width:0;min-height:0}
#arRoutingLabOverlay.open{display:block}
#arRoutingLabPanel{box-sizing:border-box;display:flex;flex-direction:column;width:100%;height:100%;min-width:0;min-height:0;overflow:hidden;background:linear-gradient(180deg,#1d303c 0%,#111e27 17%,#0d1821 100%);border:1px solid #3d525d;border-radius:3px;color:#dbeaf1}
.arPatchTop{flex:0 0 auto;display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:55px;padding:9px 15px;background:linear-gradient(#2b3b46,#14232d);border-bottom:1px solid #435864}
.arPatchBrand{font-size:14px;letter-spacing:.015em;font-weight:600;color:#edf6f8}
.arPatchBrand em{font-size:10px;letter-spacing:.1em;margin-left:9px;color:#82d7d6;font-weight:600;font-style:normal}
.arPatchSub{color:#a2bac5;font-size:10px;margin-top:5px;font-variant-numeric:tabular-nums}
/* All control pads use existing shared .btn-180 material; not a competing cap. */
.arPatchTop button:focus-visible,.arPatchFooter button:focus-visible,.arPatchPager button:focus-visible,.arPatchTabs button:focus-visible,.arPatchGrid button:focus-visible,#arRouteRows :focus-visible,#arAuxRows :focus-visible,#arSendRows :focus-visible{outline:2px solid #73cfe7;outline-offset:-2px}
.arPatchTabs{flex:0 0 auto;display:flex;align-items:center;gap:5px;padding:6px 12px;background:#14252f;border-bottom:1px solid #304852}
.arPatchTabs button{min-width:110px}
.arPatchTabs button[aria-selected=true]{--b180-color:#91edac;--b180-border:#409e70;--b180-ink:#163b2b}
.arPatchBody{flex:1 1 auto;min-height:0;overflow:auto;scrollbar-width:thin;scrollbar-color:#435b67 #111f29;padding:12px 15px 8px}
.arPatchHeading{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;margin-bottom:9px}
.arPatchHeading strong{font-size:11px;font-weight:600;letter-spacing:.06em;color:#e6f2f5}
.arPatchHeading small{display:block;margin-top:4px;font-size:10px;color:#a2b8c2}
.arPatchPager{display:flex;align-items:center;gap:7px;font-size:10px;color:#b0c8d2}
.arPatchPager button{padding:4px 10px}
.arPatchPager button:disabled,.arPatchFooter button:disabled{opacity:.45;cursor:default}
.arPatchViewport{background:#0c1922;border:1px solid #354d5b;overflow:auto;scrollbar-width:thin;scrollbar-color:#415969 #0b1821;border-radius:3px}
.arPatchGrid{display:grid;min-width:670px;max-width:1300px;width:100%;margin:0 auto;font-variant-numeric:tabular-nums}
.arPatchRow{display:contents}
.arPatchCorner,.arPatchPort,.arPatchChan,.arPatchCell{box-sizing:border-box;min-width:0;min-height:35px;border:0;border-right:1px solid #304652;border-bottom:1px solid #293d48}
.arPatchCorner{position:sticky;left:0;z-index:2;padding:11px 10px;background:#314552;color:#e5f3f5;font-size:10px;font-weight:600}
.arPatchPort{background:#2b404c;color:#dbeaf2;font-size:10px;font-weight:600;padding:4px;display:flex;flex-direction:column;align-items:center;justify-content:center}
.arPatchPort small{max-width:100%;font-size:9px;font-weight:400;color:#acc2cc;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.arPatchChan{position:sticky;left:0;z-index:1;display:flex;gap:6px;align-items:center;padding:3px 8px;background:#1c303b;border-left:3px solid #496271}
.arPatchChan[data-status=live]{border-left-color:#52c6a7}
.arPatchChan[data-status=draft]{border-left-color:#e1ae60}
.arPatchChan strong{font-size:10px;font-weight:600;color:#edf4f6;min-width:42px}
.arPatchChan select{min-width:0;width:85px;border:1px solid #52717e;border-radius:3px;background:#10242f;color:#e0edf2;font-size:10px;padding:4px}
.arPatchChan select[data-patch-channel-format]{width:62px;flex:0 0 62px;color:#a4ecd1;border-color:#4d8e83}
.arPatchSides{display:flex;gap:3px;margin-left:auto}
.arPatchSides button{width:23px;height:23px;padding:0;color:#a8c0cb;border:1px solid #49616f;border-radius:3px;background:#122a35;font-size:10px}
.arPatchSides button[aria-pressed=true]{color:#e8fff9;background:#286356;border-color:#4b9d83}
.arPatchSides button:disabled{opacity:.3}
.arPatchCell{display:flex;justify-content:center;align-items:center;background:#0b1c25;color:#547282;font-size:11px;font-weight:600;cursor:pointer}
.arPatchCell[data-patch-state=live]{background:#174c40;color:#a7f0d7;box-shadow:inset 0 0 0 1px #3f8271}
.arPatchCell[data-patch-state=draft]{background:#74592f;color:#ffe4b3;box-shadow:inset 0 0 0 1px #c89950}
.arPatchCell[data-patch-state=removed]{background:#43383b;color:#e9b6a7;box-shadow:inset 0 0 0 1px #8e6760}
.arPatchUnavailable{opacity:.25;pointer-events:none}
.arPatchCell:disabled{opacity:.35;cursor:not-allowed}
.arPatchEmpty{padding:34px 10px;text-align:center;color:#aec5d0;background:#102632;font-size:11px}
.arPatchLegend{display:flex;gap:14px;align-items:center;flex-wrap:wrap;padding:9px 2px 5px;color:#aec5ce;font-size:10px}
.arPatchLegend b{display:inline-block;width:9px;height:9px;margin-right:5px;background:#223946;border:1px solid #68808c;vertical-align:middle}
.arPatchLegend b.live{background:#49c6a6;border-color:#49c6a6}
.arPatchLegend b.draft{background:#e0b260;border-color:#e0b260}
.arPatchLegend b.removed{background:#966f61;border-color:#966f61}
.arPatchInspector{border-top:1px solid #344e5a;padding:9px 1px;display:flex;justify-content:space-between;flex-wrap:wrap;gap:5px;font-size:10px;color:#afcad2}
.arPatchInspector strong{font-weight:600;color:#e7f4f6}
.arPatchNote{color:#91adb9;font-size:10px;margin:5px 0 10px}
.arPatchHidden{display:none!important}
.arPatchFooter{flex:0 0 auto;display:flex;gap:12px;align-items:center;justify-content:space-between;min-height:51px;padding:7px 12px;background:linear-gradient(#243642,#15252f);border-top:1px solid #4a616b}
.arPatchFooter .arRouteFoot{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:0;justify-content:flex-end}
#arRouteFeedback{flex:1 1 200px;min-width:120px;font-size:10px;line-height:1.35;color:#b2d8d7}
#arRouteApply{--b180-color:#91edac;--b180-border:#409e70;--b180-ink:#163b2b}
#arPatchRevert{background:#b4c1c6}
#arRouteSaveDry{background:#a5b9c1}
#arNativeShowBrowser{margin-top:8px;padding:8px 12px;background:#122933;border-top:1px solid #43606c;color:#c4dce3}
#arNativeShowBrowser[hidden]{display:none}
.arNativeShowLine{display:flex;align-items:center;gap:8px;flex-wrap:wrap;font-size:10px}
#arNativeShowSelect{flex:1;min-width:180px;max-width:500px;background:#0c202a;color:#e3f0f2;border:1px solid #517580;border-radius:3px;padding:6px;font:11px 'Segoe UI',Arial,sans-serif}
#arNativeShowRecall{--b180-color:#91edac;--b180-border:#409e70;--b180-ink:#163b2b}
#arNativeShowRecall:disabled{opacity:.45;cursor:default}
#arNativeShowStatus{flex-basis:100%;color:#a8c2c9;font-size:10px}
#arPatchOutputMap{display:flex;gap:10px;flex-wrap:wrap}
.arPatchOutput{flex:1 1 180px;padding:16px;background:#0b202a;border:1px solid #3e606e;border-radius:3px}
.arPatchOutput b{display:block;margin-bottom:7px;color:#92e9c5;font-size:11px}
.arPatchOutput span{font-size:10px;color:#acc5cd}
#arRouteRows{display:grid;gap:3px}
.arRouteRow{display:grid;grid-template-columns:50px 80px minmax(70px,1fr) minmax(70px,1fr) 92px 145px;gap:5px;align-items:center;padding:5px 7px;background:#152935;border:1px solid #304957;border-radius:3px}
.arRouteRow>label{font-size:10px;font-weight:600;color:#e3f0f4}
.arRouteRow select,.arSendRow select,.arAuxRow select,.arRoutePluginLine select{box-sizing:border-box;width:100%;min-width:0;padding:5px;background:#0d222d;color:#dbeaf2;border:1px solid #436675;border-radius:3px;font:10px 'Segoe UI',Arial,sans-serif}
.arRouteFlags{display:flex;gap:6px;align-items:center}
.arRouteFlags label{display:flex;align-items:center;gap:3px;font-size:10px}
.arRouteFlags input,.arAuxRow input{width:12px;height:12px;margin:0}
.arRoutePeak{display:block;font-size:9px;color:#73d1c0;font-variant-numeric:tabular-nums}
.arRoutePluginLine{grid-column:1/-1;display:flex;align-items:center;gap:7px;font-size:9px;color:#a5bfc8}
.arRoutePluginLine select{max-width:450px;flex:1}
.arRoutePluginLine small{font-size:9px;color:#8facb8}
#arAuxReturns,#arRouteSends{margin-top:12px}
#arAuxReturns h3,#arRouteSends h3{font-size:11px;color:#c5e2e8;font-weight:600;margin:0 0 7px}
#arAuxReturns p{font-size:10px;color:#9db7c3;margin:0 0 7px}
.arAuxRow{display:grid;grid-template-columns:60px 55px 105px 70px minmax(110px,1fr);gap:6px;align-items:center;padding:5px 8px;margin-bottom:2px;background:#132b35;border:1px solid #31505d;color:#d6e8f0;font-size:10px}
.arAuxRow label{display:flex;align-items:center;gap:4px}
.arSendRow{display:grid;grid-template-columns:minmax(85px,1fr) minmax(85px,1fr) 76px 87px 30px;gap:5px;margin-bottom:3px}
.arSendRow button,#arAddSend{min-height:28px}.arSendRow button{--b180-color:#f1b7b5;--b180-border:#ad7778;--b180-ink:#4c1b26}
@media(max-width:980px){#arRoutingLabOverlay{inset:61px 6px 29px 7px}.arPatchTop{padding:8px 10px}.arPatchBody{padding:9px}.arPatchTabs{padding:5px}.arPatchFooter{padding:7px}.arRouteRow{min-width:640px}.arPatchTabs button{min-width:0}}
#arRoutingLabPanel{position:relative}
.arPatchFxTrigger{display:block;flex:1;min-width:0;max-width:400px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;text-align:left;padding:6px 9px;font:11px 'Segoe UI',Arial,sans-serif;color:#d6efe9;background:#132d35;border:1px solid #4a6b76;border-radius:3px;cursor:pointer}
.arPatchFxTrigger:focus-visible,#arPatchFxPicker :focus-visible{outline:2px solid #73cfe7;outline-offset:1px}
#arPatchFxPicker{display:none;position:absolute;top:112px;right:14px;z-index:20;box-sizing:border-box;width:min(390px,calc(100% - 28px));max-height:min(430px,calc(100% - 136px));background:#12242e;border:1px solid #537888;border-radius:4px;box-shadow:0 8px 16px #0008;padding:10px;gap:7px;color:#d4e6ed}
#arPatchFxPicker.open{display:flex;flex-direction:column}
#arPatchFxPicker label{font:600 11px 'Segoe UI',Arial,sans-serif;color:#9fd3d6}
#arPatchFxSearch{box-sizing:border-box;width:100%;border:1px solid #63828c;border-radius:3px;background:#071720;color:#e5f5f5;padding:9px;font:12px 'Segoe UI',Arial,sans-serif}
#arPatchFxResults{overflow:auto;min-height:0;scrollbar-width:thin;display:grid;align-content:start;gap:2px}
#arPatchFxResults button{padding:8px 10px;text-align:left;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;background:#203644;color:#e4f2f5;border:1px solid #344e5a;border-radius:3px;font:11px 'Segoe UI',Arial,sans-serif;cursor:pointer}
#arPatchFxResults button:active{background:#316755}
#arPatchFxCount{font-size:10px;color:#9fb8bf;min-height:13px}

.arAuxMatrix{display:grid;grid-template-columns:74px repeat(4,minmax(90px,1fr));gap:2px;max-width:760px;background:#0c2029;border:1px solid #34505d;padding:3px;border-radius:3px}
.arAuxMatrixHead{display:flex;align-items:center;justify-content:center;min-height:29px;font-size:10px;font-weight:600;color:#b8d4db;background:#253a46}
.arAuxMatrixLabel{display:flex;align-items:center;padding:0 8px;color:#d5e7ea;font-size:10px;background:#223742}
.arAuxMatrixCell{min-height:31px;background:#102730;color:#9bb4c0;border:1px solid #284451;border-radius:2px;font:600 10px 'Segoe UI',Arial,sans-serif;cursor:pointer}
.arAuxMatrixCell[data-state=live]{background:#185040;color:#c4f2dc;border-color:#478c76}
.arAuxMatrixCell[data-state=draft]{background:#73562a;color:#ffdfb0;border-color:#ab8040}
.arAuxMatrixCell[data-state=removed]{background:#4a3336;color:#f5c4b6;border-color:#9b6661}
.arAuxMatrixCell[aria-selected=true]{outline:2px solid #a3ddda;outline-offset:-3px}
.arAuxMatrixCell:disabled{cursor:default;opacity:.55}
.arAuxMatrixCell:focus-visible,.arAuxInspector :focus-visible{outline:2px solid #a3ddda;outline-offset:1px}
.arAuxInspector{max-width:760px;min-height:56px;margin:6px 0 9px;padding:9px 10px;background:#162e39;border:1px solid #42606d;border-radius:3px;color:#c1d8df;font-size:10px}
.arAuxInspector strong{display:block;margin-bottom:7px;color:#e3f2ef;font-size:11px}
.arAuxInspectorLine{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:4px}
.arAuxInspectorLine label{display:flex;gap:5px;align-items:center}
.arAuxInspectorLine select{background:#0c2530;color:#deebec;border:1px solid #4e6d7a;padding:5px;font:11px 'Segoe UI',Arial,sans-serif}
.arAuxInspector button{min-height:28px}.arAuxInspector button[data-aux-inspector-remove]{--b180-color:#f1b7b5;--b180-border:#ad7778;--b180-ink:#4c1b26}
.arAuxMatrixNote{font-size:10px;color:#9db6bf;margin:6px 0}

/* R3: shared BTN-180 cap + static console patchbay; grid remains flat indicators.
   No blur, CSS filter, hover glows, pointer repaint or animation. */
#arRoutingLabOverlay{background:#0d171e}
#arRoutingLabPanel{font-family:"ArSonMix Inter",Inter,Arial,sans-serif;background:#101b23;border:1px solid #52616c;border-radius:5px}
.arPatchTop{min-height:68px;padding:11px 17px;background:linear-gradient(#31434f,#1b2c37);gap:16px}
.arPatchTitle{min-width:0;flex:1}
.arPatchBrand{display:flex;align-items:center;gap:10px;font-size:17px;letter-spacing:.055em}
.arPatchBrandMark{width:31px;height:28px;display:grid;place-items:center;border:1px solid #71969f;border-radius:3px;background:#16313b;color:#b2eddf;font-size:11px;letter-spacing:0}
.arPatchBrand em{font-size:10px;letter-spacing:.13em;color:#91cdd0;margin-left:8px}
.arPatchSub{margin:7px 0 0 41px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#b3c7ce}
.arPatchHeaderActions{display:flex;align-items:center;gap:12px;flex-shrink:0}
.arPatchSafety{display:inline-flex;align-items:center;gap:7px;min-height:28px;padding:0 12px;border:1px solid #547c71;border-radius:3px;background:#173a31;color:#a1e7bd;font-size:10px;font-weight:700;letter-spacing:.05em}
.arPatchSafety::before{content:"";width:7px;height:7px;background:#89dfa6;border-radius:50%}
.arPatchSafety[data-safety=armed]{color:#ffd2b9;background:#4b2925;border-color:#ab6051}
.arPatchSafety[data-safety=armed]::before{background:#fbb184}
#arRoutingLabPanel .btn-180{min-height:31px;height:31px;white-space:nowrap;line-height:1.15;padding:0 11px!important;font-family:"ArSonMix Inter",Arial,sans-serif;letter-spacing:.12px!important;font-size:10px!important}
#arRouteClose.btn-180{min-width:34px;width:34px;padding:0!important;flex-shrink:0}
#arRouteClose svg{display:block;pointer-events:none}
.arPatchTabs{gap:8px;padding:9px 17px;background:#192a34;border-bottom:1px solid #354c59}
.arPatchTabs button.btn-180{min-width:126px}
.arPatchTabs button[aria-selected=true]{--b180-color:#91edac;--b180-border:#409e70;--b180-ink:#163b2b}
.arPatchBody{padding:15px 18px 12px;background:#101c25}
.arPatchHeading{margin-bottom:12px;flex-wrap:nowrap;align-items:flex-end}
.arPatchHeading strong{font-size:12px;letter-spacing:.035em}
.arPatchHeading small{font-size:10px;line-height:1.5}
.arPatchViewport{border:1px solid #506571;border-radius:2px;background:#0b1821}
.arPatchGrid{max-width:1300px;min-width:670px}
.arPatchCorner,.arPatchPort,.arPatchChan,.arPatchCell{min-height:34px;border-right-color:#344b56;border-bottom-color:#2f4350}
.arPatchCorner{background:#344853;padding:10px 12px}
.arPatchPort{background:#304450;color:#ebf5f6}
.arPatchChan{background:#1c303b;border-left-width:4px}
.arPatchCell{background:#12242e;color:#526d7a}
.arPatchCell[data-patch-state=live]{background:#26735e;color:#dbffe9;box-shadow:inset 0 0 0 1px #62a58c}
.arPatchCell[data-patch-state=draft]{background:#87652f;color:#ffedca;box-shadow:inset 0 0 0 1px #c49b51}
.arPatchCell[data-patch-state=removed]{background:#5f4546;color:#ffcebd;box-shadow:inset 0 0 0 1px #a67b76}
.arPatchSides button.btn-180{width:29px;min-width:29px;height:27px;min-height:27px;padding:0!important}
.arPatchSides button[aria-pressed=true]{--b180-color:#9ee5dd;--b180-border:#4c9c94;--b180-ink:#163d3b}
.arPatchLegend{padding:10px 3px;gap:17px}
.arPatchInspector{padding:11px 1px;border-top-color:#405561}
.arPatchInspector strong{color:#9be7c9}
.arPatchFooter{order:4;gap:12px;min-height:67px;padding:10px 15px;background:linear-gradient(#2a3a45,#1c2b35);border-top:1px solid #536771}
.arPatchFooter .arRouteFoot{gap:7px;flex:1 1 480px;justify-content:flex-end}
#arRouteFeedback{flex:1 1 270px;max-height:42px;overflow:auto;line-height:1.45}
#arRouteApply.btn-180{--b180-color:#91edac;--b180-border:#409e70;--b180-ink:#163b2b;min-width:128px}
#arRouteClear.btn-180{--b180-color:#f1b7b5;--b180-border:#ad7778;--b180-ink:#4c1b26}
#arNativeShowBrowser{order:3;margin:0;padding:10px 17px;background:#142632;border-top:1px solid #445d68}
.arNativeShowLine{gap:10px}
#arNativeShowSelect{min-height:32px;max-width:390px;font-family:inherit}
#arNativeShowStatus{line-height:1.4}
.arAuxMatrix{max-width:none;grid-template-columns:78px repeat(4,minmax(85px,1fr));gap:3px;background:#0f222c;border:1px solid #4c6470;padding:5px}
.arAuxMatrixHead{min-height:33px;background:#304450;color:#d7eced}
.arAuxMatrixLabel{background:#243c47}
.arAuxMatrixCell{min-height:36px;background:#112a35;border-color:#34505c}
.arAuxMatrixCell[data-state=live]{background:#24725b;border-color:#519d7f}
.arAuxMatrixCell[data-state=draft]{background:#86632e;border-color:#bd954d}
.arAuxInspector{max-width:none;background:#203541;border-color:#496674}
.arRouteRow{min-height:37px;background:#1b303b;border-color:#365362}
.arAuxRow{min-height:36px;background:#1d333e;border-color:#3c5a66}
.arPatchFxTrigger.btn-180{flex:1;display:flex;justify-content:flex-start;min-width:0;max-width:400px;overflow:hidden;text-overflow:ellipsis;text-transform:none!important}
.arPatchPager button.btn-180{width:32px;min-width:32px;padding:0!important}
#arRoutingLabPanel :is(button,select,input):focus-visible{outline:2px solid #b0e7f3;outline-offset:2px}
@media(max-width:1240px){.arPatchTop{padding:9px 12px}.arPatchBody{padding:11px}.arPatchFooter{flex-wrap:wrap}.arPatchFooter .arRouteFoot{justify-content:flex-start}.arPatchTabs{padding:8px 11px}}
@media(max-width:980px){.arPatchSub{max-width:450px}.arPatchBrand{font-size:14px}.arPatchTabs button.btn-180{min-width:0;flex:1}.arPatchBody{padding:10px}.arPatchHeaderActions{gap:6px}.arPatchFooter{gap:6px}.arPatchSafety{padding:0 7px}}
`; 
  document.head.appendChild(css);
  // Signal Check is declared in the permanent toolbar; footer holds status only.
  const overlay = document.createElement("div");
  overlay.id = "arSignalOverlay";
  overlay.innerHTML = '<section class="arSignalPanel" role="dialog" aria-modal="true">' +
    '<div class="arSignalHeader"><h2>Live Signal Check · Channel 1 → Master</h2><button id="arSignalClose">CLOSE ×</button></div>' +
    '<div class="arSignalGrid">' +
      '<div class="arSignalStage"><small>01 · INPUT</small><b id="arInputName">Mic</b><div class="arSignalMeter"><i id="arInputBar"></i></div></div>' +
      '<div class="arSignalStage"><small>02 · POST FX</small><b id="arFxName">VST3</b><div class="arSignalMeter"><i id="arFxBar"></i></div></div>' +
      '<div class="arSignalStage"><small>03 · HARDWARE OUTPUT</small><b id="arOutputName">Headphone</b><div class="arSignalMeter"><i id="arOutputBar"></i></div></div>' +
    '</div><div id="arSignalReason">Reading native audio state...</div>' +
    '<div class="arSignalActions"><button id="arSignalMonitor">MUTE OUTPUT</button>' +
      '<button id="arSignalDry">DRY TEST OFF</button><button id="arSignalSetup">AUDIO SETUP</button>' +
    '<button id="arSignalLogs">OPEN DIAGNOSTIC LOG</button><button id="arOpenRoutingLab">ROUTING · 10 CH</button></div>' +
    '<div id="arDiagnosticLab"><label for="arDiagnosticMode">P0 AUDIO ISOLATION</label>' +
      '<select id="arDiagnosticMode"><option value="NORMAL">NORMAL</option>' +
      '<option value="DRY_THROUGH">DRY THROUGH (0 FX)</option>' +
      '<option value="OUTPUT_ONLY_TONE">OUTPUT ONLY · 440 Hz -30 dBFS</option>' +
      '<option value="CAPTURE_ONLY">CAPTURE ONLY · SILENT OUTPUT</option></select>' +
      '<button id="arDiagnosticMark">MARK CRACKLE</button>' +
      '<button id="arSmartClockToggle" title="Experimental independent capture/render clock synchronization">SMART CLOCK: DIRECT</button>' +
      '<button id="arDiagnosticCopy" title="Copies native driver settings, recent events and timing data">COPY FULL REPORT</button>' +
      '<span id="arDiagnosticCopyStatus" role="status">One click → paste to ChatGPT</span></div>' +
    '<div id="arHealthPanel" style="padding:8px 0;font:11px monospace;color:#8ad6e0"></div>' +
    '<div id="arSignalFooter">The legacy Main L/R, Record and Streaming buttons are UI previews, not hardware routing. Select output using AUDIO SETUP. Use headphones at low volume.</div>' +
    '</section>';
  document.body.appendChild(overlay);
  // One anchored engineering inspector; no fake per-strip routing badges.
  // Selections are drafts until the native device validates and ACKs Apply.
  const routeOverlay=document.createElement("div");
  routeOverlay.id="arRoutingLabOverlay";
  routeOverlay.innerHTML='<section id="arRoutingLabPanel" role="region" aria-label="ArSonMix routing workspace">' +
    '<header class="arPatchTop"><div class="arPatchTitle"><div class="arPatchBrand"><span class="arPatchBrandMark">AR</span><span>ROUTING <em>PATCHBAY</em></span></div><div class="arPatchSub" id="arRouteIdentity">Reading current audio device...</div></div><div class="arPatchHeaderActions"><span class="arPatchSafety" id="arRouteSafety" data-safety="muted" role="status">OUTPUT MUTED</span><button class="btn-180 arRouteClose" id="arRouteClose" type="button" aria-label="Close routing window" title="Close routing window"><svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path fill="currentColor" d="M19 6.41 17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"/></svg></button></div></header>' +
    '<nav class="arPatchTabs" role="tablist" aria-label="Routing views"><button class="btn-180" data-patch-tab="input" role="tab" aria-selected="true">INPUT PATCH</button><button class="btn-180" data-patch-tab="internal" role="tab" aria-selected="false">INTERNAL / SENDS</button><button class="btn-180" data-patch-tab="output" role="tab" aria-selected="false">OUTPUT PATCH</button></nav>' +
    '<section class="arPatchBody" id="arPatchInputPanel" role="tabpanel"><div class="arPatchHeading"><div><strong>PHYSICAL INPUT → MIXER CHANNEL</strong><small>Click grid to assign. For stereo, select the L / R side. DRAFT becomes LIVE only after native ACK.</small></div><div class="arPatchPager"><button class="btn-180" id="arPatchPrev" title="Previous hardware inputs">‹</button><span id="arPatchRange">NO INPUTS</span><button class="btn-180" id="arPatchNext" title="Next hardware inputs">›</button></div></div>' +
    '<div class="arPatchViewport"><div id="arPatchGrid" class="arPatchGrid" role="grid" aria-label="Native input patch matrix"></div></div><div class="arPatchLegend"><span><b></b>UNPATCHED</span><span><b class="live"></b>LIVE · NATIVE ACK</span><span><b class="draft"></b>DRAFT · NOT APPLIED</span><span><b class="removed"></b>LIVE UNTIL APPLY</span></div><div class="arPatchInspector" id="arPatchInspector" role="status"><strong>10 CHANNEL INPUT PATCH</strong><span>Uses actual enabled ASIO/WASAPI hardware ports</span></div><div class="arPatchNote">No auto-unmute. Stale devices, incomplete stereo assignments and invalid physical ports are rejected before APPLY. One input can feed multiple channels.</div></section>' +
    '<section class="arPatchBody arPatchHidden" id="arPatchInternalPanel" role="tabpanel"><div class="arPatchHeading"><div><strong>CHANNEL CONTROL / INTERNAL SENDS</strong><small>Native channel controls, VST3 inserts, 4 AUX returns and PRE/POST sends.</small></div></div><div id="arRouteRows"></div><section id="arAuxReturns"><h3>4 DEDICATED AUX / FX RETURNS</h3><p>Independent stereo return with VST3, level and MAIN route.</p><div id="arAuxRows"></div></section><section id="arRouteSends"><h3>CH → AUX SEND MATRIX · PRE / POST</h3><p class="arAuxMatrixNote">10 channels × 4 AUX returns. Select a cell to edit PRE/POST sends; output changes only after MUTE + APPLY.</p><div id="arAuxSendGrid" class="arAuxMatrix" role="grid" aria-label="Channel to AUX send matrix"></div><div id="arAuxSendInspector" class="arAuxInspector" role="group" aria-label="Selected AUX send inspector">Select a CH / AUX cell to inspect sends.</div><h3>ADVANCED · CH → CH SENDS</h3><p class="arAuxMatrixNote">Directed channel sends remain available; feedback loops are rejected before native APPLY.</p><div id="arSendRows"></div><button class="btn-180" id="arAddSend">＋ ADD CH → CH SEND</button></section></section>' +
    '<section class="arPatchBody arPatchHidden" id="arPatchOutputPanel" role="tabpanel"><div class="arPatchHeading"><div><strong>OUTPUT PATCH · HARDWARE + SOFTWARE</strong><small>Map MAIN output on the selected ASIO/WASAPI device; optionally duplicate MAIN to another output device.</small></div></div><div id="arPatchOutputMap"></div><div class="arPatchNote">Independent bus-to-device routes beyond MAIN require the native output bus matrix milestone.</div>' +
    '<section id="arStreamStrip" aria-label="Secondary playback output">' +
      '<strong>ADDITIONAL OUTPUT · MAIN TO DEVICE</strong>' +
      '<div id="arStreamControls"><select id="arStreamDevice" aria-label="Additional device playback endpoint"><option value="">Select actual Windows playback endpoint...</option></select>' +
      '<button id="arStreamRefresh" type="button" title="Refresh real Windows playback devices">REFRESH</button>' +
      '<button id="arStreamConnect" type="button">CONNECT</button>' +
      '<button id="arStreamArm" type="button">ARM OUTPUT</button>' +
      '<button id="arStreamStop" type="button">DISCONNECT</button></div>' +
      '<div id="arStreamStatus" role="status">Choose a playback endpoint available on Windows. Hardware or virtual devices are both supported.</div></section>' +
    '</section>' +
    '<footer class="arPatchFooter"><div id="arRouteFeedback" role="status">Select actual input ports, then APPLY while output is muted.</div><div class="arRouteFoot"><button class="btn-180" id="arPatchRevert">REVERT DRAFT</button><button class="btn-180" id="arRouteSaveDry" title="Save real native VST3 state, routing and AUX sends while OUTPUT is muted">SAVE SHOW</button><button class="btn-180" id="arRouteBrowseShows" title="Browse SHA-verified native Shows stored in Studio/Shows">BROWSE SHOWS</button><button class="btn-180" id="arRouteApply">APPLY ROUTING</button><button class="btn-180" id="arRouteClear" title="Discard the native input patch; keep the mixer prepared and output muted">CLEAR PATCH</button></div></footer><section id="arNativeShowBrowser" hidden role="region" aria-label="Native Show browser"><div class="arNativeShowLine"><strong>NATIVE SHOWS</strong><select id="arNativeShowSelect" aria-label="Select a saved native Show"><option value="">Choose a saved Show...</option></select><button class="btn-180" id="arNativeShowRecall" type="button" disabled>RECALL SELECTED</button><span id="arNativeShowStatus" role="status">Browse saved routing snapshots; hardware OUTPUT remains MUTED on recall.</span></div></section><section id="arPatchFxPicker" role="dialog" aria-label="Choose native VST3" aria-modal="false"><label for="arPatchFxSearch">FIND VST3 · SHARED PICKER</label><input id="arPatchFxSearch" type="search" autocomplete="off" placeholder="Search plugin name or ID"><div id="arPatchFxResults"></div><div id="arPatchFxCount"></div></section></section>';
  document.body.appendChild(routeOverlay);
  // Processing channel width is independent of the hardware source mode.
  // The native graph is still the sole audio source of truth; these are DRAFTS.
  const legacyFormat=mode=>mode==="STEREO"||mode==="BUS"?"STEREO":"MONO";
  const validChannelFormat=value=>value==="MONO"||value==="STEREO";
  const typedSource=row=>({...row,
    sourceKind:row.mode==="BUS"?"BUS":row.mode==="NONE"?"NONE":"PHYSICAL",
    sourceFormat:row.mode==="MONO"||row.mode==="STEREO"?row.mode:"NONE",
    channelFormat:row.channelFormat});
  const draftRoutes=Array.from({length:10},()=>({mode:"NONE",channelFormat:"MONO",left:-1,right:-1,gain:0.25,pan:0,main:true,mute:false,solo:false,fxId:"",fxIds:[]}));
  const draftAux=Array.from({length:4},()=>({enabled:false,gain:1,main:true,mute:false,fxId:"",fxIds:[]}));
  const draftSends=[];
  let mixerDraftActive=false; // Source edits from MIX/IO share this exact controller
  let selectedAuxPair=null; // editor-only; not a second audio graph
  let draftEpoch=0; // identity of the hardware used to draw these selectors
  let draftMainOutputs=[]; // physical port IDs (2), never callback offsets
  let draftMonitorOutputs=[]; // empty means independently muted/disabled bus
  let draftMonitorSends=Array(14).fill(0); // 10 CH + four AUX POST taps
  function invalidateDraftForNewDevice(nextEpoch){
    if(draftEpoch===nextEpoch)return false;
    draftEpoch=nextEpoch;
    for(const route of draftRoutes) {
      route.mode="NONE";route.channelFormat="MONO";route.left=-1;route.right=-1;route.gain=0.25;route.pan=0;
      route.main=true;route.mute=false;route.solo=false;route.fxId="";route.fxIds=[];
    }
    for(const b of draftAux){b.enabled=false;b.gain=1;b.main=true;b.mute=false;b.fxId="";b.fxIds=[];}
    draftSends.length=0;
    mixerDraftActive=false;
    selectedAuxPair=null;
    draftMainOutputs=[];
    draftMonitorOutputs=[];
    draftMonitorSends=Array(14).fill(0);
    return true;
  }
  const safeRouteText=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const routeSelect=(port,value)=>'<option value="-1">—</option>'+port.map(p=>
    '<option value="'+Number(p.port)+'" '+(Number(p.port)===value?'selected':'')+'>'+
    safeRouteText(p.label||("Input "+(Number(p.port)+1)))+' (IN '+(Number(p.port)+1)+')</option>').join('');
  const fxPicker=document.getElementById("arPatchFxPicker");
  const fxSearch=document.getElementById("arPatchFxSearch");
  const fxResults=document.getElementById("arPatchFxResults");
  let fxPickerTarget=null;
  function fxCaption(id){return !id?"DRY · SELECT VST3":catalogNames.get(id)||"MISSING VST3 · RESELECT"}
  function fxButtonMarkup(kind,index,id){
    return '<button type="button" class="btn-180 arPatchFxTrigger" '+(kind==="ch"?"data-route-fx":"data-aux-fx")+
      ' data-fx-index="'+index+'" aria-haspopup="dialog" aria-expanded="false" aria-controls="arPatchFxPicker"'+
      ' title="'+safeRouteText(id||"DRY · no VST3")+'">'+safeRouteText(fxCaption(id))+'</button>';
  }
  function closeFxPicker(focus=false){
    const button=fxPickerTarget?.button;
    if(button)button.setAttribute("aria-expanded","false");
    fxPickerTarget=null;fxPicker.classList.remove("open");
    if(focus&&button?.isConnected)button.focus();
  }
  function drawFxResults(){
    if(!fxPickerTarget)return;
    const query=fxSearch.value.trim().toLowerCase();
    const items=['<button type="button" data-fx-picker-choice="">DRY · NO VST3</button>'];
    let total=0;
    for(const item of catalogItems){
      if(query&&!item.search.includes(query))continue;
      if(++total<=40)items.push('<button type="button" data-fx-picker-choice="'+safeRouteText(item.id)+
        '" title="'+safeRouteText(item.id)+'">'+safeRouteText(item.name)+'</button>');
    }
    fxResults.innerHTML=items.join('');
    document.getElementById("arPatchFxCount").textContent=total>40
      ?"Showing 40 of "+total+" matches · narrow the search"
      :total+" available VST3 · DRAFT until native APPLY";
  }
  function openFxPicker(kind,index,button){
    if(patchPending||!button)return;
    if(kind==="ch"&&(!Number.isInteger(index)||index<0||index>=draftRoutes.length))return;
    if(kind==="aux"&&(!Number.isInteger(index)||index<0||index>=draftAux.length))return;
    closeFxPicker();fxPickerTarget={kind,index,button};
    button.setAttribute("aria-expanded","true");fxSearch.value="";
    fxPicker.classList.add("open");drawFxResults();fxSearch.focus();
  }
  fxSearch.addEventListener("input",drawFxResults);
  fxResults.addEventListener("click",e=>{
    const selected=e.target.closest("[data-fx-picker-choice]");
    if(!selected||!fxPickerTarget)return;
    const id=selected.dataset.fxPickerChoice;
    if(id&&!catalogNames.has(id))return; // trusted native catalog IDs only
    const {kind,index,button}=fxPickerTarget;
    const selectedRoute=kind==="ch"?draftRoutes[index]:draftAux[index];
    // Compact ROUTING chooser modifies slot 0 only. Preserve the rest
    // of the native chain rather than silently deleting seven plugins.
    const previous=laneIds(selectedRoute);
    selectedRoute.fxIds=id?[id,...previous.slice(1)]:previous.slice(1);
    selectedRoute.fxId=selectedRoute.fxIds[0]||"";
    button.textContent=fxCaption(selectedRoute.fxId);
    button.title=selectedRoute.fxId||"DRY · no VST3";
    closeFxPicker(true);
  });
  routeOverlay.addEventListener("click",e=>{
    const trigger=e.target.closest("[data-route-fx],[data-aux-fx]");
    if(trigger)openFxPicker(trigger.hasAttribute("data-route-fx")?"ch":"aux",
                            Number(trigger.dataset.fxIndex),trigger);
    else if(fxPickerTarget&&!e.target.closest("#arPatchFxPicker"))closeFxPicker();
  });
  api.addEventListener("arMixCatalog",message=>{
    const items=message?.items||message?.state?.items;
    if(!Array.isArray(items))return;
    catalogNames.clear();catalogItems=[];
    for(const p of items){
      if(!p||typeof p.id!=="string"||!p.id||catalogNames.has(p.id))continue;
      const name=String(p.name||p.id);
      catalogNames.set(p.id,name);
      catalogItems.push({id:p.id,name,search:(name+" "+p.id).toLowerCase()});
    }
    // Do not destroy a channel's other draft form fields on catalog updates.
    if(routeOverlay.classList.contains("open")&&patchTab==="internal"){
      routeOverlay.querySelectorAll(".arPatchFxTrigger").forEach(button=>{
        const kind=button.hasAttribute("data-route-fx")?"ch":"aux";
        const model=kind==="ch"?draftRoutes[Number(button.dataset.fxIndex)]:draftAux[Number(button.dataset.fxIndex)];
        if(model)button.textContent=fxCaption(model.fxId);
      });
      if(fxPickerTarget)drawFxResults();
    }
  });
  // Rehydrate only from the native ACK/SSOT when opening the editor.
  // Browsing a panel or reloading the WebView must never accidentally erase
  // an already published port/FX/send arrangement with stale UI defaults.
  function hydrateAcceptedRoute(){
    const d=state.dryRoutingLab||{},saved=d.acceptedRoute;
    invalidateDraftForNewDevice(Number(d.epoch)||0);
    if(!d.ready||!saved||Number(saved.deviceEpoch)!==Number(d.epoch)||
       !Array.isArray(saved.channels)||saved.channels.length!==10)return false;
    const keys=["mode","channelFormat","left","right","gain","pan","main","mute","solo","fxId"];
    for(let i=0;i<10;i++){
      const source=saved.channels[i];
      if(!source||typeof source!=="object")return false;
      draftRoutes[i].channelFormat=validChannelFormat(source.channelFormat)
        ? source.channelFormat:legacyFormat(source.mode);
      for(const key of keys)
        if(Object.prototype.hasOwnProperty.call(source,key))
          draftRoutes[i][key]=source[key];
      draftRoutes[i].fxIds=Array.isArray(source.fxIds)
        ?source.fxIds.slice(0,8):source.fxId?[source.fxId]:[];
      draftRoutes[i].fxId=draftRoutes[i].fxIds[0]||"";
    }
    if(Array.isArray(saved.auxes)&&saved.auxes.length===4)
      for(let b=0;b<4;b++){
        for(const key of ["enabled","gain","main","mute","fxId"])
          if(Object.prototype.hasOwnProperty.call(saved.auxes[b]||{},key))draftAux[b][key]=saved.auxes[b][key];
        const src=saved.auxes[b]||{};
        draftAux[b].fxIds=Array.isArray(src.fxIds)?src.fxIds.slice(0,8):src.fxId?[src.fxId]:[];
        draftAux[b].fxId=draftAux[b].fxIds[0]||"";
      }
    draftMainOutputs=Array.isArray(saved.mainOutputs)&&saved.mainOutputs.length===2
      ?saved.mainOutputs.slice():[];
    draftMonitorOutputs=Array.isArray(saved.monitorOutputs)&&saved.monitorOutputs.length===2
      ?saved.monitorOutputs.slice():[];
    draftMonitorSends=Array.isArray(saved.monitorSends)&&saved.monitorSends.length===14
      ?saved.monitorSends.slice():Array(14).fill(0);
    draftSends.length=0;
    if(Array.isArray(saved.sends))
      for(const send of saved.sends.slice(0,32))
        if(send&&typeof send==="object")draftSends.push({...send});
    return true;
  }
  function syncNativeLevelsToRouteDraft(){
    const d=state.dryRoutingLab||{};
    if(!d.active||!d.ready||!Array.isArray(d.channelControls))return;
    // Changes performed on the main mixer must survive the next STRUCTURAL
    // APPLY; never roll back a musician's fader/PAN when editing a send.
    for(let i=0;i<Math.min(10,d.channelControls.length);++i){
      const s=d.channelControls[i],r=draftRoutes[i];
      const db=Number(s?.db),pan=Number(s?.pan);
      if(Number.isFinite(db))
        r.gain=db<=-60?0:Math.min(4,Math.max(0,10**(db/20)));
      if(Number.isFinite(pan))r.pan=Math.min(1,Math.max(-1,pan/100));
      r.mute=!!s?.mute;
      r.solo=!!s?.solo;
    }
  }
  function routeGainChoices(current){
    const base=[[.125,'−18 dB'],[.25,'−12 dB'],[.5,'−6 dB'],[1,'0 dB']];
    const matched=base.some(([v])=>Math.abs(v-current)<.00001);
    if(!matched&&Number.isFinite(current)&&current>=0&&current<=4)
      base.push([current,(current<=0?'−∞':(20*Math.log10(current)).toFixed(1))+' dB · LIVE']);
    return base.map(([v,label])=>
      '<option value="'+v+'" '+(Math.abs(v-current)<.00001?'selected':'')+'>'+label+'</option>').join('');
  }

  // R1 patchbay projection. The native acceptedRoute and device epoch are
  // authoritative; browser edits never mutate live audio on cell click.
  const patchPageSize=8;
  const patchSide=Array(10).fill("left");
  let patchPage=0,patchPending=false,patchTab="input";
  const patchGrid=document.getElementById("arPatchGrid");
  function patchPorts(){
    const source=state.dryRoutingLab?.ports;
    if(!Array.isArray(source))return [];
    const used=new Set();
    return source.filter(p=>{
      const n=p?.port;
      if(!Number.isInteger(n)||n<0||n>255||used.has(n))return false;
      used.add(n);return true;
    });
  }
  function patchRole(route,physical){
    if(!route)return "";
    if(route.mode==="MONO" && Number(route.left)===physical)return "M";
    if(route.mode==="STEREO"){
      if(Number(route.left)===physical)return "L";
      if(Number(route.right)===physical)return "R";
    }
    return "";
  }
  function patchRowChanged(draft,live){
    return !live ? draft.mode!=="NONE" || draft.channelFormat!=="MONO" :
      draft.mode!==live.mode ||
      draft.channelFormat!==(validChannelFormat(live.channelFormat)
        ?live.channelFormat:legacyFormat(live.mode)) ||
      (["MONO","STEREO"].includes(draft.mode)&&Number(draft.left)!==Number(live.left)) ||
      (draft.mode==="STEREO"&&Number(draft.right)!==Number(live.right));
  }
  function patchPreflight(){
    if(state.secondaryOutputArmed)
      return "DISARM ADDITIONAL OUTPUT before APPLY routing.";
    const d=state.dryRoutingLab||{},ports=patchPorts();
    if(!Number.isSafeInteger(draftEpoch)||draftEpoch<=0 ||draftEpoch!==Number(d.epoch))
      return "Audio device changed. Refresh ROUTING and reselect the physical inputs.";
    if(!ports.length)return "No active native input ports. Open AUDIO SETUP first.";
    const active=new Set(ports.map(p=>p.port));
    for(let i=0;i<draftRoutes.length;i++){
      const v=draftRoutes[i],ch="CH "+(i+1);
      if(!validChannelFormat(v.channelFormat))
        return ch+": processing format must be MONO or STEREO.";
      if(v.mode==="MONO"&&!active.has(v.left))return ch+": select one active MONO input.";
      if(v.mode==="STEREO"&&(!active.has(v.left)||!active.has(v.right)))
        return ch+": assign both L and R before APPLY.";
      if(v.mode==="STEREO"&&v.left===v.right)
        return ch+": L and R must be distinct physical input ports.";
    }
    // Cheap GUI preflight; native decoder/PreparedRenderGraph is final
    // authority for DAG, device epoch and atomic route publication.
    if(draftSends.length>32)return "Maximum 32 combined CH and AUX sends.";
    const edges=Array.from({length:10},()=>[]);
    const pairs=new Set();
    for(const send of draftSends){
      const {from,to,tap}=send,gain=Number(send.gain),pan=Number(send.pan??0);
      if(!Number.isInteger(from)||from<0||from>=10||!Number.isInteger(to)||to<0||to>=14)
        return "Send source or destination is out of range.";
      if(!["PRE","POST"].includes(tap)||!Number.isFinite(gain)||gain<0||gain>1||
         !Number.isFinite(pan)||pan< -1||pan>1)return "Invalid PRE/POST send level or pan.";
      if(draftRoutes[from].mode==="NONE")return "CH "+(from+1)+" must be connected before sending.";
      if(to<10&&draftRoutes[to].mode==="NONE")
        return "CH "+(to+1)+" must be assigned or BUS before receiving.";
      if(to>=10&&!draftAux[to-10].enabled)
        return "Enable AUX "+(to-9)+" before sending.";
      const key=from+"/"+to+"/"+tap;
      if(pairs.has(key))return "Duplicate "+tap+" send from CH "+(from+1)+".";
      pairs.add(key);
      if(to===from)return "Feedback loop: a channel cannot send to itself.";
      if(to<10)edges[from].push(to);
    }
    const color=Array(10).fill(0);
    const visit=n=>{
      if(color[n]===1)return true;
      if(color[n]===2)return false;
      color[n]=1;
      for(const dest of edges[n])if(visit(dest))return true;
      color[n]=2;
      return false;
    };
    for(let n=0;n<10;++n)
      if(visit(n))return "Feedback loop in CH → CH sends. Remove a return path.";
    return "";
  }
  function renderPatchOutput(){
    const pins=state.dryRoutingLab?.outputPorts;
    const list=Array.isArray(pins)?pins:[];
    document.getElementById("arPatchOutputMap").innerHTML=["MAIN L","MAIN R"].map((side,i)=>{
      const n=list[i],valid=Number.isInteger(n)&&n>=0;
      return '<div class="arPatchOutput"><b>'+side+'</b><span>'+
        (valid?'DEVICE OUTPUT '+(n+1)+' · CURRENT / READ-ONLY':'NOT AVAILABLE · SETUP REQUIRED')+
        '</span></div>';
    }).join("");
  }
  function renderPatchMatrix(){
    if(patchTab!=="input")return;
    const d=state.dryRoutingLab||{},ports=patchPorts();
    const pages=Math.max(1,Math.ceil(ports.length/patchPageSize));
    patchPage=Math.min(Math.max(0,patchPage),pages-1);
    const shown=ports.slice(patchPage*patchPageSize,(patchPage+1)*patchPageSize);
    document.getElementById("arPatchPrev").disabled=patchPage===0;
    document.getElementById("arPatchNext").disabled=patchPage>=pages-1;
    document.getElementById("arPatchRange").textContent=ports.length?
      "INPUT "+(patchPage*patchPageSize+1)+"–"+(patchPage*patchPageSize+shown.length)+" / "+ports.length:"NO ACTIVE INPUTS";
    const old=d.ready&&d.acceptedRoute?.deviceEpoch===d.epoch&&
      Array.isArray(d.acceptedRoute?.channels)?d.acceptedRoute.channels:[];
    const dirty=draftRoutes.filter((v,i)=>patchRowChanged(v,old[i])).length;
    document.getElementById("arPatchInspector").innerHTML=
      '<strong>'+(d.ready?'NATIVE ROUTING · '+d.revision:'AWAITING NATIVE PATCH')+'</strong>'+
      '<span>'+dirty+' channel patch changes · '+ports.length+' verified input ports · '+
      (state.monitor?'OUTPUT ARMED — MUTE BEFORE APPLY':'OUTPUT MUTED · READY FOR SAFE APPLY')+'</span>';
    const applied=document.getElementById("arRouteApply");
    applied.disabled=patchPending||!ports.length;
    applied.textContent=patchPending?"APPLYING…":"APPLY ROUTING";
    if(!ports.length){
      patchGrid.style.gridTemplateColumns="1fr";
      patchGrid.innerHTML='<div class="arPatchEmpty">NO VERIFIED HARDWARE INPUTS · Choose a DIRECT ASIO / WASAPI device in AUDIO SETUP</div>';
      return;
    }
    patchGrid.style.gridTemplateColumns="286px repeat("+patchPageSize+",minmax(58px,1fr))";
    let html='<div class="arPatchRow" role="row"><div class="arPatchCorner" role="columnheader">CHANNEL / FORMAT</div>';
    for(const port of shown)
      html+='<div class="arPatchPort" role="columnheader" data-patch-col="'+port.port+
        '" title="'+safeRouteText(port.label||"Audio Input")+'">IN '+(port.port+1)+
        '<small>'+safeRouteText(port.label||"Physical input")+'</small></div>';
    for(let k=shown.length;k<patchPageSize;k++)
      html+='<div class="arPatchPort arPatchUnavailable" aria-hidden="true">—</div>';
    html+='</div>';
    for(let i=0;i<10;i++){
      const v=draftRoutes[i],live=old[i],changed=patchRowChanged(v,live);
      const stereo=v.mode==="STEREO";
      html+='<div class="arPatchRow" role="row"><div class="arPatchChan" data-status="'+
        (changed?'draft':d.ready?'live':'idle')+'"><strong>CH '+String(i+1).padStart(2,"0")+'</strong>'+
        '<select data-patch-mode="'+i+'" aria-label="CH '+(i+1)+' input format">'+
        ['NONE','MONO','STEREO','BUS'].map(mode=>'<option value="'+mode+'"'+(v.mode===mode?' selected':'')+'>'+mode+'</option>').join("")+
        '</select><select data-patch-channel-format="'+i+'" title="Native processing channel format — separate from source" aria-label="CH '+(i+1)+' processing format">'+
        ['MONO','STEREO'].map(fmt=>'<option value="'+fmt+'"'+(v.channelFormat===fmt?' selected':'')+'>'+ (fmt==="MONO"?"CH M":"CH L/R") +'</option>').join("")+
        '</select><span class="arPatchSides"><button class="btn-180" data-patch-side="left" data-patch-row="'+i+
        '" aria-label="CH '+(i+1)+' assign left" aria-pressed="'+(patchSide[i]==="left")+'"'+(stereo?'':' disabled')+
        '>L</button><button class="btn-180" data-patch-side="right" data-patch-row="'+i+
        '" aria-label="CH '+(i+1)+' assign right" aria-pressed="'+(patchSide[i]==="right")+'"'+(stereo?'':' disabled')+'>R</button></span></div>';
      for(const port of shown){
        const draftRole=patchRole(v,port.port),liveRole=patchRole(live,port.port);
        const changedRole=!!draftRole && draftRole!==liveRole;
        const status=draftRole?(changedRole?'draft':'live'):liveRole?'removed':'off';
        const content=draftRole|| (liveRole?'−':'·');
        const desc='CH '+(i+1)+' '+(v.mode==='STEREO'?'L/R':v.mode)+
          ' ← '+safeRouteText(port.label||('Input '+(port.port+1)))+
          ' · '+(status==='live'?'native confirmed':status==='draft'?'draft not applied':
          status==='removed'?'live until APPLY':'unpatched');
        html+='<button type="button" class="arPatchCell" role="gridcell" data-patch-row="'+i+
          '" data-patch-port="'+port.port+'" data-patch-col="'+port.port+
          '" data-patch-state="'+status+'" aria-pressed="'+!!draftRole+
          '" aria-label="'+desc+'" title="'+desc+'"'+(patchPending?' disabled':'')+'>'+
          content+'</button>';
      }
      for(let k=shown.length;k<patchPageSize;k++)
        html+='<div class="arPatchCell arPatchUnavailable" aria-hidden="true">·</div>';
      html+='</div>';
    }
    patchGrid.innerHTML=html;

  }
  function selectPatchTab(tab){
    if(!["input","internal","output"].includes(tab))return;
    if(tab!=="internal")closeFxPicker();
    patchTab=tab;
    routeOverlay.querySelectorAll("[data-patch-tab]").forEach(el=>
      el.setAttribute("aria-selected",String(el.dataset.patchTab===tab)));
    for(const section of ["input","internal","output"]){
      document.getElementById("arPatch"+section[0].toUpperCase()+section.slice(1)+"Panel")
        .classList.toggle("arPatchHidden",section!==tab);
    }
    if(tab==="input")renderPatchMatrix();
    else if(tab==="internal")drawRoutes();
    else renderPatchOutput();
  }
  function patchFeedback(message){
    document.getElementById("arRouteFeedback").textContent=message;
  }
  routeOverlay.querySelector(".arPatchTabs").addEventListener("click",e=>{
    const tab=e.target.closest("[data-patch-tab]");
    if(tab)selectPatchTab(tab.dataset.patchTab);
  });
  document.getElementById("arPatchPrev").addEventListener("click",()=>{
    if(patchPage>0){patchPage--;renderPatchMatrix();}
  });
  document.getElementById("arPatchNext").addEventListener("click",()=>{
    if(patchPage+1<Math.ceil(patchPorts().length/patchPageSize)){patchPage++;renderPatchMatrix();}
  });
  patchGrid.addEventListener("change",e=>{
    if(patchPending)return;
    const format=e.target.closest("[data-patch-channel-format]");
    if(format){
      const i=Number(format.dataset.patchChannelFormat);
      if(!draftRoutes[i]||!validChannelFormat(format.value))return;
      draftRoutes[i].channelFormat=format.value;mixerDraftActive=true;
      patchFeedback("DRAFT · CH "+(i+1)+" PROCESSING "+format.value+
        " · physical source unchanged · MUTE + APPLY required.");
      renderPatchMatrix();window.dispatchEvent(new Event("arMixRoutingDraftChanged"));
      return;
    }
    const el=e.target.closest("[data-patch-mode]");
    if(!el)return;
    const row=Number(el.dataset.patchMode),v=draftRoutes[row],prior=v.mode;
    const next=el.value;
    if(!["NONE","MONO","STEREO","BUS"].includes(next))return;
    v.mode=next;
    if(prior==="NONE"&&next!=="NONE")v.channelFormat=legacyFormat(next);
    mixerDraftActive=true;
    if(next==="NONE"||next==="BUS"){v.left=-1;v.right=-1;}
    else if(next==="MONO"){v.right=-1;}
    else if(next==="STEREO"&&prior!=="STEREO"){v.right=-1;patchSide[row]="left";}
    patchFeedback("DRAFT · CH "+(row+1)+" "+next+" · output unchanged until APPLY.");
    renderPatchMatrix();
  });
  patchGrid.addEventListener("click",e=>{
    if(patchPending)return;
    const side=e.target.closest("[data-patch-side]");
    if(side){
      const i=Number(side.dataset.patchRow);
      if(draftRoutes[i]?.mode==="STEREO"){patchSide[i]=side.dataset.patchSide;renderPatchMatrix();}
      return;
    }
    const cell=e.target.closest("[data-patch-port]");
    if(!cell)return;
    const i=Number(cell.dataset.patchRow),port=Number(cell.dataset.patchPort),v=draftRoutes[i];
    if(!Number.isInteger(i)||i<0||i>=10||!patchPorts().some(p=>p.port===port))return;
    if(v.mode==="BUS"){patchFeedback("CH "+(i+1)+" is BUS receiver. Select MONO/STEREO before physical patch.");return;}
    if(v.mode==="NONE"){v.mode="MONO";v.channelFormat="MONO";v.left=port;v.right=-1;}
    else if(v.mode==="MONO"){
      if(v.left===port){v.mode="NONE";v.left=-1;}else v.left=port;
      v.right=-1;
    }else if(v.mode==="STEREO"){
      const side=patchSide[i];
      const opposite=side==="left"?"right":"left";
      if(v[side]!==port && v[opposite]===port){
        patchFeedback("REJECTED DRAFT · CH "+(i+1)+" stereo L/R cannot use the same physical port.");
        return;
      }
      v[side]=v[side]===port?-1:port;
    }
    patchFeedback("DRAFT · CH "+(i+1)+" input assignment edited. APPLY validates the real hardware route.");
    renderPatchMatrix();
  });
  // Pointer movement is entirely inert. Patch feedback is expressed only
  // through clicked draft state or native ACK, never per-cell mouse events.
  document.getElementById("arPatchRevert").addEventListener("click",()=>{
    const d=state.dryRoutingLab||{};
    rackFxDraft=null;
    if(!hydrateAcceptedRoute()){
      invalidateDraftForNewDevice(0);
      invalidateDraftForNewDevice(Number(d.epoch)||0);
    }
    syncNativeLevelsToRouteDraft();
    if(patchTab==="internal")drawRoutes();
    renderPatchMatrix();
    patchFeedback("DRAFT REVERTED · displayed input routes match last native ACK (or unpatched device).");
  });

  function drawRoutes(){
    // An identical numbered input on another ASIO device is NOT the same
    // microphone. Clear all prior user intent if device epoch changed.
    invalidateDraftForNewDevice(Number(state.dryRoutingLab?.epoch)||0);
    const ports=Array.isArray(state.dryRoutingLab?.ports)?state.dryRoutingLab.ports:[];
    const rows=document.getElementById("arRouteRows");
    rows.innerHTML=Array.from({length:10},(_,i)=>{
      const v=draftRoutes[i];
      return '<div class="arRouteRow" data-route-row="'+i+'"><label>CH '+(i+1)+'<small class="arRoutePeak" data-route-peak="'+i+'"></small></label>'+
      '<select data-route-mode aria-label="CH '+(i+1)+' physical source type">'+
      ['NONE','MONO','STEREO','BUS'].map(m=>'<option value="'+m+'" '+(v.mode===m?'selected':'')+'>'+m+'</option>').join('')+'</select>'+
      '<select data-route-channel-format aria-label="CH '+(i+1)+' independent processing format" title="Native channel format separate from physical source">'+
      ['MONO','STEREO'].map(f=>'<option value="'+f+'" '+(v.channelFormat===f?'selected':'')+'>'+f+'</option>').join('')+'</select>'+
      '<select data-route-left aria-label="CH '+(i+1)+' hardware left">'+routeSelect(ports,v.left)+'</select>'+
      '<select data-route-right aria-label="CH '+(i+1)+' hardware right">'+routeSelect(ports,v.right)+'</select>'+
      '<select data-route-gain aria-label="CH '+(i+1)+' test level">'+
      routeGainChoices(v.gain)+'</select>'+
      '<div class="arRouteFlags">'+
      '<label title="Send directly to MAIN"><input type="checkbox" data-route-main '+(v.main?'checked':'')+'>MAIN</label>'+
      '<label title="Mute channel and sends"><input type="checkbox" data-route-mute '+(v.mute?'checked':'')+'>M</label>'+
      '<label title="Solo channel with upstream/downstream sends"><input type="checkbox" data-route-solo '+(v.solo?'checked':'')+'>S</label>'+
      '</div><div class="arRoutePluginLine"><span>VST3 INSERT</span>'+fxButtonMarkup("ch",i,v.fxId)+'<small>Up to 8 VST3 · edit chain in FX RACK</small></div></div>';
    }).join('');
    rows.querySelectorAll('[data-route-row]').forEach(row=>{
      const model=draftRoutes[Number(row.dataset.routeRow)];
      row.querySelector('[data-route-mode]').addEventListener('change',e=>{
        const was=model.mode;model.mode=e.target.value;
        if(was==="NONE"&&model.mode!=="NONE")model.channelFormat=legacyFormat(model.mode);
        mixerDraftActive=true;refreshMode(row);
      });
      row.querySelector('[data-route-channel-format]').addEventListener('change',e=>{
        if(validChannelFormat(e.target.value)){model.channelFormat=e.target.value;mixerDraftActive=true;}
      });
      row.querySelector('[data-route-left]').addEventListener('change',e=>{model.left=Number(e.target.value);});
      row.querySelector('[data-route-right]').addEventListener('change',e=>{model.right=Number(e.target.value);});
      row.querySelector('[data-route-gain]').addEventListener('change',e=>{model.gain=Number(e.target.value);});
      row.querySelector('[data-route-main]').addEventListener('change',e=>{model.main=e.target.checked;});
      row.querySelector('[data-route-mute]').addEventListener('change',e=>{model.mute=e.target.checked;});
      row.querySelector('[data-route-solo]').addEventListener('change',e=>{model.solo=e.target.checked;});
      refreshMode(row);
    });
    drawAux();
    drawSends();
    drawAuxSendMatrix();
    updateRouteLabIdentity();
  }
  function drawAux(){
    const parent=document.getElementById("arAuxRows");
    parent.innerHTML=draftAux.map((bus,i)=>'<div class="arAuxRow" data-aux-row="'+i+'">'+
      '<strong>AUX '+(i+1)+'</strong>'+
      '<label><input data-aux-enable type="checkbox" '+(bus.enabled?'checked':'')+'>ON</label>'+
      '<select data-aux-gain aria-label="AUX '+(i+1)+' level">'+routeGainChoices(bus.gain)+'</select>'+
      '<label><input data-aux-main type="checkbox" '+(bus.main?'checked':'')+'>MAIN</label>'+
      fxButtonMarkup("aux",i,bus.fxId)+
      '</div>').join('');
    parent.querySelectorAll('[data-aux-row]').forEach(row=>{
      const bus=draftAux[Number(row.dataset.auxRow)];
      row.querySelector('[data-aux-enable]').addEventListener('change',e=>{if(patchPending)return;bus.enabled=e.target.checked;drawAuxSendMatrix();});
      row.querySelector('[data-aux-gain]').addEventListener('change',e=>{bus.gain=Number(e.target.value);});
      row.querySelector('[data-aux-main]').addEventListener('change',e=>{bus.main=e.target.checked;});
    });
  }
  function refreshMode(row){
    const type=row.querySelector('[data-route-mode]').value;
    row.querySelector('[data-route-left]').disabled=type==="NONE"||type==="BUS";
    row.querySelector('[data-route-right]').disabled=type!=="STEREO";
  }
  // R2: bounded 10x4 AUX matrix. Every cell projects the existing
  // draftSends array; no duplicate graph/model, no periodic repaint.
  // One CH->AUX pair may legitimately have both PRE and POST taps.
  function pairSends(from,to,list=draftSends){
    return list.filter(s=>s.from===from&&s.to===to);
  }
  function pairSignature(list,from,to){
    return pairSends(from,to,list).map(s=>[s.tap,Number(s.gain),Number(s.pan)||0].join(":")).sort().join("|");
  }
  function sendGainChoices(value){
    const levels=[[.125,"−18 dB"],[.25,"−12 dB"],[.5,"−6 dB"],[1,"0 dB"]];
    if(Number.isFinite(value)&&value>=0&&value<=1&&!levels.some(([v])=>Math.abs(value-v)<.00001))
      levels.push([value,value===0?"−∞ dB":(20*Math.log10(value)).toFixed(1)+" dB"]);
    return levels.map(([v,label])=>'<option value="'+v+'" '+(Math.abs(v-value)<.00001?'selected':'')+'>'+label+'</option>').join("");
  }
  function sendPanChoices(value){
    const levels=[[-1,"L100"],[-.5,"L50"],[0,"CENTER"],[.5,"R50"],[1,"R100"]];
    if(Number.isFinite(value)&&value>=-1&&value<=1&&!levels.some(([v])=>Math.abs(value-v)<.00001))
      levels.push([value,"PAN "+Math.round(value*100)]);
    return levels.map(([v,label])=>'<option value="'+v+'" '+(Math.abs(v-value)<.00001?'selected':'')+'>'+label+'</option>').join("");
  }
  function drawAuxSendMatrix(){
    const grid=document.getElementById("arAuxSendGrid");
    const native=state.dryRoutingLab||{};
    const saved=native.ready&&Number(native.acceptedRoute?.deviceEpoch)===Number(draftEpoch)
      &&Array.isArray(native.acceptedRoute?.sends)?native.acceptedRoute.sends:[];
    let html='<div class="arAuxMatrixHead">SOURCE</div>';
    for(let b=0;b<4;++b)html+='<div class="arAuxMatrixHead">AUX '+(b+1)+'</div>';
    for(let from=0;from<10;++from){
      html+='<div class="arAuxMatrixLabel">CH '+(from+1)+'</div>';
      for(let bus=0;bus<4;++bus){
        const to=10+bus,now=pairSends(from,to),was=pairSends(from,to,saved);
        const changed=pairSignature(draftSends,from,to)!==pairSignature(saved,from,to);
        const status=changed?(now.length?"draft":"removed"):(now.length?"live":"off");
        const selected=selectedAuxPair?.from===from&&selectedAuxPair?.bus===bus;
        const label=now.length?now.map(s=>s.tap==="PRE"?"PRE":"POST").join(" + "):"＋";
        const desc='CH '+(from+1)+' to AUX '+(bus+1)+', '+
          (now.length?label:was.length?"send removed":"no send");
        html+='<button type="button" class="arAuxMatrixCell" role="gridcell" '+
          'data-aux-matrix-from="'+from+'" data-aux-matrix-bus="'+bus+'" '+
          'data-state="'+status+'" aria-selected="'+selected+'" aria-label="'+desc+
          '" title="'+(now.length?label:was.length?"LIVE until APPLY":"Edit send in inspector")+
          '"'+(patchPending?' disabled':'')+'>'+label+'</button>';
      }
    }
    grid.innerHTML=html;
    drawAuxSendInspector();
  }
  function drawAuxSendInspector(){
    const root=document.getElementById("arAuxSendInspector");
    if(!selectedAuxPair){root.textContent="Select a CH / AUX cell to inspect or create its sends.";return;}
    const {from,bus}=selectedAuxPair,to=10+bus;
    const sends=draftSends.map((send,index)=>({send,index}))
      .filter(x=>x.send.from===from&&x.send.to===to)
      .sort((a,b)=>a.send.tap.localeCompare(b.send.tap));
    let html='<strong>CH '+(from+1)+' → AUX '+(bus+1)+' · DRAFT SEND INSPECTOR</strong>';
    for(const {send,index} of sends){
      html+='<div class="arAuxInspectorLine" data-aux-inspector-row="'+index+'">'+
        '<b>'+send.tap+'</b><label>LEVEL <select data-aux-inspector-level aria-label="Send level">'+
        sendGainChoices(Number(send.gain))+'</select></label>'+
        '<label>PAN <select data-aux-inspector-pan aria-label="Send pan">'+
        sendPanChoices(Number(send.pan)||0)+'</select></label>'+
        '<button class="btn-180" data-aux-inspector-remove title="Remove this draft tap">REMOVE</button></div>';
    }
    if(draftRoutes[from].mode==="NONE")
      html+='<div class="arAuxMatrixNote">Assign CH '+(from+1)+' in INPUT PATCH before creating a send.</div>';
    else if(!draftAux[bus].enabled)
      html+='<div class="arAuxMatrixNote">Enable AUX '+(bus+1)+' above before creating a send.</div>';
    else if(draftSends.length>=32)
      html+='<div class="arAuxMatrixNote">32/32 native send limit reached. Remove another send first.</div>';
    else for(const tap of ["PRE","POST"])
      if(!sends.some(x=>x.send.tap===tap))
        html+='<div class="arAuxInspectorLine"><button class="btn-180" data-aux-inspector-create="'+tap+
          '">＋ ADD '+tap+' SEND</button></div>';
    root.innerHTML=html;
    root.querySelectorAll("button,select").forEach(el=>{el.disabled=patchPending;});
  }
  document.getElementById("arAuxSendGrid").addEventListener("click",e=>{
    const cell=e.target.closest("[data-aux-matrix-from][data-aux-matrix-bus]");
    if(!cell||patchPending)return;
    selectedAuxPair={from:Number(cell.dataset.auxMatrixFrom),bus:Number(cell.dataset.auxMatrixBus)};
    drawAuxSendMatrix();
  });
  document.getElementById("arAuxSendInspector").addEventListener("click",e=>{
    if(patchPending||!selectedAuxPair)return;
    const add=e.target.closest("[data-aux-inspector-create]");
    const remove=e.target.closest("[data-aux-inspector-remove]");
    if(add){
      const {from,bus}=selectedAuxPair,to=10+bus,tap=add.dataset.auxInspectorCreate;
      if(!["PRE","POST"].includes(tap)||draftRoutes[from].mode==="NONE"||
         !draftAux[bus].enabled||draftSends.length>=32||
         draftSends.some(s=>s.from===from&&s.to===to&&s.tap===tap))return;
      draftSends.push({from,to,tap,gain:.25,pan:0});
      patchFeedback("DRAFT · "+tap+" send added. MUTE + APPLY to publish.");
      drawAuxSendMatrix();
    }else if(remove){
      const index=Number(remove.closest("[data-aux-inspector-row]")?.dataset.auxInspectorRow);
      if(!Number.isInteger(index)||index<0||index>=draftSends.length)return;
      draftSends.splice(index,1);drawAuxSendMatrix();
      patchFeedback("DRAFT · AUX send removed; native output unchanged.");
    }
  });
  document.getElementById("arAuxSendInspector").addEventListener("change",e=>{
    if(patchPending)return;
    const row=e.target.closest("[data-aux-inspector-row]");
    const index=Number(row?.dataset.auxInspectorRow);
    if(!Number.isInteger(index)||index<0||index>=draftSends.length)return;
    const send=draftSends[index],value=Number(e.target.value);
    if(!Number.isFinite(value))return;
    if(e.target.matches("[data-aux-inspector-level]")&&value>=0&&value<=1)send.gain=value;
    if(e.target.matches("[data-aux-inspector-pan]")&&value>=-1&&value<=1)send.pan=value;
    drawAuxSendMatrix();
    patchFeedback("DRAFT · send level/pan changed; APPLY required.");
  });
  function drawSends(){
    // Advanced CH->CH routes remain accessible, using original send indices.
    // AUX sends are edited ONLY through the matrix/inspector above.
    const el=document.getElementById("arSendRows");
    const channels=Array.from({length:10},(_,i)=>'<option value="'+i+'">CH '+(i+1)+'</option>');
    const listSelect=(options,value)=>options.map((option,i)=>option.replace('<option','<option '+(i===value?'selected':''))).join('');
    el.innerHTML=draftSends.map((send,index)=>({send,index})).filter(x=>x.send.to<10).map(({send,index})=>
      '<div class="arSendRow" data-send-row="'+index+'">'+
      '<select data-send-from aria-label="Send source">'+listSelect(channels,send.from)+'</select>'+
      '<select data-send-to aria-label="Send destination">'+listSelect(channels,send.to)+'</select>'+
      '<select data-send-tap aria-label="Send tap">'+['PRE','POST'].map(t=>'<option '+(t===send.tap?'selected':'')+'>'+t+'</option>').join('')+'</select>'+
      '<select data-send-gain aria-label="Send level">'+sendGainChoices(Number(send.gain))+'</select>'+
      '<button class="btn-180" data-send-remove aria-label="Remove send" title="Remove send">×</button></div>').join('');
    el.querySelectorAll('[data-send-row]').forEach(row=>{
      const i=Number(row.dataset.sendRow),m=draftSends[i];
      row.querySelector('[data-send-from]').addEventListener('change',e=>{if(!patchPending)m.from=Number(e.target.value);});
      row.querySelector('[data-send-to]').addEventListener('change',e=>{if(!patchPending)m.to=Number(e.target.value);});
      row.querySelector('[data-send-tap]').addEventListener('change',e=>{if(!patchPending)m.tap=e.target.value;});
      row.querySelector('[data-send-gain]').addEventListener('change',e=>{if(!patchPending)m.gain=Number(e.target.value);});
      row.querySelector('[data-send-remove]').addEventListener('click',()=>{if(patchPending)return;draftSends.splice(i,1);drawSends();drawAuxSendMatrix();});
    });
    document.getElementById("arAddSend").disabled=patchPending||draftSends.length>=32;
  }
  document.getElementById("arAddSend").addEventListener('click',()=>{
    if(patchPending||draftSends.length>=32)return;
    // Prefer an eligible non-self destination; drafts never change audio.
    let choice=null;
    for(let from=0;from<10&&!choice;++from)
      for(let to=0;to<10&&!choice;++to)
        if(from!==to&&draftRoutes[from].mode!=="NONE"&&draftRoutes[to].mode!=="NONE"&&
           !draftSends.some(s=>s.from===from&&s.to===to&&s.tap==="POST"))
          choice={from,to,tap:"POST",gain:.25,pan:0};
    if(!choice){patchFeedback("Assign two channels in INPUT PATCH before adding CH sends.");return;}
    draftSends.push(choice);
    drawSends();
    patchFeedback("DRAFT · CH send added; native DAG validation on APPLY.");
  });
  function updateRouteLabIdentity(){
    const d=state.dryRoutingLab||{};
    const clear=document.getElementById("arRouteClear");
    if(clear)clear.disabled=patchPending||!d.acceptedRoute||!!state.monitor;
    const safety=document.getElementById("arRouteSafety");
    if(safety){
      const mode=state.monitor?"armed":"muted";
      if(safety.dataset.safety!==mode)safety.dataset.safety=mode;
      setTextIfChanged(safety,state.monitor?"OUTPUT ARMED":"OUTPUT MUTED");
    }
    const label=document.getElementById("arRouteIdentity");
    if(!label)return;
    const liveSends=(Number(d.sendCount)||0)+(Number(d.auxSendCount)||0);
    setTextIfChanged(label,(d.active ? (d.ready?(d.acceptedRoute?"NATIVE PATCH LIVE":"NATIVE MIXER READY · UNPATCHED / SILENT"):"PATCH STALE — SILENT, REAPPLY"):"AWAITING VERIFIED DEVICE")+
      " · "+(d.ports?.length||0)+" INPUTS · "+(d.outputPorts?.length||0)+" OUTPUTS · E"+(d.epoch||0)+" / R"+(d.revision||0)+
      (liveSends?" · "+liveSends+" SENDS":""));
  }
  function openRoutingLab(){
    // Keep an intentional FX Rack draft when switching workspaces; otherwise
    // load only native ACK data. Device-epoch changes invalidate the draft.
    if((!rackFxDraft||rackFxDraft.epoch!==Number(state.dryRoutingLab?.epoch))&&!mixerDraftActive){
      rackFxDraft=null;
      hydrateAcceptedRoute();
    }
    syncNativeLevelsToRouteDraft();
    // INPUT PATCH renders first. The ten VST3 selectors, four AUX selectors
    // and send controls are built only when INTERNAL is explicitly opened.
    closeFxPicker();
    patchPage=0;
    routeOverlay.classList.add("open");
    selectPatchTab("input");
    overlay.classList.remove("open");
  }
  // Exposed only to the existing native SIGNAL FLOW GUI; does not publish a
  // routing transaction or bypass the output-MUTE operator safety gate.
  // Routing owns the only native 10CH/AUX plan. FX RACK may edit the
  // channel's VST3 selection, but MUST stage it through the SAME routing
  // draft/preflight/APPLY/ACK; no separate graph or implicit output unmute.
  let rackFxDraft=null;
  const laneIds=route=>Array.isArray(route?.fxIds)
    ?route.fxIds.slice(0,8):route?.fxId?[route.fxId]:[];
  // The bridge receives an exact native bypass ACK. Transfer only its
  // accepted graph telemetry into this controller before repaint, without
  // modifying a pending structural route draft or triggering a second APPLY.
  window.__arMixAcceptFxBypassAck=status=>{
    if(!status||typeof status!=="object")return false;
    const current=state.dryRoutingLab||{};
    if(Number(current.epoch)!==Number(status.epoch)||
       Number(current.revision)!==Number(status.revision))return false;
    state.dryRoutingLab=status;
    return true;
  };
  window.__arMixFxLaneView=channel=>{
    const d=state.dryRoutingLab||{};
    const saved=d.acceptedRoute?.channels?.[channel];
    const live=laneIds(saved);
    const pending=!!rackFxDraft&&rackFxDraft.epoch===Number(d.epoch);
    const ids=pending?laneIds(draftRoutes[channel]):live;
    const liveSlots=Array.isArray(d.fxInstances?.[channel])?d.fxInstances[channel]:[];
    return {ids,live,liveSlots,pending,ready:!!d.ready,epoch:Number(d.epoch)||0,
      revision:Number(d.revision)||0,
      monitoring:!!state.monitor,names:ids.map(id=>catalogNames.get(id)||"Missing VST3")};
  };
  window.__arMixStageRackFx=(channel,id,slot=0,operation="set")=>{
    const d=state.dryRoutingLab||{};
    if(!d.active||!d.ready)return {ok:false,error:"Select a verified DIRECT audio device."};
    if(patchPending)return {ok:false,error:"Wait for the previous native ACK."};
    if(!Number.isInteger(channel)||channel<0||channel>=10)return {ok:false,error:"Invalid input channel."};
    if(!Number.isInteger(slot)||slot<0||slot>=8)return {ok:false,error:"FX insert slot must be 1–8."};
    if(operation==="set"&&id&&!catalogNames.has(id))
      return {ok:false,error:"VST3 is not indexed; open Plugin Manager."};
    if(channel===0&&Number(state.chainCount)>0)
      return {ok:false,error:"CH1 legacy FX chain is active; use its original FX Rack."};
    if(!rackFxDraft||rackFxDraft.epoch!==Number(d.epoch)){
      hydrateAcceptedRoute();syncNativeLevelsToRouteDraft();
    }
    const current=laneIds(draftRoutes[channel]);
    if(operation==="set"){
      if(slot>current.length)return {ok:false,error:"Fill preceding FX slots first."};
      if(id)current[slot]=id;
      else if(slot<current.length)current.splice(slot,1);
    }else if(operation==="remove"){
      if(slot>=current.length)return {ok:false,error:"No VST3 in this slot."};
      current.splice(slot,1);
    }else if(operation==="up"||operation==="down"){
      const dest=slot+(operation==="up"?-1:1);
      if(slot>=current.length||dest<0||dest>=current.length)
        return {ok:false,error:"Cannot move outside the active FX chain."};
      [current[slot],current[dest]]=[current[dest],current[slot]];
    }else return {ok:false,error:"Unknown FX edit operation."};
    draftRoutes[channel].fxIds=current;
    draftRoutes[channel].fxId=current[0]||"";
    rackFxDraft={channel,id:current[slot]||"",slot,ids:current.slice(),epoch:Number(d.epoch)};
    if(patchTab==="internal"&&routeOverlay.classList.contains("open"))drawRoutes();
    window.dispatchEvent(new Event("arMixRoutingDraftChanged"));
    return {ok:true,ids:current.slice(),name:id?catalogNames.get(id):"DRY",
      message:"FX DRAFT · "+current.length+"/8 inserts. MUTE OUTPUT → APPLY → native ACK."};
  };
  window.__arMixGetRackFxDraft=channel=>rackFxDraft?.epoch===Number(state.dryRoutingLab?.epoch)&&
    rackFxDraft?.channel===channel?rackFxDraft:null;
  window.__arMixApplyRackFx=()=>{
    if(!rackFxDraft)return {ok:false,error:"No pending FX selection."};
    if(patchPending)return {ok:false,error:"Native routing request already pending."};
    if(state.monitor)return {ok:false,error:"MUTE OUTPUT before applying a structural FX edit."};
    if(draftRoutes[rackFxDraft.channel]?.mode==="NONE")
      return {ok:false,error:"Assign a physical input to this channel in ROUTING before applying its FX."};
    const failure=patchPreflight();
    if(failure)return {ok:false,error:failure};
    document.getElementById("arRouteApply").click();
    return {ok:true,message:"FX change submitted. Live only after native route ACK."};
  };
  window.__arMixOpenRoutingLab=openRoutingLab;
  window.__arMixOpenRouting=openRoutingLab;
  // G1/G2 adapters: expose a bounded snapshot and stage into the EXISTING
  // DRAFT/ACK route; never create a second frontend or native audio graph.
  // Shared INPUT/OUTPUT route draft: only the existing MUTE/APPLY/ACK may
  // publish this new physical destination; OUTPUT panel never touches DSP.
  window.__arMixStageMainOutputs=(left,right)=>{
    const d=state.dryRoutingLab||{};
    if(!d.ready||patchPending)return {ok:false,error:"Native route is not ready"};
    if(draftEpoch!==Number(d.epoch))return {ok:false,error:"Device changed; refresh patch"};
    const ports=Array.isArray(d.outputPorts)?d.outputPorts:[];
    if(!ports.includes(left)||!ports.includes(right)||left===right)
      return {ok:false,error:"MAIN requires two different active output pins"};
    if(draftMonitorOutputs.some(p=>p===left||p===right))
      return {ok:false,error:"MAIN and MONITOR must own different hardware output pins"};
    draftMainOutputs=[left,right];mixerDraftActive=true;
    window.dispatchEvent(new Event("arMixRoutingDraftChanged"));
    return {ok:true,message:"MAIN OUTPUT DRAFT · MUTE OUTPUT → APPLY → native ACK"};
  };
  window.__arMixStageMonitorPatch=(left,right)=>{
    const d=state.dryRoutingLab||{};
    if(!d.ready||patchPending||draftEpoch!==Number(d.epoch))
      return {ok:false,error:"Native device changed or routing pending"};
    if(left===-1&&right===-1){
      draftMonitorOutputs=[];draftMonitorSends=Array(14).fill(0);
    }else{
      const ports=Array.isArray(d.outputPorts)?d.outputPorts:[];
      const main=draftMainOutputs.length===2?draftMainOutputs:
        (Array.isArray(d.mainOutputs)&&d.mainOutputs.length===2?d.mainOutputs:ports.slice(0,2));
      if(!ports.includes(left)||!ports.includes(right)||left===right||
         main.includes(left)||main.includes(right))
        return {ok:false,error:"MONITOR needs two unused verified physical output pins"};
      draftMonitorOutputs=[left,right];
    }
    mixerDraftActive=true;
    window.dispatchEvent(new Event("arMixRoutingDraftChanged"));
    return {ok:true,message:"MONITOR DRAFT · OUTPUT MUTE → APPLY → native ACK"};
  };
  window.__arMixStageMonitorSend=(lane,gain)=>{
    const d=state.dryRoutingLab||{};
    if(!d.ready||patchPending||draftEpoch!==Number(d.epoch)||
       draftMonitorOutputs.length!==2)return {ok:false,error:"Enable MONITOR first"};
    if(!Number.isInteger(lane)||lane<0||lane>=14||
       !Number.isFinite(gain)||gain<0||gain>1)
      return {ok:false,error:"Invalid MONITOR send level"};
    draftMonitorSends[lane]=gain;mixerDraftActive=true;
    window.dispatchEvent(new Event("arMixRoutingDraftChanged"));
    return {ok:true,message:"MONITOR SEND DRAFT · APPLY required"};
  };
  window.__arMixRouteView=()=>{
    const d=state.dryRoutingLab||{};
    // Read-only telemetry projection. Never mutate the draft/epoch here:
    // this listener may run before the routing controller's arMixStatus
    // callback and would swallow device-epoch invalidation (stale patch UI).
    return {
      epoch:Number(d.epoch)||0,revision:Number(d.revision)||0,
      ready:!!d.ready,active:!!d.active,monitor:!!state.monitor,
      pending:patchPending,ports:patchPorts().map(p=>({port:p.port,label:p.label})),
      outputPorts:Array.isArray(d.outputPorts)?d.outputPorts.slice():[],
      mainOutputs:Array.isArray(d.mainOutputs)?d.mainOutputs.slice():[],
      draftMainOutputs:draftMainOutputs.slice(),
      monitorOutputs:Array.isArray(d.monitorOutputs)?d.monitorOutputs.slice():[],
      monitorSends:Array.isArray(d.monitorSends)?d.monitorSends.slice():[],
      draftMonitorOutputs:draftMonitorOutputs.slice(),
      draftMonitorSends:draftMonitorSends.slice(),
      acceptedRoute:d.acceptedRoute||null,
      draftRoutes:draftRoutes.map(r=>({...r,fxIds:laneIds(r)})),
      draftAux:draftAux.map(r=>({...r,fxIds:laneIds(r)})),
      draftSends:draftSends.map(r=>({...r}))
    };
  };
  // G3 shared send controller: draft only. Native routing remains sole
  // audio authority (validated device epoch, MUTE, APPLY and positive ACK).
  const sendResumeGain=new Map();
  const sendKey=(from,bus,tap)=>from+":"+bus+":"+tap;
  function sendEditGate(from,bus){
    const d=state.dryRoutingLab||{};
    if(!d.active||!d.ready||Number(d.epoch)<=0)return "Select a verified DIRECT audio device.";
    if(draftEpoch!==Number(d.epoch))return "Device epoch changed. Review hardware patch.";
    if(patchPending)return "Native route APPLY is pending.";
    if(!Number.isInteger(from)||from<0||from>=10||
       !Number.isInteger(bus)||bus<0||bus>=4)return "Invalid channel/AUX index.";
    return "";
  }
  function publishSendDraft(){
    mixerDraftActive=true;
    drawAuxSendMatrix();drawSends();
    window.dispatchEvent(new Event("arMixRoutingDraftChanged"));
  }
  window.__arMixStageAuxSendEdit=({from,bus,tap,action,value}={})=>{
    const fault=sendEditGate(from,bus);
    if(fault)return {ok:false,error:fault};
    if(!["PRE","POST"].includes(tap))return {ok:false,error:"Choose PRE or POST."};
    const to=10+bus,key=sendKey(from,bus,tap);
    const index=draftSends.findIndex(x=>x.from===from&&x.to===to&&x.tap===tap);
    const item=index>=0?draftSends[index]:null;
    if(action==="create"){
      if(item)return {ok:false,error:"Send already exists."};
      if(draftRoutes[from].mode==="NONE")return {ok:false,error:"Patch CH "+(from+1)+" physical input first."};
      if(!draftAux[bus].enabled)return {ok:false,error:"Enable AUX "+(bus+1)+" return first."};
      if(draftSends.length>=32)return {ok:false,error:"32 native sends maximum."};
      draftSends.push({from,to,tap,gain:.25,pan:0});
    }else if(!item)return {ok:false,error:"Send is missing; reopen inspector."};
    else if(action==="remove"){draftSends.splice(index,1);sendResumeGain.delete(key);}
    else if(action==="gain"){
      const n=Number(value);
      if(!Number.isFinite(n)||n<0||n>1)return {ok:false,error:"Gain must be 0–1."};
      item.gain=n;if(n>0)sendResumeGain.set(key,n);
    }else if(action==="pan"){
      const n=Number(value);
      if(!Number.isFinite(n)||n< -1||n>1)return {ok:false,error:"PAN must be -1 to +1."};
      item.pan=n;
    }else if(action==="enable"){
      if(typeof value!=="boolean")return {ok:false,error:"Invalid send ON/OFF."};
      if(!value){if(item.gain>0)sendResumeGain.set(key,item.gain);item.gain=0;}
      else if(item.gain===0)item.gain=sendResumeGain.get(key)||.25;
    }else return {ok:false,error:"Unsupported send edit."};
    publishSendDraft();
    return {ok:true,message:"SEND DRAFT · MUTE + APPLY required; live audio unchanged."};
  };
  window.__arMixStageAuxReturnEdit=(bus,field,value)=>{
    const fault=sendEditGate(0,bus);
    if(fault)return {ok:false,error:fault};
    if(!["enabled","gain","mute","main"].includes(field))
      return {ok:false,error:"Invalid AUX return field."};
    if(field==="gain"){
      const n=Number(value);
      if(!Number.isFinite(n)||n<0||n>4)return {ok:false,error:"AUX gain must be 0–4."};
      draftAux[bus].gain=n;
    }else{
      if(typeof value!=="boolean")return {ok:false,error:"Invalid AUX switch."};
      if(field==="enabled"&&!value&&draftSends.some(x=>x.to===10+bus))
        return {ok:false,error:"Remove AUX "+(bus+1)+" sends before disabling its return."};
      draftAux[bus][field]=value;
    }
    drawAux();publishSendDraft();
    return {ok:true,message:"AUX RETURN DRAFT · MUTE + APPLY required."};
  };
  window.__arMixRevertMixerDraft=()=>{
    if(patchPending)return {ok:false,error:"Native APPLY is pending."};
    rackFxDraft=null;mixerDraftActive=false;sendResumeGain.clear();
    if(!hydrateAcceptedRoute())return {ok:false,error:"No accepted route available."};
    drawAux();drawAuxSendMatrix();drawSends();renderPatchMatrix();
    window.dispatchEvent(new Event("arMixRoutingDraftChanged"));
    return {ok:true,message:"REVERTED · original native route unchanged."};
  };
  window.__arMixStageChannelFormat=(channel,format)=>{
    const d=state.dryRoutingLab||{},epoch=Number(d.epoch)||0;
    if(!d.ready||!d.active||epoch<=0)return {ok:false,error:"No verified native DIRECT device."};
    if(patchPending)return {ok:false,error:"Native routing APPLY is pending."};
    if(!Number.isInteger(channel)||channel<0||channel>=10 ||
       !validChannelFormat(format))return {ok:false,error:"Invalid channel or processing format."};
    if(draftEpoch!==epoch)return {ok:false,error:"Device changed: review INPUT PATCH first."};
    draftRoutes[channel].channelFormat=format;
    mixerDraftActive=true;renderPatchMatrix();updateRouteLabIdentity();
    window.dispatchEvent(new Event("arMixRoutingDraftChanged"));
    return {ok:true,message:"DRAFT · channel processing "+format+" · MUTE + APPLY to activate."};
  };
  window.__arMixStageChannelInput=(channel,mode,left,right)=>{
    const d=state.dryRoutingLab||{},epoch=Number(d.epoch)||0;
    if(!d.ready||!d.active||epoch<=0)return {ok:false,error:"No verified native DIRECT device."};
    if(patchPending)return {ok:false,error:"Native route APPLY already pending."};
    if(!Number.isInteger(channel)||channel<0||channel>=10)return {ok:false,error:"Invalid mixer channel."};
    if(draftEpoch!==epoch)return {ok:false,error:"Device epoch changed. Reopen I/O and review pins."};
    if(!["NONE","MONO","STEREO"].includes(mode))return {ok:false,error:"Use the full matrix for BUS routing."};
    const active=new Set(patchPorts().map(p=>p.port));
    if(mode==="MONO"&&!active.has(left))return {ok:false,error:"Selected mono hardware pin is unavailable."};
    if(mode==="STEREO"&&(!active.has(left)||!active.has(right)||left===right))
      return {ok:false,error:"Stereo L/R requires distinct enabled hardware pins."};
    const target=draftRoutes[channel];
    if(target.mode==="NONE" && mode!=="NONE")target.channelFormat=legacyFormat(mode);
    target.mode=mode;target.left=mode==="NONE"?-1:left;target.right=mode==="STEREO"?right:-1;
    mixerDraftActive=true;renderPatchMatrix();updateRouteLabIdentity();
    window.dispatchEvent(new Event("arMixRoutingDraftChanged"));
    return {ok:true};
  };
  window.__arMixApplyMixerDraft=()=>{
    if(patchPending)return {ok:false,error:"Native APPLY is already pending."};
    if(state.monitor)return {ok:false,error:"MUTE OUTPUT before applying input routing."};
    const failure=patchPreflight();
    if(failure)return {ok:false,error:failure};
    document.getElementById("arRouteApply").click(); // exact existing preflight + native ACK path
    return {ok:patchPending,error:patchPending?"":"Route was not submitted."};
  };
  window.__arMixOpenChannelPatch=(channel,tab="input")=>{
    if(!Number.isInteger(channel)||channel<0||channel>=10)return;
    openRoutingLab();
    selectPatchTab(tab==="internal"?"internal":"input");
    const target=tab==="internal"?
      document.getElementById("arAuxSendGrid")?.querySelector('[data-aux-from="'+channel+'"]'):
      document.querySelectorAll("#arPatchGrid .arPatchRow")[channel+1];
    target?.scrollIntoView?.({block:"nearest",inline:"nearest"});
  };
  // Native toolbar action remains the approved SIGNAL FLOW data-view,
  // but ROUTING opens on the same click without an intermediate lab screen.
  document.addEventListener("click",event=>{
    if(event.target.closest('[data-view="SIGNAL FLOW"]'))openRoutingLab();
  });
  const routingNav=document.querySelector('[data-view="SIGNAL FLOW"] span');
  if(routingNav)routingNav.textContent="ROUTING";
  // The main toolbar and left rail remain live. Leaving ROUTING immediately
  // hides its workspace; ESC also returns focus to the existing ROUTING tab.
  document.addEventListener("click",event=>{
    if(!routeOverlay.classList.contains("open"))return;
    if(event.target.closest('[data-view]:not([data-view="SIGNAL FLOW"]),[data-my-sounds],[data-plugin-manager],[data-native-action="audioSetup"],#arSignalOpen')){
      closeFxPicker();routeOverlay.classList.remove("open");
    }
  },true);
  document.addEventListener("keydown",event=>{
    if(event.key!=="Escape"||!routeOverlay.classList.contains("open"))return;
    if(fxPickerTarget){closeFxPicker(true);return;}
    routeOverlay.classList.remove("open");
    document.querySelector('[data-view="SIGNAL FLOW"]')?.focus();
  });
  document.getElementById("arOpenRoutingLab").addEventListener("click",openRoutingLab);
  document.getElementById("arRouteClose").addEventListener("click",()=>{closeFxPicker();routeOverlay.classList.remove("open");});
  routeOverlay.addEventListener("click",e=>{if(e.target===routeOverlay)routeOverlay.classList.remove("open");});
  document.getElementById("arRouteApply").addEventListener("click",()=>{
    const d=state.dryRoutingLab||{};
    if(patchPending)return;
    const failure=patchPreflight();
    if(failure){patchFeedback("REJECTED · "+failure);return;}
    if(state.monitor){patchFeedback("MUTE OUTPUT FIRST · structural routing cannot be applied while speakers are armed.");return;}
    syncNativeLevelsToRouteDraft();
    // patchPreflight above already checked epoch, enabled ports and stereo.
    // Enter pending only after *every* synchronous rejection gate has passed.
    patchPending=true;
    showIdle();
    renderPatchMatrix();
    document.getElementById("arRouteFeedback").textContent="Applying verified native route (output remains MUTED)...";
    api.emitEvent("arMixAction",{action:"routingDryApply",value:{
      schemaVersion:2,
      deviceEpoch:draftEpoch,revision:Number(d.revision)||0,
      mainOutputs:draftMainOutputs.length===2?draftMainOutputs.slice():(
        Array.isArray(d.mainOutputs)&&d.mainOutputs.length===2?d.mainOutputs.slice():
        (Array.isArray(d.outputPorts)?d.outputPorts.slice(0,2):[])),
      ...(draftMonitorOutputs.length===2?{
        monitorOutputs:draftMonitorOutputs.slice(),
        monitorSends:draftMonitorSends.slice()
      }:{}),
      channels:draftRoutes.map(r=>({...typedSource(r),fxIds:laneIds(r)})),
      auxes:draftAux.map(b=>({...b,fxIds:laneIds(b)})),
      sends:draftSends.map(s=>({...s}))
    }});
  });
  document.getElementById("arRouteClear").addEventListener("click",()=>{
    if(patchPending)return;
    if(state.monitor){patchFeedback("MUTE OUTPUT FIRST · clear routing while speakers are safe.");return;}
    if(!window.confirm("Clear all native INPUT PATCH assignments? OUTPUT remains MUTED."))return;
    patchPending=true;
    showIdle();
    renderPatchMatrix();
    patchFeedback("Clearing physical input patch; native mixer remains prepared and SILENT...");
    api.emitEvent("arMixAction",{action:"routingDryStop"});
  });
  // One native SAVE transaction only: no duplicated click listeners, no
  // heavy DOM recreation. Native Show can now carry FX state, but legacy
  // borrowed CH1 plugins remain deliberately blocked until unified.
  document.getElementById("arRouteSaveDry").addEventListener("click",()=>{
    const d=state.dryRoutingLab||{};
    if(nativeShowBusy || patchPending || !d.ready || !d.active ||
       !d.acceptedRoute || state.monitor || Number(state.chainCount)!==0 ||
       !Number.isInteger(Number(d.fxCount)) || Number(d.fxCount)<0){
      document.getElementById("arRouteFeedback").textContent=
        "SAVE SHOW requires native route ACK, MUTED OUTPUT and no legacy CH1 FX.";
      return;
    }
    nativeShowBusy=true;showIdle();
    document.getElementById("arRouteFeedback").textContent=
      "Capturing verified routing and native VST3 state · output remains MUTED...";
    api.emitEvent("arMixAction",{action:"routingDryShowSave"});
  });
  api.addEventListener("arMixNativeShowSaveAck",ack=>{
    nativeShowBusy=false;showIdle();
    document.getElementById("arRouteFeedback").textContent=ack?.ok
      ? "SHOW SAVED + VERIFIED · "+String(ack.showId||"")+" · Studio/Shows · ready for RECALL"
      : "SHOW NOT SAVED · "+String(ack?.error||"Unknown storage failure");
  });
  // P4 Stage 4: native Show browse/recall is explicit, worker-backed and
  // fails silent. This UI never treats a localStorage scene as a native Show.
  const nativeShowBrowser=document.getElementById("arNativeShowBrowser");
  const nativeShowSelect=document.getElementById("arNativeShowSelect");
  const nativeShowRecall=document.getElementById("arNativeShowRecall");
  const nativeShowStatus=document.getElementById("arNativeShowStatus");
  let nativeShowBusy=false;
  const nativeShowUuid=/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  function showIdle(){
    nativeShowRecall.disabled=nativeShowBusy||patchPending||!nativeShowUuid.test(nativeShowSelect.value)||
      !!state.monitor||!state.dryRoutingLab?.ready;
  }
  document.getElementById("arRouteBrowseShows").addEventListener("click",()=>{
    if(nativeShowBusy)return;
    nativeShowBrowser.hidden=false;
    nativeShowSelect.replaceChildren(new Option("Loading saved native Shows...",""));
    nativeShowStatus.textContent="Browsing Studio/Shows on native IO worker...";
    nativeShowBusy=true;showIdle();
    api.emitEvent("arMixAction",{action:"routingDryShowList"});
  });
  nativeShowSelect.addEventListener("change",showIdle);
  nativeShowRecall.addEventListener("click",()=>{
    if(nativeShowBusy||patchPending||state.monitor||!state.dryRoutingLab?.ready||
       !nativeShowUuid.test(nativeShowSelect.value))return;
    nativeShowBusy=true;showIdle();
    nativeShowStatus.textContent="Reading SHA-verified Show; OUTPUT remains MUTED...";
    api.emitEvent("arMixAction",{action:"routingDryShowRecall",value:nativeShowSelect.value});
  });
  api.addEventListener("arMixNativeShowListAck",reply=>{
    nativeShowBusy=false;
    nativeShowSelect.replaceChildren(new Option("Choose a native Show...",""));
    if(reply?.ok && Array.isArray(reply.items)){
      for(const item of reply.items.slice(0,64)){
        if(!nativeShowUuid.test(item?.id))continue;
        nativeShowSelect.add(new Option(String(item.name||"Native Show"),item.id));
      }
    }
    nativeShowStatus.textContent=reply?.ok
      ?(nativeShowSelect.options.length-1)+" saved native Shows; choose and RECALL while OUTPUT is MUTED."
      :"SHOW BROWSE REJECTED · "+String(reply?.error||"Native IO unavailable");
    showIdle();
  });
  api.addEventListener("arMixNativeShowRecallAck",reply=>{
    nativeShowBusy=false;
    if(reply?.ok && reply?.state){
      // One authoritative acceptedRoute from native; no local draft can
      // invent a LIVE mapping before the revisioned native publication ACK.
      state.dryRoutingLab=reply.state;
      hydrateAcceptedRoute();
      syncNativeLevelsToRouteDraft();
      if(patchTab==="internal")drawRoutes();
      renderPatchMatrix();
      updateRouteLabIdentity();
    }
    nativeShowStatus.textContent=reply?.ok
      ?"SHOW RESTORED · OUTPUT MUTED. Verify physical routing and plugin sound before unmute."
      :"SHOW RECALL REJECTED · "+String(reply?.error||"Device/route changed");
    showIdle();
  });

  api.addEventListener("arMixRoutingLabAck",reply=>{
    const result=reply?.state||reply||{};
    patchPending=false;
    showIdle();
    if(result && typeof result==="object" && "epoch" in result)state.dryRoutingLab=result;
    if(reply?.ok===true){
      rackFxDraft=null; // Native ACK became authoritative, not the browser draft.
      mixerDraftActive=false;
      sendResumeGain.clear();
      if(result.acceptedRoute)hydrateAcceptedRoute();
      else {
        // CLEAR resets CH, FX, and the new R2 CH→AUX matrix draft together.
        invalidateDraftForNewDevice(0);
        invalidateDraftForNewDevice(Number(result.epoch)||0);
        syncNativeLevelsToRouteDraft();
      }
    }
    renderPatchMatrix();
    if(patchTab==="internal")drawRoutes();
    if(patchTab==="output")renderPatchOutput();
    document.getElementById("arRouteFeedback").textContent=
      reply?.ok===false?("REJECTED · "+(reply.error||"Invalid route")):
      result.active&&result.ready&&result.acceptedRoute
        ?"NATIVE ACK: "+(Number(result.fxCount||0)+Number(state.chainCount||0))+" VST3 total (CH1 rack retained · "+(result.fxReused||0)+" native inserts preserved) · "+(result.auxCount||0)+" AUX · "+(result.auxSendCount||0)+" AUX sends · UNMUTE OUTPUT in SIGNAL CHECK"
        :result.active&&result.ready?"NATIVE ACK: INPUT PATCH CLEARED · 10CH+4AUX engine ready, output MUTED"
        :"NATIVE ROUTE UNAVAILABLE · output remains SILENT";
    updateRouteLabIdentity();
  });

  const el = id => document.getElementById(id);
  // 12Hz native telemetry must not replace unchanged text nodes or cause
  // needless main-thread paint/layout invalidations on static header labels.
  const setTextIfChanged=(node,value)=>{
    const text=String(value);
    if(node&&node.textContent!==text)node.textContent=text;
  };
  const percent = v => Math.max(1,Math.min(100,(20*Math.log10(Math.max(1e-6,Number(v)||0))+60)*100/60));
  // Evidence is input-side only; JUCE cannot report hardware render underruns.
  // Do not turn capture flags into fake "CPU overload" or restart devices.
  const captureFaultEvidence=d=>d?.connected && d?.diagnosticRoute==="NORMAL" &&
    !d?.adaptiveClockActive && Number(d?.chainCount)===0 &&
    d?.backendXrunMeaning==="WASAPI_CAPTURE_DISCONTINUITIES_ONLY" &&
    Number(d?.backendXrunCount)>=3 &&
    Number(d?.overBudgetCount)===0;
  function diagnosis(d) {
    if(d.dryRoutingLab?.active) return d.dryRoutingLab.ready?
      "Live 10-channel native route: "+(d.dryRoutingLab.fxCount||0)+" independent native VST3 inserts, PRE/POST sends and BUS returns. Use low headphone volume.":
      "Physical input mapping is STALE. Output is SILENT until the device is remapped and ACKed.";
    if(d.diagnosticRoute==="OUTPUT_ONLY_TONE") return "Output-only test: capture endpoint is disconnected. Listen for tone at low headphone volume and click MARK CRACKLE when audible.";
    if(d.diagnosticRoute==="CAPTURE_ONLY") return "Capture-only test: hardware render endpoint is disconnected. Observe capture input and backend discontinuity count; you should hear no audio.";
    if(d.diagnosticRoute==="DRY_THROUGH") return "Dry-through test: microphone to output with all VST3 bypassed. MARK CRACKLE at audible glitches.";
    if (d.error) return "Audio driver error: "+d.error;
    if (!d.connected) return "No audio device connected. Open AUDIO SETUP.";
    if (!d.deviceOutputChannels) return "No active output channels. Select a headphone/speaker output in AUDIO SETUP.";
    if (!d.monitor) return "OUTPUT MUTED by user. Audio routes automatically after a working input/output device is selected.";
    if (d.masterMute) return "MASTER MUTED: switch off the master MUTE button.";
    if (d.channelMute) return "CHANNEL 1 MUTED: switch off Lead Vocal MUTE.";
    if(captureFaultEvidence(d))
      return "Capture dropouts recorded with NO FX and no DSP overruns ("+
        Math.floor(Number(d.backendXrunCount))+" capture flags). This is not proof of render underruns. " +
        "Compare DIRECT with TRY INDEPENDENT CLOCK for your VB-Cable → headphone route. " +
        "Switching reopens devices; use low headphone volume and change only when safe.";
    if ((d.inputPeak||0)>.006 && (d.postFxPeak||0)<.0002) return "Mic input is detected but VST3 output is silent. Enable DRY TEST to bypass all plugins.";
    if ((d.postFxPeak||0)>.003 && (d.peak||0)<.0002) return "Post-FX audio exists, but hardware output is silent. Check faders, mutes and output settings.";
    if ((d.peak||0)>.003) return "Samples reach the output buffer. If you cannot hear them, check your interface mixer, physical output selection and headphone volume.";
    if ((d.inputPeak||0)<.0005) return "Microphone signal is low/absent. Check the selected ASIO/WASAPI input and gain.";
    return "Output armed. Speak into the microphone and inspect all three meters.";
  }
  function repaint(d) {
    if(routeOverlay.classList.contains("open")){
      const levels=Array.isArray(d.dryRoutingLab?.channelPeaks)?d.dryRoutingLab.channelPeaks:[];
      routeOverlay.querySelectorAll('[data-route-peak]').forEach(el=>{
        const peak=Number(levels[Number(el.dataset.routePeak)])||0;
        setTextIfChanged(el,d.dryRoutingLab?.ready?(Math.round(Math.min(1,peak)*100)+"%"):"—");
      });
      if(draftEpoch!==(Number(d.dryRoutingLab?.epoch)||0)) {
        patchPending=false;
        drawRoutes(); // fresh port names; reset stale user assignments
        renderPatchMatrix();
        if(patchTab==="output")renderPatchOutput();
        document.getElementById("arRouteFeedback").textContent=
          "AUDIO DEVICE CHANGED · all input assignments reset. Select ports again.";
      } else updateRouteLabIdentity();
    }
    setTextIfChanged(el("arSignalBadge"),!d.monitor ? "OUTPUT MUTED" : d.masterMute ? "MASTER MUTED" : d.channelMute ? "CH1 MUTED" : "OUTPUT ARMED");
    const outputBadge=el("arSignalBadge");
    if(outputBadge)outputBadge.style.color =
      d.monitor && !d.masterMute && !d.channelMute ? "#71e9cc" : "#e9be7c";
    const monitorButton=document.querySelector('[data-native-action="monitor"]');
    setTextIfChanged(monitorButton,d.monitor?"MUTE OUTPUT":"UNMUTE OUTPUT");
    // The original UI shell had fixed 48 kHz / 64-sample demo badges. Show
    // the REAL negotiated driver parameters instead of misleading the user.
    const rateLabel=document.querySelector('.spec:not(.spec-device):not(.bpm) strong');
    setTextIfChanged(rateLabel,d.rate?String(Math.round(d.rate/1000)):"—");
    const bufferLabel=document.querySelector('.spec.bpm strong');
    setTextIfChanged(bufferLabel,d.buffer?String(d.buffer):"—");
    const driverLabel=document.querySelector('.spec-device strong');
    setTextIfChanged(driverLabel,d.driver||"NO DRIVER");
    const liveLabel=document.querySelector('.status > span:first-child > b');
    setTextIfChanged(liveLabel,d.dryRoutingLab?.ready?"NATIVE 10CH ROUTED":"NATIVE CH1 · UNPATCHED");
    const edition=document.querySelector('.status > span:last-child');
    setTextIfChanged(edition,d.dryRoutingLab?.active?
      "AR MIX P1H · REAL 10CH FX / BUS SENDS":"AR MIX P2 · CH1 NATIVE + UI PREVIEW");
    // Live STREAM endpoint is a separate WASAPI playback clock with an
    // operator-selected device. A physical MAIN mute does NOT mute STREAM.
    // Keep the controls compact; never claim a device is active without native ACK.
    const streamConnected=!!d.secondaryOutputReady;
    const streamArmed=!!d.secondaryOutputArmed;
    el("arStreamConnect").disabled=!!d.monitor || !!d.adaptiveClockActive ||
      !d.directRouteAuthorized || !d.dryRoutingLab?.acceptedRoute;
    el("arStreamArm").disabled=!streamConnected || !!d.secondaryOutputFault ||
      !d.dryRoutingLab?.acceptedRoute || !!d.adaptiveClockActive;
    el("arStreamStop").disabled=!streamConnected && !d.secondaryOutputFault;
    setTextIfChanged(el("arStreamArm"),streamArmed?"DISARM OUTPUT":"ARM OUTPUT");
    setTextIfChanged(el("arStreamStatus"),
      d.secondaryOutputFault?"OUTPUT FAULT / SOURCE CLOCK CHANGED · DISCONNECT AND RECONNECT":
      !streamConnected?"OUTPUT DISCONNECTED · select Windows playback device":
      (streamArmed?"OUTPUT ACTIVE":"OUTPUT CONNECTED · not armed")+
      " · "+String(d.secondaryOutputName||"")+
      " · "+Math.round(Number(d.secondaryOutputRate)||0)+" Hz / ASRC "+
      (Number(d.secondaryOutputBridgeMs)||0).toFixed(1)+" ms"+
      " · FIFO underflow "+(d.secondaryOutputUnderflow||0)+
      " / overflow "+(d.secondaryOutputOverflow||0)+
      ". For external apps choose their corresponding capture device if applicable.");
    if (!overlay.classList.contains("open")) return;
    el("arInputName").textContent=d.dryRoutingLab?.active?"NATIVE 10 CH INPUTS":d.diagnosticRoute==="OUTPUT_ONLY_TONE"?"INTERNAL TEST TONE":d.inputDevice||"Input device";
    el("arFxName").textContent=d.dryRoutingLab?.active?(String(d.dryRoutingLab.fxCount||0)+" NATIVE VST3"):d.diagnosticRoute==="OUTPUT_ONLY_TONE"?"NO INPUT / NO FX":d.diagnosticRoute!=="NORMAL"?"NO FX":d.dryTest?"DRY BYPASS":"VST3 Chain";
    el("arOutputName").textContent=d.outputDevice||"Output device";
    el("arInputBar").style.width=percent(d.dryRoutingLab?.active?Math.max(d.dryRoutingLab.peakL||0,d.dryRoutingLab.peakR||0):d.inputPeak)+"%";
    el("arFxBar").style.width=percent(d.postFxPeak)+"%";
    el("arOutputBar").style.width=percent(d.peak)+"%";
    el("arSignalReason").textContent=diagnosis(d);
    el("arDiagnosticMode").value=d.diagnosticRoute||"NORMAL";
    el("arSignalMonitor").textContent=d.monitor?"MUTE OUTPUT":"UNMUTE OUTPUT";
    el("arSignalDry").textContent=d.dryTest?"DRY TEST ON":"DRY TEST OFF";
    const trial=captureFaultEvidence(d);
    el("arSmartClockToggle").textContent=d.adaptiveClockActive?
      "SMART CLOCK: INDEPENDENT (BETA)":trial?"TRY INDEPENDENT CLOCK (BETA)":"SMART CLOCK: DIRECT";
    el("arSmartClockToggle").title=trial?
      "Explicit A/B test for recorded WASAPI capture dropouts. Opens separate capture/render devices; can add latency. No automatic switch.":
      "Manual independent capture/render clock control";
    el("arHealthPanel").textContent="CALLBACK "+(d.callbackDurationUs||0)+" µs / period "+
      Math.round(d.periodUs||0)+" µs | Worst "+(d.worstCallbackUs||0)+
      " µs | Over-budget "+(d.overBudgetCount||0)+
      " | Long gaps "+(d.callbackGapCount||0)+
      " | Late intervals (>1.5x) "+(d.lateCallbackIntervals||0)+
      " | DSP p95/p99 budget (upper bound) "+(d.callbackP95BeyondDeadline?">100":Number(d.callbackP95BudgetUpperPct??0).toFixed(1))+"/"+(d.callbackP99BeyondDeadline?">100":Number(d.callbackP99BudgetUpperPct??0).toFixed(1))+"%"+
      " | Worst VST "+(d.worstPluginUs||0)+" µs"+
      " | device starts/stops "+(d.deviceStartCount||0)+"/"+(d.deviceStopCount||0)+
      " | backend XRUN "+(d.backendXrunCount??"N/A")+
      " ("+(d.backendXrunMeaning||"UNKNOWN")+")"+
      " | Render underflow UNOBSERVABLE via JUCE (timing NOT xruns)"+
      " | Clock "+(d.adaptiveClockMode||"DIRECT")+
      (d.adaptiveClockActive?" | FIFO "+(d.bridgeOccupancyFrames||0)+"f"+
      " | bridge lost capture "+(d.bridgeOverflowFrames||0)+
      " | bridge missing render "+(d.bridgeUnderflowFrames||0)+
      " | drift "+Math.round(d.bridgeCorrectionPpm||0)+"ppm":"");
    const timing=Array.isArray(d.pluginTimings)?d.pluginTimings:[];
    const fxInfo=timing.map(x=>x.slot+". "+x.name+": "+x.lastUs+" µs now, worst "+x.worstUs+" µs").join(" | ");
    if(fxInfo)el("arHealthPanel").textContent+=" | FX: "+fxInfo;
    el("arSignalFooter").textContent=(d.driver||"Audio")+" · "+(d.rate||0)+" Hz / "+(d.buffer||0)+
      " samples · "+(d.deviceInputChannels||0)+" active input / "+(d.deviceOutputChannels||0)+" active output channels · "+
      (d.latencySamples||0)+" samples plugin latency. Hardware MAIN via AUDIO SETUP; Secondary output uses user-selected WASAPI endpoint with ASRC for distinct clock. No 0ms latency claim.";
  }
  // Only a fully initialized WebView2 ArSonMix page sends this. An old-IE
  // navigation error can never mark the native engine GUI as loaded.
  // WebView2 navigation completion is NOT compositor completion. Wait for
  // embedded Inter to settle and two visible paint opportunities before
  // allowing JUCE to remove its full-window opaque startup cover.
  // No polling/interval; normal app and mixer handlers are already installed.
  const guiPaintReady=()=>requestAnimationFrame(()=>
    requestAnimationFrame(()=>api.emitEvent("arMixGuiReady",{protocol:1})));
  if(document.fonts?.ready)
    document.fonts.ready.then(guiPaintReady,guiPaintReady);
  else guiPaintReady();
  api.addEventListener("arMixStatus",d=>{
    state=d;
    // Single controller owns epoch hydration even if the full PATCH workspace
    // is closed. The status projection used by MIX is deliberately read-only.
    // When PATCH is open, repaint() performs its own redraw/invalidation.
    if(!routeOverlay.classList.contains("open") &&
       draftEpoch!==(Number(d.dryRoutingLab?.epoch)||0)){
      patchPending=false;
      hydrateAcceptedRoute();
    }
    repaint(d);
  });
  let streamPending=false;
  const requestStreamInventory=()=>
    api.emitEvent("arMixAction",{action:"audioDeviceInventory"});
  api.addEventListener("arMixAudioDeviceInventory",data=>{
    const available=Array.isArray(data?.types)?data.types.find(x=>x.name==="Windows Audio"):null;
    const options=Array.isArray(available?.outputs)?available.outputs:[];
    const select=el("arStreamDevice");
    const chosen=select.value||state.secondaryOutputName||"";
    select.replaceChildren(new Option("Select actual Windows playback endpoint...",""));
    for(const name of [...new Set(options.filter(x=>typeof x==="string"&&x.length<=240))])
      select.add(new Option(name,name));
    if([...select.options].some(x=>x.value===chosen))select.value=chosen;
  });
  el("arStreamRefresh").addEventListener("click",requestStreamInventory);
  document.querySelector('[data-patch-tab="output"]')?.addEventListener("click",requestStreamInventory);
  el("arStreamConnect").addEventListener("click",()=>{
    if(streamPending || state.monitor || state.adaptiveClockActive)return;
    const selected=el("arStreamDevice").value;
    if(!selected){
      setTextIfChanged(el("arStreamStatus"),"Select an available Windows playback device.");
      return;
    }
    streamPending=true;
    api.emitEvent("arMixAction",{action:"secondaryOutputConnect",value:selected});
  });
  el("arStreamArm").addEventListener("click",()=>{
    if(streamPending || !state.secondaryOutputReady)return;
    streamPending=true;
    api.emitEvent("arMixAction",{action:"secondaryOutputArm",value:!state.secondaryOutputArmed});
  });
  el("arStreamStop").addEventListener("click",()=>{
    if(streamPending)return;
    streamPending=true;
    api.emitEvent("arMixAction",{action:"secondaryOutputStop"});
  });
  api.addEventListener("arMixSecondaryOutputAck",d=>{
    streamPending=false;
    if(!d?.ok)setTextIfChanged(el("arStreamStatus"),
      "STREAM REJECTED · "+String(d?.error||"Device or native route unavailable"));
  });
  el("arSignalOpen")?.addEventListener("click",()=>{
    overlay.classList.add("open");
    requestStreamInventory();
    repaint(state);
  });
  el("arSignalClose").addEventListener("click",()=>overlay.classList.remove("open"));
  el("arSignalMonitor").addEventListener("click",()=>api.emitEvent("arMixAction",{action:"monitor",value:!state.monitor}));
  el("arSignalDry").addEventListener("click",()=>api.emitEvent("arMixAction",{action:"dryTest",value:!state.dryTest}));
  el("arDiagnosticMode").addEventListener("change",e=>
    api.emitEvent("arMixAction",{action:"diagnosticRoute",value:e.target.value}));
  el("arDiagnosticMark").addEventListener("click",()=>
    api.emitEvent("arMixAction",{action:"diagnosticMark"}));
  el("arSmartClockToggle").addEventListener("click",()=>
    api.emitEvent("arMixAction",{action:"smartClock",value:!state.adaptiveClockActive}));
  el("arDiagnosticCopy").addEventListener("click",()=>{
    el("arDiagnosticCopyStatus").textContent="Preparing report...";
    api.emitEvent("arMixAction",{action:"copyDiagnosticReport"});
  });
  api.addEventListener("arMixDiagnosticReportCopied",d=>{
    el("arDiagnosticCopyStatus").textContent="COPIED ("+Math.round((Number(d.bytes)||0)/1024)+" KB) · paste into chat";
  });
  el("arSignalLogs").addEventListener("click",()=>api.emitEvent("arMixAction",{action:"openDiagnostics"}));
  el("arSignalSetup").addEventListener("click",()=>{
    overlay.classList.remove("open");
    document.querySelector('#audioSetupBtn')?.click();
  });
  overlay.addEventListener("click",e=>{if(e.target===overlay)overlay.classList.remove("open");});
  document.addEventListener("click",e=>{
    if(!e.target.closest("[data-output],#monitorSource"))return;
    e.preventDefault();e.stopImmediatePropagation();
    overlay.classList.add("open");repaint(state);
  },true);
})();
