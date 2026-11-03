/* V6 contextual button focus stabilizer: pointer only, no UI-state copies.
   Prevent mouse focus auto-scroll between button A/B; browser click and native
   app handlers still run normally. Keyboard Tab/Enter/Space are unchanged.
   Never intercept MUTE/SOLO/PFL PNG buttons or physical WIRE sockets. */
(()=>{
  'use strict';
  const owner=[
    '.app .command-bar button.tool','.app .command-bar summary.tool',
    '.app .more-tools-panel button.tool','.app .identity-actions button.console-control',
    '.app .mixer-title button.wire-master','.app .mixer-title button.wire-v34-tool',
    '.app .console-body .section-rail button.rail-cell[data-section]',
    '.app .console-body .strip button.mx-util-key',
    '.app .plugin-footer button','.app .plugin-browser .plugin-head button',
    '.app .plugin-browser .plugin-actions button','.app .plugin-browser .plugin-paths button',
    '.app .route-choice-head button','.app .route-choice-option',
    '.app .wire-workspace .wire-ws-head button',
    '.app .wire-workspace .wire-ws-tabs button',
    '.app .wire-workspace .wire-ws-foot button',
    '.app .wire-workspace .wire-ws-matrix button'
  ].join(',');
  document.addEventListener('mousedown',event=>{
    if(event.button!==0 || !event.cancelable || event.defaultPrevented)return;
    const control=event.target.closest('button,summary');
    if(!control||control.disabled||!control.matches(owner))return;
    // Prevent only the browser's mouse-induced focus-scroll. Do not stop
    // propagation, prevent click, blur keyboard focus, or alter scrollTop.
    event.preventDefault();
  },true);
})();
