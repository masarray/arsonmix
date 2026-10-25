// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/DeviceRecoveryPolicy.h"
#include <cstdlib>
static void check(bool x){if(!x)std::abort();}
int main()
{
    using namespace armix::device;
    DeviceRecoveryPolicy p;
    check(p.state()==SessionState::stopped);
    auto a=p.apply(DeviceEvent::start,0,1000);
    check(a.accepted && a.action==RecoveryAction::enumerateDevices);
    const auto gen=p.generation();
    check(!p.apply(DeviceEvent::start,gen,1001).accepted);
    check(p.apply(DeviceEvent::discoveryOk,gen,1100).action==RecoveryAction::prepareCandidate);
    check(p.apply(DeviceEvent::preparationOk,gen,1200).action==RecoveryAction::publishCandidate);
    check(!p.apply(DeviceEvent::callbackHealthy,gen,25000).accepted);
    check(p.apply(DeviceEvent::callbackHealthy,gen,31200).action==RecoveryAction::persistStable);
    check(!p.apply(DeviceEvent::callbackHealthy,gen,40000).accepted);
    check(p.apply(DeviceEvent::instability,gen,42000).state==SessionState::degraded);
    check(p.apply(DeviceEvent::endpointLost,gen,43000).action==RecoveryAction::silenceAndRetire);
    check(!p.apply(DeviceEvent::preparationOk,gen,44000).accepted); // stale async reply
    auto newer=p.generation();
    check(newer>gen);
    check(!p.apply(DeviceEvent::recover,newer,43000).accepted);
    check(p.apply(DeviceEvent::recover,newer,43100).action==RecoveryAction::enumerateDevices);
    for(unsigned i=0;i<6;++i)
    {
        auto failed=p.apply(DeviceEvent::discoveryFailed,p.generation(),50000+i*20000);
        check(failed.action==RecoveryAction::scheduleRetry);
        check(failed.retryAfterMs<=10000 && failed.retryAfterMs>=100);
        check(!p.apply(DeviceEvent::recover,p.generation(),50000+i*20000).accepted);
        check(p.apply(DeviceEvent::recover,p.generation(),50000+i*20000+failed.retryAfterMs).accepted);
    }
    check(p.apply(DeviceEvent::discoveryFailed,p.generation(),999999).action==RecoveryAction::reportFailure);
    check(p.state()==SessionState::failed);
    check(p.apply(DeviceEvent::stop,0,999999).state==SessionState::stopped);
    check(p.apply(DeviceEvent::start,0,1000000).accepted);
    // Zero is a valid monotonic origin; do not use stableSince==0 as "off".
    DeviceRecoveryPolicy zero;
    auto initial=zero.apply(DeviceEvent::start,0,0);
    check(zero.apply(DeviceEvent::discoveryOk,initial.generation,0).accepted);
    check(zero.apply(DeviceEvent::preparationOk,initial.generation,0).accepted);
    check(zero.apply(DeviceEvent::callbackHealthy,initial.generation,30000).action==
          RecoveryAction::persistStable);

    // Repeated endpoint loss with momentary apparent recovery cannot bypass
    // retry limit. An explicit new START or sustained health is required.
    DeviceRecoveryPolicy flapping;
    auto wave=flapping.apply(DeviceEvent::start,0,100);
    std::uint64_t now=100;
    for(int i=0;i<8;++i)
    {
        check(flapping.apply(DeviceEvent::discoveryOk,wave.generation,now).accepted);
        check(flapping.apply(DeviceEvent::preparationOk,wave.generation,now).accepted);
        auto down=flapping.apply(DeviceEvent::endpointLost,wave.generation,now+1);
        check(down.action==RecoveryAction::silenceAndRetire);
        if(i==7)
        {
            check(down.state==SessionState::failed);
            check(!flapping.apply(DeviceEvent::recover,down.generation,now+100000).accepted);
        }
        else
        {
            check(down.retryAfterMs>=100 && down.retryAfterMs<=10000);
            check(!flapping.apply(DeviceEvent::recover,down.generation,now+1).accepted);
            now+=1+down.retryAfterMs;
            wave=flapping.apply(DeviceEvent::recover,down.generation,now);
            check(wave.action==RecoveryAction::enumerateDevices);
        }
    }
    return 0;
}
