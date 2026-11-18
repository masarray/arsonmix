// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/NativeHotkeyRegistry.h"
#include "../src/NativeHotkeyCommandPlan.h"

using armix::hotkey::Mapping;
using armix::hotkey::Registry;

int main() {
    const auto temp=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixHotkeyH1","",false);
    assert(temp.createDirectory().wasOk());
    const juce::String session="11111111-2222-4333-8444-555555555555";
    Mapping mute{"m5","SESSION",session,"5","SET_MUTE",0x77,6}; // Ctrl+Shift+F8
    Mapping solo{"s5","SESSION",session,"5","SET_SOLO",0x78,6}; // Ctrl+Shift+F9
    Mapping panic{"panic","APP","","","PANIC_MUTE",0x79,6};
    Mapping rack{"fx-rack","SESSION",session,"5","SET_FX_RACK_BYPASS",0x76,6};
    Mapping slot{"fx-slot","SESSION",session,"5","SET_FX_SLOT_BYPASS",0x75,6,2,"original-instance"};
    assert(Registry::valid(mute)&&Registry::valid(solo)&&Registry::valid(panic));
    assert(Registry::valid(rack)&&Registry::valid(slot));
    Mapping param{"fx-param","SESSION",session,"5","SET_FX_PARAM_UP",
        0x74,6,2,"original-instance","vendor-vst3-class|uid",
        "vst3-param-id-27","Stereo Width"};
    assert(Registry::valid(param));
    Mapping decodedParam;
    assert(Registry::decode(Registry::encode(param),decodedParam));
    assert(decodedParam.catalogId==param.catalogId);
    assert(decodedParam.paramId==param.paramId);
    assert(decodedParam.paramName=="Stereo Width");
    auto invalidParam=param;
    invalidParam.paramId.clear();assert(!Registry::valid(invalidParam));
    invalidParam=param;invalidParam.catalogId.clear();assert(!Registry::valid(invalidParam));
    invalidParam=param;invalidParam.slot=8;assert(!Registry::valid(invalidParam));
    invalidParam=param;invalidParam.operation="SET_FX_PARAM_EXEC";
    assert(!Registry::valid(invalidParam));
    auto damagedParam=Registry::encode(param);
    damagedParam.getDynamicObject()->removeProperty("paramId");
    assert(!Registry::decode(damagedParam,decodedParam));
    damagedParam=Registry::encode(param);
    // A literal backslash+n is a valid string, NOT a newline. The old test
    // accidentally passed "wrong\\nid" and asserted it was unsafe.
    damagedParam.getDynamicObject()->setProperty("paramId","wrong\nid");
    assert(!Registry::decode(damagedParam,decodedParam));
    damagedParam=Registry::encode(param);
    damagedParam.getDynamicObject()->setProperty("paramId","wrong\rid");
    assert(!Registry::decode(damagedParam,decodedParam));
    damagedParam=Registry::encode(param);
    damagedParam.getDynamicObject()->setProperty("paramId","wrong\tid");
    assert(!Registry::decode(damagedParam,decodedParam));
    damagedParam=Registry::encode(param);
    damagedParam.getDynamicObject()->setProperty("paramId",juce::String("wrong\\nid"));
    assert(Registry::decode(damagedParam,decodedParam));

    Mapping restoredSlot;
    assert(Registry::decode(Registry::encode(slot),restoredSlot));
    assert(restoredSlot.slot==2&&restoredSlot.insertId=="original-instance");
    auto invalidSlot=slot;
    invalidSlot.slot=8;assert(!Registry::valid(invalidSlot));
    invalidSlot=slot;invalidSlot.insertId.clear();assert(!Registry::valid(invalidSlot));
    invalidSlot=rack;invalidSlot.slot=1;assert(!Registry::valid(invalidSlot));
    auto brokenSlot=Registry::encode(slot);
    brokenSlot.getDynamicObject()->removeProperty("insertId");
    assert(!Registry::decode(brokenSlot,restoredSlot));
    auto invalid=mute;
    invalid.channelId="";assert(!Registry::valid(invalid));
    invalid=mute;invalid.modifiers=4;assert(!Registry::valid(invalid));
    invalid=mute;invalid.vk=0x7b;assert(!Registry::valid(invalid)); // F12 reserved
    invalid=mute;invalid.scope="APP";assert(!Registry::valid(invalid));
    invalid=mute;invalid.operation="DELETE_CHANNEL";assert(!Registry::valid(invalid));
    invalid=mute;invalid.modifiers=10;assert(!Registry::valid(invalid)); // WIN key
    Mapping roundtrip;
    assert(Registry::decode(Registry::encode(mute),roundtrip));
    assert(roundtrip.channelId=="5"&&roundtrip.operation=="SET_MUTE");
    const auto bad=juce::var(new juce::DynamicObject());
    assert(!Registry::decode(bad,roundtrip));
    {
        Registry registry(temp);
        registry.activateSession(session);
        assert(registry.snapshot().getProperty("entries",{}).getArray()->isEmpty());
#if defined(_WIN32)
        const auto window=::CreateWindowExW(0,L"STATIC",L"ArSonMixHotkeyH1CTest",
            WS_POPUP,0,0,1,1,nullptr,nullptr,::GetModuleHandleW(nullptr),nullptr);
        assert(window!=nullptr);
        registry.attach(window);
        assert(registry.attached());
        assert(registry.upsert(mute).isEmpty());
        assert(registry.upsert(panic).isEmpty());
        assert(registry.upsert(rack).isEmpty());
        assert(registry.upsert(slot).isEmpty());
        const auto rows=registry.snapshot().getProperty("entries",{});
        assert(rows.isArray()&&rows.getArray()->size()==4);
        for(const auto& row:*rows.getArray())
            assert(row.getProperty("status","").toString()=="REGISTERED");
        // Duplicate chord remains unmodified. The working mapping wins.
        solo.vk=mute.vk;
        assert(registry.upsert(solo)=="HOTKEY_ALREADY_MAPPED");
        solo.vk=0x78;
        assert(registry.upsert(solo).isEmpty());
        registry.setCaptureMode(true);
        {
            const auto state=registry.snapshot().getProperty("entries",{});
            for(const auto& row:*state.getArray())
                assert(row.getProperty("status","").toString()=="UNAVAILABLE");
        }
        registry.setCaptureMode(false);
        assert(registry.remove("m5").isEmpty());
        assert(registry.remove("m5")=="UNKNOWN_HOTKEY_MAPPING");
        registry.detach();
        ::DestroyWindow(window);
#else
        assert(registry.upsert(mute)=="HOTKEY_PLATFORM_UNSUPPORTED");
        assert(registry.snapshot().getProperty("entries",{}).getArray()->isEmpty());
#endif
    }
#if defined(_WIN32)
    {
        Registry reloaded(temp);
        reloaded.activateSession(session);
        const auto entries=reloaded.snapshot().getProperty("entries",{});
        assert(entries.isArray()&&entries.getArray()->size()==4);
        assert(entries.getArray()->getReference(0).getProperty("id","").toString()!="m5");
        // OS registration is currently unavailable until an HWND attaches.
        for(const auto& item:*entries.getArray())
            assert(item.getProperty("status","").toString()=="UNAVAILABLE");
    }
#endif

    // MV-13: native command construction is independent of ConsoleWindow,
    // registered chords and any extra audio/graph owner.
    using Planner=armix::hotkey::NativeHotkeyCommandPlan;
    auto accepted=juce::var(new juce::DynamicObject());
    auto device=juce::var(new juce::DynamicObject());
    device.getDynamicObject()->setProperty("epoch","18446744073709551610");
    accepted.getDynamicObject()->setProperty("device",device);
    auto graph=juce::var(new juce::DynamicObject());
    graph.getDynamicObject()->setProperty("revision","9007199254740995");
    graph.getDynamicObject()->setProperty("rosterRevision","9007199254740996");
    auto caps=juce::var(new juce::DynamicObject());
    caps.getDynamicObject()->setProperty("callbackOwnsChannelIds",true);
    juce::Array<juce::var> ops;
    ops.add("SET_MUTE");ops.add("SET_SOLO");
    caps.getDynamicObject()->setProperty("supportedOps",juce::var(ops));
    graph.getDynamicObject()->setProperty("capabilities",caps);
    auto channel=juce::var(new juce::DynamicObject());
    const juce::String largeId="18446744073709551615";
    channel.getDynamicObject()->setProperty("id",largeId);
    channel.getDynamicObject()->setProperty("role","INPUT");
    channel.getDynamicObject()->setProperty("fxRackBypassed",false);
    auto controls=juce::var(new juce::DynamicObject());
    controls.getDynamicObject()->setProperty("mute",false);
    controls.getDynamicObject()->setProperty("solo",true);
    channel.getDynamicObject()->setProperty("controls",controls);
    juce::Array<juce::var> inserts;
    for(int i=0;i<3;++i) {
        auto item=juce::var(new juce::DynamicObject());
        item.getDynamicObject()->setProperty("id",
            i==2?"original-instance":("other-"+juce::String(i)));
        item.getDynamicObject()->setProperty("bypassed",i==2);
        inserts.add(item);
    }
    channel.getDynamicObject()->setProperty("inserts",juce::var(inserts));
    juce::Array<juce::var> channels;channels.add(channel);
    graph.getDynamicObject()->setProperty("channels",juce::var(channels));
    accepted.getDynamicObject()->setProperty("graph",graph);
    Mapping largeMute=mute;largeMute.channelId=largeId;
    assert(Registry::valid(largeMute));
    assert(Planner::registrationError(Registry::encode(largeMute),accepted).isEmpty());
    auto fired=Planner::dispatch(largeMute,accepted,session,true,42);
    assert(fired.kind==Planner::Kind::intent);
    assert(fired.request.getProperty("targetChannelId","").toString()==largeId);
    assert(fired.request.getProperty("requestId","").toString()=="hotkey-42");
    assert(fired.request.getProperty("deviceEpoch","").toString()=="18446744073709551610");
    assert(fired.request.getProperty("baseGraphRevision","").toString()=="9007199254740995");
    assert(fired.request.getProperty("baseRosterRevision","").toString()=="9007199254740996");
    assert(fired.request.getProperty("payload",{})
                .getProperty("value",{}).isBool());
    assert(static_cast<bool>(fired.request.getProperty("payload",{})
                .getProperty("value",{})));
    assert(!static_cast<bool>(channel.getProperty("controls",{})
                .getProperty("mute",{}))); // never optimistic native mutation
    Mapping largeRack=rack;largeRack.channelId=largeId;
    auto power=Planner::dispatch(largeRack,accepted,session,true,43);
    assert(power.kind==Planner::Kind::action);
    assert(power.request.getProperty("action","").toString()=="routingNativeFxRackBypass");
    assert(power.request.getProperty("requestId","").toString()=="hotkey-fx-43");
    assert(static_cast<bool>(power.request.getProperty("value",{})));
    Mapping largeSlot=slot;largeSlot.channelId=largeId;
    power=Planner::dispatch(largeSlot,accepted,session,true,44);
    assert(power.kind==Planner::Kind::action);
    assert(power.request.getProperty("slot",-1)==juce::var(2));
    assert(power.request.getProperty("insertId","").toString()=="original-instance");
    assert(!static_cast<bool>(power.request.getProperty("value",{})));
    Mapping staleSlot=largeSlot;staleSlot.insertId="reordered-different-instance";
    assert(Planner::registrationError(Registry::encode(staleSlot),accepted)==
           "HOTKEY_FX_INSTANCE_NOT_ADMITTED");
    assert(Planner::dispatch(staleSlot,accepted,session,true,44).kind==
           Planner::Kind::ignore);
    auto badGraph=accepted;
    auto capsOff=juce::var(new juce::DynamicObject());
    capsOff.getDynamicObject()->setProperty("callbackOwnsChannelIds",false);
    auto noCallback=juce::var(new juce::DynamicObject());
    noCallback.getDynamicObject()->setProperty("capabilities",capsOff);
    noCallback.getDynamicObject()->setProperty("channels",juce::var(channels));
    badGraph=juce::var(new juce::DynamicObject());
    badGraph.getDynamicObject()->setProperty("graph",noCallback);
    assert(Planner::dispatch(largeMute,badGraph,session,true,45).kind==
           Planner::Kind::ignore);
    assert(Planner::registrationError(Registry::encode(largeMute),badGraph)==
           "HOTKEY_CHANNEL_NOT_ADMITTED");
    assert(Planner::dispatch(largeMute,accepted,"different-session",true,46).kind==
           Planner::Kind::ignore);
    assert(Planner::dispatch(largeMute,accepted,session,false,46).kind==
           Planner::Kind::ignore);
    Mapping largeSolo=solo;largeSolo.channelId=largeId;
    fired=Planner::dispatch(largeSolo,accepted,session,true,47);
    assert(fired.kind==Planner::Kind::intent);
    assert(!static_cast<bool>(fired.request.getProperty("payload",{})
                .getProperty("value",{})));
    Mapping largeParam=param;largeParam.channelId=largeId;
    assert(Planner::dispatch(largeParam,accepted,session,true,48).kind==
           Planner::Kind::parameter);
    Mapping wrongRole=largeMute;wrongRole.channelId="1";
    assert(Planner::dispatch(wrongRole,accepted,session,true,48).kind==
           Planner::Kind::ignore);
    auto absent=Registry::encode(largeMute);
    absent.getDynamicObject()->setProperty("operation","SET_MUTE_UNSUPPORTED");
    assert(Planner::registrationError(absent,accepted)=="HOTKEY_ACTION_UNSUPPORTED");
    auto missingOp=juce::var(new juce::DynamicObject());
    missingOp.getDynamicObject()->setProperty("callbackOwnsChannelIds",true);
    missingOp.getDynamicObject()->setProperty("supportedOps",juce::var(juce::Array<juce::var>{}));
    auto noOpGraph=juce::var(new juce::DynamicObject());
    noOpGraph.getDynamicObject()->setProperty("capabilities",missingOp);
    noOpGraph.getDynamicObject()->setProperty("channels",juce::var(channels));
    auto noOps=juce::var(new juce::DynamicObject());
    noOps.getDynamicObject()->setProperty("graph",noOpGraph);
    assert(Planner::registrationError(Registry::encode(largeMute),noOps)==
           "HOTKEY_ACTION_NOT_ADMITTED");

    assert(temp.deleteRecursively());
    std::cout<<"PASS: global chord validation, native registration rollback, "
                "session identity and crash-safe persistence\n";
    return 0;
}
