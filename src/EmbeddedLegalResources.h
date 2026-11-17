// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The native JUCE binary owns the complete offline legal notices.
// Resource names are deterministic CMake configure_file() outputs.
// No adjacent .txt/.md files or runtime filesystem requests are required.
inline bool resolveEmbeddedLegalResource (const juce::String& request,
                                          const char*& data, int& size,
                                          juce::String& mime)
{
    const auto path = request.upToFirstOccurrenceOf ("?", false, false);
    mime = "text/plain; charset=utf-8";
#define ARMIX_LEGAL_ROUTE(route, symbol) \
    if (path == route) { data = BinaryData::symbol; size = BinaryData::symbol##Size; return true; }
    ARMIX_LEGAL_ROUTE ("/legal/overview", armixnotice_txt)
    ARMIX_LEGAL_ROUTE ("/legal/agpl", armixagpl_txt)
    ARMIX_LEGAL_ROUTE ("/legal/hrir", armixhrirmit_txt)
    ARMIX_LEGAL_ROUTE ("/legal/signalsmith-stretch", armixsignalsmithstretch_txt)
    ARMIX_LEGAL_ROUTE ("/legal/signalsmith-linear", armixsignalsmithlinear_txt)
    ARMIX_LEGAL_ROUTE ("/legal/inter", armixinterofl_txt)
    ARMIX_LEGAL_ROUTE ("/legal/vb-cable", armixvbcablenotice_txt)
#undef ARMIX_LEGAL_ROUTE
    return false;
}
