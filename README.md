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

**[Open the official download page](https://masarray.github.io/arsonmix/#download)** and choose your operating system. For Windows 10/11 x64, select **Download installer ZIP**, extract it, then run Setup. The link downloads the published file directly from the landing page.

The download cards show the newest verified package available for each platform, including its version and size. Windows previews are published independently of Linux and macOS builds, so their version numbers may differ.

The built-in effect is now named **Vocal Delay** (formerly Vocal Throw); saved sessions and presets retain the original internal identity. Recent Windows previews add global mixer and supported VST3 parameter hotkeys, Harmonice stereo lead Pitch Tune and ensemble improvements, and native user presets for EQ, Compressor, Harmonice, and Vocal Delay. For the scope of an installed build, match its version to the [release notes](https://github.com/masarray/arsonmix/releases).

## Get started

1. Download the Windows installer ZIP from the [official landing page](https://masarray.github.io/arsonmix/#download), extract it, run Setup, and start ArSonMix. A supported audio driver and Windows WebView2 runtime may be required.
2. Open the audio setup and choose the actual device/pins for listening. The mixer can be edited without hardware, but it can only output sound through a valid live audio device graph.
3. Use channel controls, FX and WIRE in the [approved console](https://masarray.github.io/arsonmix/preview/). Save/Open `.smix` sessions for your work. Start testing at safe headphone/speaker volume.
4. Report reproducible driver, WIRE, VST3, session or dropout problems in [GitHub Issues](https://github.com/masarray/arsonmix/issues), with device type, channel graph, sample rate, buffer size and steps.

### Field-test limitations

Windows alpha builds are intended for field testing. If you have a 4–8-output ASIO interface and your release includes Multi Out, report your driver version, sample rate, buffer size, and results for routing, mute, and device restart.

## Preview vs real audio

**The one approved GUI:** [masarray.github.io/arsonmix/preview/](https://masarray.github.io/arsonmix/preview/). It is an interactive **visual preview only** — no browser hardware audio, no real native VST3 processing and no promise that a UI illustration is a supported sound route. Older preview URLs redirect here. The native Windows app uses the approved mixer renderer; changes must not introduce competing skins.

## Source, builds and contribution

Each [published release](https://github.com/masarray/arsonmix/releases) provides its source tag, release notes, and SHA-256 checksums. The version displayed on the landing page identifies the package being downloaded.

Contributors and development agents must read **[AGENTS.md](AGENTS.md)** and the **[Smart CI workflow](docs/SMART_CI_RUNNER_WORKFLOW.md)**, inspect live `main` plus open PRs, preserve one native JUCE PCM callback and native ChannelId/EdgeId authority, and qualify changes using selective exact-SHA tests. Hardware ASIO testing is separate from automated CI. Website deployment uses one `site/` landing and one assembled `/preview/` on GitHub Pages and Cloudflare Pages, with platform downloads resolved from verified GitHub Release assets. See [Pages publishing](docs/GITHUB_PAGES_PREVIEW.md) and [Cloudflare](docs/CLOUDFLARE_PAGES_BUILD.md).

Respect all source, SDK and VST3 plugin licenses. Third-party commercial VST3 plugins are not redistributed without authorization. Historical architecture and testing records remain in [docs/](docs/) (including the archived [GUI baseline](docs/GUI_BASELINE_LOCK_2026-10-25.md), [Smart Audio Engine ARD](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md), and [Implementation Blueprint](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md)), rather than being presented as current shipping features. Physical crackle and hardware audio FAT evidence is tracked in [issue #15](https://github.com/masarray/arsonmix/issues/15).
