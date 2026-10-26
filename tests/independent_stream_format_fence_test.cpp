// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/IndependentStreamFormatFence.h"
#include <cstdlib>
#include <limits>
#include <string>
using Fence = armix::device::IndependentStreamFormatFence;
using Fault = Fence::Fault;
using Side = Fence::Side;
static void check(bool ok) { if (!ok) std::abort(); }

static Fence::Endpoint source()
{
    return {"Virtual capture",48000.,480,"3",2};
}
static Fence::Endpoint speaker()
{
    return {"Wired render",48000.,480,"c",2};
}

int main()
{
    Fence fence;
    check(fence.fault()==Fault::none);
    check(fence.prepare(source(),speaker()));
    check(fence.observeStart(Side::capture,source()));
    check(fence.observeStart(Side::render,speaker()));
    for(int n=0;n<100000;++n)
    {
        check(fence.observeCallbackChannels(Side::capture,2));
        check(fence.observeCallbackChannels(Side::render,2));
        check(fence.fault()==Fault::none);
    }
    // Exact sample rate and period can be advertised by the same friendly
    // name after a backend re-open. Silence instead of using old ASRC taps.
    auto altered=source(); altered.rateHz=44100.;
    check(!fence.observeStart(Side::capture,altered));
    check(fence.fault()==Fault::lifecycleRate);
    check(!fence.observeCallbackChannels(Side::render,2));
    check(!fence.observeStart(Side::capture,source())); // fault stays sticky
    fence.resetStopped();
    check(fence.prepare(source(),speaker()));
    altered=source();altered.periodFrames=512;
    check(!fence.observeStart(Side::capture,altered));
    check(fence.fault()==Fault::lifecyclePeriod);
    fence.resetStopped();

    check(fence.prepare(source(),speaker()));
    altered=speaker();altered.activeChannelMaskHex="3";
    check(!fence.observeStart(Side::render,altered));
    check(fence.fault()==Fault::lifecycleChannelMap);
    fence.resetStopped();
    check(fence.prepare(source(),speaker()));
    altered=speaker();altered.name="Bluetooth render";
    check(!fence.observeStart(Side::render,altered));
    check(fence.fault()==Fault::lifecycleIdentity);
    fence.resetStopped();

    // Callback ownership count is a fixed-cost check; the callback must
    // NOT call getActiveChannels(), create strings or renegotiate a driver.
    check(fence.prepare(source(),speaker()));
    check(!fence.observeCallbackChannels(Side::capture,1));
    check(fence.fault()==Fault::callbackChannelCount);
    check(!fence.observeCallbackChannels(Side::render,2));
    check(std::string(Fence::reason(fence.fault()))
           =="INDEPENDENT_CALLBACK_CHANNEL_COUNT_CHANGED");
    fence.resetStopped();
    check(fence.prepare(source(),speaker()));
    check(!fence.observeCallbackChannels(Side::render,1));
    check(fence.fault()==Fault::callbackChannelCount);

    // More than 64 channel slots are preserved in the full hexadecimal mask.
    fence.resetStopped();
    auto wide=source();wide.activeChannelMaskHex="100000000000000000001";
    wide.activeChannelCount=2;
    check(fence.prepare(wide,speaker()));
    check(fence.observeStart(Side::capture,wide));
    altered=wide;altered.activeChannelMaskHex="200000000000000000001";
    check(!fence.observeStart(Side::capture,altered));
    check(fence.fault()==Fault::lifecycleChannelMap);

    // Reject invalid/ambiguous input even if an endpoint friendly name exists.
    fence.resetStopped();
    altered=source();altered.activeChannelMaskHex="0";
    check(!fence.prepare(altered,speaker()));
    check(fence.fault()==Fault::invalidSelection);
    fence.resetStopped();
    altered=source();altered.activeChannelMaskHex="3g";
    check(!fence.prepare(altered,speaker()));
    fence.resetStopped();
    altered=source();altered.rateHz=std::numeric_limits<double>::quiet_NaN();
    check(!fence.prepare(altered,speaker()));
    fence.resetStopped();
    altered=source();altered.periodFrames=0;
    check(!fence.prepare(altered,speaker()));
    fence.resetStopped();
    altered=source();altered.activeChannelCount=33;
    check(!fence.prepare(altered,speaker()));
    fence.resetStopped();
    check(fence.prepare(source(),speaker()));
    altered=source();altered.activeChannelMaskHex="1";
    altered.activeChannelCount=1;
    check(!fence.observeStart(Side::capture,altered));
    check(fence.fault()==Fault::lifecycleChannelMap);
    return 0;
}
