// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/NativeHotkeyRegistry.h"

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
    assert(temp.deleteRecursively());
    std::cout<<"PASS: global chord validation, native registration rollback, "
                "session identity and crash-safe persistence\n";
    return 0;
}
