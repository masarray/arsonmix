<p align="center">
  <a href="https://masarray.github.io/arsonmix/"><img src="assets/icon/brand-512.png" width="128" alt="ArSonMix official mixer icon"></a>
</p>
<p align="center"><strong>ArSonMix — Professional Live Mixing Studio</strong></p>
<p align="center">
  <a href="https://masarray.github.io/arsonmix/">Official downloads</a> ·
  <a href="https://masarray.github.io/arsonmix/preview/">Explore the mixer</a> ·
  <a href="https://github.com/masarray/arsonmix/issues">Feedback</a>
</p>

# ArSonMix

**A native live digital mixer for singers, karaoke, streaming and live sound.** Route audio through channels and WIRE connections, work with FX and music, and keep your settings in a `.smix` session. ArSonMix is a live mixing console, **not** a timeline/recording DAW or a browser-based audio mixer.

## Download ArSonMix

**[Download the latest verified ArSonMix release](https://masarray.github.io/arsonmix/#download)** from the official landing page and choose your operating system. The Windows card always selects the newest verified public Windows installer; on Windows 10/11 x64, click **Download installer ZIP**, extract it, then run Setup. No GitHub account or visit to the GitHub Releases UI is needed.

**Latest published Windows alpha as of 17 November 2026:** [v0.5.0-alpha.25 — Download installer ZIP](https://github.com/masarray/arsonmix/releases/download/v0.5.0-alpha.25/ArSonMix-v0.5.0-alpha.25-Windows-x64-Setup.zip) · [portable ZIP](https://github.com/masarray/arsonmix/releases/download/v0.5.0-alpha.25/ArSonMix-v0.5.0-alpha.25-Windows-x64-Portable.zip) · [SHA-256 checksums](https://github.com/masarray/arsonmix/releases/download/v0.5.0-alpha.25/SHA256SUMS.txt) · [release notes](docs/releases/v0.5.0-alpha.25.md). These are pinned alpha.25 hotfix links; for future verified releases, use the official version-aware landing page.

The download cards show the newest verified package available for each platform, including its version and size. Windows previews are published independently of Linux and macOS builds, so their version numbers may differ.

### What is included in the current Windows alpha

- **8D-SonKu P9.9:** the approved binaural 360° orbit, depth-aware FRONT/REAR perspective and native SPACE controls. The animation is visual feedback; one native JUCE PCM engine renders audio, with SonKu settings isolated from ImmerSIB-only scene tuning.
- **ImmerSIB:** seven concert/venue scenes, including Live Concert, Velvet Cinema and Concert Front Row XL, in a compact native JUCE editor. Factory scenes, genuine Power/bypass state, audio-applied setting ACK, 1.1–2.5 m source distance and Early Room up to 180% are included in alpha.24.
- **Vocal Delay:** native phrase-aware delay, formerly displayed as Vocal Throw. Existing `.smix` sessions and presets retain the compatible internal plugin identity.
- **Harmonice, Guitar FX, EQ & Compressor:** native effects with dedicated GUI controls, alongside supported VST3 parameter hotkeys and built-in user presets.
- **Live mixer & WIRE:** input channels, MAIN/BUS routing, gain, PRE/POST sends, mute/solo and Save/Open `.smix` sessions. ArSon Player remains part of the console workflow.

**Alpha.25 hotfix:** ImmerSIB now opens its real native JUCE concert window; switching to dearVR or other VST3 editor no longer closes the ImmerSIB window, and clicking the same insert refocuses it. False “Effect settings updated” editor-open toast is removed. No spatial DSP/preset changes. Existing alpha.24 features remain. Experimental Guitar FX redesign or unmerged branches are **not** shipped. See the [alpha.25 release notes](docs/releases/v0.5.0-alpha.25.md) and [alpha.24 baseline](docs/releases/v0.5.0-alpha.24.md) for compatibility and limitations.

## Get started

1. Download the Windows installer ZIP from the [official landing page](https://masarray.github.io/arsonmix/#download), extract it, run Setup, and start ArSonMix. A supported audio driver and Windows WebView2 runtime may be required.
2. Open the audio setup and choose the actual device/pins for listening. The mixer can be edited without hardware, but it can only output sound through a valid live audio device graph.
3. Use channel controls, FX and WIRE in the [approved console](https://masarray.github.io/arsonmix/preview/); use the installed Windows app for real 8D-SonKu and ImmerSIB audio. Save/Open `.smix` sessions for your work. Start testing at safe headphone/speaker volume.
4. Report reproducible driver, WIRE, VST3, session or dropout problems in [GitHub Issues](https://github.com/masarray/arsonmix/issues), with device type, channel graph, sample rate, buffer size and steps.

### Field-test limitations

Windows alpha builds are intended for field testing, not certified low-latency monitoring. Compare ImmerSIB's seven scenes and SonKu P9.9 at matched volume on headphones, then test slot/rack/global FX bypass, `.smix` reopen and device reconnect. Back up older ImmerSIB sessions: saved source distances beyond the new 1.1–2.5 m range are clamped on restore. For 4–8-output ASIO interfaces, report driver version, sample rate, buffer size, routing, mute and device restart results.

## Preview vs real audio

**The one approved GUI:** [masarray.github.io/arsonmix/preview/](https://masarray.github.io/arsonmix/preview/). It is an interactive **visual preview only** — no browser hardware audio, no real native VST3 processing and no promise that a UI illustration is a supported sound route. Older preview URLs redirect here. The native Windows app uses the approved mixer renderer; changes must not introduce competing skins.

## License — mixer and native built-in FX

**AGPL-3.0-only (GNU Affero General Public License version 3 only)** applies to original ArSonMix mixer code, routing and JUCE host, original desktop/web GUI implementation, and ArSonMix-owned **native built-in FX**, including EQ, Compressor, Harmonice, Vocal Delay, 8D-SonKu, Guitar FX and feature-gated ImmerSIB. See the complete [LICENSE](LICENSE) and [LICENSE-NOTICE.txt](LICENSE-NOTICE.txt). Source code and release tags are available in this repository for recipients of the application.

Third-party components are **not automatically relicensed**: JUCE, VST3/ASIO SDKs, MIT-licensed KEMAR/HRIR and Signalsmith materials, Inter font, VB-CABLE, and independently installed/approved FactoryFX VST3 may carry separate terms. Refer to distributed notices and `third_party/`. AGPL does not grant a redistribution licence for commercial VST3 plugins.

## Source, builds and contribution

Each [published release](https://github.com/masarray/arsonmix/releases) provides its source tag, release notes, and SHA-256 checksums. The version displayed on the landing page identifies the package being downloaded.

Contributors and development agents must read **[AGENTS.md](AGENTS.md)** and the **[Smart CI workflow](docs/SMART_CI_RUNNER_WORKFLOW.md)**, inspect live `main` plus open PRs, preserve one native JUCE PCM callback and native ChannelId/EdgeId authority, and qualify changes using selective exact-SHA tests. Hardware ASIO testing is separate from automated CI. Website deployment uses one `site/` landing and one assembled `/preview/` on GitHub Pages and Cloudflare Pages, with platform downloads resolved from verified GitHub Release assets. See [Pages publishing](docs/GITHUB_PAGES_PREVIEW.md) and [Cloudflare](docs/CLOUDFLARE_PAGES_BUILD.md).

Respect all source, SDK and VST3 plugin licenses. Third-party commercial VST3 plugins are not redistributed without authorization. Historical architecture and testing records remain in [docs/](docs/) (including the archived [GUI baseline](docs/GUI_BASELINE_LOCK_2026-10-25.md), [Smart Audio Engine ARD](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md), and [Implementation Blueprint](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md)), rather than being presented as current shipping features. Physical crackle and hardware audio FAT evidence is tracked in [issue #15](https://github.com/masarray/arsonmix/issues/15).
