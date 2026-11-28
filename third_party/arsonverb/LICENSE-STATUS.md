# ArSonVerb copyright and distribution

ArSonVerb — Copyright (c) 2026 Mas Ari / SonKuPik.

**Chosen by the project owner on 2026-11-01: free/open-source AGPL-3.0-only.**
See the complete GNU Affero General Public License Version 3 in [LICENSE](LICENSE).
SPDX identifier: `AGPL-3.0-only`. Source is offered under this licence;
there is no proprietary relicensing or closed-source exception in this repository.

## Third-party rights and corresponding source
- **JUCE 9.0.4** (downloaded at build time, not copied into this repo):
  used under its AGPLv3 option. Preserve JUCE ownership and comply with the
  framework's AGPLv3 terms; see https://github.com/juce-framework/JUCE/blob/9.0.4/LICENSE.md
  and https://www.gnu.org/licenses/agpl-3.0.en.html.
- **Inter Variable font:** embedded in the GUI under the SIL Open Font
  License 1.1; original copyright, terms and redistribution notice preserved
  at [assets/fonts/OFL.txt](assets/fonts/OFL.txt).
- **Logo / gold knob:** project GUI artwork, credited to ArSonVerb. Confirm
  ownership/redistribution rights to any externally contributed derivatives
  before publishing official installer binaries.
- **VST3 SDK:** downstream redistribution must retain and comply with any
  applicable SDK notices. This repository builds using JUCE, not a vendored
  proprietary Valhalla implementation.

The tagged public GitHub source archive must remain available with the exact
released binary and build instructions. Do not publish AGPL binaries lacking
the corresponding source/required upstream notices.

## P9 physical acceptance and public publication
On 2026-11-01, the owner accepted the P9 build following its startup and
preset-switch remediation and explicitly approved **public stable v1.0.0**.
Windows VST3/Standalone JUCE 12/12 CTests and Linux 11/11 plus ASan/UBSan
passed. The one-shot protected release branch ensures binary and tagged
source come from current `main`, with a full Windows rebuild/test before
publishing. This acceptance does not certify every ASIO/audio-driver or host
combination, nor claim a universal zero-xrun guarantee.
