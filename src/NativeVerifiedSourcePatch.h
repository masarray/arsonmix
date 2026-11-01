// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <limits>
#include <span>

// Message-thread SOURCE transaction: one verified input selection from an
// initially SILENT M4 session, without modifying the accepted Show or FX
// until the whole graph/epoch has compiled. Both MAIN and auxiliary outputs
// remain owned by the existing one JUCE production callback.
namespace armix::mixer {
struct NativeVerifiedSourcePatch final {
    static juce::String prepare(const juce::var& accepted,
        int channel,const juce::String& mode,int physicalLeft,int physicalRight,
        std::span<const int> enabledPhysicalPorts,
        std::uint64_t actualEpoch,std::uint64_t actualRevision,
        juce::var& destination)
    {
        if(!actualEpoch ||
           actualEpoch>static_cast<std::uint64_t>(std::numeric_limits<juce::int64>::max()) ||
           actualRevision>static_cast<std::uint64_t>(std::numeric_limits<juce::int64>::max()) ||
           !accepted.isObject() || channel<0 || channel>=10)
            return "SOURCE: stale device epoch or invalid native channel";
        if(mode!="MONO"&&mode!="STEREO")
            return "SOURCE: MONO or STEREO mode required";
        if(physicalLeft<0 || enabledPhysicalPorts.empty())
            return "SOURCE: select an enabled physical capture pin";
        if(static_cast<juce::int64>(accepted.getProperty("deviceEpoch",juce::var(-1))) !=
             static_cast<juce::int64>(actualEpoch))
            return "SOURCE: device epoch changed; accepted patch preserved";
        bool validLeft=false,validRight=false;
        for(const int port:enabledPhysicalPorts) {
            if(port==physicalLeft)validLeft=true;
            if(port==physicalRight)validRight=true;
        }
        if(!validLeft || (mode=="STEREO" &&
            (!validRight || physicalLeft==physicalRight)))
            return "SOURCE: STEREO requires two distinct active physical pins; MONO accepts one";
        // JUCE var assignment shares the underlying DynamicObject. Deep
        // clone before touching a row, or failed prepare would silently
        // mutate the still-audible accepted routing snapshot.
        auto copy=juce::JSON::parse(juce::JSON::toString(accepted,false));
        if(!copy.isObject())
            return "SOURCE: cannot clone accepted native patch";
        auto channels=copy.getProperty("channels",juce::var());
        auto* list=channels.getArray();
        if(!list || list->size()!=10 || !list->getReference(channel).isObject())
            return "SOURCE: accepted patch channel layout unavailable";
        auto* row=list->getReference(channel).getDynamicObject();
        row->setProperty("mode",mode);
        row->setProperty("left",physicalLeft);
        row->setProperty("right",mode=="STEREO"?physicalRight:-1);
        // The existing M4 producer handles revision/epoch publication and
        // fails back to its prior accepted graph on any bad plugin/routing.
        copy.getDynamicObject()->setProperty("deviceEpoch",static_cast<juce::int64>(actualEpoch));
        copy.getDynamicObject()->setProperty("revision",static_cast<juce::int64>(actualRevision));
        destination=std::move(copy);
        return {};
    }
};
} // namespace armix::mixer
