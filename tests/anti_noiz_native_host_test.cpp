// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/AntiNoizProcessor.h"
#include "../src/AntiNoizNativeBinding.h"
#include "../src/NativeBuiltInFx.h"
#include "../engine/core/LiveChannelFxGraph.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
using armix::builtin_fx::AntiNoizProcessor;
using Core=AntiNoizProcessor::Core;
using Control=AntiNoizProcessor::Control;
using Instance=armix::mixer::LiveChannelFxGraph::FxInstance;
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::abort();}}
int main(){
    juce::ScopedJuceInitialiser_GUI gui;
    using Binding=armix::builtin_fx::AntiNoizNativeBinding;
    const auto* entry=armix::builtin_fx::findNativeBuiltInFx(AntiNoizProcessor::catalogIdentity);
    check(entry&&entry->showInPicker&&entry->minimumLiveRate==44100.&&entry->maximumLiveRate==96000.,"native catalog");
    check(!entry->prepare(32000.,128,nullptr,0),"reject unsupported rate");
    Core::Settings typed;auto v=Binding::settings(typed);
    check(Binding::parseSettings(v,typed),"typed native settings");
    v.getDynamicObject()->setProperty("cleanDb","14");
    check(!Binding::parseSettings(v,typed),"reject coerced parameter");
    v=Binding::settings(typed);v.getDynamicObject()->setProperty("mode","Unknown");
    check(!Binding::parseSettings(v,typed),"reject unknown mode");
    auto envelope=std::make_unique<juce::DynamicObject>();
    envelope->setProperty("channelId","9007199254740993");
    envelope->setProperty("graphRevision","12");envelope->setProperty("deviceEpoch","0");
    envelope->setProperty("instanceId","logical-fixture");
    envelope->setProperty("slot",1);envelope->setProperty("requestId","an-5");
    envelope->setProperty("workspaceToken","saved-mixer");
    const juce::var request(envelope.release());
    using Request=armix::builtin_fx::NativeFxControlRequest;
    check(!Request::parse(request,8).valid,"existing strict epoch contract unchanged");
    const auto logical=Request::parse(request,8,true);
    check(logical.valid&&logical.unbound&&logical.epoch==0&&logical.sequence("an-")==5,"logical AN typed envelope");
    request.getDynamicObject()->setProperty("workspaceToken","");
    check(!Request::parse(request,8,true).valid,"epoch zero never live");
    for(double rate:{44100.,48000.,88200.,96000.}){
        auto p=entry->prepare(rate,128,nullptr,0);
        auto* fx=dynamic_cast<AntiNoizProcessor*>(p.get());
        check(fx!=nullptr,"actual registry preparation");const int latency=rate>48000?512:256;
        check(fx->isReady()&&p->getLatencySamples()==latency&&p->getSampleRate()==rate,"JUCE prepare");
        juce::MemoryBlock saved;p->getStateInformation(saved);
        check(saved.getSize()==armix::builtin_fx::anti_noiz::NativeState::size,"state size");
        Instance lease(std::move(p),AntiNoizProcessor::catalogIdentity,"Anti Noiz",128);
        check(lease.continuousBypassProcessing&&lease.latencySamples==latency,"actual host PDC");
        check(!fx->restoreUnpublishedState(saved.getData(),static_cast<int>(saved.getSize())),"published restore denied");
        Core::Settings s;s.mode=Core::Mode::Voice;s.silenceDb=0;
        check(fx->queueSettings(s,1)&&!lease.tryCaptureState(saved),"pending state denied");
        std::array<float,128> l{},r{};
        l[0]=.25f;r[0]=-.125f;
        Control::Completion ack;
        for(int pos=0;pos<=latency;pos+=128){
            lease.render(l.data(),r.data(),128);
            if(pos==0)check(fx->pollCompletion(ack)&&ack.applied&&ack.id==1,"native ACK");
            for(int i=0;i<128;++i){
                const float want=pos+i==latency?.25f:0.f;
                check(std::abs(l[i]-want)<2e-6f&&std::abs(r[i]+want*.5f)<2e-6f,"wet unity latency");
            }
            l.fill(0);r.fill(0);
        }
        check(lease.tryCaptureState(saved)&&saved.getSize()==armix::builtin_fx::anti_noiz::NativeState::size,"accepted native capture");
        check(fx->queueLearn(2),"native learn");lease.render(l.data(),r.data(),128);
        check(fx->pollCompletion(ack)&&ack.applied&&ack.state.learning,"native learning");
        for(int i=0;i<16;++i)lease.render(l.data(),r.data(),128,true);
        check(fx->queueLearn(3),"native bypass command");lease.render(l.data(),r.data(),128,true);
        check(fx->pollCompletion(ack)&&!ack.applied&&!ack.state.learning,"bypass learn rejected");
        AntiNoizProcessor::Snapshot meter,last;
        while(fx->pollTelemetry(meter))last=meter;
        for(int i=0;i<static_cast<int>(rate/1280.)+1;++i){lease.render(l.data(),r.data(),128,true);while(fx->pollTelemetry(meter))last=meter;}
        check(last.rackBypassed&&last.learnStatus==AntiNoizProcessor::LearnStatus::Cancelled,"bypass cancellation telemetry");
        for(int pos=0;pos<=latency;pos+=128){
            if(!pos){l[0]=.125f;r[0]=-.25f;}
            lease.render(l.data(),r.data(),128,true);
            for(int i=0;i<128;++i){
                const float want=pos+i==latency?.125f:0.f;
                check(std::abs(l[i]-want)<2e-6f&&std::abs(r[i]+2*want)<2e-6f,"host bypass exact PDC");
            }
            l.fill(0);r.fill(0);
        }
        // Actual native processor → typed bridge observation, not JS fixture.
        for(int i=0;i<16;++i){l.fill(0);r.fill(0);lease.render(l.data(),r.data(),128);}
        check(fx->queueLearn(4),"capture real spectrum");
        for(int pos=0;pos<static_cast<int>(3*rate);pos+=128){
            for(int i=0;i<128;++i){l[i]=.03f*float(std::sin(2.*std::numbers::pi_v<double>*8.*double(pos+i)/latency));r[i]=-l[i];}
            lease.render(l.data(),r.data(),128);
            if(!pos)check(fx->pollCompletion(ack)&&ack.applied,"capture ACK");
            while(fx->pollTelemetry(meter))last=meter;
        }
        check(last.profile&&last.spectrum.noiseValid&&last.spectrum.inputValid,"actual native spectral profile");
        const auto observation=Binding::observation(last);
        const auto spectrum=observation.getProperty("spectrum",{});
        const auto noise=spectrum.getProperty("noise",{}),input=spectrum.getProperty("input",{});
        check(noise.isArray()&&input.isArray()&&noise.getArray()->size()==latency/2+1&&input.getArray()->size()==latency/2+1,"typed native spectral arrays");
        check(static_cast<double>((*noise.getArray())[8])>static_cast<double>((*noise.getArray())[16]),"typed spectral peak");
        auto restored=std::make_unique<AntiNoizProcessor>();
        check(restored->restoreUnpublishedState(saved.getData(),static_cast<int>(saved.getSize())),"cold reopen");
        restored->prepareToPlay(rate,128);
        check(restored->isReady()&&restored->acceptedSettings().mode==Core::Mode::Voice,"native state roundtrip");
        std::cout<<"JUCE_HOST,"<<rate<<",latency="<<latency<<",FxInstance ACK/restore/bypass PASS\n";
    }
}
