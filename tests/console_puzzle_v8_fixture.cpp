// SPDX-License-Identifier: AGPL-3.0-only
// Portable C++ fixture generator, SAME serializer as runtime.
// g++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic -Iengine/core tests/console_puzzle_v8_fixture.cpp -o /tmp/arsonmix-v8-fixture
// /tmp/arsonmix-v8-fixture > tests/fixtures/console-v8-native-unready.json
#include "../engine/core/ConsolePuzzleV8Wire.h"
#include <cassert>
#include <iostream>
#include <string>
int main(int argc,char** argv) {
    using namespace armix::console;
    PuzzleV8Snapshot snapshot;
    // No audio driver exists in deterministic CI. UNREADY is the only
    // truthful native state: zero invented physical pins or PCM and no fake
    // ChannelIds from M4 presentation indices.
    snapshot.epoch="1";
    PuzzleV8Ack ack;
    ack.requestId="native-fixture-session:1";
    ack.deviceEpoch=snapshot.epoch;
    const auto json=PuzzleV8Wire::fixture(snapshot,ack);
    assert(json.find("\"enabledInputs\":[]")!=std::string::npos);
    assert(json.find("\"productionCallbackLive\":false")!=std::string::npos);
    assert(json.find("\"supportedOps\":[]")!=std::string::npos);
    if(argc>1 && std::string(argv[1])=="--negative") {
        snapshot.inputs.push_back({5,0,"Mic 1",true,0.125});
        snapshot.outputs.push_back({2,0,"OUT3",false,0.});
        const auto withPins=PuzzleV8Wire::snapshot(snapshot);
        assert(withPins.find("\"enabledInputs\":[5]")!=std::string::npos);
        assert(withPins.find("\"enabledOutputs\":[2]")!=std::string::npos);
        assert(withPins.find("\"rawPeak\":0.125")!=std::string::npos);
        assert(PuzzleV8Wire::ack(ack).find("native-fixture-session:1")!=std::string::npos);
        return 0;
    }
    std::cout<<json;
    return std::cout.good()?0:1;
}
