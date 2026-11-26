// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/BoundedDataInput.h"
#include "../src/NativeShowSchema.h"
#include <cstdint>
#include <cstdlib>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,std::size_t size) {
    if(size>65536)return 0;
    using namespace armix::shows;
    using Input=armix::storage::BoundedDataInput;
    juce::String json;
    if(!Input::decodeUtf8(juce::MemoryBlock(data,size),json))return 0;
    if(!Input::jsonStructure(json))return 0;
    (void)juce::JSON::parse(json);
    ShowV2 show;show.name="unchanged";show.createdMs=1234;
    SnapshotV2 snapshot;snapshot.name="unchanged";snapshot.masterGain=.625;
    if(!Codec::decode(json,show)) {
        if(show.name!="unchanged"||show.createdMs!=1234)std::abort();
    } else {
        ShowV2 next;
        if(!Codec::decode(Codec::encode(show),next))std::abort();
    }
    if(!Codec::decode(json,snapshot)) {
        if(snapshot.name!="unchanged"||snapshot.masterGain!=.625)std::abort();
    } else {
        SnapshotV2 next;
        if(!Codec::decode(Codec::encode(snapshot),next))std::abort();
    }
    return 0;
}
