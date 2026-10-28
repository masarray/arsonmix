// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/JuceActivePortSnapshot.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <array>
using namespace armix::device;
int main(){
    JuceActivePortSnapshot snap;
    JuceActivePortSnapshot::Fault fault;
    juce::BigInteger in,out;
    in.setBit(5);in.setBit(2);in.setBit(9);
    out.setBit(1);out.setBit(7);
    assert(JuceActivePortSnapshot::fromMasks(11,in,out,snap,fault));
    assert(snap.inputs.enabledCount==3);
    assert(snap.inputs.callbackIndex(2)==0);
    assert(snap.inputs.callbackIndex(5)==1);
    assert(snap.inputs.callbackIndex(9)==2);
    assert(snap.inputs.callbackIndex(3)==-1);
    assert(snap.outputPhysicalPort(0)==1);
    assert(snap.outputPhysicalPort(1)==7);
    assert(snap.outputPhysicalPort(2)==-1);
    assert(snap.matchesCallback(11,3,2));
    assert(!snap.matchesCallback(11,2,2));
    assert(!snap.matchesCallback(12,3,2));
    assert(!snap.matchesCallback(11,3,1));
    const auto previous=snap;
    assert(!JuceActivePortSnapshot::fromMasks(0,in,out,snap,fault));
    assert(snap.matchesCallback(11,3,2)); // rollback
    in.setBit(256);
    assert(!JuceActivePortSnapshot::fromMasks(12,in,out,snap,fault));
    assert(fault==JuceActivePortSnapshot::Fault::tooManyInputs);
    assert(snap.matchesCallback(11,3,2));
    in.clearBit(256);
    out.setBit(256);
    assert(!JuceActivePortSnapshot::fromMasks(12,in,out,snap,fault));
    assert(fault==JuceActivePortSnapshot::Fault::tooManyOutputs);
    out.clearBit(256);
    out.clear();
    assert(!JuceActivePortSnapshot::fromMasks(12,in,out,snap,fault));
    assert(fault==JuceActivePortSnapshot::Fault::emptyOutput);
    in.clear();
    assert(!JuceActivePortSnapshot::fromMasks(12,in,out,snap,fault));
    assert(fault==JuceActivePortSnapshot::Fault::emptyInput);
    assert(snap.inputs.callbackIndex(9)==previous.inputs.callbackIndex(9));
    assert(!JuceActivePortSnapshot::capture(nullptr,12,snap,fault));
    assert(fault==JuceActivePortSnapshot::Fault::nullDevice);
}
