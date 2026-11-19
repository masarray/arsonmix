// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace armix::device {
// Match the exact unique physical name in both inventories. An ordinal
// alone cannot identify a jack after an ASIO aggregate is re-enumerated.
inline int uniqueNamedPin(const juce::StringArray& names,const juce::String& name) {
    if(name.isEmpty())return -1;
    const auto pin=names.indexOf(name);
    return pin>=0 && names.indexOf(name,false,pin+1)<0?pin:-1;
}

inline int findSameNamedPin(const juce::StringArray& previous,
                           const juce::StringArray& live,int pin) {
    const auto name=previous[pin];
    return uniqueNamedPin(previous,name)==pin?uniqueNamedPin(live,name):-1;
}

inline std::optional<juce::BigInteger> translateNamedPins(
        const juce::BigInteger& previous,const juce::StringArray& oldNames,
        const juce::StringArray& newNames) {
    juce::BigInteger result;
    for(int pin=previous.findNextSetBit(0);pin>=0;pin=previous.findNextSetBit(pin+1)) {
        const int next=findSameNamedPin(oldNames,newNames,pin);
        if(next<0 || next>=64)return {};
        result.setBit(next);
    }
    return result;
}
} // namespace armix::device
