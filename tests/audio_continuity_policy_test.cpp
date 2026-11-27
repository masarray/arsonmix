// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AudioContinuityPolicy.h"
#include "../engine/core/BusPatchEnvelope.h"
#include <array>
#include <cassert>
#include <cstdint>

int main() {
    using namespace armix::device;
    static_assert(!AudioPowerPolicy::inspectDevice(NativePowerEvent::displayOff));
    static_assert(!AudioPowerPolicy::inspectDevice(NativePowerEvent::displayOn));
    static_assert(!AudioPowerPolicy::inspectDevice(NativePowerEvent::systemSuspend));
    static_assert(AudioPowerPolicy::inspectDevice(NativePowerEvent::systemResume));
    static_assert(!AudioPowerPolicy::changeMixerIntent(NativePowerEvent::systemResume));

    OutputContinuityIntent output;
    output.operatorRequested(true);
    output.deviceInterrupted();
    assert(output.desired() && output.interrupted());
    output.deviceInterrupted(); // repeated callback STOP is idempotent
    assert(output.desired());
    output.deviceRecovered();
    assert(output.desired() && !output.interrupted());
    output.deviceInterrupted();
    output.operatorRequested(false); // explicit operator MUTE wins
    assert(!output.desired() && !output.interrupted());
    output.deviceRecovered();
    assert(!output.desired()); // cannot silently undo explicit MUTE

    AudioContinuityRetry retry;
    retry.fault(100);
    const auto generation=retry.generation();
    assert(!retry.due(449) && retry.due(450));
    retry.failed(450);
    assert(!retry.due(1449) && retry.due(1450));
    retry.failed(1450);
    assert(retry.due(4450));
    retry.failed(4450);
    assert(retry.due(12450));
    retry.failed(12450);
    assert(retry.due(42450)); // bounded cadence but never permanently exhausted
    for(unsigned i=0;i<100;++i)retry.failed(42450);
    assert(retry.attempts()==4 && !retry.due(72449) && retry.due(72450));
    retry.endpointReturned(42500);
    assert(!retry.due(42849) && retry.due(42850));
    retry.recovered();
    assert(!retry.pending() && retry.generation()>generation);
    retry.fault(50000);
    assert(retry.pending());
    retry.cancel();
    assert(!retry.pending());

    using armix::mixer::BusPatchEnvelope;
    BusPatchEnvelope fade;
    std::array<float,32> left{},right{},tap{};
    float* pcm[]={left.data(),right.data()};
    auto block=[&](bool audible=true){
        left.fill(1.f);right.fill(1.f);tap.fill(1.f);
        return fade.process(pcm,2,tap.data(),nullptr,32,48000.,true,audible);
    };
    assert(fade.begin()&&!fade.begin());
    float last=1.f;
    BusPatchEnvelope::Block b;
    for(int i=0;i<8;++i){
        b=block();
        for(float sample:left){assert(sample>=0.f&&sample<=last);last=sample;}
        assert(left==right&&left==tap);
        if(i<7){assert(!b.complete);fade.acknowledge(b);}
    }
    assert(b.complete&&!fade.ready()); // receipt is after PCM publication
    fade.acknowledge(b);assert(fade.ready());
    b=block();assert(left.back()==0.f&&tap.back()==0.f);
    fade.finish(true);
    b=block(false);assert(!b.complete&&left.back()==0.f);
    last=0.f;
    for(int i=0;i<8;++i){
        b=block();
        for(float sample:left){assert(sample>=last&&sample<=1.f);last=sample;}
        fade.acknowledge(b);
    }
    assert(b.complete&&left.back()==1.f&&fade.begin());
    b=block();fade.finish(true);fade.acknowledge(b);
    b=block();assert(left.back()>left.front()); // cancellation restores old level
    fade.reset();b=block();assert(b.phase==BusPatchEnvelope::idle&&left.front()==1.f);
}
