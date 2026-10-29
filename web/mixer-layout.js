// ArSonMix mixer-shell coordinator. Presentation only: no routing or DSP state.
// Mounted after mixer-io.js so all native controllers and delegated handlers remain owners.
(() => {
  'use strict';
  const rail=document.querySelector('#app .left-rail');
  const shell=document.querySelector('#app .workbench');
  const bank=document.querySelector('.arMixerBankBar');
  const nav=document.querySelector('.toolbar-actions .nav');
  if(!rail||!shell||!bank||!nav)return;
  if(rail.querySelector('.arRailModeSection'))return;

  // The existing mixer-io module retains bank, mode, APPLY and I/O references.
  // Move the WHOLE bank, not its buttons (its listeners query bank descendants).
  const modeSection=document.createElement('section');
  modeSection.className='arRailModeSection';
  modeSection.setAttribute('aria-label','Mixer layer modes');
  const heading=document.createElement('div');
  heading.className='arRailHeading';heading.textContent='CHANNEL MODE';
  modeSection.append(heading,bank);
  rail.insertBefore(modeSection,document.getElementById('channelList'));
  bank.setAttribute('aria-label','Channel processing and routing mode');

  const actions=document.createElement('section');
  actions.className='arRailUtilities';
  actions.setAttribute('aria-label','Console utilities');
  const utilitiesTitle=document.createElement('div');
  utilitiesTitle.className='arRailHeading';utilitiesTitle.textContent='UTILITIES';
  const grid=document.createElement('div');grid.className='arRailUtilityGrid';
  const selectors=['[data-my-sounds]','[data-plugin-manager]','#audioSetupBtn','#arSignalOpen'];
  for(const selector of selectors){
    const button=nav.querySelector(selector);
    if(!button)continue;
    grid.appendChild(button);
  }
  actions.append(utilitiesTitle,grid);
  const footer=rail.querySelector('.rail-input-footer');
  rail.insertBefore(actions,footer);
  // Only true navigable workspaces stay in the top segmented tabs.
  const names={'MIX':'MIXER','FX RACK':'FX RACK','SIGNAL FLOW':'PATCH'};
  for(const button of nav.querySelectorAll('[data-view]')){
    const label=button.querySelector('span');
    if(label&&names[button.dataset.view])label.textContent=names[button.dataset.view];
    button.setAttribute('aria-label',names[button.dataset.view]||button.textContent.trim());
  }
  nav.setAttribute('aria-label','Workspace tabs');
  document.querySelector('.toolbar-actions')?.classList.add('arSegmentedToolbar');

  // Bank modes are operational even when the full FX/PATCH workspace is open.
  // Existing app.js remains the sole owner of view switching.
  window.addEventListener('click',e=>{
    if(!e.target.closest?.('.arRailModeSection [data-mix-mode]'))return;
    if(document.getElementById('mixer')?.classList.contains('hidden'))
      nav.querySelector('[data-view="MIX"]')?.click();
  },true);
  document.documentElement.dataset.arMixerShell='rail-modes';
})();
