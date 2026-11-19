// SPDX-License-Identifier: AGPL-3.0-only
// IO-2 device-qualified, control-prepared source map; hardware-free and JUCE-free.
#include "../engine/device/MultiDeviceInputPlan.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace io=armix::device;
namespace mix=armix::mixer;
static void check(bool yes,const char* what)
{
    if(!yes){std::fprintf(stderr,"IO2_INPUT_PLAN_FAIL: %s\n",what);std::abort();}
}
static std::array<mix::ChannelStripDefinition,2> channels()
{
    std::array<mix::ChannelStripDefinition,2> rows{};
    rows[0].id={1};rows[0].role=mix::ChannelRole::input;
    rows[0].format=mix::ProcessingFormat::mono;
    rows[1].id={5};rows[1].role=mix::ChannelRole::input;
    rows[1].format=mix::ProcessingFormat::stereo;
    return rows;
}
static void mappingAndRealtimeIsolation()
{
    const std::array<int,2> aPins{0,2},bPins{0,7};
    const std::array<io::CaptureEndpointView,2> devices{{
        {"ASIO/verified-A",91,true,aPins,1001},
        {"ASIO/verified-B",7,false,bPins,2002}
    }};
    const std::array<io::QualifiedChannelSource,2> bindings{{
        {{1},{"ASIO/verified-A",2},{},false},
        {{5},{"ASIO/verified-B",0},{"ASIO/verified-B",7},true}
    }};
    const auto roster=channels();
    io::MultiDeviceInputPlan plan;
    io::MultiDeviceInputFault error;
    check(io::MultiDeviceInputPlan::prepare(91,devices,bindings,roster,plan,error),
          "two-device prepare must succeed");
    check(error.kind==io::MultiDeviceInputFault::Kind::none,
          "success fault reset");
    check(plan.virtualInputCount()==4&&plan.deviceCount()==2&&
          plan.sourceCount()==2,"one virtual namespace for four pins");
    check(plan.device(0).verifiedKey=="ASIO/verified-A"&&
          plan.device(1).verifiedKey=="ASIO/verified-B",
          "device qualified identity");
    check(plan.slot(0).device==0&&plan.slot(0).packedOrdinal==0&&
          plan.slot(1).device==0&&plan.slot(1).packedOrdinal==1&&
          plan.slot(2).device==1&&plan.slot(2).packedOrdinal==0&&
          plan.slot(3).device==1&&plan.slot(3).packedOrdinal==1,
          "sparse pin -> correct per-device callback ordinal");
    check(plan.source(0).channel.value==1&&plan.source(0).inputL==1&&
          plan.source(0).inputR==-1&&!plan.source(0).stereo,
          "primary CH01 physical pin2 = virtual ordinal1");
    check(plan.source(1).channel.value==5&&plan.source(1).inputL==2&&
          plan.source(1).inputR==3&&plan.source(1).stereo,
          "B Input1/Input8 to CH05 without colliding with A");
    std::array<float,4> a0{0.1f,0.1f,0.1f,0.1f};
    std::array<float,4> a2{0.2f,0.2f,0.2f,0.2f};
    std::array<float,4> b0{0.4f,0.4f,0.4f,0.4f};
    std::array<float,4> b7{0.8f,0.8f,0.8f,0.8f};
    const float* aBlock[2]={a0.data(),a2.data()};
    const float* bBlock[2]={b0.data(),b7.data()};
    std::array<io::MultiDeviceInputPlan::CaptureBlock,2> blocks{{
        {aBlock,2,4,91,true,1001},{bBlock,2,4,7,true,2002}
    }};
    io::MultiDeviceInputPlan::VirtualFrame frame;
    check(plan.assemble(91,4,blocks,frame),"frame assembled");
    check(frame.count==4&&frame.available==4&&
          frame.pointers[0]==a0.data()&&frame.pointers[1]==a2.data()&&
          frame.pointers[2]==b0.data()&&frame.pointers[3]==b7.data(),
          "same graph's planar virtual pointers, no copy or second renderer");
    // Stale B epoch/detached/ASRC dropout cannot alias A.Input1.
    blocks[1].generation=8;
    check(plan.assemble(91,4,blocks,frame)&&frame.available==2&&
          frame.pointers[0]==a0.data()&&frame.pointers[1]==a2.data()&&
          frame.pointers[2]==nullptr&&frame.pointers[3]==nullptr,
          "B stale generation only silences B");
    blocks[1].generation=7;blocks[1].admitted=false;
    check(plan.assemble(91,4,blocks,frame)&&frame.available==2&&
          frame.pointers[2]==nullptr,"B no actual PCM = silence");
    blocks[1].admitted=true;blocks[1].frames=8;
    check(plan.assemble(91,4,blocks,frame)&&frame.available==2,
          "stale B callback block size rejected");
    blocks[1].frames=4;
    blocks[1].deviceLease=1001;
    check(plan.assemble(91,4,blocks,frame)&&frame.available==2,
          "native device lease prevents swapped same-generation streams");
    blocks[1].deviceLease=2002;
    check(!plan.assemble(90,4,blocks,frame)&&frame.count==0&&
          frame.available==0&&frame.pointers[1]==nullptr,
          "primary epoch change invalidates entire virtual frame");
    check(!plan.assemble(91,8193,blocks,frame)&&frame.count==0,
          "callback capacity guard");
    const std::array<io::MultiDeviceInputPlan::CaptureBlock,1> shortBlocks{
        blocks[0]
    };
    check(!plan.assemble(91,4,shortBlocks,frame),"device roster dimension fence");

    // Exact one-device parity: virtual ordinal equals primary callback ordinal.
    io::MultiDeviceInputPlan single;
    const std::array<io::CaptureEndpointView,1> primaryOnly{{devices[0]}};
    const std::array<io::QualifiedChannelSource,1> primaryBind{{bindings[0]}};
    check(io::MultiDeviceInputPlan::prepare(91,primaryOnly,primaryBind,roster,
                                          single,error),"single device prepare");
    check(single.source(0).inputL==1&&single.virtualInputCount()==2,
          "direct ASIO path keeps original packed ordinal");
    check(single.assemble(91,4,shortBlocks,frame)&&frame.available==2&&
          frame.pointers[1]==a2.data(),"single device pointer direct");
}
static void negativeContracts()
{
    const std::array<int,2> aPins{0,2},bPins{0,7};
    std::array<io::CaptureEndpointView,2> devices{{
        {"ASIO/verified-A",91,true,aPins,1001},
        {"ASIO/verified-B",7,false,bPins,2002}
    }};
    std::array<io::QualifiedChannelSource,2> bindings{{
        {{1},{"ASIO/verified-A",2},{},false},
        {{5},{"ASIO/verified-B",0},{"ASIO/verified-B",7},true}
    }};
    auto roster=channels();
    io::MultiDeviceInputPlan accepted;
    io::MultiDeviceInputFault err;
    auto attempt=[&](io::MultiDeviceInputFault::Kind expected) {
        check(!io::MultiDeviceInputPlan::prepare(91,devices,bindings,roster,
                                                accepted,err),"expected rejection");
        check(err.kind==expected,"exact fail closed rejection class");
        check(accepted.sourceCount()==2&&accepted.source(1).inputL==2,
              "failed update never mutates prior accepted plan");
    };
    check(io::MultiDeviceInputPlan::prepare(91,devices,bindings,roster,accepted,err),
          "initial accepted map");
    devices[1].verifiedDeviceKey=devices[0].verifiedDeviceKey;
    attempt(io::MultiDeviceInputFault::Kind::duplicateDevice);
    devices[1].verifiedDeviceKey="ASIO/verified-B";
    devices[1].deviceLease=devices[0].deviceLease;
    attempt(io::MultiDeviceInputFault::Kind::duplicateDeviceLease);
    devices[1].deviceLease=2002;
    devices[1].deviceLease=0;
    attempt(io::MultiDeviceInputFault::Kind::invalidDeviceLease);
    devices[1].deviceLease=2002;
    devices[0].generation=0;
    attempt(io::MultiDeviceInputFault::Kind::invalidGeneration);
    devices[0].generation=91;
    devices[1].verifiedDeviceKey="";
    attempt(io::MultiDeviceInputFault::Kind::invalidDeviceIdentity);
    devices[1].verifiedDeviceKey="ASIO/verified-B";
    devices[1].primary=true;
    attempt(io::MultiDeviceInputFault::Kind::missingPrimary);
    devices[1].primary=false;
    bindings[1].left.deviceKey="missing-device";
    attempt(io::MultiDeviceInputFault::Kind::missingDevice);
    bindings[1].left.deviceKey="ASIO/verified-B";
    bindings[1].right.physicalPin=9;
    attempt(io::MultiDeviceInputFault::Kind::unavailablePin);
    bindings[1].right.physicalPin=0;
    attempt(io::MultiDeviceInputFault::Kind::invalidStereoPair);
    bindings[1].right.physicalPin=7;
    bindings[1].right.deviceKey="ASIO/verified-A";
    attempt(io::MultiDeviceInputFault::Kind::invalidStereoPair);
    bindings[1].right.deviceKey="ASIO/verified-B";
    bindings[1].channel={1};
    attempt(io::MultiDeviceInputFault::Kind::duplicateChannel);
    bindings[1].channel={5};
    bindings[1].stereo=false;
    attempt(io::MultiDeviceInputFault::Kind::channelFormatMismatch);
    bindings[1].stereo=true;
    roster[1].role=mix::ChannelRole::group;
    attempt(io::MultiDeviceInputFault::Kind::invalidChannel);
    roster[1].role=mix::ChannelRole::input;
    const std::array<int,2> duplicatePins{0,0};
    devices[1].enabledPhysicalPinsInCallbackOrder=duplicatePins;
    attempt(io::MultiDeviceInputFault::Kind::invalidDevicePins);
    devices[1].enabledPhysicalPinsInCallbackOrder=bPins;
    const std::array<int,17> tooManySecondary{};
    devices[1].enabledPhysicalPinsInCallbackOrder=tooManySecondary;
    attempt(io::MultiDeviceInputFault::Kind::unsupportedSecondaryChannels);
    devices[1].enabledPhysicalPinsInCallbackOrder=bPins;
    devices[0].primary=false;
    attempt(io::MultiDeviceInputFault::Kind::missingPrimary);
}
int main()
{
    mappingAndRealtimeIsolation();
    negativeContracts();
    std::puts("IO2_QUALIFIED_INPUT_PLAN_PASS two devices, sparse pins, "
              "ChannelId mapping, native pointer staging, epoch isolation, "
              "single-device parity and negative validation");
}
