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
    ARMIX_LEGAL_ROUTE ("/legal/overview", armix_notice_txt)
    ARMIX_LEGAL_ROUTE ("/legal/agpl", armix_agpl_txt)
    ARMIX_LEGAL_ROUTE ("/legal/hrir", armix_hrir_mit_txt)
    ARMIX_LEGAL_ROUTE ("/legal/signalsmith-stretch", armix_signalsmith_stretch_txt)
    ARMIX_LEGAL_ROUTE ("/legal/signalsmith-linear", armix_signalsmith_linear_txt)
    ARMIX_LEGAL_ROUTE ("/legal/inter", armix_inter_ofl_txt)
    ARMIX_LEGAL_ROUTE ("/legal/vb-cable", armix_vbcable_notice_txt)
#undef ARMIX_LEGAL_ROUTE
    return false;
}
